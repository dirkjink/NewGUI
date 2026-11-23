/*
 * Copyright 2006, Haiku Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/OneElementLayouter.h
 * (commit b1d497bb69).
 */
#pragma once


#include <memory>

#include "Layouter.h"


namespace ngui::layout {


class OneElementLayouter : public Layouter {
public:
								OneElementLayouter();
	virtual						~OneElementLayouter();

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
			class MyLayoutInfo;

			float				fMin;
			float				fMax;
			float				fPreferred;
};


} // namespace ngui::layout
