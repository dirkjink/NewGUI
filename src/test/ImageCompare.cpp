/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/ImageCompare.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>


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


} // namespace ngui::test
