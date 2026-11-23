/*
 * Copyright 2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/ComplexLayouter.h (commit
 * b1d497bb69).
 *
 * Layouter implementation that can handle complex constraints.
 */
#pragma once


#include <memory>
#include <vector>

#include "Layouter.h"


namespace ngui::layout {


class LayoutOptimizer;


class ComplexLayouter : public Layouter {
public:
								ComplexLayouter(int32 elementCount,
									float spacing);
	virtual						~ComplexLayouter();

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
			struct Constraint;

			// Upper bounds of differences of the sums c[0..n]: entry
			// (a, b) bounds c[b] - c[a] (kInfinite: none).
			using Distances = std::vector<int64>;

			bool				_Layout(int32 size, const Distances& distances,
									int32* sizes);
			bool				_AddOptimizerConstraints();
			bool				_SatisfiesConstraints(int32* sizes) const;
			bool				_SatisfiesConstraintsSums(int32* sums) const;

			void				_ValidateLayout();
			void				_AddBound(Distances& distances, int32 from,
									int32 to, int64 bound) const;
			int64				_Distance(const Distances& distances,
									int32 from, int32 to) const;

			Constraint*			_NewConstraint(int32 start, int32 end,
									int32 min, int32 max);

private:
			int32				fElementCount;
			int32				fSpacing;
			std::vector<Constraint*> fConstraints;
									// per end element, a list by start
			std::vector<std::unique_ptr<Constraint>> fConstraintStorage;
			std::vector<float>	fWeights;
			Distances			fDistances;
			std::unique_ptr<LayoutOptimizer> fOptimizer;
			float				fMin;
			float				fMax;
			int32				fUnlimited;
			bool				fMinMaxValid;
			bool				fOptimizerConstraintsAdded;
};


} // namespace ngui::layout
