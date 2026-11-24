/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Shape.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Polygon.h>


namespace {


using namespace ngui;


// Records the hooks it is called with, as text.
class RecordingIterator : public BShapeIterator {
public:
	std::vector<std::string> calls;

	void IterateMoveTo(gfx::Point point) override
	{
		calls.push_back("M" + Text(point));
	}

	void IterateLineTo(std::span<const gfx::Point> points) override
	{
		std::string call = "L";
		for (const gfx::Point& point : points)
			call += Text(point);
		calls.push_back(call);
	}

	void IterateBezierTo(std::span<const gfx::Point> points) override
	{
		std::string call = "C";
		for (const gfx::Point& point : points)
			call += Text(point);
		calls.push_back(call);
	}

	void IterateClose() override
	{
		calls.push_back("Z");
	}

	void IterateArcTo(float rx, float ry, float angle, bool largeArc, bool counterClockWise,
		gfx::Point point) override
	{
		calls.push_back("A" + std::to_string(static_cast<int>(rx)) + ","
			+ std::to_string(static_cast<int>(ry)) + "," + std::to_string(static_cast<int>(angle))
			+ (largeArc ? ",large" : ",small") + (counterClockWise ? ",ccw" : ",cw")
			+ Text(point));
	}

private:
	static std::string Text(gfx::Point point)
	{
		return "(" + std::to_string(static_cast<int>(point.x)) + " "
			+ std::to_string(static_cast<int>(point.y)) + ")";
	}
};


// The points of \a path flattened finely, all contours.
std::vector<gfx::Point>
Flattened(const gfx::Path& path)
{
	std::vector<gfx::Point> points;
	for (const gfx::Path::Polyline& line : path.Flatten(0.01f))
		points.insert(points.end(), line.points.begin(), line.points.end());
	return points;
}


TEST(ShapeTest, StepsAreGroupedAsInHaiku)
{
	// What is tested: A shape built with MoveTo(), three LineTo(), two
	// BezierTo() (one from an array), Close(), a second Close(), two ArcTo(),
	// MoveTo() twice and LineTo(); its iteration; CurrentPosition(); copies,
	// AddShape(), MoveFrom() and Clear().
	// Success criteria: Haiku's grouping: the lines after a MoveTo() come in
	// one IterateLineTo() of three points, the two curves in one
	// IterateBezierTo() of six; the second Close() is dropped; each arc is
	// its own IterateArcTo() with its radii, angle and flags; a MoveTo()
	// right after a MoveTo() replaces it. The current position is the last
	// point. A copy is equal; AddShape() appends; MoveFrom() empties the
	// other shape; Clear() leaves nothing.
	// Why it matters: Haiku code walks shapes with BShapeIterator (Icon-O-
	// Matic, WebPositive); the steps must come as in Haiku.
	//
	// Given
	BShape shape;
	const gfx::Point curve[3] = {{40, 0}, {50, 10}, {40, 20}};

	// When
	shape.MoveTo({0, 0});
	shape.LineTo({10, 0});
	shape.LineTo({10, 10});
	shape.LineTo({0, 10});
	shape.BezierTo({20, 0}, {30, 10}, {20, 20});
	shape.BezierTo(curve);
	shape.Close();
	shape.Close();
	shape.ArcTo(5, 6, 1, true, false, {60, 0});
	shape.ArcTo(7, 8, 0, false, true, {70, 0});
	shape.MoveTo({90, 90});
	shape.MoveTo({100, 100});
	shape.LineTo({110, 100});
	RecordingIterator iterator;
	iterator.Iterate(shape);
	const gfx::Point current = shape.CurrentPosition();
	BShape copy(shape);
	BShape appended;
	appended.MoveTo({1, 1});
	appended.AddShape(shape);
	BShape moved;
	moved.MoveFrom(copy);

	// Then
	EXPECT_EQ(iterator.calls, (std::vector<std::string>{"M(0 0)", "L(10 0)(10 10)(0 10)",
		"C(20 0)(30 10)(20 20)(40 0)(50 10)(40 20)", "Z", "A5,6,1,large,cw(60 0)",
		"A7,8,0,small,ccw(70 0)", "M(100 100)", "L(110 100)"}));
	EXPECT_EQ(current, (gfx::Point{110, 100}));
	EXPECT_EQ(moved, shape);
	EXPECT_EQ(copy, BShape());
	EXPECT_NE(appended, shape);
	RecordingIterator appendedCalls;
	appendedCalls.Iterate(appended);
	EXPECT_EQ(appendedCalls.calls.size(), iterator.calls.size() + 1);
	shape.Clear();
	EXPECT_EQ(shape, BShape());
	EXPECT_EQ(shape.CurrentPosition(), (gfx::Point{0, 0}));
	EXPECT_EQ(shape.Bounds(), (gfx::Rect{0, 0, 0, 0}));
}


TEST(ShapeTest, ArcsBecomeBezierCurves)
{
	// What is tested: Arcs from (0, 0) to (20, 0) of radius 10, clockwise
	// and counterclockwise; the small and the large arc of radius 20; one of
	// radius 1 (too small); one of radius 0; one to its own start; an
	// ellipse of radii 10 and 5 turned by a quarter turn from (0, 0) to
	// (0, 20); an arc as the first step; the bounds of a shape with an arc.
	// Success criteria: As SVG's elliptical arcs (AGG's arc_to() in Haiku's
	// app_server): the clockwise half circle passes the top (y −10), the
	// counterclockwise one the bottom, all points 10 from the center
	// (10, 0). The large arc of radius 20 reaches far beyond the small one
	// (y +37.3 against y −2.7). The too small radius is scaled up to a half
	// circle; radius 0 is a straight line; an arc to its own start adds
	// nothing. The turned ellipse bulges 5 sideways. A first arc only moves
	// to its end. Bounds() covers the curve's control points, not the arc's
	// radii (Haiku counted them as points).
	// Why it matters: Haiku's shapes keep arcs as parameters; they must
	// look the same when drawn as Bézier curves.
	//
	// Given
	const auto arc = [](float rx, float ry, float angle, bool large, bool ccw, gfx::Point to) {
		BShape shape;
		shape.MoveTo({0, 0});
		shape.ArcTo(rx, ry, angle, large, ccw, to);
		return shape.ToPath();
	};
	const auto extremeY = [](const gfx::Path& path, bool lowest) {
		const std::vector<gfx::Point> points = Flattened(path);
		float y = points.front().y;
		for (const gfx::Point& point : points)
			y = lowest ? std::min(y, point.y) : std::max(y, point.y);
		return y;
	};

	// When
	const gfx::Path clockwise = arc(10, 10, 0, false, false, {20, 0});
	const gfx::Path counter = arc(10, 10, 0, false, true, {20, 0});
	const gfx::Path small = arc(20, 20, 0, false, true, {20, 0});
	const gfx::Path large = arc(20, 20, 0, true, true, {20, 0});
	const gfx::Path tooSmall = arc(1, 1, 0, false, false, {20, 0});
	const gfx::Path zero = arc(0, 5, 0, false, false, {20, 0});
	const gfx::Path same = arc(10, 10, 0, false, false, {0, 0});
	const gfx::Path turned = arc(10, 5, std::numbers::pi_v<float> / 2, false, false, {0, 20});
	BShape first;
	first.ArcTo(10, 10, 0, false, false, {20, 0});
	BShape bounded;
	bounded.MoveTo({0, 0});
	bounded.ArcTo(500, 500, 0, false, false, {20, 0});

	// Then
	EXPECT_NEAR(extremeY(clockwise, true), -10, 0.01f);
	EXPECT_NEAR(extremeY(clockwise, false), 0, 0.01f);
	EXPECT_NEAR(extremeY(counter, false), 10, 0.01f);
	for (const gfx::Point& point : Flattened(clockwise))
		EXPECT_NEAR(std::hypot(point.x - 10, point.y), 10, 0.02f);
	EXPECT_NEAR(extremeY(small, false), 2.68f, 0.02f);
	EXPECT_NEAR(extremeY(large, false), 37.32f, 0.02f);
	EXPECT_NEAR(extremeY(tooSmall, true), -10, 0.01f);
	EXPECT_EQ(std::vector<gfx::PathVerb>(zero.Verbs().begin(), zero.Verbs().end()),
		(std::vector<gfx::PathVerb>{gfx::PathVerb::Move, gfx::PathVerb::Line}));
	EXPECT_EQ(same.Verbs().size(), 1u);
	float sideways = 0;
	for (const gfx::Point& point : Flattened(turned))
		sideways = std::max(sideways, std::fabs(point.x));
	EXPECT_NEAR(sideways, 5, 0.01f);
	EXPECT_EQ(Flattened(turned).back(), (gfx::Point{0, 20}));
	EXPECT_EQ(first.ToPath().Points().size(), 1u);
	EXPECT_EQ(first.ToPath().Points()[0], (gfx::Point{20, 0}));
	EXPECT_LT(bounded.Bounds().Height(), 10);
	EXPECT_EQ(bounded.Bounds().x0, 0);
}


TEST(PolygonTest, PointsFrameMappingAndPath)
{
	// What is tested: A polygon of three points, one more added; an empty
	// span added; its frame; MapTo() from (0, 0, 10, 10) to (100, 50, 120,
	// 60), and from a source of no height; ToPath() closed and open; a
	// polygon of one point.
	// Success criteria: Four points in order, the frame around them. MapTo()
	// scales by 2 and 1 and moves by (100, 50); an axis of no source size
	// maps to the destination's start. ToPath() is a closed or an open
	// contour of the points; one point gives no path.
	// Why it matters: Haiku programs draw polygons (FillPolygon()); here
	// through the Canvas as paths.
	//
	// Given
	const std::vector<gfx::Point> three = {{0, 0}, {10, 0}, {10, 10}};
	const std::vector<gfx::Point> fourth = {{0, 10}};
	BPolygon polygon(three);

	// When
	polygon.AddPoints(fourth);
	polygon.AddPoints({});
	const gfx::Rect frame = polygon.Frame();
	BPolygon mapped = polygon;
	mapped.MapTo({0, 0, 10, 10}, {100, 50, 120, 60});
	BPolygon flat = polygon;
	flat.MapTo({0, 5, 10, 5}, {0, 30, 10, 40});
	const gfx::Path closed = polygon.ToPath();
	const gfx::Path open = polygon.ToPath(false);
	const BPolygon single(fourth);

	// Then
	EXPECT_EQ(polygon.CountPoints(), 4);
	EXPECT_EQ(polygon.Points()[3], (gfx::Point{0, 10}));
	EXPECT_EQ(frame, (gfx::Rect{0, 0, 10, 10}));
	EXPECT_EQ(mapped.Points()[2], (gfx::Point{120, 60}));
	EXPECT_EQ(mapped.Frame(), (gfx::Rect{100, 50, 120, 60}));
	EXPECT_EQ(flat.Points()[2], (gfx::Point{10, 30}));
	EXPECT_EQ(closed.Verbs().back(), gfx::PathVerb::Close);
	EXPECT_EQ(closed.Points().size(), 4u);
	EXPECT_EQ(open.Verbs().back(), gfx::PathVerb::Line);
	EXPECT_TRUE(single.ToPath().IsEmpty());
	EXPECT_EQ(BPolygon().Frame(), (gfx::Rect{0, 0, 0, 0}));
	EXPECT_EQ(BPolygon(three), BPolygon(three));
}


} // namespace
