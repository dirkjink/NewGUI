/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/SvgExportBackend.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/ImageRegistry.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Image.h>

#include "gfx/PngWriter.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using ngui::test::FixedTextSystem;


// The fixed metrics, and as outline of every glyph a square of 6 × 8
// pixels standing on the baseline; glyph 0 has none (unknown).
class OutlineText : public FixedTextSystem {
public:
	std::optional<Path> GlyphOutline(FontId, std::uint32_t glyph, float) const override
	{
		if (glyph == 0)
			return std::nullopt;
		Path path;
		path.MoveTo({0, 0});
		path.LineTo({6, 0});
		path.LineTo({6, -8});
		path.LineTo({0, -8});
		path.Close();
		return path;
	}

	std::vector<FontFamilyStyle> Styles() const override
	{
		return {{"Test Sans", "Bold", 1, BFontFace::Bold, false}};
	}
};


std::size_t
Count(const std::string& text, std::string_view part)
{
	std::size_t count = 0;
	for (std::size_t at = text.find(part); at != std::string::npos;
			at = text.find(part, at + 1)) {
		count++;
	}
	return count;
}


// The text "Ab" as a run of font 1 at 12 px, glyphs \a glyphs, at
// \a origin.
void
DrawText(Canvas& canvas, std::array<std::uint32_t, 2> glyphs,
	std::array<std::uint32_t, 2> clusters = {0, 1}, Point origin = {10, 50})
{
	static constexpr std::u8string_view kText = u8"Ab";
	const std::array<Point, 2> positions = {Point{0, 0}, Point{7, 0}};
	canvas.DrawGlyphRun({1, 12, glyphs, positions, kText, clusters}, origin,
		{.color = {0, 0, 0, 1}});
}


// A list with every operation of the core profile and a path: rectangles
// with a gradient and a translucent color, a rounded rectangle, a ring, an
// ellipse, a polygon, frames, a polyline, an arc, axis-parallel and
// diagonal lines, an image of 2 × 2 pixels drawn twice, a glyph run twice, a
// nested list under a clip, and a filled path.
DisplayListRef
EveryOperation(ImageHandle image)
{
	Canvas canvas({0, 0, 120, 80});
	const Paint black{.color = {0, 0, 0, 1}};
	const auto gradient = LinearGradient::Create({0, 0}, {0, 10},
		std::array<GradientStop, 2>{{{0, {1, 0, 0, 1}}, {1, {0, 0, 1, 1}}}});
	canvas.FillRect({0, 0, 20, 10}, {.gradient = *gradient});
	canvas.FillRect({20, 0, 40, 10}, {.color = {0, 0.5f, 0, 0.5f}});
	canvas.FillRRect({{40, 0, 60, 10}, {2, 2, 4, 4}}, black);
	canvas.FillRing({{60, 0, 80, 10}, {3, 3, 3, 3}}, {{61, 1, 79, 9}, {2, 2, 2, 2}}, black);
	canvas.FillEllipse({80, 0, 100, 10}, black);
	const std::array<Point, 3> triangle = {Point{100, 10}, Point{110, 0}, Point{120, 10}};
	canvas.FillConvexPolygon(triangle, black);
	canvas.StrokeRect({0, 12, 20, 22}, {.color = {0, 0, 0, 1}, .strokeWidth = 2});
	canvas.StrokeRRect({{20, 12, 40, 22}, {3, 3, 3, 3}}, black);
	const std::array<Point, 3> zigzag = {Point{40, 22}, Point{50, 12}, Point{60, 22}};
	canvas.StrokePolyline(zigzag, false, {.color = {0, 0, 0, 1}, .strokeWidth = 2,
		.cap = Paint::Cap::Round, .join = Paint::Join::Round});
	canvas.StrokeArc({60, 12, 80, 22}, 0, 90, black);
	const std::array<Line, 2> lines = {Line{{80.5f, 12}, {100.5f, 12}, {1, 0, 0, 1}},
		Line{{100, 12}, {120, 22}, {0, 0, 1, 1}}};
	canvas.DrawLines(lines, 1, Paint::Cap::Butt);
	canvas.DrawImage(image, {0, 0, 2, 2}, {0, 30, 2, 32});
	canvas.DrawImage(image, {0, 0, 2, 2}, {4, 30, 8, 34}, 0.5f);
	DrawText(canvas, {65, 98});
	DrawText(canvas, {65, 98}, {0, 1}, {10, 70});
	Canvas child({0, 0, 20, 20});
	child.FillRect({0, 0, 20, 20}, black);
	const DisplayListRef nested = child.Finish();
	canvas.Save();
	canvas.ClipRect({60, 30, 70, 40});
	canvas.DrawList(nested, {60, 30});
	canvas.Restore();
	Path path;
	path.MoveTo({90, 30});
	path.CubicTo({100, 30}, {100, 40}, {90, 40});
	path.Close();
	canvas.FillPath(path, black);
	return canvas.Finish();
}


