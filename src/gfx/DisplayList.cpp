/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/DisplayList.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>


namespace ngui::gfx {


static_assert(std::endian::native == std::endian::little,
	"the binary form of a DisplayList is the little-endian memory layout");


namespace {


// "NGDL" in the first four bytes.
constexpr std::uint32_t kMagic = 0x4c44474e;
constexpr std::size_t kHeaderSize = 48;


template<typename T>
T
_Read(const std::byte* data)
{
	T value;
	std::memcpy(&value, data, sizeof(T));
	return value;
}


template<typename T>
void
_Append(std::vector<std::byte>& out, const T& value)
{
	const std::span<const std::byte> bytes = std::as_bytes(std::span(&value, 1));
	out.insert(out.end(), bytes.begin(), bytes.end());
}


void
_AppendBytes(std::vector<std::byte>& out, std::span<const std::byte> bytes)
{
	out.insert(out.end(), bytes.begin(), bytes.end());
}


// The array that follows the record of an operation whose header is at
// \a header.
template<typename T, typename Record>
const T*
_Trailing(const std::byte* header)
{
	constexpr std::size_t kOffset = kOpHeaderSize + sizeof(Record);
	return reinterpret_cast<const T*>(header + kOffset);
}


std::unexpected<Error>
_Bad(const char* what)
{
	return Failure(ErrorCode::BadData, what);
}


bool
_AllZero(std::span<const std::byte> bytes)
{
	return std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0}; });
}


bool
_IsFinite(float value)
{
	return std::isfinite(value);
}


bool
_IsFinite(const RRect& rrect)
{
	return rrect.rect.IsFinite() && _IsFinite(rrect.radii.tl) && _IsFinite(rrect.radii.tr)
		&& _IsFinite(rrect.radii.br) && _IsFinite(rrect.radii.bl);
}


bool
_IsValid(const PaintRecord& record)
{
	if (record.cap > 2 || record.join > 2 || record.antialias > 1 || record.hasGradient > 1)
		return false;
	if (record.hasGradient == 0) {
		// Canonical: no gradient means all zero.
		if (!_AllZero(std::as_bytes(std::span(&record.gradient, 1))))
			return false;
	} else {
		if (record.gradient.count < 2 || record.gradient.count > 4)
			return false;
		const auto unused = std::span(record.gradient.stops).subspan(record.gradient.count);
		if (!_AllZero(std::as_bytes(unused)))
			return false;
	}
	return record.ToPaint().IsValid();
}


template<typename T>
std::vector<T>
_ReadArray(std::span<const std::byte> payload, std::size_t offset, std::size_t count)
{
	std::vector<T> items(count);
	if (count > 0)
		std::memcpy(items.data(), payload.data() + offset, count * sizeof(T));
	return items;
}


bool
_SameSpans(std::span<const IntRect> a, std::span<const IntRect> b)
{
	return std::ranges::equal(a, b, [](const IntRect& p, const IntRect& q) {
		return p.x0 == q.x0 && p.x1 == q.x1;
	});
}


// The canonical banded form a Region keeps (RegionSupport.cpp), checked in
// one pass: non-empty rectangles, sorted bands that do not overlap, no two
// rectangles of a band touching, and no two touching bands with the same
// rectangles.
bool
_IsCanonicalClip(std::span<const IntRect> rects)
{
	std::size_t bandStart = 0;
	std::size_t previousStart = 0;
	std::size_t previousEnd = 0;
	for (std::size_t i = 0; i <= rects.size(); i++) {
		const bool endOfBand = i == rects.size() || rects[i].y0 != rects[bandStart].y0;
		if (i < rects.size()) {
			if (rects[i].IsEmpty())
				return false;
			if (!endOfBand && i > bandStart
				&& (rects[i].y1 != rects[i - 1].y1 || rects[i].x0 <= rects[i - 1].x1)) {
				return false;
			}
		}
		if (!endOfBand || i == 0)
			continue;

		// The band [bandStart, i) is complete.
		const std::span<const IntRect> band = rects.subspan(bandStart, i - bandStart);
		if (previousEnd > previousStart) {
			const std::span<const IntRect> previous
				= rects.subspan(previousStart, previousEnd - previousStart);
			if (band.front().y0 < previous.front().y1)
				return false;
			if (band.front().y0 == previous.front().y1 && _SameSpans(band, previous))
				return false;
		}
		previousStart = bandStart;
		previousEnd = i;
		bandStart = i;
	}
	return true;
}


