/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "compat/CompatView.h"

#include <limits>
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
	// conversions FromBRect() and BRegion::ToIntRect(), also of the largest
	// BRect.
	// Success criteria: BRect(0, 0, 9, 9) has Width() 9 and IntegerWidth() 9
	// as in Haiku, covers 10 pixels after FromBRect(), and the region of it
	// is IntRect{0, 0, 10, 10}; an invalid BRect gives an empty region; the
	// largest BRect of floats (the looks' update rectangle) is kept within
	// ±2^30 instead of overflowing int (undefined behavior).
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
	const float largest = std::numeric_limits<float>::max();
	EXPECT_EQ(BRegion::ToIntRect(BRect(-largest, -largest, largest, largest)),
		(gfx::IntRect{-(1 << 30), -(1 << 30), (1 << 30) + 1, (1 << 30) + 1}));
	EXPECT_EQ(BRegion(rect).Frame(), rect);
	EXPECT_EQ(PixelCenter(BPoint(3, 4)), (gfx::Point{3.5f, 4.5f}));
}


TEST(CompatViewTest, GradientsAreSampledLikeHaikus)
{
	// What is tested: BGradientLinear (stops added out of order, one
	// recolored, one out of range) and FillRect(), FillEllipse() with it: a
	// vertical gradient from row 2 to row 11, a horizontal one, an empty one
	// and one with a single stop.
	// Success criteria: The stops are sorted by offset (AddColor returns
	// index 0 for the second, smaller offset) and an offset beyond 255 is
	// refused (-1). The vertical rectangle runs from half a pixel above row
	// 2 to half a pixel below row 11; the ellipse takes the points as they
	// are; offsets become 0 to 1. Without stops nothing is drawn, with one
	// its color fills.
	// Why it matters: Haiku's app_server fills a vertical gradient with one
	// color per row from the start row to the end row
	// (Painter::FillRectVerticalGradient()); a gradient sampled at pixel
	// centers hits those colors only with these points. Every bevel of the
	// Haiku look is such a gradient.
	//
	// Given
	BGradientLinear gradient;
	const int32 second = gradient.AddColor(make_color(0, 0, 255), 255);
	const int32 first = gradient.AddColor(make_color(255, 0, 0), 0);
	const int32 refused = gradient.AddColor(make_color(0, 255, 0), 256);
	const bool recolored = gradient.SetColor(1, make_color(0, 255, 0));
	gradient.SetStart(BPoint(2, 2));
	gradient.SetEnd(BPoint(2, 11));
	BGradientLinear horizontal = gradient;
	horizontal.SetEnd(BPoint(11, 2));
	BGradientLinear empty;
	BGradientLinear single;
	single.AddColor(make_color(0, 0, 255), 128);

	// When
	const std::string dump = Recorded([&](CompatView& view) {
		view.FillRect(BRect(2, 2, 11, 11), gradient);
		view.FillEllipse(BRect(2, 2, 11, 11), horizontal);
		view.FillRect(BRect(0, 0, 1, 1), empty);
		view.FillRect(BRect(0, 0, 1, 1), single);
	});

	// Then
	EXPECT_EQ(second, 0);
	EXPECT_EQ(first, 0);
	EXPECT_EQ(refused, -1);
	EXPECT_TRUE(recolored);
	EXPECT_EQ(gradient.CountColorStops(), 2);
	EXPECT_EQ(dump,
		"DisplayList bounds=[0 0 20 20] ops=3\n"
		"clip [0 0 20 20]\n"
		"FillRect rect=[2 2 12 12] color=(0 0 0 1) width=1 cap=Butt join=Miter aa=1"
		" gradient=(2 1.5)->(2 11.5) stop=0:(1 0 0 1) stop=1:(0 1 0 1)\n"
		"FillEllipse rect=[2 2 12 12] color=(0 0 0 1) width=1 cap=Butt join=Miter aa=1"
		" gradient=(2 2)->(11 2) stop=0:(1 0 0 1) stop=1:(0 1 0 1)\n"
		"FillRect rect=[0 0 2 2] color=(0 0 1 1) width=1 cap=Butt join=Miter aa=1\n");
}


