/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's BRegion as far as the ported looks use it, over gfx::Region:
// inclusive BRects in, inclusive BRects out, converted the way Haiku's
// BRegion converts them internally (floor of left and top, ceil of right and
// bottom plus one).


#include <cmath>

#include <ngui/gfx/IntRect.h>
#include <ngui/gfx/Region.h>

#include "Rect.h"


namespace ngui::compat {


class BRegion {
public:
								BRegion() = default;
	explicit					BRegion(BRect rect);

			void				Set(BRect rect);
			void				Include(BRect rect);
			void				Exclude(BRect rect);
			void				MakeEmpty();

			int32				CountRects() const;
			BRect				RectAt(int32 index) const;
			BRect				Frame() const;

			const gfx::Region&	Region() const;

	static	gfx::IntRect		ToIntRect(BRect rect);

private:
			gfx::Region			fRegion;
};


inline
BRegion::BRegion(BRect rect)
{
	Set(rect);
}


inline gfx::IntRect
BRegion::ToIntRect(BRect rect)
{
	if (!rect.IsValid())
		return {0, 0, 0, 0};
	return {static_cast<int32>(floorf(rect.left)), static_cast<int32>(floorf(rect.top)),
		static_cast<int32>(ceilf(rect.right)) + 1, static_cast<int32>(ceilf(rect.bottom)) + 1};
}


inline void
BRegion::Set(BRect rect)
{
	fRegion.Set(ToIntRect(rect));
}


inline void
BRegion::Include(BRect rect)
{
	fRegion.Include(ToIntRect(rect));
}


inline void
BRegion::Exclude(BRect rect)
{
	fRegion.Exclude(ToIntRect(rect));
}


inline void
BRegion::MakeEmpty()
{
	fRegion.MakeEmpty();
}


inline int32
BRegion::CountRects() const
{
	return fRegion.CountRects();
}


inline BRect
BRegion::RectAt(int32 index) const
{
	if (index < 0 || index >= fRegion.CountRects())
		return BRect();
	const gfx::IntRect rect = fRegion.RectAt(index);
	return BRect(rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1);
}


inline BRect
BRegion::Frame() const
{
	if (fRegion.IsEmpty())
		return BRect();
	const gfx::IntRect frame = fRegion.Frame();
	return BRect(frame.x0, frame.y0, frame.x1 - 1, frame.y1 - 1);
}


inline const gfx::Region&
BRegion::Region() const
{
	return fRegion;
}


} // namespace ngui::compat
