/*
 * Copyright 2003-2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Region.cpp (commit b1d497bb69)
 *
 *	Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, burton666@libero.it
 */


#include <ngui/gfx/Region.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "RegionSupport.h"


namespace ngui::gfx {


namespace {


// From Haiku's headers/private/interface/clipping.h, for the internal format.
inline void
offset_rect(clipping_rect& rect, int32 x, int32 y)
{
	rect.left += x;
	rect.top += y;
	rect.right += x;
	rect.bottom += y;
}


} // namespace


const static int32 kDataBlockSize = 8;


Region::Region()
	:
	fCount(0),
	fDataSize(0),
	fBounds(clipping_rect{ 0, 0, 0, 0 }),
	fData(NULL)
{
	_SetSize(kDataBlockSize);
}


Region::Region(const Region& other)
	:
	fCount(0),
	fDataSize(0),
	fBounds(clipping_rect{ 0, 0, 0, 0 }),
	fData(NULL)
{
	*this = other;
}


Region::Region(const IntRect& rect)
	:
	fCount(0),
	fDataSize(1),
	fBounds(clipping_rect{ 0, 0, 0, 0 }),
	fData(&fBounds)
{
	if (rect.IsEmpty())
		return;

	fBounds = _ConvertToInternal(rect);
	fCount = 1;
}


Region::Region(Region&& other)
	:
	fCount(0),
	fDataSize(0),
	fBounds(clipping_rect{ 0, 0, 0, 0 }),
	fData(NULL)
{
	MoveFrom(other);
}


// NOTE: private constructor
Region::Region(const clipping_rect& clipping)
	:
	fCount(1),
	fDataSize(1),
	fBounds(clipping),
	fData(&fBounds)
{
}


Region::~Region()
{
	if (fData != &fBounds)
		free(fData);
}


Region&
Region::operator=(const Region& other)
{
	if (&other == this)
		return *this;

	// handle reallocation if we're too small to contain the other's data
	if (_SetSize(other.fDataSize)) {
		// As in operator==: no memcpy() from the null buffer of a moved-from
		// region.
		if (other.fCount > 0)
			memcpy(fData, other.fData, other.fCount * sizeof(clipping_rect));

		fBounds = other.fBounds;
		fCount = other.fCount;
	}

	return *this;
}


Region&
Region::operator=(Region&& other)
{
	MoveFrom(other);

	return *this;
}


bool
Region::operator==(const Region& other) const
{
	if (&other == this)
		return true;

	if (fCount != other.fCount)
		return false;

	// A moved-from region has no buffer, and memcmp() with a null pointer is
	// undefined even for zero bytes (found by the fuzz target Region).
	if (fCount == 0)
		return true;

	return memcmp(fData, other.fData, fCount * sizeof(clipping_rect)) == 0;
}


void
Region::Set(const IntRect& rect)
{
	_SetSize(1);

	if (!rect.IsEmpty() && fData != NULL) {
		fCount = 1;
		fData[0] = fBounds = _ConvertToInternal(rect);
	} else
		MakeEmpty();
}


void
Region::MoveFrom(Region& other)
{
	if (&other == this)
		return;

	if (other.CountRects() <= 0) {
		MakeEmpty();
		return;
	}
	if (other.CountRects() == 1) {
		Set(other.Frame());
		other.MakeEmpty();
		return;
	}
	// Haiku overwrites fData here without freeing it, which leaks the
	// buffer of every region that is moved into (still so in Haiku's
	// commit 80fcf2df19; found by the fuzz target Region).
	if (fData != &fBounds)
		free(fData);

	fCount = other.fCount;
	fDataSize = other.fDataSize;
	fBounds = other.fBounds;
	fData = other.fData;

	other.fCount = 0;
	other.fDataSize = 0;
	other.fBounds = clipping_rect{ 0, 0, 0, 0 };
	other.fData = NULL;
}


IntRect
Region::Frame() const
{
	return _ConvertFromInternal(fBounds);
}


IntRect
Region::RectAt(int32 index) const
{
	if (index >= 0 && index < fCount)
		return _ConvertFromInternal(fData[index]);

	return IntRect{ 0, 0, 0, 0 };
		// an empty IntRect
}


int32
Region::CountRects() const
{
	return fCount;
}


bool
Region::IsEmpty() const
{
	return fCount == 0;
}


bool
Region::Intersects(const IntRect& rect) const
{
	if (rect.IsEmpty())
		return false;

	int result = Support::XRectInRegion(this, _ConvertToInternal(rect));

	return result > Support::RectangleOut;
}


bool
Region::Contains(Point point) const
{
	if (!point.IsFinite())
		return false;

	// floor() instead of Haiku's cast, which rounds towards zero and would
	// put (-0.5, 0) into the pixel (0, 0).
	return Contains((int32)floorf(point.x), (int32)floorf(point.y));
}


bool
Region::Contains(int32 x, int32 y) const
{
	return Support::XPointInRegion(this, x, y);
}


void
Region::OffsetBy(int32 x, int32 y)
{
	if (x == 0 && y == 0)
		return;

	if (fCount > 0) {
		if (fData != &fBounds) {
			for (int32 i = 0; i < fCount; i++)
				offset_rect(fData[i], x, y);
		}

		offset_rect(fBounds, x, y);
	}
}


void
Region::MakeEmpty()
{
	fBounds = clipping_rect{ 0, 0, 0, 0 };
	fCount = 0;
}


void
Region::Include(const IntRect& rect)
{
	if (rect.IsEmpty())
		return;

	// use private clipping_rect constructor which avoids malloc()
	Region temp(_ConvertToInternal(rect));

	Region result;
	Support::XUnionRegion(this, &temp, &result);

	_AdoptRegionData(result);
}


void
Region::Include(const Region& region)
{
	Region result;
	Support::XUnionRegion(this, &region, &result);

	_AdoptRegionData(result);
}


void
Region::Exclude(const IntRect& rect)
{
	if (rect.IsEmpty())
		return;

	// use private clipping_rect constructor which avoids malloc()
	Region temp(_ConvertToInternal(rect));

	Region result;
	Support::XSubtractRegion(this, &temp, &result);

	_AdoptRegionData(result);
}


void
Region::Exclude(const Region& region)
{
	Region result;
	Support::XSubtractRegion(this, &region, &result);

	_AdoptRegionData(result);
}


void
Region::IntersectWith(const Region& region)
{
	Region result;
	Support::XIntersectRegion(this, &region, &result);

	_AdoptRegionData(result);
}


void
Region::ExclusiveInclude(const Region& region)
{
	Region result;
	Support::XXorRegion(this, &region, &result);

	_AdoptRegionData(result);
}


//	#pragma mark - Region private methods


/*!
	\fn void Region::_AdoptRegionData(Region& region)
	\brief Takes over the data of \a region and empties it.

	\param region The \a region to adopt data from.
*/
void
Region::_AdoptRegionData(Region& region)
{
	fCount = region.fCount;
	fDataSize = region.fDataSize;
	fBounds = region.fBounds;
	if (fData != &fBounds)
		free(fData);
	if (region.fData != &region.fBounds)
		fData = region.fData;
	else
		fData = &fBounds;

	// NOTE: MakeEmpty() is not called since _AdoptRegionData is only
	// called with internally allocated regions, so they don't need to
	// be left in a valid state.
	region.fData = NULL;
//	region.MakeEmpty();
}


/*!
	\fn bool Region::_SetSize(int32 newSize)
	\brief Reallocate the memory in the region.

	\param newSize The amount of rectangles that the region should be
		able to hold.
*/
bool
Region::_SetSize(int32 newSize)
{
	// we never shrink the size
	newSize = fDataSize > newSize ? fDataSize : newSize;
		// The amount of rectangles that the region should be able to hold.
	if (newSize == fDataSize)
		return true;

	// align newSize to multiple of kDataBlockSize
	newSize = ((newSize + kDataBlockSize - 1) / kDataBlockSize) * kDataBlockSize;

	if (newSize > 0) {
		if (fData == &fBounds) {
			fData = (clipping_rect*)malloc(newSize * sizeof(clipping_rect));
			fData[0] = fBounds;
		} else if (fData) {
			clipping_rect* resizedData = (clipping_rect*)realloc(fData,
				newSize * sizeof(clipping_rect));
			if (!resizedData) {
				// failed to resize, but we cannot keep the
				// previous state of the object
				free(fData);
				fData = NULL;
			} else
				fData = resizedData;
		} else
			fData = (clipping_rect*)malloc(newSize * sizeof(clipping_rect));
	} else {
		// just an empty region, but no error
		MakeEmpty();
		return true;
	}

	if (!fData) {
		// allocation actually failed
		fDataSize = 0;
		MakeEmpty();
		return false;
	}

	fDataSize = newSize;
	return true;
}


clipping_rect
Region::_ConvertToInternal(const IntRect& rect)
{
	// The internal format is half-open as well: right and bottom exclusive.
	return clipping_rect{ rect.x0, rect.y0, rect.x1, rect.y1 };
}


IntRect
Region::_ConvertFromInternal(const clipping_rect& rect)
{
	return IntRect{ rect.left, rect.top, rect.right, rect.bottom };
}


} // namespace ngui::gfx
