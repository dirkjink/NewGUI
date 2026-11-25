/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "compat/CompatView.h"

#include <cmath>
#include <cstdlib>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>

#include "Rendered.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::compat;
using namespace ngui::test;


// The color of row \a row of \a rows in Haiku's vertical gradient from
// \a from to \a to (Painter::_MakeGradient() for two stops at 0 and 255):
// the first row is a step away from \a from, the last row is \a to.
int
HaikuChannel(int from, int to, int row, int rows)
{
	const float f = static_cast<float>(rows - 1 - row) / static_cast<float>(rows);
	return static_cast<int>(std::floor(static_cast<float>(from) * f
		+ static_cast<float>(to) * (1 - f) + 0.5f));
}


TEST(CompatSkiaTest, VerticalGradientRowsAreHaikus)
{
	// What is tested: FillRect() of BRect(0, 0, 3, 9) with a gradient from
	// red at row 0 to blue at row 9 through the compatibility layer, rendered
	// by Skia, read row by row.
	// Success criteria: Each of the 10 rows has the color Haiku's app_server
	// gives it, within 1 per channel: row 0 is (230 0 26), row 9 is blue.
	// Why it matters: The Haiku look fills every button and bar with such
	// gradients; pixel fidelity to Haiku (conformance level C2) needs the
	// same color in every row, not just the same ends.
	//
	// Given
	BGradientLinear gradient;
	gradient.AddColor(make_color(255, 0, 0), 0);
	gradient.AddColor(make_color(0, 0, 255), 255);
	gradient.SetStart(BPoint(0, 0));
	gradient.SetEnd(BPoint(0, 9));
	gfx::Canvas canvas({0, 0, 4, 10});
	const gfx::Caps caps;
	const FixedTextSystem text;
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	{
		CompatView view(context);
		view.FillRect(BRect(0, 0, 3, 9), gradient);
	}

	// When
	const Rendered rendered(canvas.Finish(), {4, 10});

	// Then
	for (int row = 0; row < 10; row++) {
		const Rgba pixel = rendered.At(2, row);
		EXPECT_LE(std::abs(pixel.r - HaikuChannel(255, 0, row, 10)), 1) << "row " << row;
		EXPECT_EQ(pixel.g, 0) << "row " << row;
		EXPECT_LE(std::abs(pixel.b - HaikuChannel(0, 255, row, 10)), 1) << "row " << row;
	}
	EXPECT_EQ(HaikuChannel(255, 0, 0, 10), 230);
	EXPECT_EQ(rendered.At(2, 9), kBlue);
}


} // namespace
