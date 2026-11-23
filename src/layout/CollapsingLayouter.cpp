/*
 * Copyright 2011, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/CollapsingLayouter.cpp
 * (commit b1d497bb69). NewGUI: BObjectList and raw pointers became
 * std::vector and unique_ptr. SetTo() copies the constraints in their
 * order; Haiku reversed it, and _AddConstraints() reverses it once more,
 * so a clone of a clone added them in a different order than its original.
 * Elements without a position get no weight; Haiku passed position -1,
 * which the layouters ignore. A constraint added to an existing layouter
 * goes to the element's collapsed position; Haiku passed the element's own
 * index, which is another element once one before it has collapsed.
 */


#include "CollapsingLayouter.h"

#include <ngui/LayoutUtils.h>

#include "ComplexLayouter.h"
#include "OneElementLayouter.h"
#include "SimpleLayouter.h"


namespace ngui::layout {


class CollapsingLayouter::ProxyLayoutInfo : public LayoutInfo {
public:
	ProxyLayoutInfo(std::unique_ptr<LayoutInfo> target, int32 elementCount)
		:
		fElements(static_cast<std::size_t>(elementCount), -1),
		fTarget(std::move(target)),
		fElementCount(elementCount)
	{
	}

	void
	LayoutTarget(Layouter* layouter, float size)
	{
		if (layouter)
			layouter->Layout(*fTarget, size);
	}

	void
	SetElementPosition(int32 element, int32 position)
	{
		fElements[element] = position;
	}

	float
	ElementLocation(int32 element)
	{
		if (element < 0 || element >= fElementCount || fElements[element] < 0)
			return 0;
		return fTarget->ElementLocation(fElements[element]);
	}

	float
	ElementSize(int32 element)
	{
		if (element < 0 || element >= fElementCount || fElements[element] < 0)
			return 0;
		return fTarget->ElementSize(fElements[element]);
	}

	float
	ElementRangeSize(int32 element, int32 length)
	{
		if (element < 0 || element >= fElementCount || fElements[element] < 0)
			return 0;
		return fTarget->ElementRangeSize(fElements[element], length);
	}

private:
	std::vector<int32>			fElements;
	std::unique_ptr<LayoutInfo>	fTarget;
	int32						fElementCount;
};


struct CollapsingLayouter::Constraint {
	int32 length;
	float min;
	float max;
	float preferred;
};


struct CollapsingLayouter::ElementInfo {
	float weight;
	int32 position;
	bool valid;
	std::vector<Constraint> constraints;

	ElementInfo()
		:
		weight(0),
		position(-1),
		valid(false)
	{
	}

