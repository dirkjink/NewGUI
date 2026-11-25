/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The Blend2D backend (specification 14), operation by operation as in
// 14.1; the rules A1 to A3 as in the Skia backend, so that axis-parallel
// primitives are pixel for pixel the same (conformance level C1×).


#include <ngui/gfx/Blend2DBackend.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

#include <blend2d.h>

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/ImageRegistry.h>
#include <ngui/gfx/NativeWindow.h>
#include <ngui/gfx/RRect.h>


namespace ngui::gfx {


namespace {


// Nesting depth of DrawList beyond which children are not drawn, as in the
// Skia backend.
constexpr int kMaxListDepth = 256;


class Blend2DSurface : public Surface {
public:
								Blend2DSurface(BLImage image, float scale);

			Size				DeviceSize() const override;
			float				Scale() const override;
			SurfaceKind			Kind() const override;
			std::optional<CpuPixels> Pixels() const override;

			BLImage				fImage;
			float				fScale;
};


Blend2DSurface::Blend2DSurface(BLImage image, float scale)
	:
	fImage(std::move(image)),
	fScale(scale)
{
}


Size
Blend2DSurface::DeviceSize() const
{
	return {static_cast<float>(fImage.width()), static_cast<float>(fImage.height())};
}


float
Blend2DSurface::Scale() const
{
	return fScale;
}


SurfaceKind
Blend2DSurface::Kind() const
{
	return SurfaceKind::CpuRaster;
}


std::optional<CpuPixels>
Blend2DSurface::Pixels() const
{
	BLImageData data{};
	if (fImage.getData(&data) != BL_SUCCESS || data.stride <= 0)
		return std::nullopt;
	// PRGB32 is a 32-bit 0xAARRGGBB: B, G, R, A in memory on little-endian
	// machines, which are the ones the toolkit runs on.
	const auto stride = static_cast<std::size_t>(data.stride);
	return CpuPixels{data.size.w, data.size.h, stride, PixelFormat::Bgra8Premul,
		{static_cast<const std::byte*>(data.pixelData),
			stride * static_cast<std::size_t>(data.size.h)}};
}


BLImage
MakeImage(Size device)
{
	const int width = static_cast<int>(std::lround(device.w));
	const int height = static_cast<int>(std::lround(device.h));
	BLImage image;
	if (width <= 0 || height <= 0 || image.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS)
		return {};
	return image;
}


std::uint32_t
Channel(float value)
{
	return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}


BLRgba32
ToRgba32(const Color& color)
{
	return BLRgba32(Channel(color.r), Channel(color.g), Channel(color.b), Channel(color.a));
}


// Where the logical coordinates of a list end up, as in the Skia backend:
// shifted by the offsets of Render() and of enclosing DrawLists, then
// scaled (specification 4.2).
struct Transform {
			Point				offset;
			float				scale;

