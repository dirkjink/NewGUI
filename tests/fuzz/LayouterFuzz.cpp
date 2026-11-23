/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <ngui/LayoutUtils.h>

#include "layout/CollapsingLayouter.h"
#include "layout/ComplexLayouter.h"
#include "layout/SimpleLayouter.h"


namespace {


using namespace ngui;
using namespace ngui::layout;


// Reads the input byte by byte; 0 once it is used up.
class Bytes {
public:
	explicit Bytes(std::span<const std::uint8_t> data)
		:
		fData(data)
	{
	}

	int Next()
	{
		return fNext < fData.size() ? fData[fNext++] : 0;
	}

	bool AtEnd() const
	{
		return fNext >= fData.size();
	}

private:
	std::span<const std::uint8_t> fData;
	std::size_t fNext = 0;
};


} // namespace


NGUI_FUZZ_TARGET(Layouter)
{
	// What is tested: A Simple, Complex or Collapsing layouter (first byte)
	// with 1 to 8 elements, a spacing of 0 to 7, and constraints and
	// weights from the remaining bytes (element, length, min, max, weight),
	// laid out at a size from 0 to 1020, and its clone laid out the same.
	// Success criteria: No crash or sanitizer report; minimum <= preferred
	// <= maximum; no element has a negative size or lies outside the laid
	// out size; for the Simple and Complex layouters, at a size between
	// minimum and maximum the elements and spacings fill it exactly; the
	// clone gives the same result.
	// Why it matters: Layouts get their constraints from applications and
	// from font metrics; no combination may break the layouter or leave
	// gaps that views would not cover.
	//
	// Given
	Bytes bytes(data);
	const int kind = bytes.Next() % 3;
	const int32 count = 1 + bytes.Next() % 8;
	const float spacing = (float)(bytes.Next() % 8);
	std::unique_ptr<Layouter> layouter;
	if (kind == 0)
		layouter = std::make_unique<SimpleLayouter>(count, spacing);
	else if (kind == 1)
		layouter = std::make_unique<ComplexLayouter>(count, spacing);
	else
		layouter = std::make_unique<CollapsingLayouter>(count, spacing);
	const float size = (float)(bytes.Next() * 4);
	while (!bytes.AtEnd()) {
		const int32 element = bytes.Next() % count;
		const int32 length = kind == 0 ? 1 : 1 + bytes.Next() % (count - element);
		const float min = (float)bytes.Next();
		const int maxByte = bytes.Next();
		const float max = maxByte == 255 ? kSizeUnlimited : min + (float)maxByte;
		layouter->AddConstraints(element, length, min, max, min);
		layouter->SetWeight(element, (float)(bytes.Next() % 4));
	}

	// When
	const float minSize = layouter->MinSize();
	const float maxSize = layouter->MaxSize();
	const float preferred = layouter->PreferredSize();
	const std::unique_ptr<LayoutInfo> info = layouter->CreateLayoutInfo();
	layouter->Layout(*info, size);
	const std::unique_ptr<Layouter> clone = layouter->CloneLayouter();
	const std::unique_ptr<LayoutInfo> cloneInfo = clone->CreateLayoutInfo();
	clone->Layout(*cloneInfo, size);

	// Then
	NGUI_FUZZ_CHECK(minSize >= 0);
	NGUI_FUZZ_CHECK(minSize <= preferred);
	NGUI_FUZZ_CHECK(preferred <= maxSize);
	const float laidOut = size < minSize ? minSize : size;
	for (int32 i = 0; i < count; i++) {
		NGUI_FUZZ_CHECK(info->ElementSize(i) >= 0);
		NGUI_FUZZ_CHECK(info->ElementLocation(i) >= 0);
		NGUI_FUZZ_CHECK(info->ElementLocation(i) + info->ElementSize(i) <= laidOut
			|| laidOut > maxSize);
		NGUI_FUZZ_CHECK(cloneInfo->ElementLocation(i) == info->ElementLocation(i));
		NGUI_FUZZ_CHECK(cloneInfo->ElementSize(i) == info->ElementSize(i));
	}
	if (kind != 2 && size >= minSize && size <= maxSize) {
		NGUI_FUZZ_CHECK(info->ElementLocation(count - 1) + info->ElementSize(count - 1)
			== size);
	}
}
