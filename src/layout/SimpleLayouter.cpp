/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/SimpleLayouter.cpp (commit
 * b1d497bb69). Haiku counts the pixels internally and converts at the
 * interface ("+ 1" in AddConstraints() and Layout(), "- 1" in the sizes it
 * returns); NewGUI's sizes are pixel counts already, so these are gone.
 * BList became std::vector.
 */


#include "SimpleLayouter.h"

#include <algorithm>
#include <cmath>

#include <ngui/LayoutUtils.h>


namespace ngui::layout {


// ElementLayoutInfo
class SimpleLayouter::ElementLayoutInfo {
public:
	int32	size;
	int32	location;

	ElementLayoutInfo()
		: size(0),
		  location(0)
	{
	}
};

// ElementInfo
class SimpleLayouter::ElementInfo {
public:
	int32	index;
	int32	min;
	int32	max;
	int32	preferred;
	float	weight;
	int64	tempWeight;

	ElementInfo()
		: index(0),
		  min(0),
		  max((int32)kSizeUnlimited),
		  preferred(0),
		  weight(1),
		  tempWeight(0)
	{
	}

	ElementInfo(int elementIndex)
		: index(elementIndex),
		  min(0),
		  max((int32)kSizeUnlimited),
		  preferred(0),
		  weight(1),
		  tempWeight(0)
	{
	}

	void Assign(const ElementInfo& info)
	{
		min = info.min;
		max = info.max;
		preferred = info.preferred;
		weight = info.weight;
		tempWeight = info.tempWeight;
	}
};

// MyLayoutInfo
class SimpleLayouter::MyLayoutInfo : public LayoutInfo {
public:
	int32							fSize;
	std::vector<ElementLayoutInfo>	fElements;
	int32							fElementCount;

	MyLayoutInfo(int32 elementCount)
		: fSize(0),
		  fElements(static_cast<std::size_t>(elementCount)),
		  fElementCount(elementCount)
	{
	}

	virtual float ElementLocation(int32 element)
	{
		if (element < 0 || element >= fElementCount) {
			// error
			return 0;
		}

		return (float)fElements[element].location;
	}

