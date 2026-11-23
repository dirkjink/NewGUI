/*
 * Copyright 2006, Haiku Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/Layouter.h (commit
 * b1d497bb69). Internal to the layout engine (module G10).
 *
 * NewGUI: sizes are pixel counts (ngui/LayoutUtils.h); the layouters
 * return unique_ptr instead of raw pointers and take the layout info by
 * reference.
 */
#pragma once


#include <memory>

#include <ngui/SupportDefs.h>


namespace ngui::layout {


// The result of Layouter::Layout(): where each element lies along one axis.
class LayoutInfo {
public:
								LayoutInfo();
	virtual						~LayoutInfo();

	virtual	float				ElementLocation(int32 element) = 0;
	virtual	float				ElementSize(int32 element) = 0;
	virtual	float				ElementRangeSize(int32 position, int32 length);
};


// Distributes a size along one axis over elements with constraints on
// single elements or ranges of them (min, max, preferred) and weights.
class Layouter {
public:
								Layouter();
	virtual						~Layouter();

	virtual	void				AddConstraints(int32 element, int32 length,
									float min, float max, float preferred) = 0;
	virtual	void				SetWeight(int32 element, float weight) = 0;

	virtual	float				MinSize() = 0;
	virtual	float				MaxSize() = 0;
	virtual	float				PreferredSize() = 0;

	virtual	std::unique_ptr<LayoutInfo> CreateLayoutInfo() = 0;

	virtual	void				Layout(LayoutInfo& layoutInfo, float size) = 0;

	virtual	std::unique_ptr<Layouter> CloneLayouter() = 0;
};


} // namespace ngui::layout