TEST(CompatViewTest, ShapesAndRoundRectsAreDrawnLikeHaikus)
{
	// What is tested: A closed triangle shape stroked and filled at pen
	// location (1, 1), filled with a gradient, an open shape whose second
	// MoveTo replaces the first, and StrokeRoundRect() of BRect(2, 2, 11, 11)
	// with radius 3 at pen size 1 and 2.
	// Success criteria: The shape's points move with the pen and are not
	// rounded; stroked it is a closed polyline, filled a convex polygon;
	// the gradient of a shape does not move. The open shape starts at the
	// replaced point. The round rectangle at pen size 1 lies on the pixel
	// edges [2, 12) with radius 3.5 at its outer edge; at pen size 2 its
	// stroke is centered on the corners 2 and 11, [1, 12) with radius 4.
	// Why it matters: Haiku offsets shapes by the pen location and strokes
	// round rectangles on the pixel centers (ServerWindow, Painter); the
	// Haiku look draws its slider triangles and arrows as shapes and the
	// default button's ring as a round rectangle.
	//
	// Given
	BShape triangle;
	triangle.MoveTo(BPoint(0.5f, 8.5f));
	triangle.LineTo(BPoint(8.5f, 8.5f));
	triangle.LineTo(BPoint(4.5f, 0.5f));
	triangle.Close();
	BShape open;
	open.MoveTo(BPoint(9, 9));
	open.MoveTo(BPoint(0, 0));
	open.LineTo(BPoint(5, 0));
	BGradientLinear gradient;
	gradient.AddColor(make_color(255, 0, 0), 0);
	gradient.AddColor(make_color(0, 0, 255), 255);
	gradient.SetStart(BPoint(0, 0));
	gradient.SetEnd(BPoint(0, 8));

	// When
	const std::string dump = Recorded([&](CompatView& view) {
		view.MovePenTo(BPoint(1, 1));
		view.StrokeShape(&triangle);
		view.FillShape(&triangle);
		view.FillShape(&triangle, gradient);
		view.MovePenTo(B_ORIGIN);
		view.StrokeShape(&open);
		view.StrokeRoundRect(BRect(2, 2, 11, 11), 3, 3);
		view.SetPenSize(2);
		view.StrokeRoundRect(BRect(2, 2, 11, 11), 3, 3);
	});

	// Then
	EXPECT_EQ(dump,
		"DisplayList bounds=[0 0 20 20] ops=6\n"
		"clip [0 0 20 20]\n"
		"StrokePolyline points=(1.5 9.5)(9.5 9.5)(5.5 1.5) closed=1 color=(0 0 0 1)"
		" width=1 cap=Butt join=Miter aa=1\n"
		"FillConvexPolygon points=(1.5 9.5)(9.5 9.5)(5.5 1.5) color=(0 0 0 1) width=1"
		" cap=Butt join=Miter aa=1\n"
		"FillConvexPolygon points=(1.5 9.5)(9.5 9.5)(5.5 1.5) color=(0 0 0 1) width=1"
		" cap=Butt join=Miter aa=1 gradient=(0 0)->(0 8) stop=0:(1 0 0 1)"
		" stop=1:(0 0 1 1)\n"
		"StrokePolyline points=(0 0)(5 0) closed=0 color=(0 0 0 1) width=1 cap=Butt"
		" join=Miter aa=1\n"
		"StrokeRRect rrect=[2 2 12 12] radii=(3.5 3.5 3.5 3.5) color=(0 0 0 1) width=1"
		" cap=Butt join=Miter aa=1\n"
		"StrokeRRect rrect=[1 1 12 12] radii=(4 4 4 4) color=(0 0 0 1) width=2"
		" cap=Butt join=Miter aa=1\n");
}


} // namespace