// Checks the payload of a known opcode; unknown opcodes are kept as they
// are (specification 11.2).
bool
_IsValidPayload(OpCode code, std::span<const std::byte> payload, std::uint32_t childCount,
	std::vector<std::uint32_t>& childOrder)
{
	switch (code) {
		case OpCode::FillRect:
		case OpCode::FillEllipse:
		case OpCode::StrokeRect:
		{
			if (payload.size() != sizeof(RectRecord))
				return false;
			const auto record = _Read<RectRecord>(payload.data());
			return record.rect.IsFinite() && _IsValid(record.paint);
		}
		case OpCode::FillRRect:
		case OpCode::StrokeRRect:
		{
			if (payload.size() != sizeof(RRectRecord))
				return false;
			const auto record = _Read<RRectRecord>(payload.data());
			return _IsFinite(record.rrect) && _IsValid(record.paint);
		}
		case OpCode::FillRing:
		{
			if (payload.size() != sizeof(RingRecord))
				return false;
			const auto record = _Read<RingRecord>(payload.data());
			return _IsFinite(record.outer) && _IsFinite(record.inner)
				&& _IsValid(record.paint);
		}
		case OpCode::FillConvexPolygon:
		case OpCode::StrokePolyline:
		{
			if (payload.size() < sizeof(PointsRecord))
				return false;
			const auto record = _Read<PointsRecord>(payload.data());
			const bool polygon = code == OpCode::FillConvexPolygon;
			if (record.count < (polygon ? 3u : 2u) || record.closed > (polygon ? 0u : 1u)
				|| payload.size() != sizeof(PointsRecord) + std::uint64_t(record.count) * 8
				|| !_IsValid(record.paint)) {
				return false;
			}
			const auto points = _ReadArray<Point>(payload, sizeof(PointsRecord), record.count);
			return std::ranges::all_of(points, [](const Point& p) { return p.IsFinite(); });
		}
		case OpCode::StrokeArc:
		{
			if (payload.size() != sizeof(ArcRecord))
				return false;
			const auto record = _Read<ArcRecord>(payload.data());
			return record.oval.IsFinite() && _IsFinite(record.startDeg)
				&& _IsFinite(record.sweepDeg) && _IsValid(record.paint);
		}
		case OpCode::DrawLines:
		{
			if (payload.size() < sizeof(LinesRecord))
				return false;
			const auto record = _Read<LinesRecord>(payload.data());
			if (!_IsFinite(record.width) || record.width < 0 || record.cap > 2
				|| record.reserved[0] != 0 || record.reserved[1] != 0
				|| record.reserved[2] != 0 || record.count == 0
				|| payload.size() != sizeof(LinesRecord)
					+ std::uint64_t(record.count) * sizeof(Line)) {
				return false;
			}
			const auto lines = _ReadArray<Line>(payload, sizeof(LinesRecord), record.count);
			return std::ranges::all_of(lines, [](const Line& line) {
				return line.p0.IsFinite() && line.p1.IsFinite() && line.color.IsValid();
			});
		}
		case OpCode::DrawImage:
		{
			if (payload.size() != sizeof(ImageRecord))
				return false;
			const auto record = _Read<ImageRecord>(payload.data());
			return record.image != 0 && record.alpha >= 0 && record.alpha <= 1
				&& record.src.IsFinite() && record.dst.IsFinite();
		}
		case OpCode::DrawGlyphRun:
		{
			if (payload.size() < sizeof(GlyphRunRecord))
				return false;
			const auto record = _Read<GlyphRunRecord>(payload.data());
			const std::uint64_t glyphs = record.glyphCount;
			const std::uint64_t textPadded = (std::uint64_t(record.textBytes) + 3) & ~3ull;
			const std::uint64_t expected = sizeof(GlyphRunRecord) + glyphs * 4 + glyphs * 8
				+ std::uint64_t(record.clusterCount) * 4 + textPadded;
			if (payload.size() != expected || !_IsFinite(record.sizePx) || record.sizePx <= 0
				|| !record.origin.IsFinite() || !record.color.IsValid() || glyphs == 0
				|| (record.clusterCount != 0 && record.clusterCount != glyphs)
				|| (record.clusterCount != 0 && record.textBytes == 0)) {
				return false;
			}
			const std::size_t positionsAt = sizeof(GlyphRunRecord) + glyphs * 4;
			const std::size_t clustersAt = positionsAt + glyphs * 8;
			const std::size_t textAt = clustersAt + std::size_t(record.clusterCount) * 4;
			const auto positions = _ReadArray<Point>(payload, positionsAt, glyphs);
			const auto clusters = _ReadArray<std::uint32_t>(payload, clustersAt,
				record.clusterCount);
			const bool ascending = std::ranges::is_sorted(clusters);
			const bool descending = std::ranges::is_sorted(clusters, std::greater<>());
			return std::ranges::all_of(positions, [](const Point& p) { return p.IsFinite(); })
				&& (ascending || descending)
				&& std::ranges::all_of(clusters,
					[&](std::uint32_t c) { return c < record.textBytes; })
				&& _AllZero(payload.subspan(textAt + record.textBytes));
		}
		case OpCode::DrawList:
		{
			if (payload.size() != sizeof(ListRecord))
				return false;
			const auto record = _Read<ListRecord>(payload.data());
			if (record.child >= childCount || !record.offset.IsFinite())
				return false;
			if (std::ranges::find(childOrder, record.child) == childOrder.end())
				childOrder.push_back(record.child);
			return true;
		}
	}
	return payload.size() % 4 == 0;
}


} // namespace


