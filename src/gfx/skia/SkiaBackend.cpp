/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/SkiaBackend.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"
#include "include/ports/SkFontMgr_empty.h"

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/ImageRegistry.h>
#include <ngui/gfx/NativeWindow.h>


namespace ngui::gfx {


namespace {


// Nesting depth of DrawList beyond which children are not drawn; lists from
// the Canvas can be nested deeper than Deserialize() allows.
constexpr int kMaxListDepth = 256;


class SkiaSurface : public Surface {
public:
								SkiaSurface(sk_sp<SkSurface> surface, float scale);

			Size				DeviceSize() const override;
			float				Scale() const override;
			SurfaceKind			Kind() const override;
			std::optional<CpuPixels> Pixels() const override;

			sk_sp<SkSurface>	fSurface;
			float				fScale;
};


SkiaSurface::SkiaSurface(sk_sp<SkSurface> surface, float scale)
	:
	fSurface(std::move(surface)),
	fScale(scale)
{
}


Size
SkiaSurface::DeviceSize() const
{
	return {static_cast<float>(fSurface->width()), static_cast<float>(fSurface->height())};
}


float
SkiaSurface::Scale() const
{
	return fScale;
}


SurfaceKind
SkiaSurface::Kind() const
{
	return SurfaceKind::CpuRaster;
}


std::optional<CpuPixels>
SkiaSurface::Pixels() const
{
	SkPixmap pixmap;
	if (!fSurface->peekPixels(&pixmap))
		return std::nullopt;
	const PixelFormat format = pixmap.colorType() == kBGRA_8888_SkColorType
		? PixelFormat::Bgra8Premul : PixelFormat::Rgba8Premul;
	return CpuPixels{pixmap.width(), pixmap.height(), pixmap.rowBytes(), format,
		{static_cast<const std::byte*>(pixmap.addr()), pixmap.computeByteSize()}};
}


sk_sp<SkSurface>
_MakeRaster(Size device)
{
	const int width = static_cast<int>(std::lround(device.w));
	const int height = static_cast<int>(std::lround(device.h));
	if (width <= 0 || height <= 0)
		return nullptr;
	// No color space: blending in the encoded space (specification 4.3).
	return SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
}


SkColor4f
_Color(const Color& color)
{
	return {color.r, color.g, color.b, color.a};
}


// Where the logical coordinates of a list end up: shifted by the offset of
// Render() and of enclosing DrawLists, then scaled (specification 4.2).
struct Transform {
			Point				offset;
			float				scale;

