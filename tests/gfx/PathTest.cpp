/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Path.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <string>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Dump.h>


namespace {


using namespace ngui::gfx;


TEST(PathTest, ContoursAreBuiltAsInSkia)
{
	// What is tested: A path of two MoveTo() in a row, a LineTo(), a
	// CubicTo() and Close(); then a LineTo() after Close() and a second
	// Close(); a path whose first step is LineTo(); Clear(); the fill rule;
	// Translate() and the bounds.
	// Success criteria: The second MoveTo() replaces the first; a LineTo()
	// after Close() starts a new contour where the last one started (Skia's
	// injected moveTo), as a LineTo() of an empty path does at (0, 0); a
	// second Close() adds nothing. The verbs and their points (one, three or
	// none) follow in order. Bounds cover all points, control points
	// included; Translate() moves them all. Clear() keeps the fill rule.
	// Why it matters: BShape and the DisplayList rely on one shape of
	// contours; backends get no surprises.
	//
	// Given
	Path path;

	// When
	path.MoveTo({5, 5});
	path.MoveTo({1, 2});
	path.LineTo({10, 2});
	path.CubicTo({12, 0}, {14, 8}, {10, 12});
	path.Close();
	path.LineTo({0, 20});
	path.Close();
	path.Close();
	Path implicit;
	implicit.LineTo({3, 4});
	path.SetFillRule(FillRule::EvenOdd);
	const Rect bounds = path.Bounds();
	Path moved = path;
	moved.Translate(10, -2);

	// Then
	using V = PathVerb;
	EXPECT_EQ(std::vector<PathVerb>(path.Verbs().begin(), path.Verbs().end()),
		(std::vector<PathVerb>{V::Move, V::Line, V::Cubic, V::Close, V::Move, V::Line,
			V::Close}));
	EXPECT_EQ(std::vector<Point>(path.Points().begin(), path.Points().end()),
		(std::vector<Point>{{1, 2}, {10, 2}, {12, 0}, {14, 8}, {10, 12}, {1, 2}, {0, 20}}));
	EXPECT_EQ(std::vector<PathVerb>(implicit.Verbs().begin(), implicit.Verbs().end()),
		(std::vector<PathVerb>{V::Move, V::Line}));
	EXPECT_EQ(implicit.Points()[0], (Point{0, 0}));
	EXPECT_EQ(bounds, (Rect{0, 0, 14, 20}));
	EXPECT_EQ(moved.Bounds(), (Rect{10, -2, 24, 18}));
	EXPECT_TRUE(Path::IsValidSequence(path.Verbs(), path.Points().size()));
	EXPECT_FALSE(Path::IsValidSequence(path.Verbs(), path.Points().size() - 1));
	const std::vector<PathVerb> startsWithLine = {V::Line};
	EXPECT_FALSE(Path::IsValidSequence(startsWithLine, 1));
	const std::vector<PathVerb> unknown = {V::Move, static_cast<PathVerb>(7)};
	EXPECT_FALSE(Path::IsValidSequence(unknown, 1));
	path.Clear();
	EXPECT_TRUE(path.IsEmpty());
	EXPECT_EQ(path.Rule(), FillRule::EvenOdd);
	EXPECT_EQ(path.Bounds(), (Rect{0, 0, 0, 0}));
}


TEST(PathTest, FlatteningStaysWithinTheTolerance)
{
	// What is tested: A quarter circle as one cubic (control points at
	// 0.5523 of the radius 100) flattened with tolerances 1 and 0.1; a
	// closed triangle; a lone MoveTo(); a degenerate curve.
	// Success criteria: Every point of the flattened curve lies within the
	// tolerance of the circle (the cubic deviates by 0.03 at most); the
	// finer tolerance gives more points, ends at the curve's end and starts
	// at its start. The triangle is one closed polyline of its three
	// corners; the lone MoveTo() gives no polyline; a curve with all points
	// equal gives one line.
	// Why it matters: Backends without Bézier paths draw the flattened form
	// (Caps::bezierPaths); it must look like the curve.
	//
	// Given
	constexpr float kC = 55.22847f;
	Path arc;
	arc.MoveTo({100, 0});
	arc.CubicTo({100, kC}, {kC, 100}, {0, 100});
	Path triangle;
	triangle.MoveTo({0, 0});
	triangle.LineTo({10, 0});
	triangle.LineTo({5, 8});
	triangle.Close();
	Path lone;
	lone.MoveTo({1, 1});
	Path dot;
	dot.MoveTo({3, 3});
	dot.CubicTo({3, 3}, {3, 3}, {3, 3});

	// When
	const auto coarse = arc.Flatten(1);
	const auto fine = arc.Flatten(0.1f);
	const auto corners = triangle.Flatten(0.25f);

	// Then
	ASSERT_EQ(coarse.size(), 1u);
	ASSERT_EQ(fine.size(), 1u);
	EXPECT_GT(fine[0].points.size(), coarse[0].points.size());
	EXPECT_EQ(fine[0].points.front(), (Point{100, 0}));
	EXPECT_EQ(fine[0].points.back(), (Point{0, 100}));
	EXPECT_FALSE(fine[0].closed);
	for (const auto* flattened : {&coarse, &fine}) {
		const float tolerance = flattened == &coarse ? 1.0f : 0.1f;
		const std::vector<Point>& points = (*flattened)[0].points;
		for (std::size_t i = 1; i < points.size(); i++) {
			// the middle of each line is within the tolerance of the circle
			const Point middle = {(points[i - 1].x + points[i].x) / 2,
				(points[i - 1].y + points[i].y) / 2};
			EXPECT_NEAR(std::hypot(middle.x, middle.y), 100, tolerance + 0.03f);
		}
	}
	ASSERT_EQ(corners.size(), 1u);
	EXPECT_TRUE(corners[0].closed);
	EXPECT_EQ(corners[0].points, (std::vector<Point>{{0, 0}, {10, 0}, {5, 8}}));
	EXPECT_TRUE(lone.Flatten(1).empty());
	ASSERT_EQ(dot.Flatten(1).size(), 1u);
	EXPECT_EQ(dot.Flatten(1)[0].points.size(), 2u);
}


TEST(PathTest, PathsAreRecordedSerializedAndDumped)
{
	// What is tested: A Canvas translated by (10, 20) recording FillPath()
	// of a square with an even-odd hole and a curve, StrokePath() of the
	// same path; an empty path and one with NaN; the list serialized,
	// deserialized and dumped; serialized bytes damaged in the verbs, the
	// fill rule of the stroke and the padding.
	// Success criteria: Both operations are recorded with the translation
	// applied, the fill rule kept for the fill and 0 for the stroke; the
	// empty path is dropped silently, the NaN one counted as discarded. The
	// list survives its binary form unchanged, and ToPath() gives the path
	// back. The text dump shows the verbs as SVG letters. A damaged verb, a
	// fill rule on a stroke and non-zero padding are rejected as BadData.
	// Why it matters: Paths are extended profile operations of the
	// DisplayList; its binary form is a boundary for data from outside.
	//
	// Given
	Path path;
	path.SetFillRule(FillRule::EvenOdd);
	path.MoveTo({0, 0});
	path.LineTo({10, 0});
	path.CubicTo({12, 5}, {12, 5}, {10, 10});
	path.LineTo({0, 10});
	path.Close();
	path.MoveTo({3, 3});
	path.LineTo({6, 3});
	path.LineTo({6, 6});
	path.Close();
	Path broken;
	broken.MoveTo({0, std::numeric_limits<float>::quiet_NaN()});
	broken.LineTo({1, 1});
	Canvas canvas({0, 0, 100, 100});
	canvas.Translate(10, 20);

	// When
	canvas.FillPath(path, Paint{});
	canvas.StrokePath(path, Paint{.strokeWidth = 2});
	canvas.FillPath(Path(), Paint{});
	canvas.StrokePath(broken, Paint{});
	const std::uint32_t discarded = canvas.DiscardedOperations();
	const DisplayListRef list = canvas.Finish();
	const std::vector<std::byte> bytes = list->Serialize();
	const auto restored = DisplayList::Deserialize(bytes);
	const std::string text = DumpText(*list);
	// A copy of the bytes with one byte of the first operation's payload
	// changed, deserialized.
	const auto damaged = [&](std::size_t offsetInPayload, std::byte value) {
		std::vector<std::byte> copy = bytes;
		// the payload in the serialized bytes, found by its content
		const std::span<const std::byte> payload = (*list->Ops().begin()).Payload();
		const auto found = std::search(copy.begin(), copy.end(), payload.begin(),
			payload.end());
		EXPECT_NE(found, copy.end());
		*(found + static_cast<std::ptrdiff_t>(offsetInPayload)) = value;
		return DisplayList::Deserialize(copy);
	};

	// Then
	EXPECT_EQ(discarded, 1u);
	ASSERT_EQ(list->OpCount(), 2u);
	std::vector<Op> ops(list->Ops().begin(), list->Ops().end());
	EXPECT_EQ(ops[0].Code(), OpCode::FillPath);
	EXPECT_EQ(ops[1].Code(), OpCode::StrokePath);
	EXPECT_EQ(ops[0].As<PathRecord>().fillRule, 1);
	EXPECT_EQ(ops[1].As<PathRecord>().fillRule, 0);
	Path expected = path;
	expected.Translate(10, 20);
	EXPECT_EQ(ops[0].ToPath().Points().size(), expected.Points().size());
	EXPECT_EQ(std::vector<Point>(ops[0].PathPoints().begin(), ops[0].PathPoints().end()),
		std::vector<Point>(expected.Points().begin(), expected.Points().end()));
	EXPECT_EQ(ops[0].ToPath().Rule(), FillRule::EvenOdd);
	ASSERT_TRUE(restored.has_value());
	EXPECT_EQ((*restored)->Serialize(), bytes);
	EXPECT_NE(text.find("FillPath path=M(10 20)L(20 20)C(22 25)(22 25)(20 30)L(10 30)Z"
		"M(13 23)L(16 23)L(16 26)Z rule=evenodd"), std::string::npos) << text;
	EXPECT_NE(text.find("StrokePath path=M(10 20)"), std::string::npos) << text;
	const std::size_t verbsAt = sizeof(PathRecord);
	const std::size_t ruleAt = sizeof(PaintRecord) + 8;
	EXPECT_FALSE(damaged(verbsAt, std::byte{7}).has_value());
	EXPECT_FALSE(damaged(verbsAt, std::byte{1}).has_value());
	EXPECT_FALSE(damaged(ruleAt, std::byte{2}).has_value());
	// 10 verbs: two bytes of padding after them
	EXPECT_FALSE(damaged(verbsAt + 10, std::byte{1}).has_value());
}


} // namespace