// #pragma mark - Op


Op::Op(const std::byte* header)
	:
	fHeader(header)
{
}


OpCode
Op::Code() const
{
	return static_cast<OpCode>(_Read<std::uint16_t>(fHeader));
}


std::uint16_t
Op::Flags() const
{
	return _Read<std::uint16_t>(fHeader + 2);
}


std::uint32_t
Op::ClipIndex() const
{
	return _Read<std::uint32_t>(fHeader + 8);
}


std::span<const std::byte>
Op::Payload() const
{
	return {fHeader + kOpHeaderSize, _Read<std::uint32_t>(fHeader + 4)};
}


std::span<const Point>
Op::Points() const
{
	return {_Trailing<Point, PointsRecord>(fHeader), As<PointsRecord>().count};
}


std::span<const Line>
Op::Lines() const
{
	return {_Trailing<Line, LinesRecord>(fHeader), As<LinesRecord>().count};
}


std::span<const std::uint32_t>
Op::Glyphs() const
{
	return {_Trailing<std::uint32_t, GlyphRunRecord>(fHeader),
		As<GlyphRunRecord>().glyphCount};
}


std::span<const Point>
Op::Positions() const
{
	const std::span<const std::uint32_t> glyphs = Glyphs();
	const auto* positions = reinterpret_cast<const Point*>(glyphs.data() + glyphs.size());
	return {positions, glyphs.size()};
}


std::span<const std::uint32_t>
Op::Clusters() const
{
	const std::span<const Point> positions = Positions();
	const auto* clusters = reinterpret_cast<const std::uint32_t*>(positions.data()
		+ positions.size());
	return {clusters, As<GlyphRunRecord>().clusterCount};
}


std::u8string_view
Op::Text() const
{
	const std::span<const std::uint32_t> clusters = Clusters();
	const auto* text = reinterpret_cast<const char8_t*>(clusters.data() + clusters.size());
	return {text, As<GlyphRunRecord>().textBytes};
}


// #pragma mark - DisplayList::Iterator


DisplayList::Iterator::Iterator(const std::byte* position)
	:
	fPosition(position)
{
}


Op
DisplayList::Iterator::operator*() const
{
	return Op(fPosition);
}


DisplayList::Iterator&
DisplayList::Iterator::operator++()
{
	fPosition += kOpHeaderSize + _Read<std::uint32_t>(fPosition + 4);
	return *this;
}


DisplayList::Iterator
DisplayList::Iterator::operator++(int)
{
	Iterator previous = *this;
	++*this;
	return previous;
}


// #pragma mark - DisplayList


Rect
DisplayList::Bounds() const
{
	return fBounds;
}


std::uint32_t
DisplayList::OpCount() const
{
	return fOpCount;
}


