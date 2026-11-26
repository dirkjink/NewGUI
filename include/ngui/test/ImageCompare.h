/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <string>

#include <ngui/test/Image.h>


namespace ngui::test {


/*!	How far an image may deviate from its reference (specification 21.2).
	A pixel deviates if any component differs. An image passes if at most
	maxDifferingFraction of its pixels deviate and no component differs by
	more than maxChannelDifference.
*/
struct Tolerance {
			double				maxDifferingFraction = 0;
									/*!< Share of deviating pixels, 0 to 1. */
			int					maxChannelDifference = 0;
									/*!< Largest difference of one component, 0 to 255. */

	static	constexpr Tolerance	Exact();
									/*!< Pixel-identical: axis-parallel primitives (A1/A3). */
	static	constexpr Tolerance	AntiAliased();
									/*!< ≤ 1 % of the pixels, ≤ 32/255: ellipses, rounded
										corners, arcs, diagonals. */
	static	constexpr Tolerance	Gradient();
									/*!< Any number of pixels, ≤ 3/255: gradients. */
	static	constexpr Tolerance	Text();
									/*!< ≤ 3 % of the pixels, any difference: text; meant for
										an image cut to the text's bounding box. */

			constexpr bool		operator==(const Tolerance& other) const = default;
									/*!< Same limits. */
};


/*!	The result of CompareImages(). */
struct ImageComparison {
			bool				passed = false;
									/*!< Within the tolerance, and the sizes match. */
			bool				sizesMatch = false;
									/*!< Reference and result have the same size. */
			std::int64_t		pixelCount = 0;
									/*!< Pixels compared. */
			std::int64_t		differingPixels = 0;
									/*!< Pixels with any difference. */
			int					maxChannelDifference = 0;
									/*!< Largest difference of one component. */
			Image				difference;
									/*!< The reference faded to light gray, deviating pixels
										in orange (within the tolerance) or red (beyond);
										empty if the sizes differ. */

			std::string			Describe() const;
									/*!< One line for a test failure. */
};


/*!	Compares \a actual with \a reference under \a tolerance. */
ImageComparison					CompareImages(const Image& reference, const Image& actual,
									const Tolerance& tolerance);


/*!	The result of CompareRasterizers(). */
struct RasterizerComparison {
			bool				passed = false;
									/*!< They look alike, and the sizes match. */
			double				meanDifference = 0;
									/*!< The mean of each pixel's largest component
										difference, in 1/255. */
			std::int64_t		strongPixels = 0;
									/*!< Pixels more than 64/255 apart. */
			int					maxChannelDifference = 0;
									/*!< Largest difference of one component. */

			std::string			Describe() const;
									/*!< One line for a test failure. */
};


/*!	Whether two rasterizers drew the same antialiased drawing alike
	(specification 21.2: backend against backend, CPU against GPU): their
	edge pixels cover differently, so many pixels differ a little and a few,
	at thin diagonals, a lot. Alike means a mean difference of at most
	3/255 and at most 0.5 % of the pixels more than 64/255 apart. */
RasterizerComparison			CompareRasterizers(const Image& reference,
									const Image& actual);


constexpr Tolerance
Tolerance::Exact()
{
	return {0, 0};
}


constexpr Tolerance
Tolerance::AntiAliased()
{
	return {0.01, 32};
}


constexpr Tolerance
Tolerance::Gradient()
{
	return {1, 3};
}


constexpr Tolerance
Tolerance::Text()
{
	return {0.03, 255};
}


} // namespace ngui::test
