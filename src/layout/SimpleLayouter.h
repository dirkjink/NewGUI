/*
 * Copyright 2006-2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/SimpleLayouter.h (commit
 * b1d497bb69).
 *
 * Layouter implementation that can handle simple layout constraints
 * (restricting one element) only.
 */
#pragma once


#include <memory>
#include <vector>

#include "Layouter.h"


namespace ngui::layout {


class SimpleLayouter : public Layouter {
public:
								SimpleLayouter(int32 elementCount,
									float spacing);
	virtual						~SimpleLayouter();

	virtual	void				AddConstraints(int32 element, int32 length,
									float min, float max, float preferred);
	virtual	void				SetWeight(int32 element, float weight);

	virtual	float				MinSize();
	virtual	float				MaxSize();
	virtual	float				PreferredSize();

	virtual	std::unique_ptr<LayoutInfo> CreateLayoutInfo();

	virtual	void				Layout(LayoutInfo& layoutInfo, float size);

	virtual	std::unique_ptr<Layouter> CloneLayouter();

	static	void				DistributeSize(int32 size, const float weights[],
									int32 sizes[], int32 count);

private:
			class ElementLayoutInfo;
			class ElementInfo;
			class MyLayoutInfo;

	static	int64				_CalculateSumWeight(
									std::vector<ElementInfo*>& elementInfos);

			void				_ValidateMinMax();
			void				_LayoutMax();
			void				_LayoutStandard();

private:
			int32				fElementCount;
			int32				fSpacing;
			std::vector<ElementInfo> fElements;
			int32				fMin;
			int32				fMax;
			int32				fPreferred;
			bool				fMinMaxValid;
			MyLayoutInfo*		fLayoutInfo;
};


} // namespace ngui::layout