DisplayList::OpRange
DisplayList::Ops() const
{
	if (fOps.empty())
		return {};
	return {Iterator(fOps.data()), Iterator(fOps.data() + fOps.size())};
}


std::uint32_t
DisplayList::ClipCount() const
{
	return static_cast<std::uint32_t>(fClipStarts.size() - 1);
}


std::span<const IntRect>
DisplayList::Clip(std::uint32_t index) const
{
	if (index >= ClipCount())
		return {};
	return std::span(fClipRects).subspan(fClipStarts[index],
		fClipStarts[index + 1] - fClipStarts[index]);
}


std::span<const ImageHandle>
DisplayList::Images() const
{
	return fImages;
}


std::span<const FontId>
DisplayList::Fonts() const
{
	return fFonts;
}


std::span<const DisplayListRef>
DisplayList::Children() const
{
	return fChildren;
}


std::vector<std::byte>
DisplayList::Serialize() const
{
	std::vector<std::byte> out;
	_Append(out, kMagic);
	_Append(out, kVersion);
	_Append(out, std::uint16_t{0});
	_Append(out, fBounds);
	_Append(out, fOpCount);
	_Append(out, static_cast<std::uint32_t>(fOps.size()));
	_Append(out, ClipCount());
	_Append(out, static_cast<std::uint32_t>(fImages.size()));
	_Append(out, static_cast<std::uint32_t>(fFonts.size()));
	_Append(out, static_cast<std::uint32_t>(fChildren.size()));

	for (std::uint32_t i = 0; i < ClipCount(); i++) {
		const std::span<const IntRect> rects = Clip(i);
		_Append(out, static_cast<std::uint32_t>(rects.size()));
		_AppendBytes(out, std::as_bytes(rects));
	}
	_AppendBytes(out, fOps);
	_AppendBytes(out, std::as_bytes(std::span(fImages)));
	_AppendBytes(out, std::as_bytes(std::span(fFonts)));
	for (const DisplayListRef& child : fChildren) {
		const std::vector<std::byte> bytes = child->Serialize();
		_Append(out, static_cast<std::uint32_t>(bytes.size()));
		_AppendBytes(out, bytes);
	}
	return out;
}


std::expected<DisplayListRef, Error>
DisplayList::Deserialize(std::span<const std::byte> data)
{
	return _Deserialize(data, 0);
}


