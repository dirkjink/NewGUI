/*
 * Copyright 2011, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/CollapsingLayouter.h
 * (commit b1d497bb69).
 */
#pragma once


#include <memory>
#include <vector>

#include "Layouter.h"


namespace ngui::layout {


/* This layouter wraps either a Compound, Simple or OneElement layouter, and
 * removes elements which have no constraints, or min/max constraints of
 * kSizeUnset. The child layouter is given only the constraints for the
 * remaining elements. When using the LayoutInfo of this layouter,
 * collapsed (removed) elements are given no space on screen.
 */
class CollapsingLayouter : public Layouter {
public:
								CollapsingLayouter(int32 elementCount,
									float spacing);
	virtual						~CollapsingLayouter();

	virtual	void				AddConstraints(int32 element, int32 length,
									float min, float max, float preferred);
	virtual	void				SetWeight(int32 element, float weight);

	virtual	float				MinSize();
	virtual	float				MaxSize();
	virtual	float				PreferredSize();

	virtual	std::unique_ptr<LayoutInfo> CreateLayoutInfo();

	virtual	void				Layout(LayoutInfo& layoutInfo, float size);

	virtual	std::unique_ptr<Layouter> CloneLayouter();

private:
	class	ProxyLayoutInfo;
	struct	Constraint;
	struct	ElementInfo;

			void				_ValidateLayouter();
			Layouter*			_CreateLayouter();
			void				_DoCollapse();
			void				_AddConstraints();
			void				_AddConstraints(int32 position,
									const Constraint* c);
			void				_SetWeights();

			int32				fElementCount;
			std::vector<ElementInfo> fElements;
			int32				fValidElementCount;
			bool				fHaveMultiElementConstraints;
			float				fSpacing;
			std::unique_ptr<Layouter> fLayouter;
};


} // namespace ngui::layout
