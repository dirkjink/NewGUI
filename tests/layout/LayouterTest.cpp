/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "layout/CollapsingLayouter.h"
#include "layout/ComplexLayouter.h"
#include "layout/OneElementLayouter.h"
#include "layout/SimpleLayouter.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/LayoutUtils.h>


namespace {


using namespace ngui;
using namespace ngui::layout;


// Location and size of each element after Layout(), as pairs.
struct Placed {
			float				location;
			float				size;

			bool				operator==(const Placed& other) const = default;
};


void
PrintTo(const Placed& placed, std::ostream* out)
{
	*out << "{" << placed.location << ", " << placed.size << "}";
}


std::vector<Placed>
LayOut(Layouter& layouter, float size, int32 count)
{
	const std::unique_ptr<LayoutInfo> info = layouter.CreateLayoutInfo();
	layouter.Layout(*info, size);
	std::vector<Placed> placed;
	for (int32 i = 0; i < count; i++)
		placed.push_back({info->ElementLocation(i), info->ElementSize(i)});
	return placed;
}


TEST(LayoutUtilsTest, SizesArePixelCounts)
{
	// What is tested: AddDistances(), SubtractDistances(), AddSizesInt32(),
	// SubtractSizesInt32() and FixSizeConstraints().
	// Success criteria: 10 + 20 is 30 (Haiku: 10 + 20 + 1); sums reaching
	// kSizeUnlimited stay there; a difference is never below 0 (Haiku: -1);
	// FixSizeConstraints() raises max to min and clamps preferred.
	// Why it matters: The whole layout engine counts sizes in pixels, as
	// gfx::Rect does (decision for module G10); the helpers are where Haiku's
	// convention would leak in.
	//
	// When / Then
	EXPECT_EQ(BLayoutUtils::AddDistances(10, 20), 30);
	EXPECT_EQ(BLayoutUtils::AddDistances(10, 20, 5), 35);
	EXPECT_EQ(BLayoutUtils::AddDistances(kSizeUnlimited, 5), kSizeUnlimited);
	EXPECT_EQ(BLayoutUtils::SubtractDistances(30, 10), 20);
	EXPECT_EQ(BLayoutUtils::SubtractDistances(5, 10), 0);
	EXPECT_EQ(BLayoutUtils::AddSizesInt32(10, 20, 5), 35);
	EXPECT_EQ(BLayoutUtils::AddSizesInt32((int32)kSizeUnlimited - 1, 5),
		(int32)kSizeUnlimited);
	EXPECT_EQ(BLayoutUtils::SubtractSizesInt32(5, 10), 0);
	float min = 50;
	float max = 20;
	float preferred = 10;
	BLayoutUtils::FixSizeConstraints(min, max, preferred);
	EXPECT_EQ(max, 50);
	EXPECT_EQ(preferred, 50);
}


TEST(LayouterTest, SimpleLayouterDistributesWithinMinAndMax)
{
	// What is tested: A SimpleLayouter with three elements and a spacing of
	// 5: element 0 between 10 and 20 pixels, element 1 at least 30,
	// element 2 at most 10; laid out at 110 and below its minimum at 20.
	// Success criteria: Minimum 10 + 30 + 0 + 2 · 5 = 50, no maximum,
	// preferred 50. At 110 Haiku's rounds give 33/33/34 of the 100 free
	// pixels, element 0 is cut to 20 and element 2 to 10, and the 37 left
	// go to element 1: sizes 20, 70, 10 at 0, 25, 100. At 20 the layouter
	// uses its minimum: 10, 30, 0.
	// Why it matters: Group layouts (G10c) lay out their items this way;
	// the numbers are Haiku's algorithm in pixel counts.
	//
	// Given
	SimpleLayouter layouter(3, 5);
	layouter.AddConstraints(0, 1, 10, 20, 10);
	layouter.AddConstraints(1, 1, 30, kSizeUnlimited, 30);
	layouter.AddConstraints(2, 1, 0, 10, 0);

	// When / Then
	EXPECT_EQ(layouter.MinSize(), 50);
	EXPECT_EQ(layouter.MaxSize(), kSizeUnlimited);
	EXPECT_EQ(layouter.PreferredSize(), 50);
	EXPECT_EQ(LayOut(layouter, 110, 3), (std::vector<Placed>{{0, 20}, {25, 70},
		{100, 10}}));
	EXPECT_EQ(LayOut(layouter, 20, 3), (std::vector<Placed>{{0, 10}, {15, 30},
		{50, 0}}));
}


TEST(LayouterTest, SimpleLayouterFollowsWeightsAndMaximum)
{
	// What is tested: Two unlimited elements with weights 1 and 3 laid out
	// at 100; two elements limited to 20 and 30 laid out at 100.
	// Success criteria: The weights split 100 into 25 and 75. Beyond the
	// maximum of 50 Haiku gives the extra 50 equally (not by weight): 45 and
	// 55.
	// Why it matters: Weights are how applications give space to the views
	// that should grow (BGroupLayout::SetItemWeight()).
	//
	// Given
	SimpleLayouter weighted(2, 0);
	weighted.SetWeight(0, 1);
	weighted.SetWeight(1, 3);
	SimpleLayouter limited(2, 0);
	limited.AddConstraints(0, 1, 0, 20, 0);
	limited.AddConstraints(1, 1, 0, 30, 0);

	// When / Then
	EXPECT_EQ(LayOut(weighted, 100, 2), (std::vector<Placed>{{0, 25}, {25, 75}}));
	EXPECT_EQ(limited.MaxSize(), 50);
	EXPECT_EQ(LayOut(limited, 100, 2), (std::vector<Placed>{{0, 45}, {45, 55}}));
}


TEST(LayouterTest, ComplexLayouterSolvesSpanningConstraints)
{
	// What is tested: A ComplexLayouter with three elements: elements 0 and
	// 1 together at least 100 pixels, element 2 at least 10; laid out at
	// 110, and with spacing 4 at 118.
	// Success criteria: Minimum 110. The even split (36, 37, 37) breaks the
	// spanning constraint, so the optimizer finds the closest solution:
	// 49.5 and 50.5 for the first two, floored on the sums to 49 and 51,
	// and 10. With spacing the same sizes start at 0, 53 and 108.
	// Why it matters: Grid cells spanning several columns (G10c) are such
	// constraints; the active set method of LayoutOptimizer solves them.
	//
	// Given
	ComplexLayouter layouter(3, 0);
	layouter.AddConstraints(0, 2, 100, kSizeUnlimited, 100);
	layouter.AddConstraints(2, 1, 10, kSizeUnlimited, 10);
	ComplexLayouter spaced(3, 4);
	spaced.AddConstraints(0, 2, 104, kSizeUnlimited, 104);
	spaced.AddConstraints(2, 1, 10, kSizeUnlimited, 10);

	// When / Then
	EXPECT_EQ(layouter.MinSize(), 110);
	EXPECT_EQ(layouter.MaxSize(), kSizeUnlimited);
	EXPECT_EQ(LayOut(layouter, 110, 3), (std::vector<Placed>{{0, 49}, {49, 51},
		{100, 10}}));
	EXPECT_EQ(spaced.MinSize(), 118);
	EXPECT_EQ(LayOut(spaced, 118, 3), (std::vector<Placed>{{0, 49}, {53, 51},
		{108, 10}}));
}


TEST(LayouterTest, ComplexLayouterLetsMinimumWinOverMaximum)
{
	// What is tested: Element 0 at least 50, elements 0 and 1 together at
	// most 30, element 1 at most 40; laid out at 200. And a clone of it.
	// Success criteria: The span's maximum of 30 conflicts with element 0's
	// minimum and is relaxed to 50, so minimum and maximum are both 50; at
	// 200 the elements get 50 and 0. The clone gives the same.
	// Why it matters: Haiku enforces minimums over maximums when they
	// conflict, so a view never gets less than its minimum size.
	//
	// Given
	ComplexLayouter layouter(2, 0);
	layouter.AddConstraints(0, 1, 50, kSizeUnlimited, 50);
	layouter.AddConstraints(0, 2, 0, 30, 0);
	layouter.AddConstraints(1, 1, 0, 40, 0);

	// When
	const std::unique_ptr<Layouter> clone = layouter.CloneLayouter();

	// Then
	EXPECT_EQ(layouter.MinSize(), 50);
	EXPECT_EQ(layouter.MaxSize(), 50);
	EXPECT_EQ(LayOut(layouter, 200, 2), (std::vector<Placed>{{0, 50}, {50, 0}}));
	ASSERT_NE(clone, nullptr);
	EXPECT_EQ(LayOut(*clone, 200, 2), (std::vector<Placed>{{0, 50}, {50, 0}}));
}


TEST(LayouterTest, ComplexLayouterCorrectsHaikusBounds)
{
	// What is tested: Three constraint sets that Haiku's ComplexLayouter
	// gets wrong (found by fuzzing, see ComplexLayouter.cpp):
	// (a) four elements, spacing 6: 0..1 between 220 and 268, 1..3 exactly
	//     105, element 1 at least 20, element 3 at least 53; element 2 is
	//     covered by the span only;
	// (b) three elements: 0..1 at most 10, element 1 at least 50, then
	//     element 2 at least 30 behind the relaxed span;
	// (c) five elements, spacing 2: element 0 between 20 and 91, 1..3 at
	//     most 30, 1..4 at least 197, 3..4 at most 0; laid out at 250.
	// Success criteria: (a) minimum 285 = 174 + 93 + 3 · 6 (Haiku: 111) and
	// at 112 the sizes 174, 40, 0, 53. (b) The span is relaxed to 50 and
	// element 2 keeps its own minimum: minimum 80 (Haiku kept bounds raised
	// by the relaxed span). (c) Without spacing the spans are 1..3 <= 26,
	// 1..4 >= 191 and 3..4 <= -2, raised to 0; the later span 3..4 is
	// relaxed to 191 - 26 = 165. Minimum 20 + 191 + 8 = 219, maximum
	// 91 + 191 + 8 = 290. At 250 (242 without spacing, desired about 48
	// each) 1..4 can only be 191, which forces element 3 to 0 and element 4
	// to 165; element 0 gets the remaining 51, elements 1 and 2 split 26 as
	// close to 48 and 49 as possible, 12.5 and 13.5, floored on the sums to
	// 12 and 14 (Haiku: -71 for element 3).
	// Why it matters: Grid cells spanning columns are such constraints; a
	// wrong minimum lets windows shrink below their content, a negative
	// size puts views on top of each other.
	//
	// Given
	ComplexLayouter spans(4, 6);
	spans.AddConstraints(0, 2, 220, 268, 220);
	spans.AddConstraints(1, 3, 105, 105, 105);
	spans.AddConstraints(1, 1, 20, 235, 20);
	spans.AddConstraints(3, 1, 53, 273, 53);
	ComplexLayouter relaxed(3, 0);
	relaxed.AddConstraints(0, 2, 0, 10, 0);
	relaxed.AddConstraints(1, 1, 50, kSizeUnlimited, 50);
	relaxed.AddConstraints(2, 1, 30, kSizeUnlimited, 30);
	ComplexLayouter conflicting(5, 2);
	conflicting.AddConstraints(0, 1, 20, 91, 20);
	conflicting.AddConstraints(1, 3, 0, 30, 0);
	conflicting.AddConstraints(1, 4, 197, 271, 197);
	conflicting.AddConstraints(3, 2, 0, 0, 0);

	// When
	const std::vector<Placed> spansPlaced = LayOut(spans, 112, 4);
	const std::vector<Placed> conflictingPlaced = LayOut(conflicting, 250, 5);

	// Then
	EXPECT_EQ(spans.MinSize(), 285);
	EXPECT_EQ(spansPlaced, (std::vector<Placed>{{0, 174}, {180, 40}, {226, 0},
		{232, 53}}));
	EXPECT_EQ(relaxed.MinSize(), 80);
	EXPECT_EQ(relaxed.MaxSize(), kSizeUnlimited);
	EXPECT_EQ(conflicting.MinSize(), 219);
	EXPECT_EQ(conflicting.MaxSize(), 290);
	EXPECT_EQ(conflictingPlaced, (std::vector<Placed>{{0, 51}, {53, 12}, {67, 14},
		{83, 0}, {85, 165}}));
}


TEST(LayouterTest, CollapsingLayouterDropsEmptyElements)
{
	// What is tested: A CollapsingLayouter with four elements and spacing
	// 10, constraints only on element 0 (exactly 20) and element 2 (at least
	// 30) and an unset one on element 3; laid out at 100. A second one with
	// a single constrained element (30 to 50, preferred 40) laid out at 20.
	// Success criteria: Elements 1 and 3 collapse: no spacing for them,
	// size 0 at 0. Minimum 20 + 10 + 30 = 60; at 100 element 0 gets 20 at
	// 0, element 2 the remaining 70 at 30. The single element reports 30,
	// 50 and 40 and is laid out at its minimum 30.
	// Why it matters: Grid rows and columns without items take no space
	// (Haiku's BGridLayout uses this layouter).
	//
	// Given
	CollapsingLayouter layouter(4, 10);
	layouter.AddConstraints(0, 1, 20, 20, 20);
	layouter.AddConstraints(2, 1, 30, kSizeUnlimited, 30);
	layouter.AddConstraints(3, 1, kSizeUnset, kSizeUnset, kSizeUnset);
	for (int32 i = 0; i < 4; i++)
		layouter.SetWeight(i, 1);
	CollapsingLayouter single(3, 10);
	single.AddConstraints(1, 1, 30, 50, 40);

	// When / Then
	EXPECT_EQ(layouter.MinSize(), 60);
	EXPECT_EQ(LayOut(layouter, 100, 4), (std::vector<Placed>{{0, 20}, {0, 0},
		{30, 70}, {0, 0}}));
	EXPECT_EQ(single.MinSize(), 30);
	EXPECT_EQ(single.MaxSize(), 50);
	EXPECT_EQ(single.PreferredSize(), 40);
	EXPECT_EQ(LayOut(single, 20, 3), (std::vector<Placed>{{0, 0}, {0, 30}, {0, 0}}));
}


TEST(LayouterTest, CollapsingLayouterUsesTheComplexOneForSpans)
{
	// What is tested: A CollapsingLayouter whose constraints span two
	// elements, laid out at 110, then a constraint added after the first
	// layout, and a clone.
	// Success criteria: Same result as the ComplexLayouter for the same
	// constraints (49, 51, 10); a new constraint (element 2 at least 30)
	// makes the minimum 130; the clone lays out like the original.
	// Why it matters: The wrapper must choose the layouter that can handle
	// the constraints and keep them across changes and copies.
	//
	// Given
	CollapsingLayouter layouter(3, 0);
	layouter.AddConstraints(0, 2, 100, kSizeUnlimited, 100);
	layouter.AddConstraints(2, 1, 10, kSizeUnlimited, 10);
	for (int32 i = 0; i < 3; i++)
		layouter.SetWeight(i, 1);

	// When
	const std::vector<Placed> first = LayOut(layouter, 110, 3);
	layouter.AddConstraints(2, 1, 30, kSizeUnlimited, 30);
	const std::unique_ptr<Layouter> clone = layouter.CloneLayouter();

	// Then
	EXPECT_EQ(first, (std::vector<Placed>{{0, 49}, {49, 51}, {100, 10}}));
	EXPECT_EQ(layouter.MinSize(), 130);
	EXPECT_EQ(LayOut(*clone, 130, 3), LayOut(layouter, 130, 3));
}


TEST(LayouterTest, OneElementLayouterAndEmptyLayouters)
{
	// What is tested: A OneElementLayouter with two constraint sets, and
	// Simple, Complex and Collapsing layouters without elements.
	// Success criteria: The one element keeps the stricter values (min 20,
	// max 40, preferred 30) and is never laid out below its minimum. Empty
	// layouters have minimum and preferred 0 and no maximum.
	// Why it matters: Layouts with one item or none are common (a window
	// with one view, an empty group) and must not divide by zero.
	//
	// Given
	OneElementLayouter one;
	one.AddConstraints(0, 1, 10, 40, 30);
	one.AddConstraints(0, 1, 20, 50, 25);
	SimpleLayouter simple(0, 5);
	ComplexLayouter complex(0, 5);
	CollapsingLayouter collapsing(0, 5);

	// When / Then
	EXPECT_EQ(one.MinSize(), 20);
	EXPECT_EQ(one.MaxSize(), 40);
	EXPECT_EQ(one.PreferredSize(), 30);
	EXPECT_EQ(LayOut(one, 5, 1), (std::vector<Placed>{{0, 20}}));
	EXPECT_EQ(LayOut(*one.CloneLayouter(), 35, 1), (std::vector<Placed>{{0, 35}}));
	for (Layouter* layouter : std::vector<Layouter*>{&simple, &complex, &collapsing}) {
		EXPECT_EQ(layouter->MinSize(), 0);
		EXPECT_EQ(layouter->PreferredSize(), 0);
		EXPECT_EQ(layouter->MaxSize(), kSizeUnlimited);
		EXPECT_TRUE(LayOut(*layouter, 100, 0).empty());
	}
}


} // namespace