std::expected<DisplayListRef, Error>
DisplayList::_Deserialize(std::span<const std::byte> data, int depth)
{
	if (depth > kMaxDepth)
		return _Bad("display lists nested too deeply");
	if (data.size() < kHeaderSize)
		return _Bad("display list shorter than its header");

	const std::byte* header = data.data();
	if (_Read<std::uint32_t>(header) != kMagic)
		return _Bad("not a display list");
	if (_Read<std::uint16_t>(header + 4) != kVersion)
		return _Bad("unsupported display list version");
	if (_Read<std::uint16_t>(header + 6) != 0)
		return _Bad("reserved header field not zero");

	std::shared_ptr<DisplayList> list(new DisplayList());
	list->fBounds = _Read<Rect>(header + 8);
	list->fOpCount = _Read<std::uint32_t>(header + 24);
	const std::uint32_t opBytes = _Read<std::uint32_t>(header + 28);
	const std::uint32_t clipCount = _Read<std::uint32_t>(header + 32);
	const std::uint32_t imageCount = _Read<std::uint32_t>(header + 36);
	const std::uint32_t fontCount = _Read<std::uint32_t>(header + 40);
	const std::uint32_t childCount = _Read<std::uint32_t>(header + 44);
	if (!list->fBounds.IsFinite())
		return _Bad("display list bounds not finite");

	std::span<const std::byte> rest = data.subspan(kHeaderSize);
	const auto take = [&rest](std::uint64_t size) -> std::optional<std::span<const std::byte>> {
		if (size > rest.size())
			return std::nullopt;
		const std::span<const std::byte> part = rest.first(static_cast<std::size_t>(size));
		rest = rest.subspan(static_cast<std::size_t>(size));
		return part;
	};

	// Clip table: every clip must be a canonical region.
	list->fClipStarts.push_back(0);
	for (std::uint32_t i = 0; i < clipCount; i++) {
		const auto countBytes = take(4);
		if (!countBytes)
			return _Bad("clip table truncated");
		const std::uint32_t count = _Read<std::uint32_t>(countBytes->data());
		const auto rectBytes = take(std::uint64_t(count) * sizeof(IntRect));
		if (!rectBytes)
			return _Bad("clip table truncated");

		std::vector<IntRect> rects(count);
		if (count > 0)
			std::memcpy(rects.data(), rectBytes->data(), rectBytes->size());
		if (!_IsCanonicalClip(rects))
			return _Bad("clip region not in canonical form");

		list->fClipRects.insert(list->fClipRects.end(), rects.begin(), rects.end());
		list->fClipStarts.push_back(static_cast<std::uint32_t>(list->fClipRects.size()));
	}

	// Operations.
	const auto ops = take(opBytes);
	if (!ops)
		return _Bad("operation stream truncated");
	// With memcpy, which implicitly creates the records Op::As() reads.
	list->fOps.resize(ops->size());
	if (!ops->empty())
		std::memcpy(list->fOps.data(), ops->data(), ops->size());

	std::vector<ImageHandle> usedImages;
	std::vector<FontId> usedFonts;
	std::vector<std::uint32_t> childOrder;
	std::uint32_t count = 0;
	for (std::size_t at = 0; at < list->fOps.size(); count++) {
		if (list->fOps.size() - at < kOpHeaderSize)
			return _Bad("operation header truncated");
		const std::byte* opHeader = list->fOps.data() + at;
		const std::uint32_t size = _Read<std::uint32_t>(opHeader + 4);
		if (size % 4 != 0 || size > list->fOps.size() - at - kOpHeaderSize)
			return _Bad("operation size invalid");
		if (_Read<std::uint32_t>(opHeader + 8) >= clipCount)
			return _Bad("clip index out of range");

		const Op op(opHeader);
		if (!_IsValidPayload(op.Code(), op.Payload(), childCount, childOrder))
			return _Bad("operation payload invalid");
		if (op.Code() == OpCode::DrawImage) {
			const ImageHandle image = op.As<ImageRecord>().image;
			if (std::ranges::find(usedImages, image) == usedImages.end())
				usedImages.push_back(image);
		} else if (op.Code() == OpCode::DrawGlyphRun) {
			const FontId font = op.As<GlyphRunRecord>().font;
			if (std::ranges::find(usedFonts, font) == usedFonts.end())
				usedFonts.push_back(font);
		}
		at += kOpHeaderSize + size;
	}
	if (count != list->fOpCount)
		return _Bad("operation count does not match");

	// Resources: exactly the ones the operations use, in order of first use.
	const auto readHandles = [&](std::uint32_t n, std::vector<std::uint32_t>& out) {
		const auto bytes = take(std::uint64_t(n) * 4);
		if (!bytes)
			return false;
		out.resize(n);
		if (n > 0)
			std::memcpy(out.data(), bytes->data(), bytes->size());
		return true;
	};
	if (!readHandles(imageCount, list->fImages) || list->fImages != usedImages)
		return _Bad("image list does not match the operations");
	if (!readHandles(fontCount, list->fFonts) || list->fFonts != usedFonts)
		return _Bad("font list does not match the operations");

	// Every child is used, in order: the first uses are 0, 1, 2 ... Checked
	// without allocating anything by childCount, which comes from the input
	// (found by the fuzz target DisplayList: a count near 2^32 asked for
	// 16 GB).
	if (childOrder.size() != childCount)
		return _Bad("child lists not used in order");
	for (std::uint32_t i = 0; i < childCount; i++) {
		if (childOrder[i] != i)
			return _Bad("child lists not used in order");
	}
	for (std::uint32_t i = 0; i < childCount; i++) {
		const auto sizeBytes = take(4);
		if (!sizeBytes)
			return _Bad("child list truncated");
		const auto childBytes = take(_Read<std::uint32_t>(sizeBytes->data()));
		if (!childBytes)
			return _Bad("child list truncated");
		auto child = _Deserialize(*childBytes, depth + 1);
		if (!child)
			return std::unexpected(std::move(child.error()));
		list->fChildren.push_back(std::move(*child));
	}

	if (!rest.empty())
		return _Bad("trailing bytes after the display list");
	return DisplayListRef(std::move(list));
}


} // namespace ngui::gfx
