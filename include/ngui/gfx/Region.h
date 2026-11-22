/*
 * Copyright 2003-2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: headers/os/interface/Region.h (commit b1d497bb69)
 */
#pragma once


#include <ngui/SupportDefs.h>
#include <ngui/gfx/IntRect.h>
#include <ngui/gfx/Point.h>


namespace ngui::gfx {


/*!	The rectangle format a Region stores internally: whole pixels, right and
	bottom exclusive, exactly like inside Haiku's BRegion. Not part of the
	API; it keeps the ported banding code (RegionSupport.cpp) unchanged.
*/
typedef struct {
	int32	left;	/*!< Left edge, inclusive. */
	int32	top;	/*!< Top edge, inclusive. */
	int32	right;	/*!< Right edge, exclusive. */
	int32	bottom;	/*!< Bottom edge, exclusive. */
} clipping_rect;


/*!	An area made of axis-parallel rectangles in whole pixels: the clip region
	of the Canvas (specification 7). Ported from Haiku's BRegion, which in
	turn takes its banding algorithm from X11: the rectangles are sorted by y
	and then x, every band of rows has rectangles of the same height, and no
	two rectangles of a band touch.

	**Half-open** throughout (specification 4.1): `Region(IntRect{0, 0, 10,
	10})` covers 10 × 10 pixels; Haiku's `BRegion(BRect(0, 0, 9, 9))` is the
	same area. Hence the new name (rule K5).

	Differences from BRegion besides the rectangle model: the operations take
	references instead of pointers, Contains(Point) rounds down instead of
	towards zero, and ScaleBy() and PrintToStream() are left out. As in Haiku
	a failed allocation leaves the region empty instead of throwing.
*/
class Region {
public:
								Region();
									/*!< The empty region. */
								Region(const Region& other);
									/*!< A copy of \a other. */
	explicit					Region(const IntRect& rect);
									/*!< The region covering \a rect; empty if \a rect is. */
								Region(Region&& other);
									/*!< Takes over the rectangles of \a other, which becomes
										empty. */
								~Region();

			Region&				operator=(const Region& other);
									/*!< Copies \a other. */
			Region&				operator=(Region&& other);
									/*!< Takes over the rectangles of \a other, which becomes
										empty. */
			bool				operator==(const Region& other) const;
									/*!< Both cover the same area. */

			void				Set(const IntRect& rect);
									/*!< Replaces the region by \a rect. */
			void				MoveFrom(Region& other);
									/*!< Takes over the rectangles of \a other, which becomes
										empty. */

			IntRect				Frame() const;
									/*!< The bounding rectangle; `{0, 0, 0, 0}` if empty. */
			IntRect				RectAt(int32 index) const;
									/*!< The rectangle at \a index in banded order; an empty
										rectangle if \a index is out of range. */
			int32				CountRects() const;
									/*!< The number of rectangles. */
			bool				IsEmpty() const;
									/*!< The region covers no pixel. */

			bool				Intersects(const IntRect& rect) const;
									/*!< At least one pixel of \a rect is in the region. */
			bool				Contains(Point point) const;
									/*!< The pixel containing \a point, `(floor(x), floor(y))`,
										is in the region. */
			bool				Contains(int32 x, int32 y) const;
									/*!< The pixel `(x, y)` is in the region. */

			void				OffsetBy(int32 x, int32 y);
									/*!< Moves the region; the coordinates must not overflow. */

			void				MakeEmpty();
									/*!< Removes all rectangles. */

			void				Include(const IntRect& rect);
									/*!< Union with \a rect. */
			void				Include(const Region& region);
									/*!< Union with \a region. */

			void				Exclude(const IntRect& rect);
									/*!< Difference: removes \a rect. */
			void				Exclude(const Region& region);
									/*!< Difference: removes \a region. */

			void				IntersectWith(const Region& region);
									/*!< Intersection with \a region. */

			void				ExclusiveInclude(const Region& region);
									/*!< Symmetric difference with \a region. */

private:
	class Support;
	friend class Support;

private:
								Region(const clipping_rect& clipping);

			void				_AdoptRegionData(Region& region);
			bool				_SetSize(int32 newSize);

	static	clipping_rect		_ConvertToInternal(const IntRect& rect);
	static	IntRect				_ConvertFromInternal(const clipping_rect& rect);

private:
			int32				fCount;
			int32				fDataSize;
			clipping_rect		fBounds;
			clipping_rect*		fData;
};


} // namespace ngui::gfx
