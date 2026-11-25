/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Lint.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/Catalog.h>
#include <ngui/LocaleRoster.h>
#include <ngui/Looper.h>
#include <ngui/StringView.h>
#include <ngui/Window.h>
#include <ngui/test/GTest.h>

#include "support/FixedTextSystem.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LintTest"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// A label of 7 pixels per byte (FixedTextSystem) that cuts what does not
// fit.
std::unique_ptr<BStringView>
Label(gfx::Rect frame, std::string_view name, std::string_view text)
{
	auto label = std::make_unique<BStringView>(frame, name, text);
	label->SetTruncation(TruncationMode::End);
	return label;
}


// A window with one fault of each kind, one excluded and one hidden.
class BrokenWindow : public BWindow {
public:
	BrokenWindow()
		:
		BWindow({0, 0, 300, 200}, "Broken", BWindowLook::Titled)
	{
		// Tab goes to "lower" first, though "upper" reads first
		AddChild(std::make_unique<BButton>(gfx::Rect{10, 120, 90, 145}, "lower", "Lower",
			nullptr));
		AddChild(std::make_unique<BButton>(gfx::Rect{10, 10, 90, 35}, "upper", "Upper",
			nullptr));
		// "left" and "right" overlap
		AddChild(std::make_unique<BView>(gfx::Rect{100, 10, 160, 40}, "left", kFollowLeftTop,
			BViewFlag::WillDraw));
		AddChild(std::make_unique<BView>(gfx::Rect{150, 10, 200, 40}, "right", kFollowLeftTop,
			BViewFlag::WillDraw));
		// "Truncated text" is 98 pixels in 50
		AddChild(Label({100, 50, 150, 70}, "cut", "Truncated text"));
		// beyond the window's right edge
		AddChild(std::make_unique<BView>(gfx::Rect{280, 80, 320, 100}, "beyond", kFollowLeftTop,
			BViewFlag::WillDraw));
		// smaller than its minimum
		auto small = std::make_unique<BView>(gfx::Rect{100, 150, 120, 160}, "small",
			kFollowLeftTop, BViewFlag::WillDraw);
		small->SetExplicitMinSize({40, 20});
		AddChild(std::move(small));
		// overlaps "left", but is excluded; another overlaps it, but hidden
		AddChild(std::make_unique<BView>(gfx::Rect{110, 20, 130, 30}, "ignored",
			kFollowLeftTop, BViewFlag::WillDraw));
		auto hidden = std::make_unique<BView>(gfx::Rect{100, 10, 200, 40}, "hidden",
			kFollowLeftTop, BViewFlag::WillDraw);
		hidden->Hide();
		AddChild(std::move(hidden));
	}
};


// A window without faults: a box with a button and a label that fits.
class CleanWindow : public BWindow {
public:
	CleanWindow()
		:
		BWindow({0, 0, 200, 100}, "Clean", BWindowLook::Titled)
	{
		auto box = std::make_unique<BBox>(gfx::Rect{10, 10, 190, 90}, "box");
		box->AddChild(std::make_unique<BButton>(gfx::Rect{10, 10, 90, 35}, "ok", "OK",
			nullptr));
		// "Save" is 28 pixels; pseudo-localized it does not fit
		box->AddChild(Label({10, 45, 50, 65}, "label", B_TRANSLATE("Save")));
		AddChild(std::move(box));
	}
};


template<typename Window>
BMessenger
Open()
{
	auto window = std::make_unique<Window>();
	window->Show();
	return *BLooper::Run(std::move(window));
}


std::vector<std::string>
Findings(const LintReport& report, LintCheck check)
{
	std::vector<std::string> paths;
	for (const LintFinding& finding : report.findings) {
		if (finding.check == check)
			paths.push_back(finding.path);
	}
	return paths;
}