			float				Device(float logical, float shift) const;
			BLBoxI				DeviceBox(const Rect& rect) const;
			BLPoint				DevicePoint(Point point) const;
};


// Rule A1: an edge rounded to whole device pixels after scaling.
float
Transform::Device(float logical, float shift) const
{
	return std::floor((logical + shift) * scale + 0.5f);
}


BLBoxI
Transform::DeviceBox(const Rect& rect) const
{
	return {static_cast<int>(Device(rect.x0, offset.x)),
		static_cast<int>(Device(rect.y0, offset.y)), static_cast<int>(Device(rect.x1, offset.x)),
		static_cast<int>(Device(rect.y1, offset.y))};
}


BLPoint
Transform::DevicePoint(Point point) const
{
	return {static_cast<double>((point.x + offset.x) * scale),
		static_cast<double>((point.y + offset.y) * scale)};
}


// A linear gradient from the recorded one, with points mapped by \a map.
// Blend2D interpolates premultiplied; specification 8 wants the colors
// interpolated as they are, so a stretch between stops of different
// opacity is cut into 16 steps, interpolated here.
template<typename Map>
BLGradient
MakeGradient(const PaintRecord& paint, Map map)
{
	const GradientRecord& record = paint.gradient;
	const BLPoint p0 = map(record.p0);
	const BLPoint p1 = map(record.p1);
	BLGradient gradient(BLLinearGradientValues(p0.x, p0.y, p1.x, p1.y));
	const std::uint32_t count = std::min<std::uint32_t>(record.count, 4);
	for (std::uint32_t i = 0; i < count; i++) {
		const GradientStop& stop = record.stops[i];
		if (i > 0) {
			const GradientStop& previous = record.stops[i - 1];
			if (previous.color.a != stop.color.a && stop.offset > previous.offset) {
				constexpr int kSteps = 16;
				for (int step = 1; step < kSteps; step++) {
					const float t = static_cast<float>(step) / kSteps;
					const auto mix = [t](float a, float b) { return a + (b - a) * t; };
					const Color color{mix(previous.color.r, stop.color.r),
						mix(previous.color.g, stop.color.g), mix(previous.color.b, stop.color.b),
						mix(previous.color.a, stop.color.a)};
					(void)gradient.addStop(mix(previous.offset, stop.offset), ToRgba32(color));
				}
			}
		}
		(void)gradient.addStop(stop.offset, ToRgba32(stop.color));
	}
	return gradient;
}


BLStrokeCap
ToCap(Paint::Cap cap)
{
	switch (cap) {
		case Paint::Cap::Round:
			return BL_STROKE_CAP_ROUND;
		case Paint::Cap::Square:
			return BL_STROKE_CAP_SQUARE;
		case Paint::Cap::Butt:
			break;
	}
	return BL_STROKE_CAP_BUTT;
}


BLStrokeJoin
ToJoin(Paint::Join join)
{
	switch (join) {
		case Paint::Join::Round:
			return BL_STROKE_JOIN_ROUND;
		case Paint::Join::Bevel:
			return BL_STROKE_JOIN_BEVEL;
		case Paint::Join::Miter:
			break;
	}
	// Skia's miter join: a bevel beyond the miter limit
	return BL_STROKE_JOIN_MITER_BEVEL;
}


// The contour of a rounded rectangle with quarter ellipses of radius ×
// radius at its corners (6.1), clockwise from the top left.
void
AddRRect(BLPath& path, const RRect& rrect)
{
	const RRect shape = rrect.Normalized();
	const Rect& r = shape.rect;
	const Radii& radii = shape.radii;
	constexpr double kQuarter = std::numbers::pi / 2;
	const auto corner = [&](double cx, double cy, double radius, double start) {
		if (radius <= 0) {
			(void)path.lineTo(cx, cy);
			return;
		}
		(void)path.arcTo(cx, cy, radius, radius, start, kQuarter);
	};
	(void)path.moveTo(r.x0 + radii.tl, r.y0);
	(void)path.lineTo(r.x1 - radii.tr, r.y0);
	corner(r.x1 - radii.tr, r.y0 + radii.tr, radii.tr, -kQuarter);
	(void)path.lineTo(r.x1, r.y1 - radii.br);
	corner(r.x1 - radii.br, r.y1 - radii.br, radii.br, 0);
	(void)path.lineTo(r.x0 + radii.bl, r.y1);
	corner(r.x0 + radii.bl, r.y1 - radii.bl, radii.bl, kQuarter);
	(void)path.lineTo(r.x0, r.y0 + radii.tl);
	corner(r.x0 + radii.tl, r.y0 + radii.tl, radii.tl, 2 * kQuarter);
	(void)path.close();
}


// \a pixels (RGBA, not premultiplied) into \a data (PRGB32: B, G, R, A in
// memory), premultiplied as Skia does it: (c * a + 127) / 255.
void
Premultiply(const PixelData& pixels, const BLImageData& data)
{
	for (std::int32_t y = 0; y < pixels.height; y++) {
		const std::uint8_t* from = pixels.bytes.data()
			+ static_cast<std::size_t>(y) * pixels.stride;
		auto* to = static_cast<std::uint8_t*>(data.pixelData) + y * data.stride;
		for (std::int32_t x = 0; x < pixels.width; x++) {
			const std::uint32_t alpha = from[3];
			const auto premultiply = [alpha](std::uint32_t value) {
				return static_cast<std::uint8_t>((value * alpha + 127) / 255);
			};
			to[0] = premultiply(from[2]);
			to[1] = premultiply(from[1]);
			to[2] = premultiply(from[0]);
			to[3] = static_cast<std::uint8_t>(alpha);
			from += 4;
			to += 4;
		}
	}
}


} // namespace


struct Blend2DBackend::Data {
	struct Font {
		std::vector<std::byte>	bytes;
		BLFontData				data;
		BLFontFace				face;
	};

