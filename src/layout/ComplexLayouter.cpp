/*
 * Copyright 2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/ComplexLayouter.cpp (commit
 * b1d497bb69). NewGUI: sizes are pixel counts, so the "+ 1" and "- 1" at
 * the interface are gone (Haiku counts pixels internally as well); arrays
 * became std::vector, the constraints are owned by fConstraintStorage; the
 * tracing is gone; PreferredSize() validates first, as MinSize() does.
 *
 * Corrections, all found by fuzzing (tests/fuzz/LayouterFuzz.cpp) and
 * explained where they are:
 * - _ValidateLayout() is new. Haiku computes the bounds of the sums
 *   c[i] = x_1 + ... + x_i by propagating them back and forth and relaxes
 *   conflicting maximums on the way; that lost bounds (an element covered
 *   only by spans lost the minimums before it), kept bounds of relaxed
 *   constraints, and could end inconsistent, so that layouts had elements
 *   of negative size. The constraints are differences of sums, so their
 *   exact bounds are shortest paths; NewGUI computes them incrementally
 *   and relaxes conflicting maximums as Haiku means to: in Haiku's order,
 *   each as little as needed. Where Haiku's propagation is right, the
 *   bounds are the same.
 * - Every element starts with the constraint 0 <= x_i <= unlimited, which
 *   Haiku has only implicitly.
 * - Constraint() raises its maximum to its minimum, and IsSatisfied()
 *   checks the right elements against the relaxed maximum.
 * - _AddOptimizerConstraints() gives the optimizer all constraints.
 * - _Layout() rounds sums just below an integer up for the size as well.
 * - CloneLayouter() copies the relaxed maximums and the bounds.
 */


#include "ComplexLayouter.h"

#include <algorithm>
#include <cmath>

#include <ngui/LayoutUtils.h>

#include "LayoutOptimizer.h"
#include "SimpleLayouter.h"


namespace ngui::layout {


// MyLayoutInfo
class ComplexLayouter::MyLayoutInfo : public LayoutInfo {
public:
	MyLayoutInfo(int32 elementCount, int32 spacing)
		: fCount(elementCount),
		  fSpacing(spacing),
		  // We also store the location of the virtual elementCountth element.
		  // Thus fLocation[i + 1] - fLocation[i] is the size of the ith element
		  // (not considering spacing).
		  fLocations(static_cast<std::size_t>(elementCount) + 1)
	{
	}

	void InitFromSizes(const int32* sizes)
	{
		fLocations[0] = 0;
		for (int32 i = 0; i < fCount; i++)
			fLocations[i + 1] = fLocations[i] + sizes[i] + fSpacing;
	}

	virtual float ElementLocation(int32 element)
	{
		if (element < 0 || element >= fCount)
			return 0;

		return (float)fLocations[element];
	}

	virtual float ElementSize(int32 element)
	{
		if (element < 0 || element >= fCount)
			return 0;

		return (float)(fLocations[element + 1] - fLocations[element]
			- fSpacing);
	}

	virtual float ElementRangeSize(int32 position, int32 length)
	{
		if (position < 0 || length < 0 || position + length > fCount)
			return 0;

		return (float)(fLocations[position + length] - fLocations[position]
			- fSpacing);
	}

public:
	int32				fCount;
	int32				fSpacing;
	std::vector<int32>	fLocations;
};


// Constraint
struct ComplexLayouter::Constraint {
	// NewGUI: the parameters are named apart from the members.
	Constraint(int32 startElement, int32 endElement, int32 minSize,
			int32 maxSize)
		: start(startElement),
		  end(endElement),
		  min(minSize),
		  // NewGUI: the member is raised to min, as Haiku meant to; its
		  // "if (min > max) max = min;" changed the parameter of the same
		  // name, so a span whose maximum is below its spacing could never
		  // be satisfied.
		  max(std::max(minSize, maxSize)),
		  effectiveMax(std::max(minSize, maxSize)),
		  next(nullptr)
	{
	}

	void Restrict(int32 newMin, int32 newMax)
	{
		if (newMin > min)
			min = newMin;
		if (newMax < max)
			max = newMax;
		if (min > max)
			max = min;
		effectiveMax = max;
	}

	bool IsSatisfied(int32* sumValues) const
	{
		// sumValues[k] is the sum of the first k elements, so elements
		// start to end sum up to sumValues[end + 1] - sumValues[start].
		// Haiku reads sumValues[end] - sumValues[start - 1]: one element
		// too early, and before the array for start = 0. The maximum is
		// the relaxed one, which the layout is computed with.
		int32 value = sumValues[end + 1] - sumValues[start];
		return (value >= min && value <= effectiveMax);
	}

