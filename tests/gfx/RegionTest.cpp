/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Region.h>

#include <limits>
#include <utility>

#include <gtest/gtest.h>

#include "RegionInvariants.h"


namespace {


using ngui::gfx::IntRect;
using ngui::gfx::Point;
using ngui::gfx::Region;
using ngui::test::IsCanonical;


TEST(RegionTest, RectangleIsHalfOpen)
{
	// What is tested: A region made from IntRect{0, 0, 10, 10}: its frame, its
	// rectangle and which pixels it contains.
	// Success criteria: One rectangle equal to the input; pixels 0 to 9 are
	// inside, pixel 10 in either direction is outside.
	// Why it matters: Haiku's BRegion takes inclusive BRects; this region is
	// the same area written half-open (specification 4.1), the main semantic
	// change of the port.
	//
	// Given
	const Region region(IntRect{0, 0, 10, 10});

	// When / Then
	EXPECT_EQ(region.CountRects(), 1);
	EXPECT_EQ(region.Frame(), (IntRect{0, 0, 10, 10}));
	EXPECT_EQ(region.RectAt(0), (IntRect{0, 0, 10, 10}));
	EXPECT_TRUE(region.Contains(0, 0));
	EXPECT_TRUE(region.Contains(9, 9));
	EXPECT_FALSE(region.Contains(10, 0));
	EXPECT_FALSE(region.Contains(0, 10));
	EXPECT_TRUE(IsCanonical(region));
}


TEST(RegionTest, EmptyInputGivesEmptyRegion)
{
	// What is tested: Construction, Set() and Include() with empty and
	// inverted rectangles, and RectAt() out of range.
	// Success criteria: The region stays empty, its frame is {0, 0, 0, 0}, and
	// RectAt() returns an empty rectangle.
	// Why it matters: A clip with an empty rectangle must clip everything away
	// (specification 7), not leave a degenerate rectangle behind.
	//
	// Given
	Region region(IntRect{5, 5, 5, 9});

	// When
	region.Include(IntRect{3, 3, 1, 1});

	// Then
	EXPECT_TRUE(region.IsEmpty());
	EXPECT_EQ(region.Frame(), (IntRect{0, 0, 0, 0}));
	EXPECT_TRUE(region.RectAt(0).IsEmpty());
	region.Set(IntRect{0, 0, 4, 4});
	region.Set(IntRect{4, 4, 0, 0});
	EXPECT_TRUE(region.IsEmpty());
}


TEST(RegionTest, TouchingRectanglesMerge)
{
	// What is tested: Include() of two rectangles that share an edge,
	// horizontally and vertically.
	// Success criteria: Both cases give a single rectangle covering both.
	// Why it matters: With half-open rectangles, touching means the right edge
	// of one equals the left edge of the other; the banding code must see
	// them as adjacent, or every clip would fragment.
	//
	// Given
	Region horizontal(IntRect{0, 0, 10, 10});
	Region vertical(IntRect{0, 0, 10, 10});

	// When
	horizontal.Include(IntRect{10, 0, 20, 10});
	vertical.Include(IntRect{0, 10, 10, 20});

	// Then
	EXPECT_EQ(horizontal.CountRects(), 1);
	EXPECT_EQ(horizontal.Frame(), (IntRect{0, 0, 20, 10}));
	EXPECT_EQ(vertical.CountRects(), 1);
	EXPECT_EQ(vertical.Frame(), (IntRect{0, 0, 10, 20}));
}


TEST(RegionTest, ExcludeCutsAHoleInBands)
{
	// What is tested: Exclude() of a rectangle in the middle of another.
	// Success criteria: Four rectangles in three bands: the full top row, the
	// left and right parts of the middle row, the full bottom row; the hole
	// is not contained, its border pixels are.
	// Why it matters: ClipOutRect() is implemented with Exclude()
	// (specification 7); the result must be the banded form backends rely on.
	//
	// Given
	Region region(IntRect{0, 0, 30, 30});

	// When
	region.Exclude(IntRect{10, 10, 20, 20});

	// Then
	ASSERT_EQ(region.CountRects(), 4);
	EXPECT_EQ(region.RectAt(0), (IntRect{0, 0, 30, 10}));
	EXPECT_EQ(region.RectAt(1), (IntRect{0, 10, 10, 20}));
	EXPECT_EQ(region.RectAt(2), (IntRect{20, 10, 30, 20}));
	EXPECT_EQ(region.RectAt(3), (IntRect{0, 20, 30, 30}));
	EXPECT_FALSE(region.Contains(10, 10));
	EXPECT_FALSE(region.Contains(19, 19));
	EXPECT_TRUE(region.Contains(9, 10));
	EXPECT_TRUE(region.Contains(20, 19));
	EXPECT_TRUE(IsCanonical(region));
}


TEST(RegionTest, IntersectionAndSymmetricDifference)
{
	// What is tested: IntersectWith() and ExclusiveInclude() of two
	// overlapping squares, and Intersects() with a rectangle.
	// Success criteria: The intersection is the common square; the symmetric
	// difference contains both non-shared parts and not the common square.
	// Why it matters: ClipRect() is an intersection (specification 7); the
	// other operations are needed for damage tracking.
	//
	// Given
	const Region other(IntRect{5, 5, 15, 15});
	Region intersection(IntRect{0, 0, 10, 10});
	Region difference(IntRect{0, 0, 10, 10});

	// When
	intersection.IntersectWith(other);
	difference.ExclusiveInclude(other);

	// Then
	EXPECT_EQ(intersection, Region(IntRect{5, 5, 10, 10}));
	EXPECT_TRUE(difference.Contains(0, 0));
	EXPECT_TRUE(difference.Contains(14, 14));
	EXPECT_FALSE(difference.Contains(7, 7));
	EXPECT_TRUE(IsCanonical(difference));
	EXPECT_TRUE(intersection.Intersects(IntRect{9, 9, 20, 20}));
	EXPECT_FALSE(intersection.Intersects(IntRect{10, 0, 20, 20}));
	EXPECT_FALSE(intersection.Intersects(IntRect{6, 6, 6, 6}));
}


TEST(RegionTest, ContainsPointRoundsDown)
{
	// What is tested: Contains(Point) just inside and just outside the left
	// and right edges, and with NaN.
	// Success criteria: (-0.5, 0) is outside a region starting at 0, (9.99, 0)
	// is inside one ending at 10, NaN is outside.
	// Why it matters: Haiku casts to int, which rounds towards zero and puts
	// (-0.5, 0) into pixel 0; a hit test at the left edge of a view would
	// then report the wrong view.
	//
	// Given
	const Region region(IntRect{0, 0, 10, 10});

	// When / Then
	EXPECT_FALSE(region.Contains(Point{-0.5f, 0}));
	EXPECT_TRUE(region.Contains(Point{0, 0}));
	EXPECT_TRUE(region.Contains(Point{9.99f, 0}));
	EXPECT_FALSE(region.Contains(Point{10, 0}));
	EXPECT_FALSE(region.Contains(Point{std::numeric_limits<float>::quiet_NaN(), 0}));
}


TEST(RegionTest, CopyMoveAndOffset)
{
	// What is tested: Copy construction and assignment, move construction and
	// assignment, MoveFrom() and OffsetBy() on a region of several rectangles.
	// Success criteria: Copies compare equal; a moved-from region is empty;
	// OffsetBy() moves every rectangle and the frame.
	// Why it matters: The Canvas saves and restores the clip on its state
	// stack (specification 6.1), which copies and moves regions all the time.
	//
	// Given
	Region region(IntRect{0, 0, 30, 30});
	region.Exclude(IntRect{10, 10, 20, 20});

	// When
	Region copy(region);
	Region assigned;
	assigned = region;
	Region moved(std::move(copy));
	Region target;
	target.MoveFrom(assigned);

	// Then
	EXPECT_EQ(moved, region);
	EXPECT_EQ(target, region);
	EXPECT_TRUE(copy.IsEmpty());
	EXPECT_TRUE(assigned.IsEmpty());
	region.OffsetBy(5, -5);
	EXPECT_EQ(region.Frame(), (IntRect{5, -5, 35, 25}));
	EXPECT_EQ(region.RectAt(1), (IntRect{5, 5, 15, 15}));
	EXPECT_FALSE(region == moved);
}


TEST(RegionTest, MoveIntoRegionWithData)
{
	// What is tested: Moving a region of several rectangles into a region that
	// already holds rectangles, and moving a region into itself.
	// Success criteria: The target holds the moved rectangles, the source is
	// empty; a self-move keeps the region unchanged. Under AddressSanitizer
	// (preset fuzz) the test also proves that no buffer leaks.
	// Why it matters: Haiku's MoveFrom() overwrites the buffer of the target
	// without freeing it (found by the fuzz target Region); every Restore() of
	// the Canvas state would leak.
	//
	// Given
	Region source(IntRect{0, 0, 30, 30});
	source.Exclude(IntRect{10, 10, 20, 20});
	Region target(IntRect{0, 0, 5, 5});
	target.Include(IntRect{10, 0, 15, 5});
	const Region expected = source;

	// When
	target = std::move(source);
	Region& alias = target;
	target.MoveFrom(alias);

	// Then
	EXPECT_EQ(target, expected);
	EXPECT_TRUE(source.IsEmpty());
}


} // namespace
