/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/IntRect.h>
#include <ngui/gfx/Line.h>
#include <ngui/gfx/Paint.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/RRect.h>
#include <ngui/gfx/Rect.h>


namespace ngui::gfx {


class DisplayList;

/*!	A shared, immutable DisplayList (specification 11.1). */
using DisplayListRef = std::shared_ptr<const DisplayList>;


/*!	The operations of the core profile (specification 11.2). The values are
	part of the binary format and never change. */
enum class OpCode : std::uint16_t {
	FillRect = 1,			/*!< RectRecord */
	FillRRect = 2,			/*!< RRectRecord */
	FillRing = 3,			/*!< RingRecord */
	FillEllipse = 4,		/*!< RectRecord */
	FillConvexPolygon = 5,	/*!< PointsRecord, then the points */
	StrokeRect = 6,			/*!< RectRecord */
	StrokeRRect = 7,		/*!< RRectRecord */
	StrokePolyline = 8,		/*!< PointsRecord, then the points */
	StrokeArc = 9,			/*!< ArcRecord */
	DrawLines = 10,			/*!< LinesRecord, then the lines */
	DrawImage = 11,			/*!< ImageRecord */
	DrawGlyphRun = 12,		/*!< GlyphRunRecord, then glyphs, positions, clusters, text */
	DrawList = 13			/*!< ListRecord */
};


// The records are the payloads of the operations, exactly as stored and
// serialized: only 4-byte fields and groups of four bytes, so there is no
// padding and the layout is the same with every compiler (checked below).


/*!	A LinearGradient as stored in a DisplayList. */
struct GradientRecord {
			Point				p0;			/*!< LinearGradient::p0. */
			Point				p1;			/*!< LinearGradient::p1. */
			GradientStop		stops[4];	/*!< LinearGradient::stops; unused ones zero. */
			std::uint32_t		count;		/*!< LinearGradient::count. */
};


/*!	A Paint as stored in a DisplayList; Paint itself has no fixed layout
	(std::optional). */
struct PaintRecord {
			Color				color;			/*!< Paint::color. */
			float				strokeWidth;	/*!< Paint::strokeWidth. */
			std::uint8_t		cap;			/*!< Paint::cap. */
			std::uint8_t		join;			/*!< Paint::join. */
			std::uint8_t		antialias;		/*!< Paint::antialias, 0 or 1. */
			std::uint8_t		hasGradient;	/*!< Paint::gradient is set, 0 or 1. */
			GradientRecord		gradient;		/*!< Paint::gradient; all zero if not set. */

