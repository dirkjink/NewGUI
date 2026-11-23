/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/Layouter.cpp (commit
 * b1d497bb69).
 */


#include "Layouter.h"


namespace ngui::layout {


// constructor
LayoutInfo::LayoutInfo()
{
}


// destructor
LayoutInfo::~LayoutInfo()
{
}


// ElementRangeSize
float
LayoutInfo::ElementRangeSize(int32 position, int32 length)
{
	if (length == 1)
		return ElementSize(position);

	int lastIndex = position + length - 1;
	return ElementLocation(lastIndex) + ElementSize(lastIndex)
		- ElementLocation(position);
}


// #pragma mark -


// constructor
Layouter::Layouter()
{
}


// destructor
Layouter::~Layouter()
{
}


} // namespace ngui::layout
