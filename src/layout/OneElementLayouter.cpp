/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/OneElementLayouter.cpp
 * (commit b1d497bb69). NewGUI: sizes are pixel counts, so the smallest size
 * is 0 where Haiku starts at -1.
 */


#include "OneElementLayouter.h"

#include <algorithm>

#include <ngui/LayoutUtils.h>


namespace ngui::layout {


class OneElementLayouter::MyLayoutInfo : public LayoutInfo {
public:
	float	fSize;

	MyLayoutInfo()
		: fSize(0)
	{
	}

	virtual float ElementLocation(int32 /*element*/)
	{
		return 0;
	}

	virtual float ElementSize(int32 /*element*/)
	{
		return fSize;
	}
};


// constructor
OneElementLayouter::OneElementLayouter()
	: fMin(0),
	  fMax(kSizeUnlimited),
	  fPreferred(0)
{
}

// destructor
OneElementLayouter::~OneElementLayouter()
{
}

// AddConstraints
void
OneElementLayouter::AddConstraints(int32 /*element*/, int32 /*length*/,
	float min, float max, float preferred)
{
	fMin = std::max(fMin, min);
	fMax = std::min(fMax, max);
	fMax = std::max(fMax, fMin);
	fPreferred = std::max(fPreferred, preferred);
	fPreferred = std::max(fPreferred, fMin);
	fPreferred = std::min(fPreferred, fMax);
}

// SetWeight
void
OneElementLayouter::SetWeight(int32 /*element*/, float /*weight*/)
{
	// not needed
}

// MinSize
float
OneElementLayouter::MinSize()
{
	return fMin;
}

// MaxSize
float
OneElementLayouter::MaxSize()
{
	return fMax;
}

// PreferredSize
float
OneElementLayouter::PreferredSize()
{
	return fPreferred;
}

// CreateLayoutInfo
std::unique_ptr<LayoutInfo>
OneElementLayouter::CreateLayoutInfo()
{
	return std::make_unique<MyLayoutInfo>();
}

// Layout
void
OneElementLayouter::Layout(LayoutInfo& layoutInfo, float size)
{
	static_cast<MyLayoutInfo&>(layoutInfo).fSize = std::max(size, fMin);
}

// CloneLayouter
std::unique_ptr<Layouter>
OneElementLayouter::CloneLayouter()
{
	auto layouter = std::make_unique<OneElementLayouter>();
	layouter->fMin = fMin;
	layouter->fMax = fMax;
	layouter->fPreferred = fPreferred;

	return layouter;
}


} // namespace ngui::layout