	int32		start;
	int32		end;
	int32		min;
	int32		max;
	int32		effectiveMax;
	Constraint*	next;
};


namespace {


// No bound: larger than any sum of bounds, without overflowing a sum of two.
constexpr int64 kInfinite = (int64)1 << 50;


} // namespace


// #pragma mark - ComplexLayouter


// constructor
ComplexLayouter::ComplexLayouter(int32 elementCount, float spacing)
	: fElementCount(elementCount),
	  fSpacing((int32)spacing),
	  fConstraints(static_cast<std::size_t>(elementCount), nullptr),
	  fWeights(static_cast<std::size_t>(elementCount), 1.0f),
	  fOptimizer(std::make_unique<LayoutOptimizer>(elementCount)),
	  fMin(0),
	  fMax(0),
	  fUnlimited((int32)kSizeUnlimited / (elementCount == 0 ? 1 : elementCount)),
	  fMinMaxValid(false),
	  fOptimizerConstraintsAdded(false)
{
	// NewGUI: every element starts with the constraint that it is at least
	// 0 and at most unlimited, see the comment at the top of this file.
	for (int32 i = 0; i < fElementCount; i++)
		fConstraints[i] = _NewConstraint(i, i, 0, fUnlimited);
}


// destructor
ComplexLayouter::~ComplexLayouter()
{
}


// _NewConstraint
ComplexLayouter::Constraint*
ComplexLayouter::_NewConstraint(int32 start, int32 end, int32 min, int32 max)
{
	fConstraintStorage.push_back(std::make_unique<Constraint>(start, end, min,
		max));
	return fConstraintStorage.back().get();
}


// AddConstraints
void
ComplexLayouter::AddConstraints(int32 element, int32 length,
	float _min, float _max, float /*_preferred*/)
{
	if (element < 0 || length <= 0 || element + length > fElementCount)
		return;

	int32 spacing = fSpacing * (length - 1);
	int32 min = (int32)_min - spacing;
	int32 max = (int32)_max - spacing;

	if (min < 0)
		min = 0;
	if (max > fUnlimited)
		max = fUnlimited;

	int32 end = element + length - 1;
	Constraint** slot = &fConstraints[end];
	while (*slot != nullptr && (*slot)->start > element)
		slot = &(*slot)->next;

	if (*slot != nullptr && (*slot)->start == element) {
		// previous constraint exists -- use stricter values
		(*slot)->Restrict(min, max);
	} else {
		// no previous constraint -- create new one
		Constraint* constraint = _NewConstraint(element, end, min, max);
		constraint->next = *slot;
		*slot = constraint;
	}

	fMinMaxValid = false;
}


// SetWeight
void
ComplexLayouter::SetWeight(int32 element, float weight)
{
	if (element < 0 || element >= fElementCount)
		return;

	fWeights[element] = std::max(weight, 0.0f);
}


// MinSize
float
ComplexLayouter::MinSize()
{
	_ValidateLayout();
	return fMin;
}


// MaxSize
float
ComplexLayouter::MaxSize()
{
	_ValidateLayout();
	return fMax;
}


// PreferredSize
float
ComplexLayouter::PreferredSize()
{
	_ValidateLayout();
	return fMin;
}


// CreateLayoutInfo
std::unique_ptr<LayoutInfo>
ComplexLayouter::CreateLayoutInfo()
{
	return std::make_unique<MyLayoutInfo>(fElementCount, fSpacing);
}


// Layout
void
ComplexLayouter::Layout(LayoutInfo& _layoutInfo, float _size)
{
	if (fElementCount == 0)
		return;

	_ValidateLayout();

	MyLayoutInfo* layoutInfo = static_cast<MyLayoutInfo*>(&_layoutInfo);

	const int32 nodes = fElementCount + 1;
	int32 min = (int32)-fDistances[(std::size_t)fElementCount * nodes];
	int32 max = (int32)fDistances[fElementCount];

	int32 size = (int32)_size - (fElementCount - 1) * fSpacing;
	if (size < min)
		size = min;
	if (size > max)
		size = max;

	// fix the total: c[n] = size
	Distances distances = fDistances;
	_AddBound(distances, 0, fElementCount, size);
	_AddBound(distances, fElementCount, 0, -size);

	std::vector<int32> sizes(static_cast<std::size_t>(fElementCount));
	if (!_Layout(size, distances, sizes.data())) {
	}

	layoutInfo->InitFromSizes(sizes.data());
}


// CloneLayouter
std::unique_ptr<Layouter>
ComplexLayouter::CloneLayouter()
{
	auto layouter = std::make_unique<ComplexLayouter>(fElementCount,
		(float)fSpacing);
	if (!layouter->fOptimizer->AddConstraintsFrom(fOptimizer.get()))
		return nullptr;

	// clone the constraints
	for (int32 i = 0; i < fElementCount; i++) {
		Constraint* constraint = fConstraints[i];
		layouter->fConstraints[i] = nullptr;
		Constraint** end = &layouter->fConstraints[i];
		while (constraint) {
			*end = layouter->_NewConstraint(constraint->start, constraint->end,
				constraint->min, constraint->max);
			// NewGUI: the relaxed maximum as well, since the clone takes
			// over the bounds and fMinMaxValid, which were computed with it.
			(*end)->effectiveMax = constraint->effectiveMax;

			end = &(*end)->next;
			constraint = constraint->next;
		}
	}

	// copy the other stuff
	layouter->fWeights = fWeights;
	layouter->fDistances = fDistances;
	layouter->fMin = fMin;
	layouter->fMax = fMax;
	layouter->fMinMaxValid = fMinMaxValid;

	return layouter;
}


// _Layout
bool
ComplexLayouter::_Layout(int32 size, const Distances& distances, int32* sizes)
{
	// prepare the desired solution
	SimpleLayouter::DistributeSize(size, fWeights.data(), sizes, fElementCount);
	if (_SatisfiesConstraints(sizes)) {
		// The desired solution already satisfies all constraints.
		return true;
	}

	std::vector<double> realSizes(static_cast<std::size_t>(fElementCount));
	for (int32 i = 0; i < fElementCount; i++)
		realSizes[i] = sizes[i];

	if (!_AddOptimizerConstraints())
		return false;


	// prepare a feasible solution (the minimum)
	// NewGUI: the smallest sums -d(i, 0) of the constraints with the total
	// fixed; they satisfy every constraint (triangle inequality).
	std::vector<double> values(static_cast<std::size_t>(fElementCount));
	for (int32 i = 0; i < fElementCount; i++) {
		values[i] = (double)(_Distance(distances, i, 0)
			- _Distance(distances, i + 1, 0));
	}

	// solve
	if (!fOptimizer->Solve(realSizes.data(), size, values.data()))
		return false;

	// compute integer solution
	// The basic strategy is to floor() the sums. This guarantees that the
	// difference between two rounded sums remains in the range of floor()
	// and ceil() of their real value difference. Since the constraints have
	// integer values, the integer solution will therefore satisfy all
	// constraints the real solution satisfied.
	double realSum = 0;
	double previousSum = 0;
	for (int32 i = 0; i < fElementCount; i++) {
		realSum += values[i];
		double roundedRealSum = std::floor(realSum);
		if (fuzzy_equals(realSum, roundedRealSum + 1)) {
			// NewGUI: the size is computed with the rounded up sum as well;
			// Haiku rounded up realSum only, so a sum just below an integer
			// gave the element one pixel less and a later one -1.
			roundedRealSum += 1;
			realSum = roundedRealSum;
		}
		sizes[i] = int32(roundedRealSum - previousSum);
		previousSum = roundedRealSum;
	}

	return _SatisfiesConstraints(sizes);
}


// _AddOptimizerConstraints
bool
ComplexLayouter::_AddOptimizerConstraints()
{
	if (fOptimizerConstraintsAdded)
		return true;

	fOptimizer->RemoveAllConstraints();

	// add constraints
	for (int32 i = 0; i < fElementCount; i++) {
		Constraint* constraint = fConstraints[i];
		while (constraint != nullptr) {
			// NewGUI: all constraints go to the optimizer, except maximums of
			// "unlimited", which no layout reaches. Haiku left out those it
			// considered redundant because the bounds of the sums were
			// already stricter than what the constraint gives alone. But the
			// bounds come from the constraints and the optimizer does not
			// know them, so it broke constraints it never got (elements of
			// -10.5 pixels, or -7 where exactly 48 were asked for).
			bool minRedundant = false;
			bool maxRedundant = constraint->effectiveMax >= fUnlimited;

			if (!minRedundant || !maxRedundant) {
				bool success = true;
				if (constraint->min == constraint->effectiveMax) {
					// min and max equal -- add an equality constraint
					success = fOptimizer->AddConstraint(constraint->start - 1,
						constraint->end, constraint->min, true);
				} else {
					// min and max not equal -- add them individually,
					// unless redundant
					if (!minRedundant) {
						success |= fOptimizer->AddConstraint(
							constraint->start - 1, constraint->end,
							constraint->min, false);
					}
					if (!maxRedundant) {
						success |= fOptimizer->AddConstraint(constraint->end,
							constraint->start - 1,
							-constraint->effectiveMax, false);
					}
				}

				if (!success)
					return false;
			}

			constraint = constraint->next;
		}
	}

	fOptimizerConstraintsAdded = true;
	return true;
}


// _SatisfiesConstraints
bool
ComplexLayouter::_SatisfiesConstraints(int32* sizes) const
{
	std::vector<int32> sumValues(static_cast<std::size_t>(fElementCount) + 1);
	sumValues[0] = 0;
	for (int32 i = 0; i < fElementCount; i++)
		sumValues[i + 1] = sumValues[i] + sizes[i];

	return _SatisfiesConstraintsSums(sumValues.data());
}


// _SatisfiesConstraintsSums
bool
ComplexLayouter::_SatisfiesConstraintsSums(int32* sumValues) const
{
	for (int32 i = 0; i < fElementCount; i++) {
		Constraint* constraint = fConstraints[i];
		while (constraint) {
			if (!constraint->IsSatisfied(sumValues))
				return false;

			constraint = constraint->next;
		}
	}

	return true;
}


// _ValidateLayout
void
ComplexLayouter::_ValidateLayout()
{
	// NewGUI: new, see the top of this file. As in Haiku, the constraints
	// on the elements x_1, ..., x_n become constraints on the sums
	//   c[0] = 0, c[i] = x_1 + ... + x_i:
	//   c[end + 1] - c[start] >= min and <= max.
	// These are difference constraints; the tightest bounds they imply are
	// shortest paths in the graph with an edge a -> b of weight w for each
	// c[b] - c[a] <= w (a minimum m is the edge b -> a of weight -m). The
	// matrix fDistances holds them for all pairs and is updated with each
	// constraint added. All minimums go first: they only go backwards and
	// cannot contradict each other. Then each maximum is relaxed to the
	// smallest difference the constraints so far allow, if it is below it,
	// and added; the system stays consistent. The bounds of the sums are
	// minc[i] = -d(i, 0) and maxc[i] = d(0, i).

	if (fMinMaxValid)
		return;

	const int32 nodes = fElementCount + 1;
	fDistances.assign((std::size_t)nodes * nodes, kInfinite);
	for (int32 i = 0; i < nodes; i++)
		fDistances[(std::size_t)i * nodes + i] = 0;

	// apply the min constraints
	for (int32 i = 0; i < fElementCount; i++) {
		for (Constraint* constraint = fConstraints[i]; constraint != nullptr;
				constraint = constraint->next) {
			_AddBound(fDistances, constraint->end + 1, constraint->start,
				-(int64)constraint->min);
		}
	}

	// apply the max constraints, relaxing conflicting ones
	for (int32 i = 0; i < fElementCount; i++) {
		for (Constraint* constraint = fConstraints[i]; constraint != nullptr;
				constraint = constraint->next) {
			int64 smallest = -_Distance(fDistances, constraint->end + 1,
				constraint->start);
			int64 max = std::max((int64)constraint->max, smallest);
			constraint->effectiveMax = (int32)max;
			_AddBound(fDistances, constraint->start, constraint->end + 1, max);
		}
	}

	if (fElementCount == 0) {
		fMin = 0;
		fMax = kSizeUnlimited;
	} else {
		int32 spacing = (fElementCount - 1) * fSpacing;
		fMin = (float)(-_Distance(fDistances, fElementCount, 0) + spacing);
		fMax = (float)std::min(_Distance(fDistances, 0, fElementCount) + spacing,
			(int64)kSizeUnlimited);
		if (fMax >= (float)fUnlimited)
			fMax = kSizeUnlimited;
	}

	fOptimizerConstraintsAdded = false;
	fMinMaxValid = true;
}


// _AddBound
/*!	Adds c[to] - c[from] <= bound to \a distances and updates the shortest
	paths through the new edge.
*/
void
ComplexLayouter::_AddBound(Distances& distances, int32 from, int32 to,
	int64 bound) const
{
	const int32 nodes = fElementCount + 1;
	if (_Distance(distances, from, to) <= bound)
		return;

	for (int32 a = 0; a < nodes; a++) {
		const int64 toFrom = _Distance(distances, a, from);
		if (toFrom >= kInfinite)
			continue;
		for (int32 b = 0; b < nodes; b++) {
			const int64 fromTo = _Distance(distances, to, b);
			if (fromTo >= kInfinite)
				continue;
			int64& distance = distances[(std::size_t)a * nodes + b];
			distance = std::min(distance, toFrom + bound + fromTo);
		}
	}
}


// _Distance
int64
ComplexLayouter::_Distance(const Distances& distances, int32 from,
	int32 to) const
{
	return distances[(std::size_t)from * (fElementCount + 1) + to];
}


} // namespace ngui::layout
