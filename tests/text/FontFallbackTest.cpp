/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/text/FontSource.h>


namespace {


using namespace ngui;
using namespace ngui::text;


constexpr float kTolerance = 1e-3f;

// The test collection: face 0 (Noto Sans CJK JP) has 一 日 本 語, face 1
// (Noto Sans CJK KR) 한 글; tests/data/fonts/README.md.
const std::string kCollection = std::string(NGUI_TEST_FONTS_DIR) + "/NotoSansCJK-Subset.ttc";


// A font source over the test collection that records what it is asked:
// Han in face 0, Hangul in face 1, Thai in a file that does not exist,
// U+0E02 in a face the collection does not have, and an emoji in face 0,
// which lacks it.
class TestSource : public FontSource {
public:
	struct Request {
		char32_t		codePoint;
		std::string		family;
		std::string		style;
	};

	explicit TestSource(std::shared_ptr<std::vector<Request>> requests)
		:
		fRequests(std::move(requests))
	{
	}

	std::optional<FontLocation> Fallback(char32_t codePoint,
		const FontFamilyStyle& like) const override
	{
		{
			std::scoped_lock lock(fLock);
			fRequests->push_back({codePoint, like.family, like.style});
		}
		if (codePoint >= 0x4e00 && codePoint <= 0x9fff)
			return FontLocation{kCollection, 0};
		if (codePoint >= 0xac00 && codePoint <= 0xd7af)
			return FontLocation{kCollection, 1};
		if (codePoint == 0x0e01)
			return FontLocation{std::string(NGUI_TEST_FONTS_DIR) + "/missing.ttf", 0};
		if (codePoint == 0x0e02)
			return FontLocation{kCollection, 7};
		if (codePoint == 0x1f600)
			return FontLocation{kCollection, 0};
		return std::nullopt;
	}

private:
	mutable std::mutex fLock;
	std::shared_ptr<std::vector<Request>> fRequests;
};


TEST(FontFallbackTest, CharactersComeFromTheOtherBundledFonts)
{
	// What is tested: "a→b" in the plain role of the bundled text system:
	// Noto Sans has no arrows, Noto Sans Mono has; "一" that no bundled font
	// has; "→" truncated.
	// Success criteria: Three runs: "a" in font 1, "→" in font 3 (a real
	// glyph, not the missing one), "b" in font 1; Runs() views them with
	// their clusters, offsets in the whole text (0, 1, 4). The arrow is
	// Noto Sans Mono's 7.2 px wide (600 units at 12 px), so "b" starts
	// 7.2 px after the arrow; StringWidth() is the advance of all. "一"
	// stays in font 1 as its missing glyph, in one run. Truncation uses the
	// arrow's 7.2 px: "→…" fits 20 px.
	// Why it matters: Text must not show boxes for characters the default
	// font lacks while another font has them (module X1).
	//
	// Given
	const auto text = std::move(*FontTextSystem::CreateBundled());

	// When
	const ShapedText shaped = text->Shape("a→b", FontRole::Plain);
	const std::vector<gfx::GlyphRun> runs = shaped.Runs();
	const ShapedText none = text->Shape("一", FontRole::Plain);

	// Then
	ASSERT_EQ(shaped.glyphs.size(), 3u);
	ASSERT_EQ(shaped.fontRuns.size(), 3u);
	EXPECT_EQ(shaped.fontRuns[0].font, 1u);
	EXPECT_EQ(shaped.fontRuns[1].font, 3u);
	EXPECT_EQ(shaped.fontRuns[1].end, 2u);
	EXPECT_EQ(shaped.fontRuns[2].font, 1u);
	EXPECT_EQ(shaped.FontOf(1), 3u);
	EXPECT_NE(shaped.glyphs[1], 0u);
	EXPECT_EQ(shaped.clusters, (std::vector<std::uint32_t>{0, 1, 4}));
	EXPECT_NEAR(shaped.positions[2].x - shaped.positions[1].x, 7.2f, kTolerance);
	EXPECT_NEAR(text->StringWidth("a→b", FontRole::Plain), shaped.advance, kTolerance);
	ASSERT_EQ(runs.size(), 3u);
	EXPECT_EQ(runs[1].font, 3u);
	ASSERT_EQ(runs[1].glyphs.size(), 1u);
	ASSERT_EQ(runs[1].clusters.size(), 1u);
	EXPECT_EQ(runs[1].clusters[0], 1u);
	EXPECT_EQ(runs[2].clusters[0], 4u);
	EXPECT_EQ(runs[1].text.size(), 5u);
	ASSERT_EQ(none.glyphs.size(), 1u);
	EXPECT_EQ(none.glyphs[0], 0u);
	EXPECT_TRUE(none.fontRuns.empty());
	EXPECT_EQ(none.Runs().size(), 1u);
	EXPECT_EQ(text->Truncate("→→→→", 20, TruncationMode::End,
		FontRole::Plain), "→…");
	EXPECT_EQ(text->Fonts().size(), 3u);
}


TEST(FontFallbackTest, ASourceAddsFontsOfTheSystem)
{
	// What is tested: A text system whose source finds the test collection:
	// "a日本한X😀" in the plain role, shaped twice; "日" in the bold role;
	// "一一一" truncated to 25 px; Thai characters whose fonts cannot be
	// read (a missing file, a face the collection lacks).
	// Success criteria: Runs "a" in font 1, "日本" in font 4 (face 0),
	// "한" in font 5 (face 1), "X😀" in font 1 (the source's font lacks the
	// emoji, which stays a missing glyph). Fonts() lists 4 and 5 with their
	// face indices and the mapped file. The source is asked once per font
	// and character, like the font asked for ("Noto Sans" "Regular", then
	// "Bold"), not again for the second shaping; the bold "日" reuses font
	// 4. Truncation uses 一's 12 px (1000 units): "一…" fits 25 px, two
	// would not. Unreadable fonts are not added and the text keeps its font.
	// Why it matters: Fonts of the system fill in what the bundled ones
	// lack, loaded once and handed to the backends (module X1).
	//
	// Given
	auto requests = std::make_shared<std::vector<TestSource::Request>>();
	const auto text = std::move(*FontTextSystem::Create(std::make_unique<TestSource>(requests)));

	// When
	const ShapedText shaped = text->Shape("a日本한X\U0001f600", FontRole::Plain);
	const std::size_t asked = requests->size();
	(void)text->Shape("a日本한X\U0001f600", FontRole::Plain);
	const std::size_t askedAgain = requests->size();
	const ShapedText bold = text->Shape("日", FontRole::Bold);
	const std::string truncated = text->Truncate("一一一", 25, TruncationMode::End,
		FontRole::Plain);
	const ShapedText thai = text->Shape("กข", FontRole::Plain);
	const std::vector<FontFile> fonts = text->Fonts();

	// Then
	ASSERT_EQ(shaped.fontRuns.size(), 4u);
	EXPECT_EQ(shaped.fontRuns[0].font, 1u);
	EXPECT_EQ(shaped.fontRuns[1].font, 4u);
	EXPECT_EQ(shaped.fontRuns[1].end, 3u);
	EXPECT_EQ(shaped.fontRuns[2].font, 5u);
	EXPECT_EQ(shaped.fontRuns[3].font, 1u);
	EXPECT_NE(shaped.glyphs[1], 0u);
	EXPECT_NE(shaped.glyphs[3], 0u);
	EXPECT_EQ(shaped.glyphs.back(), 0u);
	EXPECT_EQ(asked, 4u);
	EXPECT_EQ(askedAgain, asked);
	ASSERT_GE(requests->size(), 5u);
	EXPECT_EQ((*requests)[0].family, "Noto Sans");
	EXPECT_EQ((*requests)[0].style, "Regular");
	EXPECT_EQ((*requests)[4].style, "Bold");
	ASSERT_EQ(bold.fontRuns.size(), 1u);
	EXPECT_EQ(bold.fontRuns[0].font, 4u);
	EXPECT_EQ(truncated, "一…");
	EXPECT_TRUE(thai.fontRuns.empty());
	ASSERT_EQ(fonts.size(), 5u);
	EXPECT_EQ(fonts[3].id, 4u);
	EXPECT_EQ(fonts[3].faceIndex, 0u);
	EXPECT_EQ(fonts[4].id, 5u);
	EXPECT_EQ(fonts[4].faceIndex, 1u);
	EXPECT_GT(fonts[4].data.size(), 1000u);
}


TEST(FontFallbackTest, ClustersStayTogetherAndThreadsShareTheFonts)
{
	// What is tested: "日" with a combining acute accent (one grapheme
	// cluster; the collection lacks the accent, Noto Sans has it), and
	// "日한" shaped from eight threads at once on a new text system.
	// Success criteria: The cluster stays in one run, font 4, both glyphs
	// with cluster 0. From the threads every shaping has the same runs, and
	// each font is loaded once: Fonts() has five.
	// Why it matters: Splitting a cluster between fonts would draw an
	// accent away from its letter; window threads shape at once (the text
	// system is thread-safe).
	//
	// Given
	auto requests = std::make_shared<std::vector<TestSource::Request>>();
	const auto text = std::move(*FontTextSystem::Create(std::make_unique<TestSource>(requests)));
	const auto shared = std::move(*FontTextSystem::Create(std::make_unique<TestSource>(
		std::make_shared<std::vector<TestSource::Request>>())));

	// When
	const ShapedText accented = text->Shape("日́", FontRole::Plain);
	std::atomic<int> wrong = 0;
	std::vector<std::thread> threads;
	for (int i = 0; i < 8; i++) {
		threads.emplace_back([&] {
			for (int k = 0; k < 20; k++) {
				const ShapedText shaped = shared->Shape("日한", FontRole::Plain);
				if (shaped.fontRuns.size() != 2 || shaped.fontRuns[0].font == 1
					|| shaped.fontRuns[1].font == 1)
					wrong++;
			}
		});
	}
	for (std::thread& thread : threads)
		thread.join();

	// Then
	ASSERT_EQ(accented.glyphs.size(), 2u);
	ASSERT_EQ(accented.fontRuns.size(), 1u);
	EXPECT_EQ(accented.fontRuns[0].font, 4u);
	EXPECT_EQ(accented.clusters, (std::vector<std::uint32_t>{0, 0}));
	EXPECT_EQ(wrong, 0);
	EXPECT_EQ(shared->Fonts().size(), 5u);
}


} // namespace