	virtual float ElementSize(int32 element)
	{
		if (element < 0 || element >= fElementCount) {
			// error
			return 0;
		}

		return (float)fElements[element].size;
	}
};


// constructor
SimpleLayouter::SimpleLayouter(int32 elementCount, float spacing)
	: fElementCount(elementCount),
	  fSpacing((int32)spacing),
	  fElements(static_cast<std::size_t>(elementCount)),
	  fMin(0),
	  fMax((int32)kSizeUnlimited),
	  fPreferred(0),
	  fMinMaxValid(false),
	  fLayoutInfo(nullptr)
{
	for (int i = 0; i < elementCount; i++)
		fElements[i].index = i;
}

// destructor
SimpleLayouter::~SimpleLayouter()
{
}

// AddConstraints
void
SimpleLayouter::AddConstraints(int32 element, int32 length,
	float _min, float _max, float /*_preferred*/)
{
	if (element < 0 || element >= fElementCount) {
		// error
		return;
	}
	if (length != 1) {
		// error
		return;
	}

	int32 min = (int32)_min;
	int32 max = (int32)_max;

	ElementInfo& info = fElements[element];
	info.min = std::max(info.min, min);
	info.max = std::min(info.max, max);
	info.preferred = std::max(info.min, min);

	fMinMaxValid = false;
}

// SetWeight
void
SimpleLayouter::SetWeight(int32 element, float weight)
{
	if (element < 0 || element >= fElementCount) {
		// error
		return;
	}

	fElements[element].weight = weight;
}

// MinSize
float
SimpleLayouter::MinSize()
{
	_ValidateMinMax();
	return (float)fMin;
}

// MaxSize
float
SimpleLayouter::MaxSize()
{
	_ValidateMinMax();
	return (float)fMax;
}

// PreferredSize
float
SimpleLayouter::PreferredSize()
{
	_ValidateMinMax();
	return (float)fPreferred;
}

// CreateLayoutInfo
std::unique_ptr<LayoutInfo>
SimpleLayouter::CreateLayoutInfo()
{
	return std::make_unique<MyLayoutInfo>(fElementCount);
}

// Layout
void
SimpleLayouter::Layout(LayoutInfo& layoutInfo, float _size)
{
	int32 size = int32(_size);

	fLayoutInfo = static_cast<MyLayoutInfo*>(&layoutInfo);

	_ValidateMinMax();

	if (fElementCount == 0)
		return;

	fLayoutInfo->fSize = std::max(size, fMin);

	// layout the elements
	if (fLayoutInfo->fSize >= fMax)
		_LayoutMax();
	else
		_LayoutStandard();

	// set locations
	int location = 0;
	for (int i = 0; i < fElementCount; i++) {
		fLayoutInfo->fElements[i].location = location;
		location += fSpacing + fLayoutInfo->fElements[i].size;
	}
}

// CloneLayouter
std::unique_ptr<Layouter>
SimpleLayouter::CloneLayouter()
{
	auto layouter = std::make_unique<SimpleLayouter>(fElementCount, (float)fSpacing);

	for (int i = 0; i < fElementCount; i++)
		layouter->fElements[i].Assign(fElements[i]);

	layouter->fMin = fMin;
	layouter->fMax = fMax;
	layouter->fPreferred = fPreferred;

	return layouter;
}

// DistributeSize
void
SimpleLayouter::DistributeSize(int32 size, const float weights[], int32 sizes[],
	int32 count)
{
	// create element infos
	std::vector<ElementInfo> infos;
	std::vector<ElementInfo*> elementInfos;
	infos.reserve(static_cast<std::size_t>(count));
	for (int32 i = 0; i < count; i++) {
		infos.emplace_back(i);
		infos.back().weight = weights[i];
		elementInfos.push_back(&infos.back());
	}

	// compute integer weights
	int64 sumWeight = _CalculateSumWeight(elementInfos);

	// distribute the size
	int64 weight = 0;
	int32 sumSize = 0;
	for (int32 i = 0; i < count; i++) {
		ElementInfo* info = elementInfos[i];
		weight += info->tempWeight;
		int32 oldSumSize = sumSize;
		sumSize = (int32)(size * weight / sumWeight);
		sizes[i] = sumSize - oldSumSize;
	}
}

// _CalculateSumWeight
int64
SimpleLayouter::_CalculateSumWeight(std::vector<ElementInfo*>& elementInfos)
{
	if (elementInfos.empty())
		return 0;
	int32 count = (int32)elementInfos.size();

	// sum up the floating point weight, so we get a scale
	double scale = 0;
	for (int32 i = 0; i < count; i++) {
		ElementInfo* info = elementInfos[i];
		scale += info->weight;
	}

	int64 weight = 0;

	if (scale == 0) {
		// The weight sum is 0: We assign each info a temporary weight of 1.
		for (int32 i = 0; i < count; i++) {
			ElementInfo* info = elementInfos[i];
			info->tempWeight = 1;
			weight += info->tempWeight;
		}
	} else {
		// We scale the weights so that their sum is about 100000. This should
		// give us ample resolution. If possible make the scale integer, so that
		// integer weights will produce exact results.
		if (scale >= 1 && scale <= 100000)
			scale = (double)std::lround(100000 / scale);
		else
			scale = 100000 / scale;

		for (int32 i = 0; i < count; i++) {
			ElementInfo* info = elementInfos[i];
			info->tempWeight = (int64)(info->weight * scale);
			weight += info->tempWeight;
		}
	}

	return weight;
}

// _ValidateMinMax
void
SimpleLayouter::_ValidateMinMax()
{
	if (fMinMaxValid)
		return;

	fMinMaxValid = true;

	if (fElementCount == 0) {
		fMin = 0;
		fMax = (int32)kSizeUnlimited;
		fPreferred = 0;
		return;
	}

	int spacing = (fElementCount - 1) * fSpacing;
	fMin = spacing;
	fMax = spacing;
	fPreferred = spacing;

	for (int i = 0; i < fElementCount; i++) {
		ElementInfo& info = fElements[i];

		// correct the preferred and maximum sizes
		if (info.max < info.min)
			info.max = info.min;
		if (info.preferred < info.min)
			info.preferred = info.min;
		else if (info.preferred > info.max)
			info.preferred = info.max;

		// sum up
		fMin += info.min;
		fMax = BLayoutUtils::AddSizesInt32(fMax, info.max);
		fPreferred = BLayoutUtils::AddSizesInt32(fPreferred, info.preferred);
	}
}

// _LayoutMax
void
SimpleLayouter::_LayoutMax()
{
	ElementInfo* infos = fElements.data();
	int32 count = fElementCount;
	if (count == 0)
		return;

	int32 additionalSpace = fLayoutInfo->fSize - fMax;

	// layout to the maximum first
	for (int i = 0; i < count; i++)
		fLayoutInfo->fElements[infos[i].index].size = infos[i].max;

	// Haiku: "Mmh, distributing according to the weights doesn't look that
	// good." -- the code that did is not ported.

	// distribute the additional space equally
	int64 sumSize = 0;
	for (int i = 0; i < count; i++) {
		int64 oldSumSize = sumSize;
		sumSize = (int64)additionalSpace * (i + 1) / count;
		fLayoutInfo->fElements[infos[i].index].size
			+= int32(sumSize - oldSumSize);
	}
}

// _LayoutStandard
void
SimpleLayouter::_LayoutStandard()
{
	int32 space = fLayoutInfo->fSize - (fElementCount - 1) * fSpacing;

	std::vector<ElementInfo*> infosToLayout;
	infosToLayout.reserve(static_cast<std::size_t>(fElementCount));
	for (int i = 0; i < fElementCount; i++) {
		infosToLayout.push_back(&fElements[i]);
		fLayoutInfo->fElements[i].size = 0;
	}

	std::vector<ElementInfo*> infosUnderMax;
	std::vector<ElementInfo*> infosOverMin;
	while (!infosToLayout.empty()) {
		int32 remainingSpace = 0;
		int32 infoCount = (int32)infosToLayout.size();
		int64 sumWeight = _CalculateSumWeight(infosToLayout);
		int64 assignedWeight = 0;
		int32 assignedSize = 0;

		for (int32 i = 0; i < infoCount; i++) {
			ElementInfo* info = infosToLayout[i];
			ElementLayoutInfo& layoutInfo = fLayoutInfo->fElements[info->index];
			// The simple algorithm is this:
			// info.size += (int)(space * info.tempWeight / sumWeight);
			// I.e. we simply assign space according to the weight. To avoid the
			// rounding problematic, we make it a bit more complicated. We
			// assign the difference of total assignment for all infos including
			// the current one minus the total excluding the current one.
			assignedWeight += info->tempWeight;
			int32 oldAssignedSize = assignedSize;
			assignedSize = (int32)(space * assignedWeight / sumWeight);
			layoutInfo.size += assignedSize - oldAssignedSize;

			if (layoutInfo.size < info->min) {
				remainingSpace += layoutInfo.size - info->min;
				layoutInfo.size = info->min;
			} else if (layoutInfo.size > info->max) {
				remainingSpace += layoutInfo.size - info->max;
				layoutInfo.size = info->max;
			}

			if (layoutInfo.size > info->min)
				infosOverMin.push_back(info);
			if (layoutInfo.size < info->max)
				infosUnderMax.push_back(info);
		}

		infosToLayout.clear();
		if (remainingSpace > 0)
			infosToLayout = infosUnderMax;
		else if (remainingSpace < 0)
			infosToLayout = infosOverMin;
		infosUnderMax.clear();
		infosOverMin.clear();
		space = remainingSpace;
	}
}


} // namespace ngui::layout
