/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/ImageCompare.h>

#include <gtest/gtest.h>


namespace {


using namespace ngui::test;


// 10 × 10 white with \a count pixels of the first row changed by \a delta.
Image
WithChanges(int count, int delta)
{
	Image image(10, 10, {255, 255, 255, 255});
	for (int x = 0; x < count; x++) {
		const auto value = static_cast<std::uint8_t>(255 - delta);
		image.Set(x, 0, {value, 255, 255, 255});
	}
	return image;
}


TEST(ImageCompareTest, TolerancesAreThoseOfTheSpecification)
{
	// What is tested: CompareImages() of a 10 × 10 reference against copies
	// with 1 or 2 changed pixels (1 % and 2 %) by 3, 4, 32 or 33, under the
	// four tolerances.
	// Success criteria: Exact passes only the identical image; AntiAliased
	// passes 1 % up to 32 and fails 2 % or 33; Gradient passes any number up
	// to 3 and fails 4; Text passes 1 % with any difference and fails above
	// 3 %. Counts and the largest difference are reported.
	// Why it matters: These are the conformance criteria of specification
	// 21.2; an off-by-one in a limit would let a wrong rendering pass.
	//
	// Given
	const Image reference = WithChanges(0, 0);

	// When / Then
	EXPECT_TRUE(CompareImages(reference, reference, Tolerance::Exact()).passed);
	EXPECT_FALSE(CompareImages(reference, WithChanges(1, 1), Tolerance::Exact()).passed);

	EXPECT_TRUE(CompareImages(reference, WithChanges(1, 32), Tolerance::AntiAliased()).passed);
	EXPECT_FALSE(CompareImages(reference, WithChanges(1, 33), Tolerance::AntiAliased()).passed);
	EXPECT_FALSE(CompareImages(reference, WithChanges(2, 1), Tolerance::AntiAliased()).passed);

	EXPECT_TRUE(CompareImages(reference, WithChanges(10, 3), Tolerance::Gradient()).passed);
	EXPECT_FALSE(CompareImages(reference, WithChanges(1, 4), Tolerance::Gradient()).passed);

	EXPECT_TRUE(CompareImages(reference, WithChanges(3, 255), Tolerance::Text()).passed);
	EXPECT_FALSE(CompareImages(reference, WithChanges(4, 1), Tolerance::Text()).passed);

	const ImageComparison comparison = CompareImages(reference, WithChanges(2, 33),
		Tolerance::AntiAliased());
	EXPECT_EQ(comparison.pixelCount, 100);
	EXPECT_EQ(comparison.differingPixels, 2);
	EXPECT_EQ(comparison.maxChannelDifference, 33);
	EXPECT_EQ(comparison.Describe(),
		"2 of 100 pixels differ (2.000 %), largest component difference 33");
}


TEST(ImageCompareTest, DifferenceImageMarksThePixels)
{
	// What is tested: The difference image of a comparison with one pixel
	// within and one beyond the tolerance, and a comparison of different
	// sizes.
	// Success criteria: Equal pixels are a light gray of the reference (white
	// stays white, black becomes 192); within the tolerance orange, beyond it
	// red. Different sizes fail with an empty difference image.
	// Why it matters: The difference image is what a developer looks at in
	// the report to see where a rendering went wrong.
	//
	// Given
	Image reference(3, 1, {255, 255, 255, 255});
	reference.Set(2, 0, {0, 0, 0, 255});
	Image actual = reference;
	actual.Set(0, 0, {250, 255, 255, 255});
	actual.Set(1, 0, {0, 255, 255, 255});

	// When
	const ImageComparison comparison = CompareImages(reference, actual, {1, 10});
	const ImageComparison sizes = CompareImages(reference, Image(2, 1), Tolerance::Exact());

	// Then
	EXPECT_FALSE(comparison.passed);
	EXPECT_EQ(comparison.difference.At(0, 0), (Rgba8{255, 160, 0, 255}));
	EXPECT_EQ(comparison.difference.At(1, 0), (Rgba8{255, 0, 0, 255}));
	EXPECT_EQ(comparison.difference.At(2, 0), (Rgba8{192, 192, 192, 255}));
	EXPECT_FALSE(sizes.passed);
	EXPECT_FALSE(sizes.sizesMatch);
	EXPECT_TRUE(sizes.difference.IsEmpty());
	EXPECT_EQ(sizes.Describe(), "the sizes differ");
}


} // namespace
