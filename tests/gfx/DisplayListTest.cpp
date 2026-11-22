/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/DisplayList.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "gfx/DisplayListBuilder.h"


namespace {


using namespace ngui::gfx;


constexpr Color kRed = {1, 0, 0, 1};
constexpr Color kBlue = {0, 0, 1, 1};


Region
FullClip()
{
	return Region(IntRect{0, 0, 100, 100});
}


std::vector<OpCode>
Codes(const DisplayList& list)
{
	std::vector<OpCode> codes;
	for (const Op& op : list.Ops())
		codes.push_back(op.Code());
	return codes;
}


// A list with one operation of every kind, a gradient, two clips and a
// nested list, for the round-trip and rejection tests.
DisplayListRef
EveryOperation()
{
	DisplayListBuilder childBuilder({0, 0, 10, 10});
	const std::uint32_t childClip = childBuilder.AddClip(Region(IntRect{0, 0, 10, 10}));
	childBuilder.FillRect(childClip, {1, 1, 9, 9}, Paint{.color = kBlue});
	const DisplayListRef child = childBuilder.Finish();

	const std::array<GradientStop, 2> stops = {{{0, kRed}, {1, kBlue}}};
	const Paint gradient = {.gradient = *LinearGradient::Create({0, 0}, {0, 10}, stops)};
	const std::array<Point, 3> triangle = {{{0, 0}, {10, 0}, {5, 8}}};
	const std::array<Line, 2> lines = {{{{0, 0}, {10, 0}, kRed}, {{0, 5}, {10, 5}, kBlue}}};
	const std::array<std::uint32_t, 2> glyphs = {36, 72};
	const std::array<Point, 2> positions = {{{0, 0}, {7, 0}}};
	const std::array<std::uint32_t, 2> clusters = {0, 1};

	DisplayListBuilder builder({0, 0, 100, 100});
	const std::uint32_t full = builder.AddClip(FullClip());
	Region holed = FullClip();
	holed.Exclude(IntRect{40, 40, 60, 60});
	const std::uint32_t hole = builder.AddClip(holed);

	builder.FillRect(full, {0, 0, 50, 50}, gradient);
	builder.FillRRect(full, {{0, 0, 30, 20}, Radii::Uniform(4)}, Paint{.color = kRed});
	builder.FillRing(full, {{0, 0, 30, 20}, Radii::Uniform(4)},
		{{1, 1, 29, 19}, Radii::Uniform(3)}, Paint{});
	builder.FillEllipse(hole, {10, 10, 90, 90}, Paint{.antialias = false});
	builder.FillConvexPolygon(hole, triangle, Paint{});
	builder.StrokeRect(hole, {5, 5, 95, 95}, Paint{.strokeWidth = 2});
	builder.StrokeRRect(full, {{0, 0, 30, 20}, Radii::Uniform(4)}, Paint{});
	builder.StrokePolyline(full, triangle, true,
		Paint{.cap = Paint::Cap::Round, .join = Paint::Join::Bevel});
	builder.StrokeArc(full, {0, 0, 20, 20}, 0, 270, Paint{});
	builder.DrawLines(full, lines, 1, Paint::Cap::Square);
	builder.DrawImage(full, 7, {0, 0, 16, 16}, {20, 20, 36, 36}, 0.5f);
	builder.DrawGlyphRun(full, {3, 12, glyphs, positions, u8"Hi", clusters}, {10, 50}, kRed);
	builder.DrawList(hole, child, {50, 50});
	return builder.Finish();
}


std::vector<std::byte>
EveryOperationBytes()
{
	return EveryOperation()->Serialize();
}


TEST(DisplayListTest, RecordsOperationsInOrder)
{
	// What is tested: Recording one operation of every kind and reading them
	// back with the iterator and the typed accessors.
	// Success criteria: The opcodes come in recording order; records, points,
	// lines and glyph data are exactly what was recorded, including the
	// gradient of the paint.
	// Why it matters: Backends render by iterating the list (specification
	// 11.2); every operation must arrive with its exact arguments.
	//
	// When
	const DisplayListRef list = EveryOperation();

	// Then
	EXPECT_EQ(list->OpCount(), 13u);
	EXPECT_EQ(list->Bounds(), (Rect{0, 0, 100, 100}));
	EXPECT_EQ(Codes(*list), (std::vector<OpCode>{OpCode::FillRect, OpCode::FillRRect,
		OpCode::FillRing, OpCode::FillEllipse, OpCode::FillConvexPolygon, OpCode::StrokeRect,
		OpCode::StrokeRRect, OpCode::StrokePolyline, OpCode::StrokeArc, OpCode::DrawLines,
		OpCode::DrawImage, OpCode::DrawGlyphRun, OpCode::DrawList}));

	for (const Op& op : list->Ops()) {
		switch (op.Code()) {
			case OpCode::FillRect:
			{
				const Paint paint = op.As<RectRecord>().paint.ToPaint();
				ASSERT_TRUE(paint.gradient.has_value());
				EXPECT_EQ(paint.gradient->Stops()[1].color, kBlue);
				EXPECT_EQ(op.As<RectRecord>().rect, (Rect{0, 0, 50, 50}));
				break;
			}
			case OpCode::FillConvexPolygon:
				EXPECT_EQ(op.Points().size(), 3u);
				EXPECT_EQ(op.Points()[2], (Point{5, 8}));
				EXPECT_EQ(op.ClipIndex(), 1u);
				break;
			case OpCode::StrokePolyline:
				EXPECT_EQ(op.As<PointsRecord>().closed, 1u);
				EXPECT_EQ(op.As<PointsRecord>().paint.ToPaint().join, Paint::Join::Bevel);
				break;
			case OpCode::DrawLines:
				ASSERT_EQ(op.Lines().size(), 2u);
				EXPECT_EQ(op.Lines()[1].color, kBlue);
				EXPECT_EQ(op.As<LinesRecord>().cap, std::uint8_t(Paint::Cap::Square));
				break;
			case OpCode::DrawGlyphRun:
				EXPECT_EQ(op.Glyphs()[1], 72u);
				EXPECT_EQ(op.Positions()[1], (Point{7, 0}));
				EXPECT_EQ(op.Clusters()[1], 1u);
				// Compared as bool: vcpkg builds GoogleTest in an older
				// language mode, without a printer for char8_t strings.
				EXPECT_TRUE(op.Text() == u8"Hi");
				EXPECT_EQ(op.As<GlyphRunRecord>().origin, (Point{10, 50}));
				break;
			case OpCode::DrawList:
				EXPECT_EQ(op.As<ListRecord>().child, 0u);
				EXPECT_EQ(op.As<ListRecord>().offset, (Point{50, 50}));
				break;
			default:
				EXPECT_EQ(op.Flags(), 0u);
				break;
		}
	}
}


TEST(DisplayListTest, SharesClipsAndResources)
{
	// What is tested: AddClip() with the same region twice in a row and with
	// a different one, and the resource lists for repeated images, fonts and
	// child lists.
	// Success criteria: Consecutive equal clips share one entry; images,
	// fonts and children are listed once each, in order of first use.
	// Why it matters: Specification 11.2 requires the shared clip entries;
	// the resource lists drive the reference counting of images and lists.
	//
	// Given
	DisplayListBuilder childBuilder({0, 0, 1, 1});
	const DisplayListRef child = childBuilder.Finish();
	const std::array<std::uint32_t, 1> glyph = {1};
	const std::array<Point, 1> position = {{{0, 0}}};
	DisplayListBuilder builder({0, 0, 100, 100});

	// When
	const std::uint32_t a = builder.AddClip(FullClip());
	const std::uint32_t b = builder.AddClip(FullClip());
	const std::uint32_t c = builder.AddClip(Region(IntRect{0, 0, 50, 50}));
	builder.DrawImage(a, 9, {0, 0, 1, 1}, {0, 0, 1, 1}, 1);
	builder.DrawImage(a, 4, {0, 0, 1, 1}, {0, 0, 1, 1}, 1);
	builder.DrawImage(a, 9, {0, 0, 1, 1}, {0, 0, 1, 1}, 1);
	builder.DrawGlyphRun(a, {5, 10, glyph, position, {}, {}}, {0, 0}, kRed);
	builder.DrawGlyphRun(a, {5, 10, glyph, position, {}, {}}, {0, 0}, kRed);
	builder.DrawList(a, child, {0, 0});
	builder.DrawList(c, child, {1, 1});
	const DisplayListRef list = builder.Finish();

	// Then
	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
	EXPECT_EQ(list->ClipCount(), 2u);
	EXPECT_EQ(list->Clip(c).size(), 1u);
	EXPECT_TRUE(list->Clip(99).empty());
	EXPECT_EQ(std::vector<ImageHandle>(list->Images().begin(), list->Images().end()),
		(std::vector<ImageHandle>{9, 4}));
	EXPECT_EQ(list->Fonts().size(), 1u);
	EXPECT_EQ(list->Children().size(), 1u);
}


TEST(DisplayListTest, SerializationRoundTrips)
{
	// What is tested: Serialize() and Deserialize() of a list with every
	// operation, two clips and a nested list.
	// Success criteria: Deserializing succeeds, serializing the result gives
	// the same bytes, and the operations and the child list are intact.
	// Why it matters: The binary form is used for remote rendering and test
	// snapshots (specification 11.3); it must be lossless and canonical.
	//
	// Given
	const std::vector<std::byte> bytes = EveryOperationBytes();

	// When
	const auto restored = DisplayList::Deserialize(bytes);

	// Then
	ASSERT_TRUE(restored.has_value()) << restored.error().message;
	EXPECT_EQ((*restored)->Serialize(), bytes);
	EXPECT_EQ(Codes(**restored), Codes(*EveryOperation()));
	ASSERT_EQ((*restored)->Children().size(), 1u);
	EXPECT_EQ((*restored)->Children()[0]->OpCount(), 1u);
	EXPECT_EQ((*restored)->Clip(1).size(), 4u);
}


TEST(DisplayListTest, DeserializeRejectsDamagedInput)
{
	// What is tested: Deserialize() with every truncation of a valid list, a
	// trailing byte, and with single fields made invalid: magic, version, a
	// NaN coordinate, a clip index, an image list.
	// Success criteria: Every case fails with ErrorCode::BadData.
	// Why it matters: A serialized list may come from outside (remote
	// rendering); damaged input must be rejected before a backend sees it.
	//
	// Given
	const std::vector<std::byte> bytes = EveryOperationBytes();
	const auto rejected = [](std::vector<std::byte> data) {
		const auto result = DisplayList::Deserialize(data);
		return !result.has_value() && result.error().code == ngui::ErrorCode::BadData;
	};
	const auto patched = [&bytes](std::size_t offset, auto value) {
		std::vector<std::byte> copy = bytes;
		std::memcpy(copy.data() + offset, &value, sizeof(value));
		return copy;
	};
	// The first operation starts behind the header (48 bytes) and the two
	// clips (4 + 16 and 4 + 4 * 16 bytes); its record behind its header.
	const std::size_t firstOp = 48 + 20 + 68;
	const std::size_t firstRect = firstOp + kOpHeaderSize;

	// When / Then
	for (std::size_t length = 0; length < bytes.size(); length++) {
		EXPECT_TRUE(rejected({bytes.begin(), bytes.begin() + std::ptrdiff_t(length)}))
			<< "length " << length;
	}
	std::vector<std::byte> longer = bytes;
	longer.push_back(std::byte{0});
	EXPECT_TRUE(rejected(longer));
	EXPECT_TRUE(rejected(patched(0, std::uint32_t{0x12345678})));
	EXPECT_TRUE(rejected(patched(4, std::uint16_t{2})));
	EXPECT_TRUE(rejected(patched(firstRect, std::numeric_limits<float>::quiet_NaN())));
	EXPECT_TRUE(rejected(patched(firstOp + 8, std::uint32_t{2})));
	EXPECT_TRUE(rejected(patched(36, std::uint32_t{0})));
	EXPECT_EQ(patched(firstOp, std::uint16_t{1}), bytes);
}


TEST(DisplayListTest, UnknownOpcodesAreKept)
{
	// What is tested: A serialized list with an operation whose opcode this
	// version does not know.
	// Success criteria: Deserialize() accepts it, the iterator yields it with
	// its payload, and Serialize() writes it back unchanged.
	// Why it matters: Specification 11.2 requires skipping unknown opcodes
	// (for instance from the extended profile) by their size, not rejecting
	// the list.
	//
	// Given
	DisplayListBuilder builder({0, 0, 10, 10});
	const std::uint32_t clip = builder.AddClip(Region(IntRect{0, 0, 10, 10}));
	builder.StrokeArc(clip, {0, 0, 10, 10}, 0, 90, Paint{});
	std::vector<std::byte> bytes = builder.Finish()->Serialize();
	// The StrokeArc operation starts behind the header and the clip.
	const std::uint16_t unknown = 999;
	std::memcpy(bytes.data() + 48 + 20, &unknown, sizeof(unknown));

	// When
	const auto list = DisplayList::Deserialize(bytes);

	// Then
	ASSERT_TRUE(list.has_value()) << list.error().message;
	const Op op = *(*list)->Ops().begin();
	EXPECT_EQ(static_cast<std::uint16_t>(op.Code()), 999u);
	EXPECT_EQ(op.Payload().size(), sizeof(ArcRecord));
	EXPECT_EQ((*list)->Serialize(), bytes);
}


TEST(DisplayListTest, NestingDepthIsLimited)
{
	// What is tested: Deserialize() of lists nested kMaxDepth and
	// kMaxDepth + 1 levels deep.
	// Success criteria: The first is accepted, the second rejected.
	// Why it matters: Deserialization recurses into child lists; without a
	// limit a crafted input could exhaust the stack.
	//
	// Given
	const auto nested = [](int levels) {
		DisplayListBuilder leaf({0, 0, 1, 1});
		DisplayListRef list = leaf.Finish();
		for (int i = 0; i < levels; i++) {
			DisplayListBuilder builder({0, 0, 1, 1});
			const std::uint32_t clip = builder.AddClip(Region(IntRect{0, 0, 1, 1}));
			builder.DrawList(clip, list, {0, 0});
			list = builder.Finish();
		}
		return list->Serialize();
	};

	// When / Then
	EXPECT_TRUE(DisplayList::Deserialize(nested(DisplayList::kMaxDepth)).has_value());
	EXPECT_FALSE(DisplayList::Deserialize(nested(DisplayList::kMaxDepth + 1)).has_value());
}


} // namespace