			SkMatrix			Matrix() const;
			float				Device(float logical, float shift) const;
			SkIRect				DeviceRect(const Rect& rect) const;
};


SkMatrix
Transform::Matrix() const
{
	return SkMatrix::Scale(scale, scale).preTranslate(offset.x, offset.y);
}


// Rule A1: an edge rounded to whole device pixels after scaling.
float
Transform::Device(float logical, float shift) const
{
	return std::floor((logical + shift) * scale + 0.5f);
}


SkIRect
Transform::DeviceRect(const Rect& rect) const
{
	return SkIRect::MakeLTRB(static_cast<int>(Device(rect.x0, offset.x)),
		static_cast<int>(Device(rect.y0, offset.y)),
		static_cast<int>(Device(rect.x1, offset.x)),
		static_cast<int>(Device(rect.y1, offset.y)));
}


// The SkPaint of a recorded paint; with a gradient, its shader maps the
// logical gradient points through \a shaderMatrix (the transform when
// drawing in device space, none when drawing under the canvas matrix).
SkPaint
_Paint(const PaintRecord& record, const SkMatrix* shaderMatrix)
{
	SkPaint paint;
	paint.setColor4f(_Color(record.color), nullptr);
	paint.setAntiAlias(record.antialias != 0);
	paint.setStrokeWidth(record.strokeWidth);
	paint.setStrokeCap(static_cast<Paint::Cap>(record.cap) == Paint::Cap::Round
		? SkPaint::kRound_Cap
		: static_cast<Paint::Cap>(record.cap) == Paint::Cap::Square
			? SkPaint::kSquare_Cap : SkPaint::kButt_Cap);
	paint.setStrokeJoin(static_cast<Paint::Join>(record.join) == Paint::Join::Round
		? SkPaint::kRound_Join
		: static_cast<Paint::Join>(record.join) == Paint::Join::Bevel
			? SkPaint::kBevel_Join : SkPaint::kMiter_Join);
	if (record.hasGradient != 0) {
		const GradientRecord& gradient = record.gradient;
		const std::uint32_t count = std::min<std::uint32_t>(gradient.count, 4);
		SkColor4f colors[4];
		float positions[4];
		for (std::uint32_t i = 0; i < count; i++) {
			colors[i] = _Color(gradient.stops[i].color);
			positions[i] = gradient.stops[i].offset;
		}
		const SkPoint points[2] = {{gradient.p0.x, gradient.p0.y},
			{gradient.p1.x, gradient.p1.y}};
		// Interpolation not premultiplied, in the encoded space
		// (specification 8).
		SkGradient::Interpolation interpolation;
		interpolation.fInPremul = SkGradient::Interpolation::InPremul::kNo;
		const SkGradient description(SkGradient::Colors({colors, count}, {positions, count},
			SkTileMode::kClamp), interpolation);
		paint.setShader(SkShaders::LinearGradient(points, description, shaderMatrix));
	}
	return paint;
}


SkRect
_Rect(const Rect& rect)
{
	return SkRect::MakeLTRB(rect.x0, rect.y0, rect.x1, rect.y1);
}


SkRRect
_RRect(const RRect& rrect)
{
	const SkVector radii[4] = {{rrect.radii.tl, rrect.radii.tl}, {rrect.radii.tr, rrect.radii.tr},
		{rrect.radii.br, rrect.radii.br}, {rrect.radii.bl, rrect.radii.bl}};
	SkRRect result;
	result.setRectRadii(_Rect(rrect.rect), radii);
	return result;
}


bool
_HasRadii(const RRect& rrect)
{
	return rrect.radii.tl > 0 || rrect.radii.tr > 0 || rrect.radii.br > 0 || rrect.radii.bl > 0;
}


} // namespace


struct SkiaBackend::Data {
			std::mutex			imageLock;
			std::unordered_map<ImageHandle, sk_sp<SkImage>> images;
			ImageHandle			lastImage = 0;

			std::mutex			fontLock;
			sk_sp<SkFontMgr>	fontManager = SkFontMgr_New_Custom_Empty();
			std::unordered_map<FontId, sk_sp<SkTypeface>> fonts;

			SkiaSurface*		frame = nullptr;

			void				RenderList(SkCanvas* canvas, const DisplayList& list,
									const Transform& transform, int depth);
			void				Draw(SkCanvas* canvas, const DisplayList& list, const Op& op,
									const Transform& transform, int depth);

