/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/IconUtils.h>

#include <array>
#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include <ngui/Bitmap.h>
#include <ngui/svg/LunaSvgRenderer.h>


namespace {


using namespace ngui;


std::span<const std::uint8_t>
Bytes(std::string_view text)
{
	return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}


// B, G, R, A of pixel (\a x, \a y) of a B_RGBA32 bitmap.
std::array<std::uint8_t, 4>
Pixel(const BBitmap& bitmap, int32 x, int32 y)
{
	const auto at = static_cast<std::size_t>(y * bitmap.BytesPerRow() + x * 4);
	const std::span<const std::uint8_t> bits = bitmap.Bits();
	return {bits[at], bits[at + 1], bits[at + 2], bits[at + 3]};
}


// The lunasvg renderer installed for the duration of a test.
class InstalledRenderer {
public:
	InstalledRenderer()
	{
		VectorIconRenderer::SetDefault(&fRenderer);
	}

	~InstalledRenderer()
	{
		VectorIconRenderer::SetDefault(nullptr);
	}

private:
	svg::LunaSvgRenderer fRenderer;
};


constexpr std::string_view kRedSquare
	= R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4">)"
	R"(<rect width="4" height="4" fill="#ff0000"/></svg>)";


TEST(IconUtilsTest, VectorIconsFillTheBitmap)
{
	// What is tested: A red SVG square of 4 × 4 drawn with the lunasvg
	// renderer into an 8 × 8 B_RGBA32 bitmap, into one of 8 × 4, into a
	// B_GRAY8 bitmap; a square of half opacity.
	// Success criteria: The icon fills the square bitmap (red, opaque, B,
	// G, R, A in memory). In the wide bitmap it is fitted evenly and
	// centered: transparent columns 0, 1, 6 and 7, red between. Into
	// B_GRAY8 it is converted: Haiku's brightness of red, 255 · 308 / 1024
	// = 76. Half opacity gives alpha 128 and red, not premultiplied.
	// Why it matters: Icons are drawn at any size into bitmaps of any color
	// space, as with Haiku's BIconUtils::GetVectorIcon().
	//
	// Given
	InstalledRenderer renderer;
	BBitmap square({0, 0, 8, 8}, BColorSpace::RGBA32);
	BBitmap wide({0, 0, 8, 4}, BColorSpace::RGBA32);
	BBitmap gray({0, 0, 8, 8}, BColorSpace::Gray8);
	BBitmap half({0, 0, 4, 4}, BColorSpace::RGBA32);

	// When
	const auto drawnSquare = BIconUtils::GetVectorIcon(Bytes(kRedSquare), &square);
	const auto drawnWide = BIconUtils::GetVectorIcon(Bytes(kRedSquare), &wide);
	const auto drawnGray = BIconUtils::GetVectorIcon(Bytes(kRedSquare), &gray);
	const auto drawnHalf = BIconUtils::GetVectorIcon(Bytes(
		R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4">)"
		R"(<rect width="4" height="4" fill="#ff0000" fill-opacity="0.5"/></svg>)"), &half);

	// Then
	ASSERT_TRUE(drawnSquare.has_value());
	ASSERT_TRUE(drawnWide.has_value());
	ASSERT_TRUE(drawnGray.has_value());
	ASSERT_TRUE(drawnHalf.has_value());
	using Color = std::array<std::uint8_t, 4>;
	EXPECT_EQ(Pixel(square, 0, 0), (Color{0, 0, 255, 255}));
	EXPECT_EQ(Pixel(square, 7, 7), (Color{0, 0, 255, 255}));
	EXPECT_EQ(Pixel(wide, 1, 2), (Color{0, 0, 0, 0}));
	EXPECT_EQ(Pixel(wide, 2, 2), (Color{0, 0, 255, 255}));
	EXPECT_EQ(Pixel(wide, 5, 2), (Color{0, 0, 255, 255}));
	EXPECT_EQ(Pixel(wide, 6, 2), (Color{0, 0, 0, 0}));
	EXPECT_EQ(gray.Bits()[0], 76);
	const Color halfRed = Pixel(half, 1, 1);
	EXPECT_NEAR(halfRed[3], 128, 1);
	EXPECT_EQ(halfRed[2], 255);
	EXPECT_EQ(halfRed[0], 0);
}


TEST(IconUtilsTest, DashesAreDrawnSolid)
{
	// What is tested: An SVG of 10 × 10 with a horizontal line through its
	// middle to x 20 000 000, stroked 2 wide with dashes of 1 and gaps of 2,
	// drawn into a 10 × 10 bitmap.
	// Success criteria: It is drawn at once, with the line solid: the
	// pixels at x 1 and 2 on it (a gap, dashed) are both opaque.
	// Why it matters: plutovg makes every dash a path of its own; this
	// line would take gigabytes (found by the fuzz target VectorIcon).
	// Icons need no dashes.
	//
	// Given
	InstalledRenderer renderer;
	BBitmap bitmap({0, 0, 10, 10}, BColorSpace::RGBA32);

	// When
	const auto drawn = BIconUtils::GetVectorIcon(Bytes(
		R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">)"
		R"(<path d="M0 5L20000000 5" stroke="#000" stroke-width="2")"
		R"( stroke-dasharray="1 2"/></svg>)"), &bitmap);

	// Then
	ASSERT_TRUE(drawn.has_value());
	EXPECT_EQ(Pixel(bitmap, 1, 4)[3], 255);
	EXPECT_EQ(Pixel(bitmap, 2, 4)[3], 255);
}


TEST(IconUtilsTest, SystemIconsAndErrors)
{
	// What is tested: The four system icons drawn into 32 × 32 bitmaps; an
	// unknown name; then the errors: no renderer installed, no or an
	// invalid bitmap, data that is no SVG, an empty one, an SVG of width 0,
	// and the renderer given a bitmap other than B_RGBA32.
	// Success criteria: Each system icon draws something opaque in the
	// middle and leaves a corner transparent. An unknown name is NotFound
	// and has no data. Without a renderer GetVectorIcon() is NotSupported; a
	// missing or invalid bitmap is InvalidArgument; unreadable or empty
	// SVG is BadData; the renderer itself takes B_RGBA32 only
	// (InvalidArgument).
	// Why it matters: BAlert shows these icons; applications must learn why
	// an icon could not be drawn.
	//
	// Given
	BBitmap icon({0, 0, 32, 32}, BColorSpace::RGBA32);
	const BBitmap invalid({0, 0, 0, 0}, BColorSpace::RGBA32);
	BBitmap invalidCopy = invalid;
	BBitmap gray({0, 0, 8, 8}, BColorSpace::Gray8);
	const svg::LunaSvgRenderer direct;

	// When / Then
	const auto withoutRenderer = BIconUtils::GetVectorIcon(Bytes(kRedSquare), &icon);
	ASSERT_FALSE(withoutRenderer.has_value());
	EXPECT_EQ(withoutRenderer.error().code, ErrorCode::NotSupported);

	InstalledRenderer renderer;
	for (std::string_view name : {"dialog-information", "dialog-idea", "dialog-warning",
			"dialog-error"}) {
		SCOPED_TRACE(name);
		BBitmap system({0, 0, 32, 32}, BColorSpace::RGBA32);
		EXPECT_FALSE(BIconUtils::SystemIconData(name).empty());
		ASSERT_TRUE(BIconUtils::GetSystemIcon(name, &system).has_value());
		EXPECT_EQ(Pixel(system, 16, 16)[3], 255);
		EXPECT_EQ(Pixel(system, 0, 0)[3], 0);
	}
	const auto unknown = BIconUtils::GetSystemIcon("dialog-question", &icon);
	ASSERT_FALSE(unknown.has_value());
	EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);
	EXPECT_TRUE(BIconUtils::SystemIconData("dialog-question").empty());

	const auto codeOf = [](const std::expected<void, Error>& result) {
		return result.has_value() ? std::optional<ErrorCode>() : result.error().code;
	};
	EXPECT_EQ(codeOf(BIconUtils::GetVectorIcon(Bytes(kRedSquare), nullptr)),
		ErrorCode::InvalidArgument);
	EXPECT_EQ(codeOf(BIconUtils::GetVectorIcon(Bytes(kRedSquare), &invalidCopy)),
		ErrorCode::InvalidArgument);
	EXPECT_EQ(codeOf(BIconUtils::GetVectorIcon(Bytes("not an svg"), &icon)),
		ErrorCode::BadData);
	EXPECT_EQ(codeOf(BIconUtils::GetVectorIcon({}, &icon)), ErrorCode::BadData);
	EXPECT_EQ(codeOf(BIconUtils::GetVectorIcon(Bytes(
		R"(<svg xmlns="http://www.w3.org/2000/svg" width="0" height="4"/>)"),
		&icon)), ErrorCode::BadData);
	EXPECT_EQ(codeOf(direct.Render(Bytes(kRedSquare), gray)), ErrorCode::InvalidArgument);
}


} // namespace
