/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <ngui/gfx/IntRect.h>
#include <ngui/gfx/Region.h>

#include "../gfx/RegionInvariants.h"


namespace {


using ngui::gfx::IntRect;
using ngui::gfx::Region;


// The model: one bool per pixel of a small grid. Coordinates of the
// operations lie in [kMin, kMax], so every pixel they touch is in the grid.
constexpr int kMin = -8;
constexpr int kMax = 24;
constexpr int kSize = kMax - kMin;


class Bitmap {
public:
			bool				Get(int x, int y) const;
			void				Set(const IntRect& rect, bool value);
			bool				AnyIn(const IntRect& rect) const;
			bool				operator==(const Bitmap& other) const = default;

			template<typename Function>
			void				Combine(const Bitmap& other, Function function);

private:
			std::array<bool, kSize * kSize> fPixels = {};
};


bool
Bitmap::Get(int x, int y) const
{
	return fPixels[(y - kMin) * kSize + (x - kMin)];
}


void
Bitmap::Set(const IntRect& rect, bool value)
{
	for (int y = rect.y0; y < rect.y1; y++) {
		for (int x = rect.x0; x < rect.x1; x++)
			fPixels[(y - kMin) * kSize + (x - kMin)] = value;
	}
}


bool
Bitmap::AnyIn(const IntRect& rect) const
{
	for (int y = rect.y0; y < rect.y1; y++) {
		for (int x = rect.x0; x < rect.x1; x++) {
			if (Get(x, y))
				return true;
		}
	}
	return false;
}


template<typename Function>
void
Bitmap::Combine(const Bitmap& other, Function function)
{
	for (std::size_t i = 0; i < fPixels.size(); i++)
		fPixels[i] = function(fPixels[i], other.fPixels[i]);
}


// Reads the operations; missing bytes count as zero.
class Reader {
public:
								Reader(std::span<const std::uint8_t> data);

			bool				AtEnd() const;
			std::uint8_t		Byte();
			IntRect				Rect();

private:
			std::span<const std::uint8_t> fData;
};


Reader::Reader(std::span<const std::uint8_t> data)
	:
	fData(data)
{
}


bool
Reader::AtEnd() const
{
	return fData.empty();
}


std::uint8_t
Reader::Byte()
{
	if (fData.empty())
		return 0;
	const std::uint8_t value = fData.front();
	fData = fData.subspan(1);
	return value;
}


IntRect
Reader::Rect()
{
	// Inverted and empty rectangles stay possible on purpose.
	const auto coordinate = [this]() {
		return static_cast<std::int32_t>(Byte() % (kSize + 1)) + kMin;
	};
	const std::int32_t x0 = coordinate();
	const std::int32_t y0 = coordinate();
	const std::int32_t x1 = coordinate();
	const std::int32_t y1 = coordinate();
	return {x0, y0, x1, y1};
}


// The model of an operation on a rectangle, clipped to its non-empty part.
void
Apply(Bitmap& bitmap, const IntRect& rect, bool value)
{
	if (!rect.IsEmpty())
		bitmap.Set(rect, value);
}


bool
Matches(const Region& region, const Bitmap& bitmap)
{
	for (int y = kMin - 1; y <= kMax; y++) {
		for (int x = kMin - 1; x <= kMax; x++) {
			const bool inGrid = x >= kMin && x < kMax && y >= kMin && y < kMax;
			if (region.Contains(x, y) != (inGrid && bitmap.Get(x, y)))
				return false;
		}
	}
	return true;
}


} // namespace


NGUI_FUZZ_TARGET(Region)
{
	// What is tested: Arbitrary sequences of Region operations (Set, Include,
	// Exclude, IntersectWith, ExclusiveInclude, copy and move) on two regions,
	// against a bitmap model of the same operations.
	// Success criteria: After every operation both regions cover exactly the
	// pixels of their model, are in canonical banded form, answer Intersects
	// like the model, and compare equal exactly when their models are equal.
	// Why it matters: The region is the clip state of the Canvas
	// (specification 7) and a port of 1,600 lines of X11 banding code; the
	// half-open conversion and the canonical form are what a bitmap oracle
	// checks exhaustively and hand-written tests cannot.
	//
	// Given
	Reader reader(data);
	Region a;
	Region b;
	Bitmap modelA;
	Bitmap modelB;

	for (int step = 0; step < 64 && !reader.AtEnd(); step++) {
		// When
		const std::uint8_t operation = reader.Byte() % 11;
		const IntRect rect = reader.Rect();
		switch (operation) {
			case 0:
				a.Include(rect);
				Apply(modelA, rect, true);
				break;
			case 1:
				a.Exclude(rect);
				Apply(modelA, rect, false);
				break;
			case 2:
				b.Include(rect);
				Apply(modelB, rect, true);
				break;
			case 3:
				b.Exclude(rect);
				Apply(modelB, rect, false);
				break;
			case 4:
				a.IntersectWith(b);
				modelA.Combine(modelB, [](bool p, bool q) { return p && q; });
				break;
			case 5:
				a.Include(b);
				modelA.Combine(modelB, [](bool p, bool q) { return p || q; });
				break;
			case 6:
				a.Exclude(b);
				modelA.Combine(modelB, [](bool p, bool q) { return p && !q; });
				break;
			case 7:
				a.ExclusiveInclude(b);
				modelA.Combine(modelB, [](bool p, bool q) { return p != q; });
				break;
			case 8:
				a.Set(rect);
				modelA = Bitmap();
				Apply(modelA, rect, true);
				break;
			case 9:
				a = b;
				modelA = modelB;
				break;
			case 10:
				// The moved-from region must be empty, as documented.
				a = std::move(b);
				modelA = modelB;
				modelB = Bitmap();
				break;
		}

		// Then
		NGUI_FUZZ_CHECK(ngui::test::IsCanonical(a));
		NGUI_FUZZ_CHECK(ngui::test::IsCanonical(b));
		NGUI_FUZZ_CHECK(Matches(a, modelA));
		NGUI_FUZZ_CHECK(Matches(b, modelB));
		NGUI_FUZZ_CHECK(a.Intersects(rect) == (!rect.IsEmpty() && modelA.AnyIn(rect)));
		NGUI_FUZZ_CHECK((a == b) == (modelA == modelB));
	}
}
