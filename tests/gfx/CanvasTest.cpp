/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Canvas.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>


namespace {


using namespace ngui::gfx;


constexpr Color kRed = {1, 0, 0, 1};
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();


std::vector<Op>
Operations(const DisplayList& list)
{
	return {list.Ops().begin(), list.Ops().end()};
}


std::vector<IntRect>
ClipOf(const DisplayList& list, const Op& op)
{
	const std::span<const IntRect> rects = list.Clip(op.ClipIndex());
	return {rects.begin(), rects.end()};
}


TEST(CanvasTest, TranslationIsAppliedAndRestored)
{
	// What is tested: Translate() inside Save()/Restore() pairs, nested, and
	// a FillRect after each step.
	// Success criteria: The recorded rectangles carry the accumulated
	// translation; after Restore() the previous translation applies again.
	// Why it matters: The DisplayList holds only absolute coordinates, and
	// Save, Restore and Translate do not appear in it (specification 11.2).
	//
	// Given
	Canvas canvas({0, 0, 100, 100});

	// When
	canvas.Save();
	canvas.Translate(10, 20);
	canvas.FillRect({0, 0, 5, 5}, Paint{});
	canvas.Save();
	canvas.Translate(1, 1);
	canvas.FillRect({0, 0, 5, 5}, Paint{});
	canvas.Restore();
	canvas.FillRect({0, 0, 5, 5}, Paint{});
	canvas.Restore();
	canvas.FillRect({0, 0, 5, 5}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// Then
	const std::vector<Op> ops = Operations(*list);
	ASSERT_EQ(ops.size(), 4u);
	EXPECT_EQ(ops[0].As<RectRecord>().rect, (Rect{10, 20, 15, 25}));
	EXPECT_EQ(ops[1].As<RectRecord>().rect, (Rect{11, 21, 16, 26}));
	EXPECT_EQ(ops[2].As<RectRecord>().rect, (Rect{10, 20, 15, 25}));
	EXPECT_EQ(ops[3].As<RectRecord>().rect, (Rect{0, 0, 5, 5}));
	EXPECT_EQ(list->Bounds(), (Rect{0, 0, 100, 100}));
}


TEST(CanvasTest, ClipIsRoundedAfterTranslation)
{
	// What is tested: ClipRect() with fractional edges under a fractional
	// translation, and ClipOutRect() inside it.
	// Success criteria: The edges are translated first and then rounded with
	// floor(v + 0.5): 0.3 + 0.2 = 0.5 becomes 1; ClipOutRect() cuts a hole.
	// Why it matters: This is the clip rule of specification 7; rounding
	// before the translation would shift clips by a pixel.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});

	// When
	canvas.Translate(0.2f, 0);
	canvas.ClipRect({0.3f, 0, 40.2f, 40});
	canvas.ClipOutRect({9.8f, 10, 19.8f, 20});
	canvas.FillRect({0, 0, 100, 100}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// Then
	const std::vector<Op> ops = Operations(*list);
	ASSERT_EQ(ops.size(), 1u);
	EXPECT_EQ(ClipOf(*list, ops[0]), (std::vector<IntRect>{{1, 0, 40, 10}, {1, 10, 10, 20},
		{20, 10, 40, 20}, {1, 20, 40, 40}}));
}


TEST(CanvasTest, ClipsAreSharedUntilTheyChange)
{
	// What is tested: Several operations under the same clip, then under a
	// narrower clip, then after Restore() under the first clip again.
	// Success criteria: Consecutive operations under one clip share a clip
	// entry; a change gives a new entry; Restore() brings back the old area.
	// Why it matters: Specification 11.2 shares clip entries between
	// consecutive operations; Save and Restore must restore the clip.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});

	// When
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	canvas.FillRect({10, 10, 20, 20}, Paint{});
	canvas.Save();
	canvas.ClipRect({0, 0, 50, 50});
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	canvas.Restore();
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// Then
	const std::vector<Op> ops = Operations(*list);
	ASSERT_EQ(ops.size(), 4u);
	EXPECT_EQ(ops[0].ClipIndex(), ops[1].ClipIndex());
	EXPECT_NE(ops[1].ClipIndex(), ops[2].ClipIndex());
	EXPECT_EQ(ClipOf(*list, ops[2]), (std::vector<IntRect>{{0, 0, 50, 50}}));
	EXPECT_EQ(ClipOf(*list, ops[3]), (std::vector<IntRect>{{0, 0, 100, 100}}));
}


TEST(CanvasTest, EmptyClipDropsDrawingUntilRestore)
{
	// What is tested: Drawing after ClipRect() with a rectangle outside the
	// bounds, then after Restore().
	// Success criteria: Nothing is recorded under the empty clip, and it does
	// not count as discarded; after Restore() drawing is recorded again.
	// Why it matters: Specification 7: with an empty clip region the frontend
	// drops all drawing operations until the matching Restore.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});

	// When
	canvas.Save();
	canvas.ClipRect({200, 200, 300, 300});
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	canvas.DrawImage(1, {0, 0, 1, 1}, {0, 0, 1, 1});
	canvas.Restore();
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// Then
	EXPECT_EQ(list->OpCount(), 1u);
	EXPECT_EQ(canvas.DiscardedOperations(), 0u);
}


TEST(CanvasTest, InvalidInputIsDropped)
{
	// What is tested: Every drawing operation with an invalid argument: NaN
	// geometry, an invalid paint, a negative width, too few points, an empty
	// span, image handle 0, alpha out of range, a glyph run without glyphs,
	// a missing child list, and a NaN translation.
	// Success criteria: Nothing is recorded, every case counts as discarded,
	// and the Canvas keeps working.
	// Why it matters: Specification 6.2 drops invalid operations
	// deterministically, independent of the backend; the list must stay what
	// DisplayList::Deserialize() accepts.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});
	const Paint bad = {.color = {2, 0, 0, 1}};
	const std::array<Point, 2> two = {{{0, 0}, {1, 1}}};
	const std::array<Point, 1> one = {{{0, 0}}};
	const std::array<std::uint32_t, 1> glyph = {1};

	// When
	canvas.FillRect({0, 0, kNan, 1}, Paint{});
	canvas.FillRect({0, 0, 1, 1}, bad);
	canvas.FillRRect({{0, 0, 10, 10}, {kNan, 0, 0, 0}}, Paint{});
	canvas.FillRing({{0, 0, 10, 10}, {}}, {{1, 1, kNan, 9}, {}}, Paint{});
	canvas.FillEllipse({0, 0, 10, 10}, Paint{.strokeWidth = -1});
	canvas.FillConvexPolygon(two, Paint{});
	canvas.StrokeRect({kNan, 0, 1, 1}, Paint{});
	canvas.StrokeRRect({{0, 0, 10, 10}, {}}, bad);
	canvas.StrokePolyline(one, false, Paint{});
	canvas.StrokeArc({0, 0, 10, 10}, kNan, 90, Paint{});
	canvas.DrawLines({}, 1, Paint::Cap::Butt);
	canvas.DrawImage(0, {0, 0, 1, 1}, {0, 0, 1, 1});
	canvas.DrawImage(1, {0, 0, 1, 1}, {0, 0, 1, 1}, 1.5f);
	canvas.DrawGlyphRun({1, 12, {}, {}, {}, {}}, {0, 0}, Paint{});
	canvas.DrawGlyphRun({1, 12, glyph, {}, {}, {}}, {0, 0}, Paint{});
	canvas.DrawList(nullptr, {0, 0});
	canvas.Translate(kNan, 0);
	canvas.FillRect({0, 0, 1, 1}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// Then
	EXPECT_EQ(canvas.DiscardedOperations(), 17u);
	ASSERT_EQ(list->OpCount(), 1u);
	EXPECT_EQ(Operations(*list)[0].As<RectRecord>().rect, (Rect{0, 0, 1, 1}));
}


TEST(CanvasTest, EmptyRectanglesHaveNoEffect)
{
	// What is tested: Fill and stroke operations on empty and inverted
	// rectangles with otherwise valid arguments.
	// Success criteria: Nothing is recorded, and nothing counts as discarded.
	// Why it matters: Specification 4.1: operations on an empty rectangle
	// have no effect. They are valid calls, not errors.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});

	// When
	canvas.FillRect({5, 5, 5, 10}, Paint{});
	canvas.FillRRect({{5, 5, 1, 1}, Radii::Uniform(2)}, Paint{});
	canvas.FillEllipse({0, 0, 0, 0}, Paint{});
	canvas.StrokeRect({3, 3, 3, 3}, Paint{});
	canvas.StrokeArc({0, 0, 10, 0}, 0, 90, Paint{});
	canvas.DrawImage(1, {0, 0, 1, 1}, {4, 4, 4, 9});
	const DisplayListRef list = canvas.Finish();

	// Then
	EXPECT_EQ(list->OpCount(), 0u);
	EXPECT_EQ(canvas.DiscardedOperations(), 0u);
}


TEST(CanvasTest, GradientsAndRadiiAreRecordedReadyToDraw)
{
	// What is tested: FillRect() with a gradient under a translation, and
	// FillRRect() with oversized radii.
	// Success criteria: The gradient points are translated like the
	// rectangle; the radii are recorded normalized.
	// Why it matters: Gradient points are given in Canvas coordinates before
	// translation (specification 8), the list holds absolute coordinates;
	// normalizing once means every backend draws the same radii.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});
	const std::array<GradientStop, 2> stops = {{{0, kRed}, {1, {0, 0, 1, 1}}}};
	const Paint gradient = {.gradient = *LinearGradient::Create({0, 0}, {0, 10}, stops)};

	// When
	canvas.Translate(5, 7);
	canvas.FillRect({0, 0, 10, 10}, gradient);
	canvas.FillRRect({{0, 0, 40, 10}, Radii::Uniform(20)}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// Then
	const std::vector<Op> ops = Operations(*list);
	ASSERT_EQ(ops.size(), 2u);
	const Paint recorded = ops[0].As<RectRecord>().paint.ToPaint();
	ASSERT_TRUE(recorded.gradient.has_value());
	EXPECT_EQ(recorded.gradient->p0, (Point{5, 7}));
	EXPECT_EQ(recorded.gradient->p1, (Point{5, 17}));
	EXPECT_EQ(ops[1].As<RRectRecord>().rrect.radii, Radii::Uniform(5));
}


TEST(CanvasTest, GlyphRunsAreCopied)
{
	// What is tested: DrawGlyphRun() with text and clusters, a change of the
	// caller's arrays afterwards, and SetRecordGlyphText(false).
	// Success criteria: The list keeps the glyphs as they were when drawn;
	// with text recording off, text and clusters are left out; the origin is
	// translated and the paint's color is used.
	// Why it matters: Specification 10 copies the spans, so the caller need
	// not keep them alive, and lets the Canvas drop the optional text.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});
	std::array<std::uint32_t, 2> glyphs = {10, 20};
	const std::array<Point, 2> positions = {{{0, 0}, {6, 0}}};
	const std::array<std::uint32_t, 2> clusters = {0, 1};
	const GlyphRun run = {4, 11, glyphs, positions, u8"ab", clusters};

	// When
	canvas.Translate(1, 2);
	canvas.DrawGlyphRun(run, {10, 20}, Paint{.color = kRed});
	glyphs[0] = 99;
	canvas.SetRecordGlyphText(false);
	canvas.DrawGlyphRun(run, {10, 20}, Paint{.color = kRed});
	const DisplayListRef list = canvas.Finish();

	// Then
	const std::vector<Op> ops = Operations(*list);
	ASSERT_EQ(ops.size(), 2u);
	EXPECT_EQ(ops[0].Glyphs()[0], 10u);
	EXPECT_EQ(ops[0].Clusters().size(), 2u);
	EXPECT_EQ(ops[0].As<GlyphRunRecord>().origin, (Point{11, 22}));
	EXPECT_EQ(ops[0].As<GlyphRunRecord>().color, kRed);
	EXPECT_EQ(ops[1].Glyphs()[0], 99u);
	EXPECT_TRUE(ops[1].Text().empty());
	EXPECT_TRUE(ops[1].Clusters().empty());
	EXPECT_EQ(list->Fonts().size(), 1u);
}


TEST(CanvasTest, NestedListsAndFinish)
{
	// What is tested: DrawList() of a finished child list under a
	// translation, Finish() with an open Save(), and the Canvas after
	// Finish().
	// Success criteria: The child is referenced with the translated offset;
	// Finish() succeeds despite the open Save(); afterwards nothing is
	// recorded and a second Finish() returns nullptr.
	// Why it matters: Windows compose the lists of their views with DrawList
	// (specification 11.4); Finish() closes open Saves implicitly (6.1).
	//
	// Given
	Canvas childCanvas({0, 0, 10, 10});
	childCanvas.FillRect({0, 0, 10, 10}, Paint{});
	const DisplayListRef child = childCanvas.Finish();
	Canvas canvas({0, 0, 100, 100});

	// When
	canvas.Save();
	canvas.Translate(30, 40);
	canvas.DrawList(child, {5, 5});
	const DisplayListRef list = canvas.Finish();
	canvas.FillRect({0, 0, 1, 1}, Paint{});

	// Then
	ASSERT_NE(list, nullptr);
	ASSERT_EQ(list->Children().size(), 1u);
	EXPECT_EQ(list->Children()[0], child);
	EXPECT_EQ(Operations(*list)[0].As<ListRecord>().offset, (Point{35, 45}));
	EXPECT_EQ(canvas.Finish(), nullptr);
	EXPECT_EQ(list->OpCount(), 1u);
}


TEST(CanvasTest, RecordedListsDeserialize)
{
	// What is tested: A list recorded with one operation of every kind,
	// serialized and deserialized.
	// Success criteria: Deserialize() accepts it and gives the same bytes
	// back.
	// Why it matters: The Canvas and DisplayList::Deserialize() must agree on
	// what is valid; the fuzz target Canvas checks the same for arbitrary
	// calls.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});
	const std::array<Point, 3> triangle = {{{0, 0}, {10, 0}, {5, 8}}};
	const std::array<Line, 1> lines = {{{{0, 0}, {10, 0}, kRed}}};
	const std::array<std::uint32_t, 1> glyph = {1};
	const std::array<Point, 1> position = {{{0, 0}}};
	Canvas childCanvas({0, 0, 5, 5});
	const DisplayListRef child = childCanvas.Finish();

	// When
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	canvas.FillRRect({{0, 0, 10, 10}, Radii::Uniform(3)}, Paint{});
	canvas.FillRing({{0, 0, 10, 10}, {}}, {{1, 1, 9, 9}, {}}, Paint{});
	canvas.FillEllipse({0, 0, 10, 10}, Paint{});
	canvas.FillConvexPolygon(triangle, Paint{});
	canvas.StrokeRect({0, 0, 10, 10}, Paint{});
	canvas.StrokeRRect({{0, 0, 10, 10}, Radii::Uniform(3)}, Paint{});
	canvas.StrokePolyline(triangle, false, Paint{});
	canvas.StrokeArc({0, 0, 10, 10}, 0, 180, Paint{});
	canvas.DrawLines(lines, 1, Paint::Cap::Butt);
	canvas.DrawImage(3, {0, 0, 4, 4}, {0, 0, 8, 8}, 0.25f);
	canvas.DrawGlyphRun({1, 12, glyph, position, {}, {}}, {0, 0}, Paint{});
	canvas.DrawList(child, {0, 0});
	const std::vector<std::byte> bytes = canvas.Finish()->Serialize();

	// Then
	const auto restored = DisplayList::Deserialize(bytes);
	ASSERT_TRUE(restored.has_value()) << restored.error().message;
	EXPECT_EQ((*restored)->OpCount(), 13u);
	EXPECT_EQ((*restored)->Serialize(), bytes);
}


TEST(CanvasTest, RestoreWithoutSaveIsAProgrammingError)
{
	// What is tested: Restore() without a matching Save().
	// Success criteria: Debug builds stop with an assertion; release builds
	// ignore the call and keep recording.
	// Why it matters: Specification 6.1 treats it as a programming error;
	// release builds must still not corrupt the state stack.
	//
	// Given
	Canvas canvas({0, 0, 10, 10});

	// When / Then
	EXPECT_DEBUG_DEATH(canvas.Restore(), "without a matching Save");
	canvas.FillRect({0, 0, 1, 1}, Paint{});
	EXPECT_EQ(canvas.Finish()->OpCount(), 1u);
}


} // namespace
