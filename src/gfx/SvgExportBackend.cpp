/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The SVG export backend (specification 19.3). The body is written first,
// collecting what goes into <defs> (clip paths, gradients, images, glyphs)
// in order of first use; then header, defs and body go out together.


#include <ngui/gfx/SvgExportBackend.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>
#include <vector>

#include <ngui/TextSystem.h>
#include <ngui/gfx/ImageRegistry.h>
#include <ngui/gfx/Path.h>
#include <ngui/gfx/RRect.h>

#include "DumpNumber.h"
#include "PngWriter.h"


namespace ngui::gfx {


namespace {


// How deep nested lists are followed, as in the render backends.
constexpr int kMaxListDepth = 64;


std::string
Number(float value)
{
	return DumpNumber(value);
}


std::uint8_t
Channel(float value)
{
	return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}


// "#rrggbb" (E3).
std::string
Hex(const Color& color)
{
	static constexpr std::string_view kDigits = "0123456789abcdef";
	std::string text = "#";
	for (const float channel : {color.r, color.g, color.b}) {
		const std::uint8_t value = Channel(channel);
		text += kDigits[value >> 4];
		text += kDigits[value & 15];
	}
	return text;
}


// \a text for an attribute value or element content.
std::string
Escaped(std::string_view text)
{
	std::string escaped;
	for (const char c : text) {
		switch (c) {
			case '&':
				escaped += "&amp;";
				break;
			case '<':
				escaped += "&lt;";
				break;
			case '>':
				escaped += "&gt;";
				break;
			case '"':
				escaped += "&quot;";
				break;
			default:
				escaped += c;
		}
	}
	return escaped;
}


// A point on the ellipse of \a oval at \a degrees (0° at 3 o'clock,
// clockwise).
Point
OnEllipse(const Rect& oval, float degrees)
{
	const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
	const double cx = (static_cast<double>(oval.x0) + oval.x1) / 2;
	const double cy = (static_cast<double>(oval.y0) + oval.y1) / 2;
	return {static_cast<float>(cx + (oval.x1 - oval.x0) / 2.0 * std::cos(radians)),
		static_cast<float>(cy + (oval.y1 - oval.y0) / 2.0 * std::sin(radians))};
}


std::string
PointText(Point point)
{
	return Number(point.x) + " " + Number(point.y);
}


// The contour of a rounded rectangle, clockwise from the top left, with
// elliptical arcs of radius × radius at the corners (6.1).
std::string
RRectPath(const RRect& rrect)
{
	const RRect shape = rrect.Normalized();
	const Rect& r = shape.rect;
	const Radii& radii = shape.radii;
	const auto arc = [](float radius, Point end) {
		if (radius <= 0)
			return "L" + PointText(end);
		return "A" + Number(radius) + " " + Number(radius) + " 0 0 1 " + PointText(end);
	};
	std::string d = "M" + PointText({r.x0 + radii.tl, r.y0});
	d += "L" + PointText({r.x1 - radii.tr, r.y0});
	d += arc(radii.tr, {r.x1, r.y0 + radii.tr});
	d += "L" + PointText({r.x1, r.y1 - radii.br});
	d += arc(radii.br, {r.x1 - radii.br, r.y1});
	d += "L" + PointText({r.x0 + radii.bl, r.y1});
	d += arc(radii.bl, {r.x0, r.y1 - radii.bl});
	d += "L" + PointText({r.x0, r.y0 + radii.tl});
	d += arc(radii.tl, {r.x0 + radii.tl, r.y0});
	return d + "Z";
}


std::string
PathData(std::span<const PathVerb> verbs, std::span<const Point> points)
{
	std::string d;
	std::size_t at = 0;
	for (const PathVerb verb : verbs) {
		switch (verb) {
			case PathVerb::Move:
				if (at < points.size())
					d += "M" + PointText(points[at++]);
				break;
			case PathVerb::Line:
				if (at < points.size())
					d += "L" + PointText(points[at++]);
				break;
			case PathVerb::Cubic:
				if (at + 3 <= points.size()) {
					d += "C" + PointText(points[at]) + " " + PointText(points[at + 1]) + " "
						+ PointText(points[at + 2]);
					at += 3;
				}
				break;
			case PathVerb::Close:
				d += "Z";
				break;
		}
	}
	return d;
}


std::string
PointsText(std::span<const Point> points)
{
	std::string text;
	for (const Point& point : points) {
		if (!text.empty())
			text += ' ';
		text += Number(point.x) + "," + Number(point.y);
	}
	return text;
}


const char*
CapName(Paint::Cap cap)
{
	switch (cap) {
		case Paint::Cap::Round:
			return "round";
		case Paint::Cap::Square:
			return "square";
		case Paint::Cap::Butt:
			break;
	}
	return "butt";
}


const char*
JoinName(Paint::Join join)
{
	switch (join) {
		case Paint::Join::Round:
			return "round";
		case Paint::Join::Bevel:
			return "bevel";
		case Paint::Join::Miter:
			break;
	}
	return "miter";
}


// The SVG of one export: the defs found on the way and the body.
class Writer {
public:
	Writer(const TextSystem& text, const ExportOptions& options)
		:
		fText(text),
		fOptions(options)
	{
	}

