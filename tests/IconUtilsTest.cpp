/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/IconUtils.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Bitmap.h>
#include <ngui/svg/LunaSvgRenderer.h>
#include <ngui/test/GTest.h>

#include "support/ScopedEnvironment.h"


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


// Whether some pixel of \a bitmap is opaque.
bool
Opaque(const BBitmap& bitmap)
{
	const std::span<const std::uint8_t> bits = bitmap.Bits();
	for (std::size_t at = 3; at < bits.size(); at += 4) {
		if (bits[at] == 255)
			return true;
	}
	return false;
}


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


TEST(IconUtilsTest, ContentFarOutsideIsRefused)
{
	// What is tested: SVGs of 20 × 10 drawn into a 10 × 5 bitmap (scale
	// 0.5): lines to x 23 333 330, 100 000, 200 000 and 1000; a short line
	// stroked 300 000 wide; a rectangle moved by a transform to x 200 000.
	// Success criteria: Content reaching more than 65 536 pixels out of the
	// bitmap is BadData, measured with its stroke and transform: the lines
	// to 11.7 million and 100 000 pixels, the stroke reaching 75 000, the
	// rectangle at 100 000. The lines to 50 000 and 500 pixels are drawn.
	// Why it matters: plutovg's rasterizer computes with long, 32 bits on
	// Windows, and hung on the far line there (found by the fuzz target
	// VectorIcon); far content is never part of an icon.
	//
	// Given
	InstalledRenderer renderer;
	BBitmap bitmap({0, 0, 10, 5}, BColorSpace::RGBA32);
	const auto draw = [&](std::string_view content) {
		const std::string svg = std::string(
			R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10">)")
			+ std::string(content) + "</svg>";
		const auto result = BIconUtils::GetVectorIcon(Bytes(svg), &bitmap);
		return result.has_value() ? std::optional<ErrorCode>() : result.error().code;
	};

	// When / Then
	EXPECT_EQ(draw(R"(<path d="M0 0L23333330 10" stroke="#000"/>)"), ErrorCode::BadData);
	EXPECT_EQ(draw(R"(<path d="M0 0L100000 10" stroke="#000"/>)"), std::nullopt);
	EXPECT_EQ(draw(R"(<path d="M0 0L200000 10" stroke="#000"/>)"), ErrorCode::BadData);
	EXPECT_EQ(draw(R"(<path d="M0 5L10 5" stroke="#000" stroke-width="300000"/>)"),
		ErrorCode::BadData);
	EXPECT_EQ(draw(R"svg(<g transform="translate(200000 0)"><rect width="1" height="1"/></g>)svg"),
		ErrorCode::BadData);
	EXPECT_EQ(draw(R"(<path d="M0 0L1000 10" stroke="#000"/>)"), std::nullopt);
}


TEST(IconUtilsTest, DashesAreDrawnSolid)
{
	// What is tested: An SVG of 10 × 10 with a horizontal line through its
	// middle to x 60 000, stroked 2 wide with dashes of 0.001 and gaps of
	// 0.002, drawn into a 10 × 10 bitmap.
	// Success criteria: It is drawn at once, with the line solid: the
	// pixels at x 1 and 2 on it are both opaque.
	// Why it matters: plutovg makes every dash a path of its own; these 20
	// million dashes would take gigabytes (found by the fuzz target
	// VectorIcon). Icons need no dashes.
	//
	// Given
	InstalledRenderer renderer;
	BBitmap bitmap({0, 0, 10, 10}, BColorSpace::RGBA32);

	// When
	const auto drawn = BIconUtils::GetVectorIcon(Bytes(
		R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">)"
		R"(<path d="M0 5L60000 5" stroke="#000" stroke-width="2")"
		R"( stroke-dasharray="0.001 0.002"/></svg>)"), &bitmap);

	// Then
	ASSERT_TRUE(drawn.has_value());
	EXPECT_EQ(Pixel(bitmap, 1, 4)[3], 255);
	EXPECT_EQ(Pixel(bitmap, 2, 4)[3], 255);
}


TEST(IconUtilsTest, SystemIconsAndErrors)
{
	// What is tested: The four built-in system icons, Haiku's alert icons,
	// drawn into 32 × 32 bitmaps, without an icon directory; an unknown
	// name; then the errors: no renderer installed, no or an
	// invalid bitmap, data that is no SVG, an empty one, an SVG of width 0,
	// and the renderer given a bitmap other than B_RGBA32.
	// Success criteria: Each system icon draws something opaque and leaves
	// a corner transparent. An unknown name is NotFound and has no data.
	// Without a renderer GetVectorIcon() is NotSupported; a
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
	for (std::string_view name : {"Alert_Info", "Alert_Idea", "Alert_Warning", "Alert_Stop"}) {
		SCOPED_TRACE(name);
		BBitmap system({0, 0, 32, 32}, BColorSpace::RGBA32);
		EXPECT_FALSE(BIconUtils::SystemIconData(name).empty());
		ASSERT_TRUE(BIconUtils::GetSystemIcon(name, &system).has_value());
		EXPECT_TRUE(Opaque(system));
		EXPECT_EQ(Pixel(system, 0, 0)[3], 0);
	}
	const auto unknown = BIconUtils::GetSystemIcon("Alert_Question", &icon);
	ASSERT_FALSE(unknown.has_value());
	EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);
	EXPECT_TRUE(BIconUtils::SystemIconData("Alert_Question").empty());

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


// The icon directory set for the duration of a test.
class IconDirectory {
public:
	explicit IconDirectory(const std::filesystem::path& directory)
	{
		BIconUtils::SetSystemIconDirectory(directory);
	}

	~IconDirectory()
	{
		BIconUtils::SetSystemIconDirectory({});
	}
};


// Whether some pixel of \a bitmap is not transparent.
bool
DrawsSomething(const BBitmap& bitmap)
{
	const std::span<const std::uint8_t> bits = bitmap.Bits();
	for (std::size_t at = 3; at < bits.size(); at += 4) {
		if (bits[at] != 0)
			return true;
	}
	return false;
}


TEST(IconUtilsTest, TheLevelOfDetailFollowsTheSize)
{
	// What is tested: An SVG as Haiku's icons say their levels of detail
	// (64 wide): a red left half of class lod1, hidden up to 21.07 px; a
	// blue right half of class lod2, display="none" but shown up to
	// 21.08 px; drawn at 16 × 16 and at 64 × 64.
	// Success criteria: At 16 px the left half is transparent and the right
	// one blue; at 64 px the left half is red and the right one
	// transparent.
	// Why it matters: lunasvg ignores media queries; resolved for the width
	// drawn, a small icon shows Haiku's simpler drawing, as Haiku's renderer
	// does.
	//
	// Given
	constexpr std::string_view kLevels
		= R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><style>)"
		R"(@media (max-width:21.07px){.lod1{display:none}})"
		R"(@media (max-width:21.08px){.lod2{display:inline}}</style>)"
		R"(<rect class="lod1" width="32" height="64" fill="#ff0000"/>)"
		R"(<rect class="lod2" display="none" x="32" width="32" height="64" fill="#0000ff"/>)"
		R"(</svg>)";
	InstalledRenderer renderer;
	BBitmap small({0, 0, 16, 16}, BColorSpace::RGBA32);
	BBitmap large({0, 0, 64, 64}, BColorSpace::RGBA32);

	// When
	ASSERT_TRUE(BIconUtils::GetVectorIcon(Bytes(kLevels), &small).has_value());
	ASSERT_TRUE(BIconUtils::GetVectorIcon(Bytes(kLevels), &large).has_value());

	// Then
	EXPECT_EQ(Pixel(small, 4, 8)[3], 0);
	EXPECT_EQ(Pixel(small, 12, 8), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
	EXPECT_EQ(Pixel(large, 16, 32), (std::array<std::uint8_t, 4>{0, 0, 255, 255}));
	EXPECT_EQ(Pixel(large, 48, 32)[3], 0);
}


TEST(IconUtilsTest, TheIconDirectoryGivesHaikusOtherIcons)
{
	// What is tested: Haiku's icons not built in (Folder_generic, a flag in
	// a subdirectory with spaces, "flags/DE Germany"), asked with the icon
	// directory set to data/icons/haiku/, then with NGUI_ICON_DIR only, then
	// without either; names leading out of the directory ("../README",
	// "/etc/passwd", "flags/../Alert_Info", "a\b", "C:x"); an unknown
	// name.
	// Success criteria: With the directory set or the variable given the
	// icons draw something; the same name gives the same data twice (read
	// once). Without a directory they are NotFound, while the built-in
	// alert icons still draw. Names leading out and unknown ones are
	// NotFound.
	// Why it matters: Applications show any of Haiku's 708 icons by name
	// without the library carrying them all; a name must not read files
	// outside the icon directory.
	//
	// Given
	InstalledRenderer renderer;
	BBitmap icon({0, 0, 32, 32}, BColorSpace::RGBA32);
	const auto draws = [&icon](std::string_view name) {
		icon = BBitmap({0, 0, 32, 32}, BColorSpace::RGBA32);
		return BIconUtils::GetSystemIcon(name, &icon).has_value() && DrawsSomething(icon);
	};

	// When / Then
	{
		const IconDirectory directory(NGUI_HAIKU_ICONS_DIR);
		EXPECT_TRUE(draws("Folder_generic"));
		EXPECT_TRUE(draws("flags/DE Germany"));
		EXPECT_EQ(BIconUtils::SystemIconData("Folder_generic").data(),
			BIconUtils::SystemIconData("Folder_generic").data());
		for (std::string_view name : {"../README", "/etc/passwd", "flags/../Alert_Info",
				"flags//DE Germany", "./Folder_generic", "a\\b", "C:x", "No_Such_Icon"}) {
			SCOPED_TRACE(name);
			EXPECT_TRUE(BIconUtils::SystemIconData(name).empty());
		}
	}
	{
		const test::ScopedEnvironment variable("NGUI_ICON_DIR", NGUI_HAIKU_ICONS_DIR);
		EXPECT_EQ(BIconUtils::SystemIconDirectory(), std::filesystem::path(NGUI_HAIKU_ICONS_DIR));
		EXPECT_TRUE(draws("App_CD-Burner"));
	}
	EXPECT_TRUE(BIconUtils::SystemIconDirectory().empty());
	const auto missing = BIconUtils::GetSystemIcon("App_Magnify", &icon);
	ASSERT_FALSE(missing.has_value());
	EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
	EXPECT_TRUE(draws("Alert_Warning"));
}


TEST(IconUtilsTest, EveryHaikuIconDraws)
{
	// What is tested: Each of the SVGs in data/icons/haiku/ (708, with
	// flags/ and Pulse/), drawn at 16, 32 and 64 pixels.
	// Success criteria: Every one draws without an error and shows something
	// at each size, except Misc_Hand at 16 pixels, which Haiku's icon hides
	// below 32 (its level of detail); there are 708.
	// Why it matters: These are the icons NewGUI and its applications show;
	// none may be one lunasvg cannot read, nor vanish at a level of detail.
	//
	// Given
	InstalledRenderer renderer;
	std::vector<std::filesystem::path> files;
	for (const auto& entry
			: std::filesystem::recursive_directory_iterator(NGUI_HAIKU_ICONS_DIR)) {
		if (entry.is_regular_file() && entry.path().extension() == ".svg")
			files.push_back(entry.path());
	}

	// When / Then
	EXPECT_EQ(files.size(), 708u);
	for (const std::filesystem::path& file : files) {
		SCOPED_TRACE(file.string());
		std::ifstream stream(file, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());
		for (const float size : {16.0f, 32.0f, 64.0f}) {
			BBitmap icon({0, 0, size, size}, BColorSpace::RGBA32);
			const auto drawn = BIconUtils::GetVectorIcon(Bytes(text), &icon);
			ASSERT_TRUE(drawn.has_value()) << size << ": " << drawn.error().message;
			// Haiku's hand shows from 32 pixels on only: all its shapes are of
			// a level of detail hidden below
			const bool hidden = file.filename() == "Misc_Hand.svg" && size < 32;
			EXPECT_EQ(DrawsSomething(icon), !hidden) << size;
		}
	}
}


TEST(IconUtilsTest, HaikusIconsMatchTheirSnapshots)
{
	// What is tested: Icons with levels of detail and the alert icons
	// (App_CD-Burner, Folder_generic, App_Magnify, Action_Search,
	// Alert_Warning, Alert_Stop), side by side at 16, 32 and 64 pixels,
	// drawn by lunasvg.
	// Success criteria: Each row matches its reference ("HaikuIcons16",
	// "HaikuIcons32", "HaikuIcons64"), compared with the tolerance for
	// anti-aliased edges.
	// Why it matters: These are Haiku's icons as NewGUI shows them; a change
	// to the renderer or the resolving of the levels of detail shows here.
	//
	// Given
	InstalledRenderer renderer;
	const IconDirectory directory(NGUI_HAIKU_ICONS_DIR);
	const std::array<std::string_view, 6> names = {"App_CD-Burner", "Folder_generic",
		"App_Magnify", "Action_Search", "Alert_Warning", "Alert_Stop"};

	for (const int size : {16, 32, 64}) {
		SCOPED_TRACE(size);
		// When
		const auto side = static_cast<float>(size);
		BBitmap row({0, 0, side * static_cast<float>(names.size()), side},
			BColorSpace::RGBA32);
		for (std::size_t i = 0; i < names.size(); i++) {
			BBitmap icon({0, 0, side, side}, BColorSpace::RGBA32);
			ASSERT_TRUE(BIconUtils::GetSystemIcon(names[i], &icon).has_value()) << names[i];
			const std::span<const std::uint8_t> from = icon.Bits();
			const std::span<std::uint8_t> to = row.Bits();
			for (int y = 0; y < size; y++) {
				const auto source = static_cast<std::size_t>(y * icon.BytesPerRow());
				const auto target = static_cast<std::size_t>(y * row.BytesPerRow())
					+ i * static_cast<std::size_t>(size) * 4;
				std::copy_n(from.begin() + static_cast<std::ptrdiff_t>(source), size * 4,
					to.begin() + static_cast<std::ptrdiff_t>(target));
			}
		}

		// Then
		EXPECT_IMAGE_SNAPSHOT(row, "HaikuIcons" + std::to_string(size),
			{.look = "none", .backend = "lunasvg", .tolerance = test::Tolerance::AntiAliased()});
	}
}


} // namespace