	static	PaintRecord			From(const Paint& paint);
									/*!< The record of \a paint. */
			Paint				ToPaint() const;
									/*!< The Paint this record stands for. */
};


/*!	Payload of FillRect, FillEllipse and StrokeRect. */
struct RectRecord {
			Rect				rect;	/*!< The rectangle (for FillEllipse: its bounds). */
			PaintRecord			paint;	/*!< How to fill or stroke. */
};


/*!	Payload of FillRRect and StrokeRRect. */
struct RRectRecord {
			RRect				rrect;	/*!< The rounded rectangle, as recorded. */
			PaintRecord			paint;	/*!< How to fill or stroke. */
};


/*!	Payload of FillRing. */
struct RingRecord {
			RRect				outer;	/*!< Outer edge. */
			RRect				inner;	/*!< Inner edge, inside #outer. */
			PaintRecord			paint;	/*!< How to fill. */
};


/*!	Payload of FillConvexPolygon and StrokePolyline; the points follow. */
struct PointsRecord {
			PaintRecord			paint;	/*!< How to fill or stroke. */
			std::uint32_t		count;	/*!< Number of points that follow. */
			std::uint32_t		closed;	/*!< StrokePolyline: 1 if closed; 0 otherwise. */
};


/*!	Payload of StrokeArc. */
struct ArcRecord {
			Rect				oval;		/*!< Bounds of the ellipse. */
			float				startDeg;	/*!< Start angle, 0° at 3 o'clock, clockwise. */
			float				sweepDeg;	/*!< Sweep angle, clockwise if positive. */
			PaintRecord			paint;		/*!< How to stroke. */
};


/*!	Payload of DrawLines; the lines follow. */
struct LinesRecord {
			float				width;		/*!< Line width. */
			std::uint8_t		cap;		/*!< Paint::Cap of all lines. */
			std::uint8_t		reserved[3];/*!< Zero. */
			std::uint32_t		count;		/*!< Number of lines that follow. */
};


/*!	Payload of DrawImage. */
struct ImageRecord {
			ImageHandle			image;	/*!< The image; listed in DisplayList::Images(). */
			float				alpha;	/*!< Multiplied into the image, `[0, 1]`. */
			Rect				src;	/*!< Part of the image, in image pixels. */
			Rect				dst;	/*!< Where to draw it. */
};


/*!	Payload of DrawGlyphRun; then follow `glyphCount` glyph indices,
	`glyphCount` positions, `clusterCount` cluster offsets and `textBytes`
	bytes of UTF-8 text, padded with zeros to a multiple of 4. */
struct GlyphRunRecord {
			FontId				font;			/*!< GlyphRun::font; listed in Fonts(). */
			float				sizePx;			/*!< GlyphRun::sizePx. */
			Point				origin;			/*!< Where the run starts. */
			Color				color;			/*!< Solid color (text has no gradient). */
			std::uint32_t		glyphCount;		/*!< Number of glyphs and positions. */
			std::uint32_t		clusterCount;	/*!< 0 or #glyphCount. */
			std::uint32_t		textBytes;		/*!< Length of the text. */
};


/*!	Payload of DrawList. */
struct ListRecord {
			std::uint32_t		child;	/*!< Index into DisplayList::Children(). */
			Point				offset;	/*!< Where the child list is placed. */
};


/*!	One operation of a DisplayList, as the iterator of DisplayList::Ops()
	yields it. The accessors for a record or its trailing arrays may only be
	used for the opcodes they belong to.
*/
class Op {
public:
			OpCode				Code() const;
									/*!< The opcode; may be one this version does not know. */
			std::uint16_t		Flags() const;
									/*!< Reserved for the extended profile, kept as recorded. */
			std::uint32_t		ClipIndex() const;
									/*!< Index of the clip region, see DisplayList::Clip(). */
			std::span<const std::byte> Payload() const;
									/*!< The payload: the record and what follows it. */

			template<typename Record>
			const Record&		As() const;
									/*!< The record at the start of the payload. */

			std::span<const Point> Points() const;
									/*!< FillConvexPolygon, StrokePolyline: the points. */
			std::span<const Line> Lines() const;
									/*!< DrawLines: the lines. */
			std::span<const std::uint32_t> Glyphs() const;
									/*!< DrawGlyphRun: the glyph indices. */
			std::span<const Point> Positions() const;
									/*!< DrawGlyphRun: the glyph positions. */
			std::span<const std::uint32_t> Clusters() const;
									/*!< DrawGlyphRun: the cluster offsets, may be empty. */
			std::u8string_view	Text() const;
									/*!< DrawGlyphRun: the source text, may be empty. */

private:
	friend class DisplayList;

								Op(const std::byte* header);

			const std::byte*	fHeader;
};


/*!	The recorded drawing of one View (specification 11): immutable,
	reference counted (DisplayListRef), thread-neutral and self-contained.
	Coordinates are absolute in the space of the list; translations were
	applied when recording, and Save, Restore and Translate do not appear.

	Every operation refers to a clip region of the clip table, a list of
	canonical rectangles from Region. Images and fonts are referred to by
	handle and listed in Images() and Fonts(); nested lists (DrawList) are
	held in Children().

	Created by the Canvas (Canvas::Finish()) or by Deserialize(). The binary
	form of Serialize() is the memory layout, little endian and versioned
	(specification 11.3); it is canonical, so `Serialize(Deserialize(data))`
	gives `data` again.
*/
class DisplayList {
public:
	/*!	A forward iterator over the operations. */
	class Iterator {
	public:
		using iterator_category = std::forward_iterator_tag;	/*!< Forward iterator. */
		using value_type = Op;									/*!< Yields Op. */
		using difference_type = std::ptrdiff_t;					/*!< Distance type. */