	void List(const DisplayList& list, int depth);
	std::string Document(const Rect& bounds) const;

private:
	void _Open(const std::string& element);
	void _Close(const std::string& element);
	void _Element(const std::string& element);
	void _Op(const DisplayList& list, const Op& op, int depth);
	std::string _Fill(const PaintRecord& paint);
	std::string _Stroke(const PaintRecord& paint, float width);
	std::string _Crisp(const PaintRecord& paint) const;
	std::string _Clip(std::span<const IntRect> rects);
	std::string _Gradient(const GradientRecord& gradient);
	std::optional<std::string> _Image(ImageHandle image, const Rect& src);
	std::optional<std::string> _Glyph(FontId font, std::uint32_t glyph, float sizePx);
	void _GlyphRun(const Op& op);
	bool _TextRun(const Op& op);
	void _DefLine(const std::string& element);

	const TextSystem&	fText;
	const ExportOptions& fOptions;
	std::string			fBody;
	int					fDepth = 1;
	std::string			fDefs;
	std::map<std::string, std::string> fClips;		// rects → id
	std::map<std::string, std::string> fGradients;	// definition → id
	std::map<std::tuple<ImageHandle, float, float, float, float>, std::string> fImages;
	std::map<std::tuple<FontId, std::uint32_t, float>, std::optional<std::string>> fGlyphs;
	int					fNextId = 1;
};


void
Writer::_Element(const std::string& element)
{
	if (fOptions.pretty)
		fBody += std::string(static_cast<std::size_t>(fDepth) * 2, ' ');
	fBody += element;
	if (fOptions.pretty)
		fBody += '\n';
}


void
Writer::_Open(const std::string& element)
{
	_Element(element);
	fDepth++;
}


void
Writer::_Close(const std::string& element)
{
	fDepth--;
	_Element(element);
}


void
Writer::_DefLine(const std::string& element)
{
	if (fOptions.pretty)
		fDefs += "    ";
	fDefs += element;
	if (fOptions.pretty)
		fDefs += '\n';
}


// fill and fill-opacity, or a gradient (E3).
std::string
Writer::_Fill(const PaintRecord& paint)
{
	if (paint.hasGradient != 0)
		return " fill=\"url(#" + _Gradient(paint.gradient) + ")\"";
	std::string text = " fill=\"" + Hex(paint.color) + "\"";
	if (paint.color.a < 1)
		text += " fill-opacity=\"" + Number(paint.color.a) + "\"";
	return text;
}


std::string
Writer::_Stroke(const PaintRecord& paint, float width)
{
	std::string text = " fill=\"none\"";
	if (paint.hasGradient != 0)
		text += " stroke=\"url(#" + _Gradient(paint.gradient) + ")\"";
	else {
		text += " stroke=\"" + Hex(paint.color) + "\"";
		if (paint.color.a < 1)
			text += " stroke-opacity=\"" + Number(paint.color.a) + "\"";
	}
	return text + " stroke-width=\"" + Number(width) + "\"";
}


// Without antialiasing (Paint::antialias false, rule A2).
std::string
Writer::_Crisp(const PaintRecord& paint) const
{
	return paint.antialias == 0 ? " shape-rendering=\"crispEdges\"" : "";
}


std::string
Writer::_Clip(std::span<const IntRect> rects)
{
	// the extent in 64 bits: a region may span the whole range of int32
	std::string key;
	for (const IntRect& rect : rects) {
		key += "<rect x=\"" + std::to_string(rect.x0) + "\" y=\"" + std::to_string(rect.y0)
			+ "\" width=\"" + std::to_string(std::int64_t{rect.x1} - rect.x0) + "\" height=\""
			+ std::to_string(std::int64_t{rect.y1} - rect.y0) + "\"/>";
	}
	const auto found = fClips.find(key);
	if (found != fClips.end())
		return found->second;
	const std::string id = "c" + std::to_string(fNextId++);
	fClips.emplace(key, id);
	_DefLine("<clipPath id=\"" + id + "\">" + key + "</clipPath>");
	return id;
}


std::string
Writer::_Gradient(const GradientRecord& gradient)
{
	std::string body = " gradientUnits=\"userSpaceOnUse\" x1=\"" + Number(gradient.p0.x)
		+ "\" y1=\"" + Number(gradient.p0.y) + "\" x2=\"" + Number(gradient.p1.x) + "\" y2=\""
		+ Number(gradient.p1.y) + "\">";
	for (std::uint32_t i = 0; i < std::min<std::uint32_t>(gradient.count, 4); i++) {
		const GradientStop& stop = gradient.stops[i];
		body += "<stop offset=\"" + Number(stop.offset) + "\" stop-color=\""
			+ Hex(stop.color) + "\"";
		if (stop.color.a < 1)
			body += " stop-opacity=\"" + Number(stop.color.a) + "\"";
		body += "/>";
	}
	const auto found = fGradients.find(body);
	if (found != fGradients.end())
		return found->second;
	const std::string id = "g" + std::to_string(fNextId++);
	fGradients.emplace(body, id);
	_DefLine("<linearGradient id=\"" + id + "\"" + body + "</linearGradient>");
	return id;
}


// The part \a src of \a image as an <image> in the defs, once.
std::optional<std::string>
Writer::_Image(ImageHandle image, const Rect& src)
{
	const auto key = std::make_tuple(image, src.x0, src.y0, src.x1, src.y1);
	const auto found = fImages.find(key);
	if (found != fImages.end())
		return found->second;
	const std::optional<Size> size = ImageRegistry::Default().PixelSize(image);
	if (!size.has_value())
		return std::nullopt;
	// the source in whole pixels inside the image
	const auto clampTo = [](float value, float limit) {
		return static_cast<std::int32_t>(std::clamp(std::floor(value + 0.5f), 0.0f, limit));
	};
	const std::int32_t x0 = clampTo(src.x0, size->w);
	const std::int32_t y0 = clampTo(src.y0, size->h);
	const std::int32_t x1 = clampTo(src.x1, size->w);
	const std::int32_t y1 = clampTo(src.y1, size->h);
	if (x1 <= x0 || y1 <= y0)
		return std::nullopt;
	const std::string id = "i" + std::to_string(fNextId++);
	std::string element = "<image id=\"" + id + "\" width=\"" + std::to_string(x1 - x0)
		+ "\" height=\"" + std::to_string(y1 - y0) + "\" preserveAspectRatio=\"none\"";
	if (fOptions.embedImages) {
		if (const auto pixels = ImageRegistry::Default().Pixels(image)) {
			const auto width = static_cast<std::size_t>(size->w);
			std::vector<std::uint8_t> part;
			for (std::int32_t y = y0; y < y1; y++) {
				const auto row = pixels->begin()
					+ static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * width
						+ static_cast<std::size_t>(x0)) * 4);
				part.insert(part.end(), row, row + (x1 - x0) * 4);
			}
			element += " xlink:href=\"data:image/png;base64,"
				+ detail::Base64(detail::EncodeStoredPng(static_cast<std::uint32_t>(x1 - x0),
					static_cast<std::uint32_t>(y1 - y0), part))
				+ "\"";
		}
	}
	_DefLine(element + "/>");
	fImages.emplace(key, id);
	return id;
}


