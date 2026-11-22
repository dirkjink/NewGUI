/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Dump.h>

#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <utility>


namespace ngui::gfx {


namespace {


// A number rounded to three decimals, trailing zeros removed, "-0" as "0".
// std::format is locale-independent and rounds exactly on every platform.
std::string
_Number(float value)
{
	if (!std::isfinite(value))
		return std::isnan(value) ? "nan" : (value > 0 ? "inf" : "-inf");
	std::string text = std::format("{:.3f}", value);
	while (text.back() == '0')
		text.pop_back();
	if (text.back() == '.')
		text.pop_back();
	if (text == "-0")
		text = "0";
	return text;
}


std::string
_JsonNumber(float value)
{
	return std::isfinite(value) ? _Number(value) : "null";
}


const char*
_OpName(OpCode code)
{
	switch (code) {
		case OpCode::FillRect:
			return "FillRect";
		case OpCode::FillRRect:
			return "FillRRect";
		case OpCode::FillRing:
			return "FillRing";
		case OpCode::FillEllipse:
			return "FillEllipse";
		case OpCode::FillConvexPolygon:
			return "FillConvexPolygon";
		case OpCode::StrokeRect:
			return "StrokeRect";
		case OpCode::StrokeRRect:
			return "StrokeRRect";
		case OpCode::StrokePolyline:
			return "StrokePolyline";
		case OpCode::StrokeArc:
			return "StrokeArc";
		case OpCode::DrawLines:
			return "DrawLines";
		case OpCode::DrawImage:
			return "DrawImage";
		case OpCode::DrawGlyphRun:
			return "DrawGlyphRun";
		case OpCode::DrawList:
			return "DrawList";
	}
	return nullptr;
}


const char*
_CapName(std::uint8_t cap)
{
	static const char* const kNames[] = {"Butt", "Round", "Square"};
	return cap < 3 ? kNames[cap] : "?";
}


const char*
_JoinName(std::uint8_t join)
{
	static const char* const kNames[] = {"Miter", "Round", "Bevel"};
	return join < 3 ? kNames[join] : "?";
}


// Appends the code point to UTF-8.
void
_AppendUtf8(std::string& out, std::uint32_t c)
{
	if (c < 0x80) {
		out += static_cast<char>(c);
	} else if (c < 0x800) {
		out += static_cast<char>(0xc0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += static_cast<char>(0xe0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
		out += static_cast<char>(0x80 | (c & 0x3f));
	} else {
		out += static_cast<char>(0xf0 | (c >> 18));
		out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
		out += static_cast<char>(0x80 | (c & 0x3f));
	}
}


// Decodes one UTF-8 sequence at the start of text; returns the code point
// and its length, or U+FFFD and 1 for an invalid byte (overlong forms,
// surrogates and values beyond U+10FFFF included).
std::pair<std::uint32_t, std::size_t>
_DecodeUtf8(std::u8string_view text)
{
	const auto byte = [&](std::size_t i) { return static_cast<std::uint32_t>(text[i]); };
	const std::uint32_t first = byte(0);
	if (first < 0x80)
		return {first, 1};

	std::size_t length;
	std::uint32_t c;
	std::uint32_t minimum;
	if ((first & 0xe0) == 0xc0) {
		length = 2;
		c = first & 0x1f;
		minimum = 0x80;
	} else if ((first & 0xf0) == 0xe0) {
		length = 3;
		c = first & 0x0f;
		minimum = 0x800;
	} else if ((first & 0xf8) == 0xf0) {
		length = 4;
		c = first & 0x07;
		minimum = 0x10000;
	} else {
		return {0xfffd, 1};
	}
	if (text.size() < length)
		return {0xfffd, 1};
	for (std::size_t i = 1; i < length; i++) {
		if ((byte(i) & 0xc0) != 0x80)
			return {0xfffd, 1};
		c = (c << 6) | (byte(i) & 0x3f);
	}
	if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
		return {0xfffd, 1};
	return {c, length};
}


// A quoted string: valid UTF-8, with quote, backslash and control
// characters escaped as in JSON.
std::string
_Quoted(std::u8string_view text)
{
	std::string out = "\"";
	while (!text.empty()) {
		const auto [c, length] = _DecodeUtf8(text);
		text.remove_prefix(length);
		if (c == '"' || c == '\\') {
			out += '\\';
			out += static_cast<char>(c);
		} else if (c < 0x20) {
			out += std::format("\\u{:04x}", c);
		} else {
			_AppendUtf8(out, c);
		}
	}
	return out + "\"";
}


// #pragma mark - text


class TextWriter {
public:
			void				Write(const DisplayList& list, int depth);
			std::string			Take() { return std::move(fOut); }

private:
			void				_Line(int depth, std::string_view text);
	static	std::string			_Rect(const Rect& rect);
	static	std::string			_RRect(const RRect& rrect);
	static	std::string			_Point(Point point);
	static	std::string			_Color(const Color& color);
	static	std::string			_Paint(const PaintRecord& paint);
	static	std::string			_Op(const Op& op);

			std::string			fOut;
};


void
TextWriter::Write(const DisplayList& list, int depth)
{
	_Line(depth, std::format("DisplayList bounds={} ops={}", _Rect(list.Bounds()),
		list.OpCount()));

	std::uint32_t clip = UINT32_MAX;
	for (const Op& op : list.Ops()) {
		if (op.ClipIndex() != clip) {
			clip = op.ClipIndex();
			std::string line = "clip";
			for (const IntRect& rect : list.Clip(clip))
				line += std::format(" [{} {} {} {}]", rect.x0, rect.y0, rect.x1, rect.y1);
			_Line(depth, line);
		}
		_Line(depth, _Op(op));
		if (op.Code() == OpCode::DrawList) {
			const std::span<const DisplayListRef> children = list.Children();
			Write(*children[op.As<ListRecord>().child], depth + 1);
		}
	}
}


void
TextWriter::_Line(int depth, std::string_view text)
{
	fOut.append(static_cast<std::size_t>(depth) * 2, ' ');
	fOut += text;
	fOut += '\n';
}


std::string
TextWriter::_Rect(const Rect& rect)
{
	return std::format("[{} {} {} {}]", _Number(rect.x0), _Number(rect.y0), _Number(rect.x1),
		_Number(rect.y1));
}


std::string
TextWriter::_RRect(const RRect& rrect)
{
	return std::format("{} radii=({} {} {} {})", _Rect(rrect.rect), _Number(rrect.radii.tl),
		_Number(rrect.radii.tr), _Number(rrect.radii.br), _Number(rrect.radii.bl));
}


std::string
TextWriter::_Point(Point point)
{
	return std::format("({} {})", _Number(point.x), _Number(point.y));
}


std::string
TextWriter::_Color(const Color& color)
{
	return std::format("({} {} {} {})", _Number(color.r), _Number(color.g), _Number(color.b),
		_Number(color.a));
}


std::string
TextWriter::_Paint(const PaintRecord& paint)
{
	std::string text = std::format("color={} width={} cap={} join={} aa={}",
		_Color(paint.color), _Number(paint.strokeWidth), _CapName(paint.cap),
		_JoinName(paint.join), paint.antialias);
	if (paint.hasGradient != 0) {
		const GradientRecord& gradient = paint.gradient;
		text += std::format(" gradient={}->{}", _Point(gradient.p0), _Point(gradient.p1));
		for (std::uint32_t i = 0; i < gradient.count && i < 4; i++) {
			text += std::format(" stop={}:{}", _Number(gradient.stops[i].offset),
				_Color(gradient.stops[i].color));
		}
	}
	return text;
}


std::string
TextWriter::_Op(const Op& op)
{
	const char* name = _OpName(op.Code());
	if (name == nullptr) {
		return std::format("Unknown code={} flags={} bytes={}",
			static_cast<std::uint16_t>(op.Code()), op.Flags(), op.Payload().size());
	}

	std::string text = name;
	switch (op.Code()) {
		case OpCode::FillRect:
		case OpCode::FillEllipse:
		case OpCode::StrokeRect:
		{
			const RectRecord& record = op.As<RectRecord>();
			text += std::format(" rect={} {}", _Rect(record.rect), _Paint(record.paint));
			break;
		}
		case OpCode::FillRRect:
		case OpCode::StrokeRRect:
		{
			const RRectRecord& record = op.As<RRectRecord>();
			text += std::format(" rrect={} {}", _RRect(record.rrect), _Paint(record.paint));
			break;
		}
		case OpCode::FillRing:
		{
			const RingRecord& record = op.As<RingRecord>();
			text += std::format(" outer={} inner={} {}", _RRect(record.outer),
				_RRect(record.inner), _Paint(record.paint));
			break;
		}
		case OpCode::FillConvexPolygon:
		case OpCode::StrokePolyline:
		{
			const PointsRecord& record = op.As<PointsRecord>();
			text += " points=";
			for (const Point& point : op.Points())
				text += _Point(point);
			if (op.Code() == OpCode::StrokePolyline)
				text += std::format(" closed={}", record.closed);
			text += " " + _Paint(record.paint);
			break;
		}
		case OpCode::StrokeArc:
		{
			const ArcRecord& record = op.As<ArcRecord>();
			text += std::format(" oval={} start={} sweep={} {}", _Rect(record.oval),
				_Number(record.startDeg), _Number(record.sweepDeg), _Paint(record.paint));
			break;
		}
		case OpCode::DrawLines:
		{
			const LinesRecord& record = op.As<LinesRecord>();
			text += std::format(" width={} cap={}", _Number(record.width), _CapName(record.cap));
			for (const Line& line : op.Lines()) {
				text += std::format(" line={}->{}:{}", _Point(line.p0), _Point(line.p1),
					_Color(line.color));
			}
			break;
		}
		case OpCode::DrawImage:
		{
			const ImageRecord& record = op.As<ImageRecord>();
			text += std::format(" image={} src={} dst={} alpha={}", record.image,
				_Rect(record.src), _Rect(record.dst), _Number(record.alpha));
			break;
		}
		case OpCode::DrawGlyphRun:
		{
			const GlyphRunRecord& record = op.As<GlyphRunRecord>();
			text += std::format(" font={} size={} origin={} color={} glyphs=", record.font,
				_Number(record.sizePx), _Point(record.origin), _Color(record.color));
			const std::span<const std::uint32_t> glyphs = op.Glyphs();
			const std::span<const Point> positions = op.Positions();
			for (std::size_t i = 0; i < glyphs.size(); i++)
				text += std::format("{}{}@{}", i == 0 ? "" : ",", glyphs[i], _Point(positions[i]));
			if (!op.Text().empty())
				text += " text=" + _Quoted(op.Text());
			if (!op.Clusters().empty()) {
				text += " clusters=";
				for (std::size_t i = 0; i < op.Clusters().size(); i++)
					text += std::format("{}{}", i == 0 ? "" : ",", op.Clusters()[i]);
			}
			break;
		}
		case OpCode::DrawList:
		{
			const ListRecord& record = op.As<ListRecord>();
			text += std::format(" offset={}", _Point(record.offset));
			break;
		}
	}
	return text;
}


// #pragma mark - JSON


class JsonWriter {
public:
			void				Write(const DisplayList& list);
			std::string			Take() { return std::move(fOut); }

private:
			void				_Op(const DisplayList& list, const Op& op);
	static	std::string			_Rect(const Rect& rect);
	static	std::string			_RRect(const RRect& rrect);
	static	std::string			_Point(Point point);
	static	std::string			_Color(const Color& color);
	static	std::string			_Paint(const PaintRecord& paint);

			std::string			fOut;
};


void
JsonWriter::Write(const DisplayList& list)
{
	fOut += std::format("{{\"bounds\":{},\"ops\":[", _Rect(list.Bounds()));
	bool first = true;
	for (const Op& op : list.Ops()) {
		if (!first)
			fOut += ',';
		first = false;
		_Op(list, op);
	}
	fOut += "]}";
}


void
JsonWriter::_Op(const DisplayList& list, const Op& op)
{
	const char* name = _OpName(op.Code());
	fOut += std::format("{{\"op\":\"{}\",\"clip\":[", name != nullptr ? name : "Unknown");
	bool first = true;
	for (const IntRect& rect : list.Clip(op.ClipIndex())) {
		fOut += std::format("{}[{},{},{},{}]", first ? "" : ",", rect.x0, rect.y0, rect.x1,
			rect.y1);
		first = false;
	}
	fOut += ']';

	if (name == nullptr) {
		fOut += std::format(",\"code\":{},\"flags\":{},\"bytes\":{}}}",
			static_cast<std::uint16_t>(op.Code()), op.Flags(), op.Payload().size());
		return;
	}

	switch (op.Code()) {
		case OpCode::FillRect:
		case OpCode::FillEllipse:
		case OpCode::StrokeRect:
		{
			const RectRecord& record = op.As<RectRecord>();
			fOut += std::format(",\"rect\":{},\"paint\":{}", _Rect(record.rect),
				_Paint(record.paint));
			break;
		}
		case OpCode::FillRRect:
		case OpCode::StrokeRRect:
		{
			const RRectRecord& record = op.As<RRectRecord>();
			fOut += std::format(",\"rrect\":{},\"paint\":{}", _RRect(record.rrect),
				_Paint(record.paint));
			break;
		}
		case OpCode::FillRing:
		{
			const RingRecord& record = op.As<RingRecord>();
			fOut += std::format(",\"outer\":{},\"inner\":{},\"paint\":{}", _RRect(record.outer),
				_RRect(record.inner), _Paint(record.paint));
			break;
		}
		case OpCode::FillConvexPolygon:
		case OpCode::StrokePolyline:
		{
			const PointsRecord& record = op.As<PointsRecord>();
			fOut += ",\"points\":[";
			bool firstPoint = true;
			for (const Point& point : op.Points()) {
				fOut += (firstPoint ? "" : ",") + _Point(point);
				firstPoint = false;
			}
			fOut += ']';
			if (op.Code() == OpCode::StrokePolyline)
				fOut += std::format(",\"closed\":{}", record.closed != 0);
			fOut += ",\"paint\":" + _Paint(record.paint);
			break;
		}
		case OpCode::StrokeArc:
		{
			const ArcRecord& record = op.As<ArcRecord>();
			fOut += std::format(",\"oval\":{},\"start\":{},\"sweep\":{},\"paint\":{}",
				_Rect(record.oval), _JsonNumber(record.startDeg), _JsonNumber(record.sweepDeg),
				_Paint(record.paint));
			break;
		}
		case OpCode::DrawLines:
		{
			const LinesRecord& record = op.As<LinesRecord>();
			fOut += std::format(",\"width\":{},\"cap\":\"{}\",\"lines\":[",
				_JsonNumber(record.width), _CapName(record.cap));
			bool firstLine = true;
			for (const Line& line : op.Lines()) {
				fOut += std::format("{}{{\"p0\":{},\"p1\":{},\"color\":{}}}",
					firstLine ? "" : ",", _Point(line.p0), _Point(line.p1), _Color(line.color));
				firstLine = false;
			}
			fOut += ']';
			break;
		}
		case OpCode::DrawImage:
		{
			const ImageRecord& record = op.As<ImageRecord>();
			fOut += std::format(",\"image\":{},\"src\":{},\"dst\":{},\"alpha\":{}",
				record.image, _Rect(record.src), _Rect(record.dst), _JsonNumber(record.alpha));
			break;
		}
		case OpCode::DrawGlyphRun:
		{
			const GlyphRunRecord& record = op.As<GlyphRunRecord>();
			fOut += std::format(",\"font\":{},\"size\":{},\"origin\":{},\"color\":{},\"glyphs\":[",
				record.font, _JsonNumber(record.sizePx), _Point(record.origin),
				_Color(record.color));
			const std::span<const std::uint32_t> glyphs = op.Glyphs();
			const std::span<const Point> positions = op.Positions();
			for (std::size_t i = 0; i < glyphs.size(); i++) {
				fOut += std::format("{}{{\"glyph\":{},\"at\":{}}}", i == 0 ? "" : ",", glyphs[i],
					_Point(positions[i]));
			}
			fOut += "],\"text\":" + _Quoted(op.Text()) + ",\"clusters\":[";
			for (std::size_t i = 0; i < op.Clusters().size(); i++)
				fOut += std::format("{}{}", i == 0 ? "" : ",", op.Clusters()[i]);
			fOut += ']';
			break;
		}
		case OpCode::DrawList:
		{
			const ListRecord& record = op.As<ListRecord>();
			fOut += std::format(",\"offset\":{},\"list\":", _Point(record.offset));
			Write(*list.Children()[record.child]);
			break;
		}
	}
	fOut += '}';
}


std::string
JsonWriter::_Rect(const Rect& rect)
{
	return std::format("[{},{},{},{}]", _JsonNumber(rect.x0), _JsonNumber(rect.y0),
		_JsonNumber(rect.x1), _JsonNumber(rect.y1));
}


std::string
JsonWriter::_RRect(const RRect& rrect)
{
	return std::format("{{\"rect\":{},\"radii\":[{},{},{},{}]}}", _Rect(rrect.rect),
		_JsonNumber(rrect.radii.tl), _JsonNumber(rrect.radii.tr), _JsonNumber(rrect.radii.br),
		_JsonNumber(rrect.radii.bl));
}


std::string
JsonWriter::_Point(Point point)
{
	return std::format("[{},{}]", _JsonNumber(point.x), _JsonNumber(point.y));
}


std::string
JsonWriter::_Color(const Color& color)
{
	return std::format("[{},{},{},{}]", _JsonNumber(color.r), _JsonNumber(color.g),
		_JsonNumber(color.b), _JsonNumber(color.a));
}


std::string
JsonWriter::_Paint(const PaintRecord& paint)
{
	std::string text = std::format(
		"{{\"color\":{},\"width\":{},\"cap\":\"{}\",\"join\":\"{}\",\"antialias\":{}",
		_Color(paint.color), _JsonNumber(paint.strokeWidth), _CapName(paint.cap),
		_JoinName(paint.join), paint.antialias != 0);
	if (paint.hasGradient != 0) {
		const GradientRecord& gradient = paint.gradient;
		text += std::format(",\"gradient\":{{\"p0\":{},\"p1\":{},\"stops\":[",
			_Point(gradient.p0), _Point(gradient.p1));
		for (std::uint32_t i = 0; i < gradient.count && i < 4; i++) {
			text += std::format("{}{{\"offset\":{},\"color\":{}}}", i == 0 ? "" : ",",
				_JsonNumber(gradient.stops[i].offset), _Color(gradient.stops[i].color));
		}
		text += "]}";
	}
	return text + "}";
}


} // namespace


std::string
DumpText(const DisplayList& list)
{
	TextWriter writer;
	writer.Write(list, 0);
	return writer.Take();
}


std::string
DumpJson(const DisplayList& list)
{
	JsonWriter writer;
	writer.Write(list);
	return writer.Take();
}


} // namespace ngui::gfx