								Iterator() = default;
									/*!< The end of any list. */
			Op					operator*() const;
									/*!< The current operation. */
			Iterator&			operator++();
									/*!< Moves to the next operation. */
			Iterator			operator++(int);
									/*!< Moves to the next operation, returns the old position. */
			bool				operator==(const Iterator& other) const = default;
									/*!< Same position. */

	private:
		friend class DisplayList;

								Iterator(const std::byte* position);

			const std::byte*	fPosition = nullptr;
	};

	/*!	The operations, for a range-based for loop. */
	struct OpRange {
			Iterator			first;	/*!< First operation. */
			Iterator			last;	/*!< Behind the last operation. */

			Iterator			begin() const { return first; }	/*!< #first */
			Iterator			end() const { return last; }	/*!< #last */
	};

	/*!	The current version of the binary format. */
	static constexpr std::uint16_t kVersion = 1;

	/*!	Nesting depth Deserialize() accepts for DrawList. */
	static constexpr int kMaxDepth = 64;

								~DisplayList();
									/*!< Gives back the image references the list holds. */

			Rect				Bounds() const;
									/*!< Bounds of the recording View. */
			std::uint32_t		OpCount() const;
									/*!< Number of operations. */
			OpRange				Ops() const;
									/*!< The operations in recording order. */

			std::uint32_t		ClipCount() const;
									/*!< Number of clip regions. */
			std::span<const IntRect> Clip(std::uint32_t index) const;
									/*!< The canonical rectangles of clip region \a index. */

			std::span<const ImageHandle> Images() const;
									/*!< All images the operations use, in order of first use.
										The list holds a reference to each of them in
										ImageRegistry::Default(), as far as it knows them. */
			std::span<const FontId> Fonts() const;
									/*!< All fonts the operations use, in order of first use. */
			std::span<const DisplayListRef> Children() const;
									/*!< All nested lists, in order of first use. */

			std::vector<std::byte> Serialize() const;
									/*!< The binary form (specification 11.3). */
	static	std::expected<DisplayListRef, Error> Deserialize(std::span<const std::byte> data);
									/*!< Reads a binary form; ErrorCode::BadData unless \a data
										is exactly what Serialize() would give for a list the
										Canvas could have recorded (finite coordinates, valid
										paints, indices in range, at most #kMaxDepth levels). */

private:
	friend class DisplayListBuilder;

								DisplayList() = default;

	static	std::expected<DisplayListRef, Error> _Deserialize(std::span<const std::byte> data,
									int depth);
			void				_RetainImages();

			Rect				fBounds = {0, 0, 0, 0};
			std::uint32_t		fOpCount = 0;
			std::vector<std::byte> fOps;
			std::vector<std::uint32_t> fClipStarts;
			std::vector<IntRect> fClipRects;
			std::vector<ImageHandle> fImages;
			std::vector<ImageHandle> fRetainedImages;
			std::vector<FontId> fFonts;
			std::vector<DisplayListRef> fChildren;
};


/*!	Size of the header in front of every payload in the operation stream:
	opcode, flags, payload size, clip index (specification 11.2). */
inline constexpr std::size_t kOpHeaderSize = 12;


template<typename Record>
const Record&
Op::As() const
{
	// The payload was written with memcpy into the list's own buffer, which
	// implicitly creates the record there (C++20, P0593); it is 4-byte
	// aligned like every record.
	return *reinterpret_cast<const Record*>(fHeader + kOpHeaderSize);
}


static_assert(sizeof(GradientRecord) == 100);
static_assert(sizeof(PaintRecord) == 124);
static_assert(sizeof(RectRecord) == 140);
static_assert(sizeof(RRectRecord) == 156);
static_assert(sizeof(RingRecord) == 188);
static_assert(sizeof(PointsRecord) == 132);
static_assert(sizeof(ArcRecord) == 148);
static_assert(sizeof(LinesRecord) == 12);
static_assert(sizeof(ImageRecord) == 40);
static_assert(sizeof(GlyphRunRecord) == 44);
static_assert(sizeof(ListRecord) == 12);


} // namespace ngui::gfx