// A red, green, blue and half transparent white image of 2 × 2.
ImageHandle
FourPixels()
{
	static const std::array<std::uint8_t, 16> kPixels = {255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 128};
	return *ImageRegistry::Default().Register({2, 2, 8, kPixels});
}


TEST(SvgExportTest, EveryOperationHasItsElement)
{
	// What is tested: The SVG of a list with every operation (glyphs as
	// paths, images embedded, pretty), against the snapshot
	// SvgExportTest_EveryOperation.none.svg.1x.svg, and parts of it.
	// Success criteria: It matches the reference, checked by eye in a
	// browser. Rectangles and axis-parallel lines are crisp <rect>s (A1,
	// A3), the frame lies inside (6.1: x = 1 for a width of 2), the ring
	// is evenodd, the gradient lies in user space, the translucent fill has
	// fill-opacity 0.5 (E3); the image is one <image> used twice, the
	// second scaled by 2 at opacity 0.5; each of the two glyphs is one path
	// used by both runs; the nested list is a translated group; the viewBox
	// is the bounds (E2).
	// Why it matters: The SVG must show what the render backends show
	// (specification 19.3), for documentation and review.
	//
	// Given
	const ImageHandle image = FourPixels();
	const DisplayListRef list = EveryOperation(image);
	const OutlineText text;

	// When
	const auto svg = ExportSvg(*list, list->Bounds(), {.pretty = true}, &text);
	ImageRegistry::Default().Release(image);

	// Then
	ASSERT_TRUE(svg.has_value()) << svg.error().message;
	EXPECT_NE(svg->find("viewBox=\"0 0 120 80\""), std::string::npos);
	EXPECT_NE(svg->find("<rect x=\"1\" y=\"13\" width=\"18\" height=\"8\" fill=\"none\" "
		"stroke=\"#000000\" stroke-width=\"2\" shape-rendering=\"crispEdges\"/>"),
		std::string::npos) << *svg;
	EXPECT_NE(svg->find("fill=\"#008000\" fill-opacity=\"0.5\""), std::string::npos);
	EXPECT_NE(svg->find("fill-rule=\"evenodd\""), std::string::npos);
	EXPECT_NE(svg->find("gradientUnits=\"userSpaceOnUse\""), std::string::npos);
	EXPECT_EQ(Count(*svg, "<image "), 1u);
	EXPECT_EQ(Count(*svg, "xlink:href=\"#i"), 2u);
	EXPECT_NE(svg->find("scale(2 2)\" opacity=\"0.5\""), std::string::npos);
	EXPECT_EQ(Count(*svg, "<path id=\"p"), 2u);
	EXPECT_EQ(Count(*svg, "xlink:href=\"#p"), 4u);
	EXPECT_NE(svg->find("<g transform=\"translate(60 30)\">"), std::string::npos);
	EXPECT_SVG_SNAPSHOT(*list, "EveryOperation", {}, {}, &text);
}


TEST(SvgExportTest, TheOutputIsDeterministic)
{
	// What is tested: The same list exported twice, compact and pretty, and
	// at scale 2; two nested lists under the same clip region.
	// Success criteria: Both exports are byte for byte the same (E1); ids
	// are numbered in order of first use, the clip region of both nested
	// lists is one clipPath; at scale 2 width and height double while the
	// viewBox stays (E2); the compact form has no line breaks at all.
	// Why it matters: SVG snapshots in tests and documentation in a
	// repository only work if unchanged drawing gives unchanged files.
	//
	// Given
	Canvas canvas({0, 0, 40, 20});
	Canvas child({0, 0, 10, 10});
	child.FillRect({0, 0, 10, 10}, {.color = {0, 0, 0, 1}});
	const DisplayListRef nested = child.Finish();
	canvas.ClipRect({0, 0, 30, 20});
	canvas.DrawList(nested, {0, 0});
	canvas.DrawList(nested, {20, 0});
	const DisplayListRef list = canvas.Finish();
	const FixedTextSystem text;

	// When
	const auto first = ExportSvg(*list, list->Bounds(), {}, &text);
	const auto second = ExportSvg(*list, list->Bounds(), {}, &text);
	const auto doubled = ExportSvg(*list, list->Bounds(), {.scale = 2}, &text);

	// Then
	ASSERT_TRUE(first.has_value() && second.has_value() && doubled.has_value());
	EXPECT_EQ(*first, *second);
	EXPECT_EQ(first->find('\n'), std::string::npos);
	EXPECT_EQ(Count(*first, "<clipPath "), 2u);
	EXPECT_NE(first->find("<clipPath id=\"c1\"><rect x=\"0\" y=\"0\" width=\"30\""),
		std::string::npos) << *first;
	EXPECT_EQ(Count(*first, "url(#c2)"), 2u) << *first;
	EXPECT_NE(doubled->find("width=\"80\" height=\"40\" viewBox=\"0 0 40 20\""),
		std::string::npos);
}