// The outline of a glyph as a <path> in the defs, once; nullopt without one.
std::optional<std::string>
Writer::_Glyph(FontId font, std::uint32_t glyph, float sizePx)
{
	const auto key = std::make_tuple(font, glyph, sizePx);
	const auto found = fGlyphs.find(key);
	if (found != fGlyphs.end())
		return found->second;
	std::optional<std::string> id;
	if (const std::optional<Path> outline = fText.GlyphOutline(font, glyph, sizePx)) {
		id = "p" + std::to_string(fNextId++);
		_DefLine("<path id=\"" + *id + "\" d=\"" + PathData(outline->Verbs(),
			outline->Points()) + "\"/>");
	}
	fGlyphs.emplace(key, id);
	return id;
}


// A run as <text> with the family of its font and a position per character;
// false if its glyphs do not map 1:1 to the characters of its text.
bool
Writer::_TextRun(const Op& op)
{
	const GlyphRunRecord& record = op.As<GlyphRunRecord>();
	const std::u8string_view text = op.Text();
	const std::span<const std::uint32_t> clusters = op.Clusters();
	const std::span<const Point> positions = op.Positions();
	if (text.empty() || clusters.size() != positions.size())
		return false;
	// one glyph per code point, in order
	std::vector<std::size_t> starts;
	for (std::size_t i = 0; i < text.size(); i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			starts.push_back(i);
	}
	if (starts.size() != clusters.size())
		return false;
	for (std::size_t i = 0; i < starts.size(); i++) {
		if (clusters[i] != starts[i])
			return false;
	}
	std::string family = "sans-serif";
	std::string weight;
	std::string style;
	for (const FontFamilyStyle& font : fText.Styles()) {
		if (font.font == record.font) {
			family = font.family;
			if (font.face.Has(BFontFace::Bold))
				weight = " font-weight=\"bold\"";
			if (font.face.Has(BFontFace::Italic))
				style = " font-style=\"italic\"";
			break;
		}
	}
	std::string xs;
	std::string ys;
	for (const Point& position : positions) {
		xs += (xs.empty() ? "" : " ") + Number(record.origin.x + position.x);
		ys += (ys.empty() ? "" : " ") + Number(record.origin.y + position.y);
	}
	std::string element = "<text x=\"" + xs + "\" y=\"" + ys + "\" font-family=\""
		+ Escaped(family) + "\" font-size=\"" + Number(record.sizePx) + "\"" + weight + style
		+ " fill=\"" + Hex(record.color) + "\"";
	if (record.color.a < 1)
		element += " fill-opacity=\"" + Number(record.color.a) + "\"";
	const std::string_view chars(reinterpret_cast<const char*>(text.data()), text.size());
	_Element(element + " xml:space=\"preserve\">" + Escaped(chars) + "</text>");
	return true;
}