			std::mutex			imageLock;
			std::unordered_map<ImageHandle, BLImage> images;
			ImageHandle			lastImage = 0;

			std::mutex			fontLock;
			std::unordered_map<FontId, std::shared_ptr<Font>> fonts;

			Blend2DSurface*		frame = nullptr;
			BLContext			context;
			Blend2DBackend*		owner = nullptr;

			void				RenderList(const DisplayList& list, const Transform& transform,
									int depth);
			void				Draw(const DisplayList& list, const Op& op,
									const Transform& transform, int depth);
			void				FillDevice(const BLBoxI& box, const PaintRecord& paint,
									const Transform& transform);
			void				Under(const Transform& transform);
			void				SetFill(const PaintRecord& paint);
			void				SetStroke(const PaintRecord& paint);
			BLImage				Image(ImageHandle image);
			std::shared_ptr<Font> FontOf(FontId font);
};


// The clip region of each operation, as rectangles in device pixels, one
// pass per rectangle (14.2); nested lists add their own region inside.
void
Blend2DBackend::Data::RenderList(const DisplayList& list, const Transform& transform,
	int depth)
{
	std::uint32_t clip = UINT32_MAX;
	std::vector<BLBoxI> boxes;
	for (const Op& op : list.Ops()) {
		if (op.ClipIndex() != clip) {
			clip = op.ClipIndex();
			boxes.clear();
			for (const IntRect& rect : list.Clip(clip)) {
				const BLBoxI box = transform.DeviceBox(rect.ToRect());
				if (box.x1 > box.x0 && box.y1 > box.y0)
					boxes.push_back(box);
			}
		}
		for (const BLBoxI& box : boxes) {
			(void)context.save();
			(void)context.resetTransform();
			(void)context.clipToRect(BLRectI(box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0));
			Draw(list, op, transform, depth);
			(void)context.restore();
		}
	}
}


// Device pixels: the transform of the list, for operations under rule A2.
void
Blend2DBackend::Data::Under(const Transform& transform)
{
	(void)context.resetTransform();
	(void)context.scale(transform.scale);
	(void)context.translate(transform.offset.x, transform.offset.y);
}


void
Blend2DBackend::Data::SetFill(const PaintRecord& paint)
{
	if (paint.hasGradient != 0) {
		// in user space, mapped by the context's transform
		(void)context.setFillStyle(MakeGradient(paint,
			[](Point point) { return BLPoint(point.x, point.y); }));
	} else
		(void)context.setFillStyle(ToRgba32(paint.color));
}


void
Blend2DBackend::Data::SetStroke(const PaintRecord& paint)
{
	if (paint.hasGradient != 0) {
		(void)context.setStrokeStyle(MakeGradient(paint,
			[](Point point) { return BLPoint(point.x, point.y); }));
	} else
		(void)context.setStrokeStyle(ToRgba32(paint.color));
	(void)context.setStrokeWidth(paint.strokeWidth);
	(void)context.setStrokeCaps(ToCap(static_cast<Paint::Cap>(paint.cap)));
	(void)context.setStrokeJoin(ToJoin(static_cast<Paint::Join>(paint.join)));
	(void)context.setStrokeMiterLimit(4);
}


// Rule A1: a rectangle in whole device pixels, which Blend2D covers fully.
void
Blend2DBackend::Data::FillDevice(const BLBoxI& box, const PaintRecord& paint,
	const Transform& transform)
{
	if (box.x1 <= box.x0 || box.y1 <= box.y0)
		return;
	(void)context.resetTransform();
	if (paint.hasGradient != 0) {
		(void)context.setFillStyle(MakeGradient(paint,
			[&](Point point) { return transform.DevicePoint(point); }));
	} else
		(void)context.setFillStyle(ToRgba32(paint.color));
	(void)context.fillBox(box);
}


void
Blend2DBackend::Data::Draw(const DisplayList& list, const Op& op, const Transform& transform,
	int depth)
{
	switch (op.Code()) {
		case OpCode::FillRect:
		{
			const RectRecord& record = op.As<RectRecord>();
			FillDevice(transform.DeviceBox(record.rect), record.paint, transform);
			break;
		}
		case OpCode::FillRRect:
		{
			const RRectRecord& record = op.As<RRectRecord>();
			const Radii& radii = record.rrect.radii;
			if (radii.tl <= 0 && radii.tr <= 0 && radii.br <= 0 && radii.bl <= 0) {
				FillDevice(transform.DeviceBox(record.rrect.rect), record.paint, transform);
				break;
			}
			Under(transform);
			SetFill(record.paint);
			BLPath path;
			AddRRect(path, record.rrect);
			(void)context.fillPath(path);
			break;
		}
		case OpCode::FillRing:
		{
			const RingRecord& record = op.As<RingRecord>();
			Under(transform);
			SetFill(record.paint);
			BLPath path;
			AddRRect(path, record.outer);
			AddRRect(path, record.inner);
			(void)context.setFillRule(BL_FILL_RULE_EVEN_ODD);
			(void)context.fillPath(path);
			break;
		}
		case OpCode::FillEllipse:
		{
			const RectRecord& record = op.As<RectRecord>();
			const Rect& r = record.rect;
			Under(transform);
			SetFill(record.paint);
			(void)context.fillEllipse((static_cast<double>(r.x0) + r.x1) / 2,
				(static_cast<double>(r.y0) + r.y1) / 2, (static_cast<double>(r.x1) - r.x0) / 2,
				(static_cast<double>(r.y1) - r.y0) / 2);
			break;
		}
		case OpCode::FillConvexPolygon:
		case OpCode::StrokePolyline:
		{
			const PointsRecord& record = op.As<PointsRecord>();
			std::vector<BLPoint> points;
			for (const Point& point : op.Points())
				points.emplace_back(point.x, point.y);
			Under(transform);
			if (op.Code() == OpCode::FillConvexPolygon) {
				SetFill(record.paint);
				(void)context.fillPolygon(points.data(), points.size());
			} else {
				SetStroke(record.paint);
				if (record.closed != 0)
					(void)context.strokePolygon(points.data(), points.size());
				else
					(void)context.strokePolyline(points.data(), points.size());
			}
			break;
		}
		case OpCode::FillPath:
		case OpCode::StrokePath:
		{
			const PathRecord& record = op.As<PathRecord>();
			const std::span<const Point> points = op.PathPoints();
			BLPath path;
			std::size_t next = 0;
			for (const PathVerb verb : op.PathVerbs()) {
				switch (verb) {
					case PathVerb::Move:
						(void)path.moveTo(points[next].x, points[next].y);
						next++;
						break;
					case PathVerb::Line:
						(void)path.lineTo(points[next].x, points[next].y);
						next++;
						break;
					case PathVerb::Cubic:
						(void)path.cubicTo(points[next].x, points[next].y, points[next + 1].x,
							points[next + 1].y, points[next + 2].x, points[next + 2].y);
						next += 3;
						break;
					case PathVerb::Close:
						(void)path.close();
						break;
				}
			}
			Under(transform);
			if (op.Code() == OpCode::FillPath) {
				SetFill(record.paint);
				(void)context.setFillRule(record.fillRule == 1 ? BL_FILL_RULE_EVEN_ODD
					: BL_FILL_RULE_NON_ZERO);
				(void)context.fillPath(path);
			} else {
				SetStroke(record.paint);
				(void)context.strokePath(path);
			}
			break;
		}
		case OpCode::StrokeRect:
		{
			// Rule A1: the frame inside the rectangle (6.1) as four device
			// rectangles, as in the Skia backend.
			const RectRecord& record = op.As<RectRecord>();
			const BLBoxI outer = transform.DeviceBox(record.rect);
			const int width = static_cast<int>(std::floor(record.paint.strokeWidth
				* transform.scale + 0.5f));
			if (width <= 0)
				break;
			if (2 * width >= outer.x1 - outer.x0 || 2 * width >= outer.y1 - outer.y0) {
				FillDevice(outer, record.paint, transform);
				break;
			}
			FillDevice({outer.x0, outer.y0, outer.x1, outer.y0 + width}, record.paint, transform);
			FillDevice({outer.x0, outer.y1 - width, outer.x1, outer.y1}, record.paint, transform);
			FillDevice({outer.x0, outer.y0 + width, outer.x0 + width, outer.y1 - width},
				record.paint, transform);
			FillDevice({outer.x1 - width, outer.y0 + width, outer.x1, outer.y1 - width},
				record.paint, transform);
			break;
		}
		case OpCode::StrokeRRect:
		{
			// inside: inset by half the width, then stroked centered (14.1)
			const RRectRecord& record = op.As<RRectRecord>();
			const float half = record.paint.strokeWidth / 2;
			const Radii& radii = record.rrect.radii;
			const RRect inset = {record.rrect.rect.InsetByCopy(half, half),
				{std::max(0.0f, radii.tl - half), std::max(0.0f, radii.tr - half),
					std::max(0.0f, radii.br - half), std::max(0.0f, radii.bl - half)}};
			if (inset.rect.IsEmpty() || record.paint.strokeWidth <= 0)
				break;
			Under(transform);
			SetStroke(record.paint);
			BLPath path;
			AddRRect(path, inset);
			(void)context.strokePath(path);
			break;
		}
		case OpCode::StrokeArc:
		{
			// 0° at 3 o'clock, clockwise with y down: Blend2D's convention
			// in radians.
			const ArcRecord& record = op.As<ArcRecord>();
			const Rect& oval = record.oval;
			constexpr double kRadians = std::numbers::pi / 180;
			Under(transform);
			SetStroke(record.paint);
			(void)context.strokeArc((static_cast<double>(oval.x0) + oval.x1) / 2,
				(static_cast<double>(oval.y0) + oval.y1) / 2,
				(static_cast<double>(oval.x1) - oval.x0) / 2,
				(static_cast<double>(oval.y1) - oval.y0) / 2, record.startDeg * kRadians,
				record.sweepDeg * kRadians);
			break;
		}
		case OpCode::DrawLines:
		{
			const LinesRecord& record = op.As<LinesRecord>();
			const auto cap = static_cast<Paint::Cap>(record.cap);
			const float half = record.width / 2;
			for (const Line& line : op.Lines()) {
				PaintRecord paint = PaintRecord::From({.color = line.color});
				const bool horizontal = line.p0.y == line.p1.y;
				const bool vertical = line.p0.x == line.p1.x;
				if ((horizontal || vertical) && cap != Paint::Cap::Round) {
					// Rule A3, as in the Skia backend.
					const float extend = cap == Paint::Cap::Square ? half : 0;
					const float xMin = std::min(line.p0.x, line.p1.x);
					const float xMax = std::max(line.p0.x, line.p1.x);
					const float yMin = std::min(line.p0.y, line.p1.y);
					const float yMax = std::max(line.p0.y, line.p1.y);
					const Rect rect = horizontal && !(vertical && cap == Paint::Cap::Butt)
						? Rect{xMin - extend, yMin - half, xMax + extend, yMax + half}
						: Rect{xMin - half, yMin - extend, xMax + half, yMax + extend};
					FillDevice(transform.DeviceBox(rect), paint, transform);
					continue;
				}
				Under(transform);
				paint.strokeWidth = record.width;
				paint.cap = record.cap;
				SetStroke(paint);
				(void)context.strokeLine(line.p0.x, line.p0.y, line.p1.x, line.p1.y);
			}
			break;
		}
		case OpCode::DrawImage:
		{
			const ImageRecord& record = op.As<ImageRecord>();
			const ImageHandle resource = ImageRegistry::Default().BackendImage(record.image,
				*owner);
			const BLImage image = Image(resource);
			if (image.empty())
				break;
			// nearest at 1:1, otherwise linear (6.1), judged in device pixels
			const bool oneToOne = record.src.Width() == record.dst.Width() * transform.scale
				&& record.src.Height() == record.dst.Height() * transform.scale;
			Under(transform);
			(void)context.setPatternQuality(oneToOne ? BL_PATTERN_QUALITY_NEAREST
				: BL_PATTERN_QUALITY_BILINEAR);
			(void)context.setGlobalAlpha(record.alpha);
			const BLRectI area(static_cast<int>(std::lround(record.src.x0)),
				static_cast<int>(std::lround(record.src.y0)),
				static_cast<int>(std::lround(record.src.Width())),
				static_cast<int>(std::lround(record.src.Height())));
			(void)context.blitImage(BLRect(record.dst.x0, record.dst.y0, record.dst.Width(),
				record.dst.Height()), image, area);
			break;
		}
		case OpCode::DrawGlyphRun:
		{
			const GlyphRunRecord& record = op.As<GlyphRunRecord>();
			const std::shared_ptr<Font> font = FontOf(record.font);
			if (!font)
				break;
			BLFont sized;
			if (sized.createFromFace(font->face, record.sizePx) != BL_SUCCESS)
				break;
			const std::span<const std::uint32_t> glyphs = op.Glyphs();
			const std::span<const Point> positions = op.Positions();
			std::vector<BLPoint> placements;
			for (const Point& position : positions)
				placements.emplace_back(position.x, position.y);
			BLGlyphRun run{};
			run.setGlyphData(glyphs.data());
			run.setPlacementData(placements.data());
			run.size = glyphs.size();
			run.placementType = BL_GLYPH_PLACEMENT_TYPE_USER_UNITS;
			Under(transform);
			(void)context.setFillStyle(ToRgba32(record.color));
			(void)context.fillGlyphRun(BLPoint(record.origin.x, record.origin.y), sized, run);
			break;
		}
		case OpCode::DrawList:
		{
			const ListRecord& record = op.As<ListRecord>();
			if (depth >= kMaxListDepth)
				break;
			const Transform child = {transform.offset + record.offset, transform.scale};
			RenderList(*list.Children()[record.child], child, depth + 1);
			break;
		}
		default:
			// unknown opcodes are skipped (specification 12)
			break;
	}
}


BLImage
Blend2DBackend::Data::Image(ImageHandle image)
{
	const std::scoped_lock lock(imageLock);
	const auto found = images.find(image);
	return found != images.end() ? found->second : BLImage();
}


std::shared_ptr<Blend2DBackend::Data::Font>
Blend2DBackend::Data::FontOf(FontId font)
{
	const std::scoped_lock lock(fontLock);
	const auto found = fonts.find(font);
	return found != fonts.end() ? found->second : nullptr;
}


Blend2DBackend::Blend2DBackend()
	:
	fData(std::make_unique<Data>())
{
	fData->owner = this;
}


Blend2DBackend::~Blend2DBackend()
{
	if (fData->context.isValid())
		(void)fData->context.end();
	ImageRegistry::Default().DetachBackend(*this);
}


bool
Blend2DBackend::Register(BackendRegistry& registry, int priority)
{
	return registry.Register("blend2d", priority,
		[] { return std::make_unique<Blend2DBackend>(); });
}


bool
Blend2DBackend::AddFont(FontId font, std::span<const std::byte> fontFile,
	std::uint32_t faceIndex)
{
	auto made = std::make_shared<Data::Font>();
	made->bytes.assign(fontFile.begin(), fontFile.end());
	if (made->data.createFromData(made->bytes.data(), made->bytes.size()) != BL_SUCCESS
		|| made->face.createFromData(made->data, faceIndex) != BL_SUCCESS) {
		return false;
	}
	const std::scoped_lock lock(fData->fontLock);
	fData->fonts[font] = std::move(made);
	return true;
}


std::expected<std::vector<std::byte>, Error>
Blend2DBackend::EncodePng(const PixelData& pixels)
{
	if (!pixels.IsValid())
		return Failure(ErrorCode::InvalidArgument, "no pixels");
	// Blend2D has no unpremultiplied 32-bit format: premultiplied first,
	// which the codec undoes; translucent pixels may change by rounding
	BLImage image;
	if (image.create(pixels.width, pixels.height, BL_FORMAT_PRGB32) != BL_SUCCESS)
		return Failure(ErrorCode::OutOfMemory, "Blend2D cannot make the image");
	BLImageData data{};
	if (image.makeMutable(&data) != BL_SUCCESS)
		return Failure(ErrorCode::OutOfMemory, "Blend2D cannot write the image");
	Premultiply(pixels, data);
	BLImageCodec codec;
	BLArray<uint8_t> encoded;
	if (codec.findByName("PNG") != BL_SUCCESS || image.writeToData(encoded, codec) != BL_SUCCESS)
		return Failure(ErrorCode::IoError, "Blend2D cannot encode the PNG");
	const auto* bytes = reinterpret_cast<const std::byte*>(encoded.data());
	return std::vector<std::byte>(bytes, bytes + encoded.size());
}


Caps
Blend2DBackend::Capabilities() const
{
	// FillPath and StrokePath (extended profile) are drawn natively.
	return Caps{.bezierPaths = true, .concavePolygons = true};
}


ImageHandle
Blend2DBackend::CreateImage(const PixelData& pixels)
{
	if (!pixels.IsValid())
		return 0;
	BLImage image;
	BLImageData data{};
	if (image.create(pixels.width, pixels.height, BL_FORMAT_PRGB32) != BL_SUCCESS
		|| image.makeMutable(&data) != BL_SUCCESS) {
		return 0;
	}
	Premultiply(pixels, data);
	const std::scoped_lock lock(fData->imageLock);
	const ImageHandle handle = ++fData->lastImage;
	fData->images.emplace(handle, std::move(image));
	return handle;
}


void
Blend2DBackend::ReleaseImage(ImageHandle image)
{
	const std::scoped_lock lock(fData->imageLock);
	fData->images.erase(image);
}


SurfaceRef
Blend2DBackend::CreateWindowSurface(const NativeWindow& window, SurfaceKind kind)
{
	if (kind != SurfaceKind::CpuRaster)
		return nullptr;
	return CreateOffscreenSurface({std::round(window.size.w * window.scale),
		std::round(window.size.h * window.scale)}, window.scale);
}


SurfaceRef
Blend2DBackend::CreateOffscreenSurface(Size device, float scale)
{
	if (!device.IsFinite() || !(scale > 0) || !std::isfinite(scale))
		return nullptr;
	BLImage image = MakeImage(device);
	if (image.empty())
		return nullptr;
	return std::make_shared<Blend2DSurface>(std::move(image), scale);
}


void
Blend2DBackend::ResizeSurface(Surface& surface, Size device, float scale)
{
	auto* blend2d = dynamic_cast<Blend2DSurface*>(&surface);
	if (blend2d == nullptr || !device.IsFinite() || !(scale > 0) || !std::isfinite(scale))
		return;
	BLImage image = MakeImage(device);
	if (image.empty())
		return;
	if (fData->frame == blend2d) {
		(void)fData->context.end();
		fData->frame = nullptr;
	}
	blend2d->fImage = std::move(image);
	blend2d->fScale = scale;
}


void
Blend2DBackend::BeginFrame(Surface& surface, const Color& clear)
{
	if (fData->context.isValid())
		(void)fData->context.end();
	fData->frame = dynamic_cast<Blend2DSurface*>(&surface);
	if (fData->frame == nullptr)
		return;
	// synchronous rendering, the default: the frame is done when
	// EndFrame() returns
	if (fData->context.begin(fData->frame->fImage) != BL_SUCCESS) {
		fData->frame = nullptr;
		return;
	}
	(void)fData->context.setCompOp(BL_COMP_OP_SRC_COPY);
	(void)fData->context.fillAll(ToRgba32(clear));
	(void)fData->context.setCompOp(BL_COMP_OP_SRC_OVER);
}


void
Blend2DBackend::Render(const DisplayList& list, Point offset)
{
	if (fData->frame == nullptr || !offset.IsFinite())
		return;
	fData->RenderList(list, {offset, fData->frame->fScale}, 0);
}


void
Blend2DBackend::EndFrame(Surface& surface)
{
	if (fData->frame != &surface)
		return;
	(void)fData->context.end();
	fData->frame = nullptr;
}


} // namespace ngui::gfx