			// Rule A1: fills a rectangle in device pixels, without
			// antialiasing.
			void				FillDevice(SkCanvas* canvas, const SkIRect& rect,
									const SkPaint& paint);
			sk_sp<SkImage>		Image(ImageHandle image);
			sk_sp<SkTypeface>	Typeface(FontId font);
			SkiaBackend*		owner = nullptr;
};


void
SkiaBackend::Data::RenderList(SkCanvas* canvas, const DisplayList& list,
	const Transform& transform, int depth)
{
	const int listLevel = canvas->getSaveCount();
	std::uint32_t clip = UINT32_MAX;
	for (const Op& op : list.Ops()) {
		if (op.ClipIndex() != clip) {
			clip = op.ClipIndex();
			canvas->restoreToCount(listLevel);
			canvas->save();
			// The clip of the list, in device pixels (specification 13.2);
			// intersected with the clips of enclosing lists by save().
			std::vector<SkIRect> rects;
			for (const IntRect& rect : list.Clip(clip))
				rects.push_back(transform.DeviceRect(rect.ToRect()));
			SkRegion region;
			region.setRects(rects.data(), static_cast<int>(rects.size()));
			canvas->clipRegion(region);
		}
		Draw(canvas, list, op, transform, depth);
	}
	canvas->restoreToCount(listLevel);
}


void
SkiaBackend::Data::FillDevice(SkCanvas* canvas, const SkIRect& rect, const SkPaint& paint)
{
	if (rect.isEmpty())
		return;
	SkPaint device = paint;
	device.setAntiAlias(false);
	device.setStyle(SkPaint::kFill_Style);
	canvas->drawIRect(rect, device);
}


void
SkiaBackend::Data::Draw(SkCanvas* canvas, const DisplayList& list, const Op& op,
	const Transform& transform, int depth)
{
	const SkMatrix matrix = transform.Matrix();
	// Operations under rule A2 draw under the canvas matrix; A1 operations
	// draw in device pixels with the identity matrix.
	const auto underMatrix = [&](auto draw) {
		canvas->save();
		canvas->concat(matrix);
		draw();
		canvas->restore();
	};

	switch (op.Code()) {
		case OpCode::FillRect:
		{
			const RectRecord& record = op.As<RectRecord>();
			FillDevice(canvas, transform.DeviceRect(record.rect), _Paint(record.paint, &matrix));
			break;
		}
		case OpCode::FillRRect:
		{
			const RRectRecord& record = op.As<RRectRecord>();
			if (!_HasRadii(record.rrect)) {
				FillDevice(canvas, transform.DeviceRect(record.rrect.rect),
					_Paint(record.paint, &matrix));
				break;
			}
			underMatrix([&] { canvas->drawRRect(_RRect(record.rrect),
				_Paint(record.paint, nullptr)); });
			break;
		}
		case OpCode::FillRing:
		{
			const RingRecord& record = op.As<RingRecord>();
			underMatrix([&] { canvas->drawDRRect(_RRect(record.outer), _RRect(record.inner),
				_Paint(record.paint, nullptr)); });
			break;
		}
		case OpCode::FillEllipse:
		{
			const RectRecord& record = op.As<RectRecord>();
			underMatrix([&] { canvas->drawOval(_Rect(record.rect),
				_Paint(record.paint, nullptr)); });
			break;
		}
		case OpCode::FillConvexPolygon:
		case OpCode::StrokePolyline:
		{
			const PointsRecord& record = op.As<PointsRecord>();
			const std::span<const Point> points = op.Points();
			SkPathBuilder builder;
			builder.moveTo(points[0].x, points[0].y);
			for (std::size_t i = 1; i < points.size(); i++)
				builder.lineTo(points[i].x, points[i].y);
			const bool fill = op.Code() == OpCode::FillConvexPolygon;
			if (fill || record.closed != 0)
				builder.close();
			const SkPath path = builder.detach();
			SkPaint paint = _Paint(record.paint, nullptr);
			paint.setStyle(fill ? SkPaint::kFill_Style : SkPaint::kStroke_Style);
			underMatrix([&] { canvas->drawPath(path, paint); });
			break;
		}
		case OpCode::FillPath:
		case OpCode::StrokePath:
		{
			// Extended profile: Skia draws paths natively.
			const PathRecord& record = op.As<PathRecord>();
			const std::span<const Point> points = op.PathPoints();
			const bool fill = op.Code() == OpCode::FillPath;
			SkPathBuilder builder(fill && record.fillRule == 1 ? SkPathFillType::kEvenOdd
				: SkPathFillType::kWinding);
			std::size_t next = 0;
			for (const PathVerb verb : op.PathVerbs()) {
				switch (verb) {
					case PathVerb::Move:
						builder.moveTo(points[next].x, points[next].y);
						next++;
						break;
					case PathVerb::Line:
						builder.lineTo(points[next].x, points[next].y);
						next++;
						break;
					case PathVerb::Cubic:
						builder.cubicTo(points[next].x, points[next].y, points[next + 1].x,
							points[next + 1].y, points[next + 2].x, points[next + 2].y);
						next += 3;
						break;
					case PathVerb::Close:
						builder.close();
						break;
				}
			}
			const SkPath path = builder.detach();
			SkPaint paint = _Paint(record.paint, nullptr);
			paint.setStyle(fill ? SkPaint::kFill_Style : SkPaint::kStroke_Style);
			underMatrix([&] { canvas->drawPath(path, paint); });
			break;
		}
		case OpCode::StrokeRect:
		{
			// Rule A1: the frame lies inside the rectangle (specification
			// 6.1), drawn as four device rectangles.
			const RectRecord& record = op.As<RectRecord>();
			const SkIRect outer = transform.DeviceRect(record.rect);
			const int width = static_cast<int>(std::floor(record.paint.strokeWidth
				* transform.scale + 0.5f));
			if (width <= 0)
				break;
			const SkPaint paint = _Paint(record.paint, &matrix);
			if (2 * width >= outer.width() || 2 * width >= outer.height()) {
				FillDevice(canvas, outer, paint);
				break;
			}
			FillDevice(canvas, {outer.fLeft, outer.fTop, outer.fRight, outer.fTop + width},
				paint);
			FillDevice(canvas, {outer.fLeft, outer.fBottom - width, outer.fRight,
				outer.fBottom}, paint);
			FillDevice(canvas, {outer.fLeft, outer.fTop + width, outer.fLeft + width,
				outer.fBottom - width}, paint);
			FillDevice(canvas, {outer.fRight - width, outer.fTop + width, outer.fRight,
				outer.fBottom - width}, paint);
			break;
		}
		case OpCode::StrokeRRect:
		{
			// The frame lies inside: inset by half the width, then stroked
			// centered (specification 13.1).
			const RRectRecord& record = op.As<RRectRecord>();
			const float half = record.paint.strokeWidth / 2;
			const RRect inset = {record.rrect.rect.InsetByCopy(half, half),
				{std::max(0.0f, record.rrect.radii.tl - half),
					std::max(0.0f, record.rrect.radii.tr - half),
					std::max(0.0f, record.rrect.radii.br - half),
					std::max(0.0f, record.rrect.radii.bl - half)}};
			if (inset.rect.IsEmpty() || record.paint.strokeWidth <= 0)
				break;
			SkPaint paint = _Paint(record.paint, nullptr);
			paint.setStyle(SkPaint::kStroke_Style);
			underMatrix([&] { canvas->drawRRect(_RRect(inset), paint); });
			break;
		}
		case OpCode::StrokeArc:
		{
			// Same angle convention as Skia: 0° at 3 o'clock, clockwise.
			const ArcRecord& record = op.As<ArcRecord>();
			SkPaint paint = _Paint(record.paint, nullptr);
			paint.setStyle(SkPaint::kStroke_Style);
			underMatrix([&] { canvas->drawArc(_Rect(record.oval), record.startDeg,
				record.sweepDeg, false, paint); });
			break;
		}
		case OpCode::DrawLines:
		{
			const LinesRecord& record = op.As<LinesRecord>();
			const auto cap = static_cast<Paint::Cap>(record.cap);
			const float half = record.width / 2;
			for (const Line& line : op.Lines()) {
				SkPaint paint;
				paint.setColor4f(_Color(line.color), nullptr);
				const bool horizontal = line.p0.y == line.p1.y;
				const bool vertical = line.p0.x == line.p1.x;
				if ((horizontal || vertical) && cap != Paint::Cap::Round) {
					// Rule A3: the line widened by w/2 to both sides, the cap
					// extends Square lines by w/2 along their length.
					const float extend = cap == Paint::Cap::Square ? half : 0;
					const float xMin = std::min(line.p0.x, line.p1.x);
					const float xMax = std::max(line.p0.x, line.p1.x);
					const float yMin = std::min(line.p0.y, line.p1.y);
					const float yMax = std::max(line.p0.y, line.p1.y);
					const Rect rect = horizontal && !(vertical && cap == Paint::Cap::Butt)
						? Rect{xMin - extend, yMin - half, xMax + extend, yMax + half}
						: Rect{xMin - half, yMin - extend, xMax + half, yMax + extend};
					FillDevice(canvas, transform.DeviceRect(rect), paint);
					continue;
				}
				paint.setAntiAlias(true);
				paint.setStyle(SkPaint::kStroke_Style);
				paint.setStrokeWidth(record.width);
				paint.setStrokeCap(cap == Paint::Cap::Round ? SkPaint::kRound_Cap
					: cap == Paint::Cap::Square ? SkPaint::kSquare_Cap : SkPaint::kButt_Cap);
				underMatrix([&] { canvas->drawLine(line.p0.x, line.p0.y, line.p1.x,
					line.p1.y, paint); });
			}
			break;
		}
		case OpCode::DrawImage:
		{
			const ImageRecord& record = op.As<ImageRecord>();
			const ImageHandle resource = ImageRegistry::Default().BackendImage(record.image,
				*owner);
			const sk_sp<SkImage> image = Image(resource);
			if (!image)
				break;
			// Nearest at 1:1, otherwise linear (specification 6.1), judged
			// in device pixels.
			const bool oneToOne = record.src.Width() == record.dst.Width() * transform.scale
				&& record.src.Height() == record.dst.Height() * transform.scale;
			const SkSamplingOptions sampling = oneToOne
				? SkSamplingOptions(SkFilterMode::kNearest)
				: SkSamplingOptions(SkFilterMode::kLinear);
			SkPaint paint;
			paint.setAlphaf(record.alpha);
			underMatrix([&] { canvas->drawImageRect(image, _Rect(record.src), _Rect(record.dst),
				sampling, &paint, SkCanvas::kStrict_SrcRectConstraint); });
			break;
		}
		case OpCode::DrawGlyphRun:
		{
			const GlyphRunRecord& record = op.As<GlyphRunRecord>();
			const sk_sp<SkTypeface> typeface = Typeface(record.font);
			if (!typeface)
				break;
			// Specification 13.4: subpixel positioning, grayscale
			// antialiasing, no hinting.
			SkFont font(typeface, record.sizePx);
			font.setSubpixel(true);
			font.setEdging(SkFont::Edging::kAntiAlias);
			font.setHinting(SkFontHinting::kNone);
			const std::span<const std::uint32_t> glyphs = op.Glyphs();
			const std::span<const Point> positions = op.Positions();
			SkTextBlobBuilder builder;
			const SkTextBlobBuilder::RunBuffer& run = builder.allocRunPos(font,
				static_cast<int>(glyphs.size()));
			for (std::size_t i = 0; i < glyphs.size(); i++) {
				run.glyphs[i] = static_cast<SkGlyphID>(glyphs[i]);
				run.points()[i] = {positions[i].x, positions[i].y};
			}
			const sk_sp<SkTextBlob> blob = builder.make();
			SkPaint paint;
			paint.setColor4f(_Color(record.color), nullptr);
			paint.setAntiAlias(true);
			underMatrix([&] { canvas->drawTextBlob(blob, record.origin.x, record.origin.y,
				paint); });
			break;
		}
		case OpCode::DrawList:
		{
			const ListRecord& record = op.As<ListRecord>();
			if (depth >= kMaxListDepth)
				break;
			const Transform child = {transform.offset + record.offset, transform.scale};
			RenderList(canvas, *list.Children()[record.child], child, depth + 1);
			break;
		}
		default:
			// Unknown opcodes are skipped (specification 12).
			break;
	}
}


sk_sp<SkImage>
SkiaBackend::Data::Image(ImageHandle image)
{
	const std::scoped_lock lock(imageLock);
	const auto found = images.find(image);
	return found != images.end() ? found->second : nullptr;
}


sk_sp<SkTypeface>
SkiaBackend::Data::Typeface(FontId font)
{
	const std::scoped_lock lock(fontLock);
	const auto found = fonts.find(font);
	return found != fonts.end() ? found->second : nullptr;
}


SkiaBackend::SkiaBackend()
	:
	fData(std::make_unique<Data>())
{
	fData->owner = this;
}


SkiaBackend::~SkiaBackend()
{
	ImageRegistry::Default().DetachBackend(*this);
}


bool
SkiaBackend::Register(BackendRegistry& registry, int priority)
{
	return registry.Register("skia", priority, [] { return std::make_unique<SkiaBackend>(); });
}


bool
SkiaBackend::AddFont(FontId font, std::span<const std::byte> fontFile)
{
	const sk_sp<SkData> data = SkData::MakeWithCopy(fontFile.data(), fontFile.size());
	const std::scoped_lock lock(fData->fontLock);
	sk_sp<SkTypeface> typeface = fData->fontManager->makeFromData(data);
	if (!typeface)
		return false;
	fData->fonts[font] = std::move(typeface);
	return true;
}


Caps
SkiaBackend::Capabilities() const
{
	// FillPath and StrokePath (extended profile) are drawn natively.
	return Caps{.bezierPaths = true, .concavePolygons = true};
}


ImageHandle
SkiaBackend::CreateImage(const PixelData& pixels)
{
	if (!pixels.IsValid())
		return 0;
	const SkImageInfo info = SkImageInfo::Make(pixels.width, pixels.height,
		kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
	const SkPixmap pixmap(info, pixels.bytes.data(), pixels.stride);
	sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);
	if (!image)
		return 0;
	const std::scoped_lock lock(fData->imageLock);
	const ImageHandle handle = ++fData->lastImage;
	fData->images.emplace(handle, std::move(image));
	return handle;
}


void
SkiaBackend::ReleaseImage(ImageHandle image)
{
	const std::scoped_lock lock(fData->imageLock);
	fData->images.erase(image);
}


SurfaceRef
SkiaBackend::CreateWindowSurface(const NativeWindow& window, SurfaceKind kind)
{
	// A CPU surface of the window's size in device pixels; the CPU
	// presenter shows it (specification 12).
	if (kind != SurfaceKind::CpuRaster)
		return nullptr;
	return CreateOffscreenSurface({std::round(window.size.w * window.scale),
		std::round(window.size.h * window.scale)}, window.scale);
}


SurfaceRef
SkiaBackend::CreateOffscreenSurface(Size device, float scale)
{
	if (!device.IsFinite() || !(scale > 0) || !std::isfinite(scale))
		return nullptr;
	sk_sp<SkSurface> surface = _MakeRaster(device);
	if (!surface)
		return nullptr;
	return std::make_shared<SkiaSurface>(std::move(surface), scale);
}


void
SkiaBackend::ResizeSurface(Surface& surface, Size device, float scale)
{
	auto* skia = dynamic_cast<SkiaSurface*>(&surface);
	if (skia == nullptr || !device.IsFinite() || !(scale > 0) || !std::isfinite(scale))
		return;
	sk_sp<SkSurface> raster = _MakeRaster(device);
	if (!raster)
		return;
	skia->fSurface = std::move(raster);
	skia->fScale = scale;
}


void
SkiaBackend::BeginFrame(Surface& surface, const Color& clear)
{
	fData->frame = dynamic_cast<SkiaSurface*>(&surface);
	if (fData->frame == nullptr)
		return;
	SkCanvas* canvas = fData->frame->fSurface->getCanvas();
	canvas->restoreToCount(1);
	canvas->resetMatrix();
	canvas->clear(_Color(clear));
}


void
SkiaBackend::Render(const DisplayList& list, Point offset)
{
	if (fData->frame == nullptr || !offset.IsFinite())
		return;
	SkCanvas* canvas = fData->frame->fSurface->getCanvas();
	fData->RenderList(canvas, list, {offset, fData->frame->fScale}, 0);
}


void
SkiaBackend::EndFrame(Surface& surface)
{
	if (fData->frame != &surface)
		return;
	fData->frame = nullptr;
}


} // namespace ngui::gfx