void
Writer::_GlyphRun(const Op& op)
{
	if (fOptions.text == ExportOptions::TextMode::Text && _TextRun(op))
		return;
	const GlyphRunRecord& record = op.As<GlyphRunRecord>();
	const std::span<const std::uint32_t> glyphs = op.Glyphs();
	const std::span<const Point> positions = op.Positions();
	std::vector<std::string> uses;
	bool outlined = true;
	for (std::size_t i = 0; i < glyphs.size() && i < positions.size(); i++) {
		const std::optional<std::string> id = _Glyph(record.font, glyphs[i], record.sizePx);
		if (!id.has_value()) {
			outlined = false;
			break;
		}
		uses.push_back("<use xlink:href=\"#" + *id + "\" x=\""
			+ Number(record.origin.x + positions[i].x) + "\" y=\""
			+ Number(record.origin.y + positions[i].y) + "\"/>");
	}
	// without outlines, the text if it can be written
	if (!outlined) {
		(void)_TextRun(op);
		return;
	}
	std::string group = "<g fill=\"" + Hex(record.color) + "\"";
	if (record.color.a < 1)
		group += " fill-opacity=\"" + Number(record.color.a) + "\"";
	_Open(group + ">");
	for (const std::string& use : uses)
		_Element(use);
	_Close("</g>");
}