	void SetTo(const ElementInfo& other)
	{
		weight = other.weight;
		position = other.position;
		valid = other.valid;
		constraints = other.constraints;
	}
};


CollapsingLayouter::CollapsingLayouter(int32 elementCount, float spacing)
	:
	fElementCount(elementCount),
	fElements(static_cast<std::size_t>(elementCount)),
	fValidElementCount(0),
	fHaveMultiElementConstraints(false),
	fSpacing(spacing)
{
}


CollapsingLayouter::~CollapsingLayouter()
{
}


void
CollapsingLayouter::AddConstraints(int32 element, int32 length, float min,
	float max, float preferred)
{
	if (min == kSizeUnset && max == kSizeUnset)
		return;
	if (element < 0 || length <= 0 || element + length > fElementCount)
		return;

	Constraint constraint;
	constraint.length = length;
	constraint.min = min;
	constraint.max = max;
	constraint.preferred = preferred;

	if (length > 1)
		fHaveMultiElementConstraints = true;

	int32 validElements = fValidElementCount;

	for (int32 i = element; i < element + length; i++) {
		if (fElements[i].valid == false) {
			fElements[i].valid = true;
			fValidElementCount++;
		}
	}

	fElements[element].constraints.push_back(constraint);
	if (fValidElementCount > validElements)
		fLayouter.reset();

	if (fLayouter)
		_AddConstraints(fElements[element].position, &constraint);
}


void
CollapsingLayouter::SetWeight(int32 element, float weight)
{
	if (element < 0 || element >= fElementCount)
		return;

	ElementInfo& elementInfo = fElements[element];
	elementInfo.weight = weight;

	if (fLayouter && elementInfo.position >= 0)
		fLayouter->SetWeight(elementInfo.position, weight);
}


float
CollapsingLayouter::MinSize()
{
	_ValidateLayouter();
	return fLayouter ? fLayouter->MinSize() : 0;
}


float
CollapsingLayouter::MaxSize()
{
	_ValidateLayouter();
	return fLayouter ? fLayouter->MaxSize() : kSizeUnlimited;
}


float
CollapsingLayouter::PreferredSize()
{
	_ValidateLayouter();
	return fLayouter ? fLayouter->PreferredSize() : 0;
}


std::unique_ptr<LayoutInfo>
CollapsingLayouter::CreateLayoutInfo()
{
	_ValidateLayouter();

	std::unique_ptr<LayoutInfo> info = fLayouter ? fLayouter->CreateLayoutInfo()
		: nullptr;
	return std::make_unique<ProxyLayoutInfo>(std::move(info), fElementCount);
}


void
CollapsingLayouter::Layout(LayoutInfo& layoutInfo, float size)
{
	_ValidateLayouter();
	ProxyLayoutInfo& info = static_cast<ProxyLayoutInfo&>(layoutInfo);
	for (int32 i = 0; i < fElementCount; i++) {
		info.SetElementPosition(i, fElements[i].position);
	}

	info.LayoutTarget(fLayouter.get(), size);
}


std::unique_ptr<Layouter>
CollapsingLayouter::CloneLayouter()
{
	auto clone = std::make_unique<CollapsingLayouter>(fElementCount, fSpacing);
	for (int32 i = 0; i < fElementCount; i++)
		clone->fElements[i].SetTo(fElements[i]);

	clone->fValidElementCount = fValidElementCount;
	clone->fHaveMultiElementConstraints = fHaveMultiElementConstraints;

	if (fLayouter)
		clone->fLayouter = fLayouter->CloneLayouter();
	return clone;
}


void
CollapsingLayouter::_ValidateLayouter()
{
	if (fLayouter)
		return;

	_CreateLayouter();
	_DoCollapse();
	_AddConstraints();
	_SetWeights();
}


Layouter*
CollapsingLayouter::_CreateLayouter()
{
	if (fLayouter)
		return fLayouter.get();

	if (fValidElementCount == 0) {
		fLayouter = nullptr;
	} else if (fValidElementCount == 1) {
		fLayouter = std::make_unique<OneElementLayouter>();
	} else if (fHaveMultiElementConstraints) {
		fLayouter = std::make_unique<ComplexLayouter>(fValidElementCount,
			fSpacing);
	} else {
		fLayouter = std::make_unique<SimpleLayouter>(fValidElementCount,
			fSpacing);
	}

	return fLayouter.get();
}


void
CollapsingLayouter::_DoCollapse()
{
	int32 shift = 0;
	for (int32 i = 0; i < fElementCount; i++) {
		ElementInfo& element = fElements[i];
		if (!element.valid) {
			shift++;
			element.position = -1;
			continue;
		} else {
			element.position = i - shift;
		}
	}
}


void
CollapsingLayouter::_AddConstraints()
{
	if (fLayouter == nullptr)
		return;

	for (int32 i = 0; i < fElementCount; i++) {
		ElementInfo& element = fElements[i];
		for (int32 k = (int32)element.constraints.size() - 1; k >= 0; k--)
			_AddConstraints(element.position, &element.constraints[k]);
	}
}


void
CollapsingLayouter::_AddConstraints(int32 position, const Constraint* c)
{
	fLayouter->AddConstraints(position, c->length, c->min, c->max,
		c->preferred);
}


void
CollapsingLayouter::_SetWeights()
{
	if (!fLayouter)
		return;

	for (int32 i = 0; i < fElementCount; i++) {
		if (fElements[i].position >= 0)
			fLayouter->SetWeight(fElements[i].position, fElements[i].weight);
	}
}


} // namespace ngui::layout
