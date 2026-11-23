/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "compat/CompatView.h"

#include <string>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Dump.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::compat;
using ngui::test::FixedTextSystem;


// The dump of what \a draw records through a CompatView on a 20 × 20 Canvas.
template<typename Draw>
std::string
Recorded(Draw draw)
{
	gfx::Canvas canvas({0, 0, 20, 20});
	const gfx::Caps caps;
	const FixedTextSystem text;
	DrawContext context = {canvas, caps, Palette::Default(), text, 1};
	{
		CompatView view(context);
		draw(view);
	}
	return gfx::DumpText(*canvas.Finish());
}


TEST(CompatViewTest, RectanglesAndLinesBecomeHalfOpen)
{
	// What is tested: FillRect, StrokeLine and a line array with Haiku's
	// inclusive coordinates, recorded through the compatibility layer.
	// Success criteria: BRect(0, 0, 9, 9) becomes [0, 10) × [0, 10); lines go
	// through the pixel centers with square caps and width 1, each line of an
	// array in its own color, the array as one DrawLines.
	// Why it matters: These are the conversions of specification 17.1 and
	// 17.2; the bevels of the BeOS look are made of exactly these calls
	// (270 AddLine calls in Haiku's BeControlLook).
	//
	// When
	const std::string dump = Recorded([](CompatView& view) {
		view.SetHighColor(255, 0, 0);
		view.FillRect(BRect(0, 0, 9, 9));
		view.StrokeLine(BPoint(0, 12), BPoint(9, 12));
		view.BeginLineArray(2);
		view.AddLine(BPoint(0, 14), BPoint(9, 14), make_color(0, 0, 255));
		view.AddLine(BPoint(0, 15), BPoint(0, 19), make_color(0, 255, 0));
		view.EndLineArray();
	});

	// Then
	EXPECT_EQ(dump,
		"DisplayList bounds=[0 0 20 20] ops=3\n"
		"clip [0 0 20 20]\n"
		"FillRect rect=[0 0 10 10] color=(1 0 0 1) width=1 cap=Butt join=Miter aa=1\n"
		"DrawLines width=1 cap=Square line=(0.5 12.5)->(9.5 12.5):(1 0 0 1)\n"
		"DrawLines width=1 cap=Square line=(0.5 14.5)->(9.5 14.5):(0 0 1 1)"
		" line=(0.5 15.5)->(0.5 19.5):(0 1 0 1)\n");
}


TEST(CompatViewTest, StateIsPushedAndPopped)
{
	// What is tested: PushState(), ClipToRect() and SetHighColor() inside,
	// PopState(), and StrokeArc() after it.
	// Success criteria: Inside, the clip is BRect(0, 0, 4, 4) as [0, 5) and
	// the color is blue; after PopState() the full clip and red are back; the
	// arc's angles are negated, as Haiku's run counterclockwise.
	// Why it matters: The looks bracket every part with PushState() and
	// PopState() (21 times in BeControlLook); state must not leak out.
	//
	// When
	const std::string dump = Recorded([](CompatView& view) {
		view.SetHighColor(255, 0, 0);
		view.PushState();
		view.ClipToRect(BRect(0, 0, 4, 4));
		view.SetHighColor(0, 0, 255);
		view.FillEllipse(BRect(0, 0, 9, 9));
		view.PopState();
		view.StrokeArc(BRect(0, 0, 9, 9), 0, 90);
	});

	// Then
	EXPECT_EQ(dump,
		"DisplayList bounds=[0 0 20 20] ops=2\n"
		"clip [0 0 5 5]\n"
		"FillEllipse rect=[0 0 10 10] color=(0 0 1 1) width=1 cap=Butt join=Miter aa=1\n"
		"clip [0 0 20 20]\n"
		"StrokeArc oval=[0 0 10 10] start=0 sweep=-90 color=(1 0 0 1) width=1 cap=Butt"
		" join=Miter aa=1\n");
}


TEST(CompatViewTest, ColorsComeFromThePalette)
{
	// What is tested: UiColor() for the panel background, tint_color() of it,
	// and B_SOLID_LOW with a low color.
	// Success criteria: The panel color is 216, B_DARKEN_1_TINT makes it 184
	// exactly as Haiku; B_SOLID_LOW fills with the low color.
	// Why it matters: The looks take every color from ui_color() and
	// tint_color() (specification 18.1, L3); through the layer they come
	// from the palette of the DrawContext.
	//
	// Given
	rgb_color panel;
	rgb_color darker;

	// When
	const std::string dump = Recorded([&](CompatView& view) {
		panel = view.UiColor(B_PANEL_BACKGROUND_COLOR);
		darker = tint_color(panel, B_DARKEN_1_TINT);
		view.SetLowColor(darker);
		view.FillRect(BRect(0, 0, 0, 0), B_SOLID_LOW);
	});

	// Then
	EXPECT_EQ(panel, make_color(216, 216, 216));
	EXPECT_EQ(darker, make_color(184, 184, 184));
	EXPECT_NE(dump.find("FillRect rect=[0 0 1 1] color=(0.722 0.722 0.722 1)"),
		std::string::npos) << dump;
}