void
Writer::_Op(const DisplayList& list, const Op& op, int depth)
{
	switch (op.Code()) {
		case OpCode::FillRect:
		{
			const RectRecord& record = op.As<RectRecord>();
			const Rect& r = record.rect;
			_Element("<rect x=\"" + Number(r.x0) + "\" y=\"" + Number(r.y0) + "\" width=\""
				+ Number(r.x1 - r.x0) + "\" height=\"" + Number(r.y1 - r.y0) + "\""
				+ _Fill(record.paint) + " shape-rendering=\"crispEdges\"/>");
			break;
		}
		case OpCode::FillRRect:
		{
			const RRectRecord& record = op.As<RRectRecord>();
			_Element("<path d=\"" + RRectPath(record.rrect) + "\"" + _Fill(record.paint)
				+ _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::FillRing:
		{
			const RingRecord& record = op.As<RingRecord>();
			_Element("<path d=\"" + RRectPath(record.outer) + RRectPath(record.inner)
				+ "\" fill-rule=\"evenodd\"" + _Fill(record.paint) + _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::FillEllipse:
		{
			const RectRecord& record = op.As<RectRecord>();
			const Rect& r = record.rect;
			_Element("<ellipse cx=\"" + Number((r.x0 + r.x1) / 2) + "\" cy=\""
				+ Number((r.y0 + r.y1) / 2) + "\" rx=\"" + Number((r.x1 - r.x0) / 2)
				+ "\" ry=\"" + Number((r.y1 - r.y0) / 2) + "\"" + _Fill(record.paint)
				+ _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::FillConvexPolygon:
		{
			const PointsRecord& record = op.As<PointsRecord>();
			_Element("<polygon points=\"" + PointsText(op.Points()) + "\""
				+ _Fill(record.paint) + _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::StrokeRect:
		{
			// inside the rectangle (6.1): its center line half the width in
			const RectRecord& record = op.As<RectRecord>();
			const float half = record.paint.strokeWidth / 2;
			const Rect r = {record.rect.x0 + half, record.rect.y0 + half,
				record.rect.x1 - half, record.rect.y1 - half};
			if (r.x1 < r.x0 || r.y1 < r.y0)
				break;
			_Element("<rect x=\"" + Number(r.x0) + "\" y=\"" + Number(r.y0) + "\" width=\""
				+ Number(r.x1 - r.x0) + "\" height=\"" + Number(r.y1 - r.y0) + "\""
				+ _Stroke(record.paint, record.paint.strokeWidth)
				+ " shape-rendering=\"crispEdges\"/>");
			break;
		}
		case OpCode::StrokeRRect:
		{
			const RRectRecord& record = op.As<RRectRecord>();
			const float half = record.paint.strokeWidth / 2;
			const Radii& radii = record.rrect.radii;
			const RRect inset = {record.rrect.rect.InsetByCopy(half, half),
				{std::max(0.0f, radii.tl - half), std::max(0.0f, radii.tr - half),
					std::max(0.0f, radii.br - half), std::max(0.0f, radii.bl - half)}};
			if (inset.rect.IsEmpty())
				break;
			_Element("<path d=\"" + RRectPath(inset) + "\""
				+ _Stroke(record.paint, record.paint.strokeWidth) + _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::StrokePolyline:
		{
			const PointsRecord& record = op.As<PointsRecord>();
			const auto cap = static_cast<Paint::Cap>(record.paint.cap);
			const auto join = static_cast<Paint::Join>(record.paint.join);
			_Element(std::string(record.closed != 0 ? "<polygon" : "<polyline")
				+ " points=\"" + PointsText(op.Points()) + "\""
				+ _Stroke(record.paint, record.paint.strokeWidth) + " stroke-linecap=\""
				+ CapName(cap) + "\" stroke-linejoin=\"" + JoinName(join) + "\""
				+ _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::StrokeArc:
		{
			const ArcRecord& record = op.As<ArcRecord>();
			const Rect& oval = record.oval;
			const float rx = (oval.x1 - oval.x0) / 2;
			const float ry = (oval.y1 - oval.y0) / 2;
			const float sweep = std::clamp(record.sweepDeg, -360.0f, 360.0f);
			std::string d = "M" + PointText(OnEllipse(oval, record.startDeg));
			// a full turn as two halves; SVG cannot draw an arc to its start
			const int halves = std::abs(sweep) >= 360 ? 2 : 1;
			const float step = sweep / static_cast<float>(halves);
			for (int i = 1; i <= halves; i++) {
				d += "A" + Number(rx) + " " + Number(ry) + " 0 "
					+ (std::abs(step) > 180 ? "1" : "0") + " " + (step > 0 ? "1" : "0") + " "
					+ PointText(OnEllipse(oval, record.startDeg + step
						* static_cast<float>(i)));
			}
			const auto cap = static_cast<Paint::Cap>(record.paint.cap);
			_Element("<path d=\"" + d + "\"" + _Stroke(record.paint, record.paint.strokeWidth)
				+ " stroke-linecap=\"" + CapName(cap) + "\"" + _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::DrawLines:
		{
			const LinesRecord& record = op.As<LinesRecord>();
			const auto cap = static_cast<Paint::Cap>(record.cap);
			const float half = record.width / 2;
			for (const Line& line : op.Lines()) {
				const bool horizontal = line.p0.y == line.p1.y;
				const bool vertical = line.p0.x == line.p1.x;
				std::string color = Hex(line.color);
				const std::string opacity = line.color.a < 1
					? "\" fill-opacity=\"" + Number(line.color.a) : "";
				if ((horizontal || vertical) && cap != Paint::Cap::Round) {
					// rule A3, as the render backends draw it
					const float extend = cap == Paint::Cap::Square ? half : 0;
					const float xMin = std::min(line.p0.x, line.p1.x);
					const float xMax = std::max(line.p0.x, line.p1.x);
					const float yMin = std::min(line.p0.y, line.p1.y);
					const float yMax = std::max(line.p0.y, line.p1.y);
					const Rect r = horizontal && !(vertical && cap == Paint::Cap::Butt)
						? Rect{xMin - extend, yMin - half, xMax + extend, yMax + half}
						: Rect{xMin - half, yMin - extend, xMax + half, yMax + extend};
					if (r.x1 <= r.x0 || r.y1 <= r.y0)
						continue;
					_Element("<rect x=\"" + Number(r.x0) + "\" y=\"" + Number(r.y0)
						+ "\" width=\"" + Number(r.x1 - r.x0) + "\" height=\""
						+ Number(r.y1 - r.y0) + "\" fill=\"" + color + opacity
						+ "\" shape-rendering=\"crispEdges\"/>");
					continue;
				}
				std::string element = "<line x1=\"" + Number(line.p0.x) + "\" y1=\""
					+ Number(line.p0.y) + "\" x2=\"" + Number(line.p1.x) + "\" y2=\""
					+ Number(line.p1.y) + "\" stroke=\"" + color + "\"";
				if (line.color.a < 1)
					element += " stroke-opacity=\"" + Number(line.color.a) + "\"";
				_Element(element + " stroke-width=\"" + Number(record.width)
					+ "\" stroke-linecap=\"" + CapName(cap) + "\"/>");
			}
			break;
		}
		case OpCode::DrawImage:
		{
			const ImageRecord& record = op.As<ImageRecord>();
			const std::optional<std::string> id = _Image(record.image, record.src);
			if (!id.has_value())
				break;
			const Rect& src = record.src;
			const Rect& dst = record.dst;
			const float scaleX = (dst.x1 - dst.x0) / std::max(src.x1 - src.x0, 1e-6f);
			const float scaleY = (dst.y1 - dst.y0) / std::max(src.y1 - src.y0, 1e-6f);
			std::string element = "<use xlink:href=\"#" + *id + "\" transform=\"translate("
				+ Number(dst.x0) + " " + Number(dst.y0) + ")";
			if (scaleX != 1 || scaleY != 1)
				element += " scale(" + Number(scaleX) + " " + Number(scaleY) + ")";
			element += "\"";
			if (record.alpha < 1)
				element += " opacity=\"" + Number(record.alpha) + "\"";
			_Element(element + "/>");
			break;
		}
		case OpCode::DrawGlyphRun:
			_GlyphRun(op);
			break;
		case OpCode::DrawList:
		{
			const ListRecord& record = op.As<ListRecord>();
			if (depth >= kMaxListDepth || record.child >= list.Children().size())
				break;
			_Open("<g transform=\"translate(" + Number(record.offset.x) + " "
				+ Number(record.offset.y) + ")\">");
			List(*list.Children()[record.child], depth + 1);
			_Close("</g>");
			break;
		}
		case OpCode::FillPath:
		{
			const PathRecord& record = op.As<PathRecord>();
			const char* rule = static_cast<FillRule>(record.fillRule) == FillRule::EvenOdd
				? " fill-rule=\"evenodd\"" : "";
			_Element("<path d=\"" + PathData(op.PathVerbs(), op.PathPoints()) + "\"" + rule
				+ _Fill(record.paint) + _Crisp(record.paint) + "/>");
			break;
		}
		case OpCode::StrokePath:
		{
			const PathRecord& record = op.As<PathRecord>();
			const auto cap = static_cast<Paint::Cap>(record.paint.cap);
			const auto join = static_cast<Paint::Join>(record.paint.join);
			_Element("<path d=\"" + PathData(op.PathVerbs(), op.PathPoints()) + "\""
				+ _Stroke(record.paint, record.paint.strokeWidth) + " stroke-linecap=\""
				+ CapName(cap) + "\" stroke-linejoin=\"" + JoinName(join) + "\""
				+ _Crisp(record.paint) + "/>");
			break;
		}
		default:
			// unknown opcodes are skipped (specification 12)
			break;
	}
}


// The operations of \a list, each run of them with the same clip region in
// a group clipped to it.
void
Writer::List(const DisplayList& list, int depth)
{
	std::uint32_t clip = UINT32_MAX;
	bool open = false;
	for (const Op& op : list.Ops()) {
		if (op.ClipIndex() != clip) {
			if (open)
				_Close("</g>");
			clip = op.ClipIndex();
			_Open("<g clip-path=\"url(#" + _Clip(list.Clip(clip)) + ")\">");
			open = true;
		}
		_Op(list, op, depth);
	}
	if (open)
		_Close("</g>");
}


std::string
Writer::Document(const Rect& bounds) const
{
	const char* newline = fOptions.pretty ? "\n" : "";
	std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\""
		+ Number((bounds.x1 - bounds.x0) * fOptions.scale) + "\" height=\""
		+ Number((bounds.y1 - bounds.y0) * fOptions.scale) + "\" viewBox=\""
		+ Number(bounds.x0) + " " + Number(bounds.y0) + " " + Number(bounds.x1 - bounds.x0)
		+ " " + Number(bounds.y1 - bounds.y0) + "\">" + newline;
	if (!fDefs.empty())
		svg += std::string(fOptions.pretty ? "  " : "") + "<defs>" + newline + fDefs
			+ (fOptions.pretty ? "  " : "") + "</defs>" + newline;
	return svg + fBody + "</svg>" + newline;
}


} // namespace


SvgExportBackend::SvgExportBackend(const TextSystem* text)
	:
	fText(text)
{
}


Caps
SvgExportBackend::Capabilities() const
{
	Caps caps;
	caps.bezierPaths = true;
	caps.concavePolygons = true;
	return caps;
}


std::expected<void, Error>
SvgExportBackend::Export(const DisplayList& list, const Rect& bounds, std::ostream& out,
	const ExportOptions& options)
{
	if (!bounds.IsFinite() || bounds.IsEmpty())
		return Failure(ErrorCode::InvalidArgument, "the bounds of an export must not be empty");
	if (!(options.scale > 0) || !std::isfinite(options.scale))
		return Failure(ErrorCode::InvalidArgument, "the scale of an export must be positive");
	Writer writer(fText != nullptr ? *fText : TextSystem::Default(), options);
	writer.List(list, 0);
	out << writer.Document(bounds);
	if (!out)
		return Failure(ErrorCode::IoError, "the SVG could not be written");
	return {};
}


std::expected<std::string, Error>
ExportSvg(const DisplayList& list, const Rect& bounds, const ExportOptions& options,
	const TextSystem* text)
{
	std::ostringstream out;
	SvgExportBackend backend(text);
	if (auto exported = backend.Export(list, bounds, out, options); !exported)
		return std::unexpected(exported.error());
	return out.str();
}


} // namespace ngui::gfx