TEST(LintTest, TheLintFindsBrokenLayouts)
{
	// What is tested: The lint of a window with two overlapping views, a
	// label that cuts its text, a view beyond the window, a view smaller
	// than its minimum, buttons whose focus order goes against the reading
	// order, an excluded view and a hidden one that overlap others.
	// Success criteria: One finding each: "left" overlaps (an error),
	// "cut" cuts "Truncated text" (an error, from the text system's
	// report), "beyond" reaches out (an error), "small" is below its
	// minimum (an error), "upper" comes after "lower" in Tab order (a
	// warning). The excluded and the hidden views add nothing. The report
	// text names the window, path and check.
	// Why it matters: Layout faults show in tests before users see them
	// (specification 20.6).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-lint-test");
	Driver driver(app);
	const BMessenger window = Open<BrokenWindow>();
	LintOptions options;
	options.exclude = {"ignored"};

	// When
	const LintReport report = Lint(driver.Window("Broken"), options);
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(Findings(report, LintCheck::Overlap), (std::vector<std::string>{"left"}))
		<< report.Text();
	EXPECT_EQ(Findings(report, LintCheck::Truncated), (std::vector<std::string>{"cut"}));
	EXPECT_EQ(Findings(report, LintCheck::Outside), (std::vector<std::string>{"beyond"}));
	EXPECT_EQ(Findings(report, LintCheck::BelowMinimum), (std::vector<std::string>{"small"}));
	EXPECT_EQ(Findings(report, LintCheck::FocusOrder), (std::vector<std::string>{"upper"}));
	EXPECT_EQ(report.Count(LintSeverity::Error), 4u);
	EXPECT_EQ(report.Count(LintSeverity::Warning), 1u);
	EXPECT_TRUE(report.HasErrors());
	EXPECT_NE(report.Text().find("Broken: cut (truncated): it cuts \"Truncated text\""),
		std::string::npos) << report.Text();
}


TEST(LintTest, ACleanWindowAndItsLayoutSnapshot)
{
	// What is tested: The lint and the layout snapshot of a window with a
	// box, a button and a label that fits.
	// Success criteria: No findings; the layout text has the window and
	// each view with its class, name and frame, indented by depth, and
	// matches LintTest_Clean.none.layout.1x.txt.
	// Why it matters: A layout snapshot shows changes of the view tree in a
	// review (specification 20.5).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-lint-clean-test");
	Driver driver(app);
	const BMessenger window = Open<CleanWindow>();
	WindowRef clean = driver.Window("Clean");

	// When
	const LintReport report = Lint(clean);
	const std::string layout = LayoutText(clean);

	// Then
	EXPECT_LINT_CLEAN(report);
	EXPECT_TRUE(report.findings.empty()) << report.Text();
	EXPECT_EQ(layout, "BWindow \"Clean\" (0, 0, 200, 100)\n"
		"  BBox \"box\" (10, 10, 190, 90)\n"
		"    BButton \"ok\" (10, 10, 90, 35)\n"
		"    BStringView \"label\" (10, 45, 50, 65)\n");
	EXPECT_LAYOUT_SNAPSHOT(clean, "Clean");
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);
}


TEST(LintTest, TheMatrixFindsWhatLongerTextsBreak)
{
	// What is tested: LintMatrix() over the default matrix (1x, 1.5x, 2x,
	// pseudo-localized) for an application with the clean window, whose
	// label is narrower than the pseudo-localized "Save"; afterwards the
	// default text system and catalog.
	// Success criteria: Only the pseudo-localized configuration finds the
	// label cut, named "pseudo"; the scales find nothing. Text system and
	// catalog are back as before.
	// Why it matters: Translations are longer than English; the lint
	// shows where a layout will not hold them before translators do.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	const std::vector<LintConfiguration> matrix = DefaultLintMatrix();
	const auto makeApplication = [] {
		return std::make_unique<BApplication>("application/x-vnd.ngui-lint-matrix-test");
	};
	const auto openWindows = [](Driver&) { (void)Open<CleanWindow>(); };

	// When
	const LintReport report = LintMatrix(matrix, makeApplication, openWindows);
	const TextSystem* after = &TextSystem::Default();
	const bool catalogBack = BLocaleRoster::Default()->Catalog() == nullptr;
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_EQ(report.findings.size(), 1u) << report.Text();
	EXPECT_EQ(report.findings[0].check, LintCheck::Truncated);
	EXPECT_EQ(report.findings[0].configuration, "pseudo");
	EXPECT_EQ(report.findings[0].path, "box/label");
	EXPECT_EQ(after, &text);
	EXPECT_TRUE(catalogBack);
	EXPECT_EQ(matrix.size(), 4u);
}


} // namespace