TEST(CompatViewTest, TextGoesThroughTheTextSystem)
{
	// What is tested: StringWidth() and DrawString() with the plain font and
	// with the font enlarged to 24, and TruncateString().
	// Success criteria: Widths and glyph positions scale with the font size;
	// the run is drawn at the baseline point; truncation keeps what fits.
	// Why it matters: Labels are drawn and measured through the text
	// subsystem (specification 17.2); a look must not compute metrics of its
	// own.
	//
	// When
	float plainWidth = 0;
	float largeWidth = 0;
	std::string truncated = "Label";
	const std::string dump = Recorded([&](CompatView& view) {
		plainWidth = view.StringWidth("OK");
		// Any BFont to copy the view's font into; GetFont() overwrites it.
		static const FixedTextSystem kOther;
		BFont large(kOther);
		view.GetFont(&large);
		large.SetSize(24);
		view.SetFont(&large);
		largeWidth = view.StringWidth("OK");
		large.TruncateString(&truncated, B_TRUNCATE_END, 3 * 14.0f);
		view.DrawString("OK", BPoint(2, 15));
	});

	// Then
	EXPECT_EQ(plainWidth, 14);
	EXPECT_EQ(largeWidth, 28);
	EXPECT_EQ(truncated, "Lab");
	EXPECT_NE(dump.find("DrawGlyphRun font=1 size=24 origin=(2 15) color=(0 0 0 1)"
		" glyphs=79@(0 0),75@(14 0)"), std::string::npos) << dump;
}


TEST(CompatViewTest, CoordinatesAreRoundedLikeHaikus)
{
	// What is tested: FillRect() and FillTriangle() with fractional
	// coordinates, without and with B_SUBPIXEL_PRECISE.
	// Success criteria: Without the flag, the coordinates are rounded down
	// to whole pixels first: BRect(0.6, 0.6, 9.4, 9.4) fills [0, 10) and a
	// triangle point (2.5, 0.5) goes through the center of pixel (2, 0); with
	// the flag they are taken as they are.
	// Why it matters: Haiku's app_server rounds this way unless a view asks
	// for subpixel precision (Painter::_Align()); the BeOS look relies on
	// both, e.g. for the pop-up marker.
	//
	// When
	const std::string dump = Recorded([](CompatView& view) {
		view.FillRect(BRect(0.6f, 0.6f, 9.4f, 9.4f));
		view.FillTriangle(BPoint(2.5f, 0.5f), BPoint(4, 0), BPoint(3, 2));
		view.SetFlags(B_SUBPIXEL_PRECISE);
		view.FillRect(BRect(0.5f, 0.5f, 9.5f, 9.5f));
		view.FillTriangle(BPoint(2.5f, 0.5f), BPoint(4, 0), BPoint(3, 2));
	});

	// Then
	EXPECT_EQ(dump,
		"DisplayList bounds=[0 0 20 20] ops=4\n"
		"clip [0 0 20 20]\n"
		"FillRect rect=[0 0 10 10] color=(0 0 0 1) width=1 cap=Butt join=Miter aa=1\n"
		"FillConvexPolygon points=(2.5 0.5)(4.5 0.5)(3.5 2.5) color=(0 0 0 1) width=1"
		" cap=Butt join=Miter aa=1\n"
		"FillRect rect=[0.5 0.5 10.5 10.5] color=(0 0 0 1) width=1 cap=Butt join=Miter"
		" aa=1\n"
		"FillConvexPolygon points=(3 1)(4.5 0.5)(3.5 2.5) color=(0 0 0 1) width=1"
		" cap=Butt join=Miter aa=1\n");
}


TEST(CompatViewTest, BRectKeepsHaikusSemantics)
{
	// What is tested: The ported BRect: width, integer width, inset, and the
	// conversions FromBRect() and BRegion::ToIntRect().
	// Success criteria: BRect(0, 0, 9, 9) has Width() 9 and IntegerWidth() 9
	// as in Haiku, covers 10 pixels after FromBRect(), and the region of it
	// is IntRect{0, 0, 10, 10}; an invalid BRect gives an empty region.
	// Why it matters: The ported look code computes with these semantics on
	// every line; the layer converts only when drawing (specification 17.1).
	//
	// Given
	const BRect rect(0, 0, 9, 9);

	// When / Then
	EXPECT_EQ(rect.Width(), 9);
	EXPECT_EQ(rect.IntegerWidth(), 9);
	EXPECT_EQ(rect.InsetByCopy(1, 1), BRect(1, 1, 8, 8));
	EXPECT_EQ(FromBRect(rect), (gfx::Rect{0, 0, 10, 10}));
	EXPECT_EQ(BRegion::ToIntRect(rect), (gfx::IntRect{0, 0, 10, 10}));
	EXPECT_EQ(BRegion::ToIntRect(BRect()), (gfx::IntRect{0, 0, 0, 0}));
	EXPECT_EQ(BRegion(rect).Frame(), rect);
	EXPECT_EQ(PixelCenter(BPoint(3, 4)), (gfx::Point{3.5f, 4.5f}));
}


} // namespace