TEST(SvgExportTest, TextIsTextWhereItMapsToCharacters)
{
	// What is tested: Glyph runs exported as text: "Ab" with one glyph per
	// character; the same with clusters that do not map 1:1 (a ligature);
	// and as paths, with a glyph that has no outline.
	// Success criteria: The first run is <text> with an x per character,
	// the family "Test Sans" in bold and the escaped text. The ligature
	// falls back to paths. A glyph without an outline falls back to text.
	// Why it matters: Text in an SVG can be selectable and searchable
	// (specification 19.3, TextMode::Text), but only where each glyph is a
	// character; otherwise the outlines keep the look.
	//
	// Given
	Canvas canvas({0, 0, 60, 60});
	DrawText(canvas, {65, 98});
	const DisplayListRef plain = canvas.Finish();
	Canvas ligature({0, 0, 60, 60});
	DrawText(ligature, {65, 98}, {0, 0});
	const DisplayListRef joined = ligature.Finish();
	Canvas unknown({0, 0, 60, 60});
	DrawText(unknown, {0, 98});
	const DisplayListRef missing = unknown.Finish();
	const OutlineText text;
	const ExportOptions asText{.text = ExportOptions::TextMode::Text};

	// When
	const auto written = ExportSvg(*plain, plain->Bounds(), asText, &text);
	const auto outlined = ExportSvg(*joined, joined->Bounds(), asText, &text);
	const auto fallback = ExportSvg(*missing, missing->Bounds(), {}, &text);

	// Then
	ASSERT_TRUE(written.has_value() && outlined.has_value() && fallback.has_value());
	EXPECT_NE(written->find("<text x=\"10 17\" y=\"50 50\" font-family=\"Test Sans\" "
		"font-size=\"12\" font-weight=\"bold\" fill=\"#000000\" xml:space=\"preserve\">Ab"
		"</text>"), std::string::npos) << *written;
	EXPECT_EQ(Count(*outlined, "<text"), 0u);
	EXPECT_EQ(Count(*outlined, "<use "), 2u);
	EXPECT_EQ(Count(*fallback, "<text"), 1u) << *fallback;
}


TEST(SvgExportTest, BadArgumentsAndAFailingStreamAreErrors)
{
	// What is tested: Export() with empty bounds, a scale of 0, and a
	// stream that fails.
	// Success criteria: InvalidArgument, InvalidArgument and IoError;
	// Capabilities() reports the core profile with Bézier paths and
	// concave polygons, nothing else.
	// Why it matters: An export to a full disk must say so instead of
	// leaving a broken file behind unnoticed.
	//
	// Given
	Canvas canvas({0, 0, 10, 10});
	canvas.FillRect({0, 0, 10, 10}, {.color = {0, 0, 0, 1}});
	const DisplayListRef list = canvas.Finish();
	SvgExportBackend backend;
	std::ostringstream fine;
	std::ostringstream broken;
	broken.setstate(std::ios::badbit);

	// When
	const auto empty = backend.Export(*list, {0, 0, 0, 10}, fine, {});
	const auto unscaled = backend.Export(*list, list->Bounds(), fine, {.scale = 0});
	const auto failed = backend.Export(*list, list->Bounds(), broken, {});

	// Then
	ASSERT_FALSE(empty.has_value());
	EXPECT_EQ(empty.error().code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(unscaled.has_value());
	EXPECT_EQ(unscaled.error().code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(failed.has_value());
	EXPECT_EQ(failed.error().code, ErrorCode::IoError);
	Caps expected;
	expected.bezierPaths = true;
	expected.concavePolygons = true;
	EXPECT_EQ(backend.Capabilities(), expected);
}


TEST(SvgExportTest, EmbeddedImagesArePngs)
{
	// What is tested: The PNG writer of the export with an image of 300 ×
	// 300 (more than one stored deflate block), decoded with libpng; and
	// base64 of 1, 2 and 3 bytes.
	// Success criteria: The PNG decodes to the same pixels; base64 of "M",
	// "Ma" and "Man" is "TQ==", "TWE=" and "TWFu" (RFC 4648).
	// Why it matters: Images in the SVG are data: URIs of PNGs; the writer
	// has no zlib, so its blocks, checksums and padding must be right.
	//
	// Given
	std::vector<std::uint8_t> pixels(300 * 300 * 4);
	for (std::size_t i = 0; i < pixels.size(); i++)
		pixels[i] = static_cast<std::uint8_t>(i * 7 % 251);

	// When
	const std::vector<std::uint8_t> png = detail::EncodeStoredPng(300, 300, pixels);
	const auto decoded = test::DecodePng(std::as_bytes(std::span(png)));
	const std::array<std::uint8_t, 3> man = {'M', 'a', 'n'};

	// Then
	ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
	ASSERT_EQ(decoded->Width(), 300);
	ASSERT_EQ(decoded->Height(), 300);
	EXPECT_TRUE(std::ranges::equal(decoded->Bytes(), pixels));
	EXPECT_EQ(detail::Base64(std::span(man).first(1)), "TQ==");
	EXPECT_EQ(detail::Base64(std::span(man).first(2)), "TWE=");
	EXPECT_EQ(detail::Base64(man), "TWFu");
}


} // namespace
