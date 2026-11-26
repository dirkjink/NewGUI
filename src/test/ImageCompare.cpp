/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/ImageCompare.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>


namespace ngui::test {


std::string
ImageComparison::Describe() const
{
	char line[200];
	if (!sizesMatch) {
		std::snprintf(line, sizeof(line), "the sizes differ");
		return line;
	}
	const double percent = pixelCount > 0
		? 100.0 * static_cast<double>(differingPixels) / static_cast<double>(pixelCount) : 0;
	std::snprintf(line, sizeof(line),
		"%lld of %lld pixels differ (%.3f %%), largest component difference %d",
		static_cast<long long>(differingPixels), static_cast<long long>(pixelCount), percent,
		maxChannelDifference);
	return line;
}


ImageComparison
CompareImages(const Image& reference, const Image& actual, const Tolerance& tolerance)
{
	ImageComparison result;
	result.sizesMatch = reference.Width() == actual.Width()
		&& reference.Height() == actual.Height();
	if (!result.sizesMatch)
		return result;

	result.pixelCount = static_cast<std::int64_t>(reference.Width()) * reference.Height();
	result.difference = Image(reference.Width(), reference.Height());
	for (std::int32_t y = 0; y < reference.Height(); y++) {
		for (std::int32_t x = 0; x < reference.Width(); x++) {
			const Rgba8 a = reference.At(x, y);
			const Rgba8 b = actual.At(x, y);
			const int difference = std::max({std::abs(a.r - b.r), std::abs(a.g - b.g),
				std::abs(a.b - b.b), std::abs(a.a - b.a)});
			if (difference == 0) {
				// Faded: light enough that every marked pixel stands out.
				const int luma = (a.r * 3 + a.g * 6 + a.b + 5) / 10;
				const auto faded = static_cast<std::uint8_t>(255 - (255 - luma) / 4);
				result.difference.Set(x, y, {faded, faded, faded, 255});
				continue;
			}
			result.differingPixels++;
			result.maxChannelDifference = std::max(result.maxChannelDifference, difference);
			result.difference.Set(x, y, difference <= tolerance.maxChannelDifference
				? Rgba8{255, 160, 0, 255} : Rgba8{255, 0, 0, 255});
		}
	}
	const double fraction = result.pixelCount > 0
		? static_cast<double>(result.differingPixels) / static_cast<double>(result.pixelCount)
		: 0;
	result.passed = result.maxChannelDifference <= tolerance.maxChannelDifference
		&& fraction <= tolerance.maxDifferingFraction;
	return result;
}


RasterizerComparison
CompareRasterizers(const Image& reference, const Image& actual)
{
	// Measured with Skia and Blend2D 0.12 over every drawing method of the
	// BeOS and Haiku looks (the mean at most 2.04/255, more than 64/255 at
	// most 10 of 4,000 pixels) and with Skia on Mesa's llvmpipe against
	// Skia's CPU raster (the galleries of the three looks: the mean at most
	// 0.17/255, none more than 64/255).
	constexpr double kMeanLimit = 3;
	constexpr double kStrongLimit = 0.005;

	RasterizerComparison result;
	if (reference.Width() != actual.Width() || reference.Height() != actual.Height()
		|| reference.IsEmpty()) {
		return result;
	}
	double sum = 0;
	for (std::int32_t y = 0; y < reference.Height(); y++) {
		for (std::int32_t x = 0; x < reference.Width(); x++) {
			const Rgba8 p = reference.At(x, y);
			const Rgba8 q = actual.At(x, y);
			const int difference = std::max({std::abs(p.r - q.r), std::abs(p.g - q.g),
				std::abs(p.b - q.b), std::abs(p.a - q.a)});
			sum += difference;
			result.strongPixels += difference > 64 ? 1 : 0;
			result.maxChannelDifference = std::max(result.maxChannelDifference, difference);
		}
	}
	const double pixels = static_cast<double>(reference.Width()) * reference.Height();
	result.meanDifference = sum / pixels;
	result.passed = result.meanDifference <= kMeanLimit
		&& static_cast<double>(result.strongPixels) / pixels <= kStrongLimit;
	return result;
}


std::string
RasterizerComparison::Describe() const
{
	return "mean difference " + std::to_string(meanDifference) + ", "
		+ std::to_string(strongPixels) + " pixels more than 64 apart, largest "
		+ std::to_string(maxChannelDifference);
}


} // namespace ngui::test
