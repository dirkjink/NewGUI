/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/LookPlugin.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Application.h>
#include <ngui/LookRegistry.h>
#include <ngui/Palette.h>
#include <ngui/gfx/Canvas.h>

#include "support/FixedTextSystem.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;


// The file name extension of the test plugins: CMake's for MODULE libraries
// (.so also on macOS).
constexpr const char* kExtension = NGUI_TEST_LOOK_PLUGIN_SUFFIX;


// A plugin of tests/plugin/TestLookPlugin.cpp by its name.
std::filesystem::path
Plugin(const std::string& name)
{
	return std::filesystem::path(NGUI_TEST_LOOK_PLUGIN_DIR) / (name + kExtension);
}


// The rectangle \a look leaves of [0, 100) × [0, 40) after
// DrawButtonFrame() (the plugin's StubLook shrinks it by 1).
gfx::Rect
FrameLeft(const ControlLook& look)
{
	gfx::Canvas canvas({0, 0, 100, 40});
	const gfx::Caps caps;
	const test::FixedTextSystem text;
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	gfx::Rect rect{0, 0, 100, 40};
	const gfx::Color panel = Palette::Default().Get(BColorWhich::PanelBackground);
	look.DrawButtonFrame(context, rect, panel, panel);
	return rect;
}


TEST(LookPluginTest, ADirectoryOfPluginsIsLoadedAndChecked)
{
	// What is tested: LoadPlugins() of the test plugins' directory in a new
	// registry: a valid plugin of the look "plugin-look", one that declines
	// the interface version, one with another build key, a library without
	// the plugin functions, a second plugin of "plugin-look", and a text
	// file; then LoadPlugin() of a file that does not exist and of the
	// valid plugin again.
	// Success criteria: Only "plugin-look" is registered; its look draws
	// through the plugin's code (the frame shrinks by 1). Each refused
	// plugin gives a warning, in the order of the file names: the taken
	// id, no look plugin, declined version (L12), other build; the text
	// file is not tried. A missing file is NotFound, the valid plugin a
	// second time InvalidArgument (its id is taken).
	// Why it matters: A plugin is foreign code in the program; the loader
	// must take exactly the ones built for it and say why it refuses the
	// others (specification 18.6).
	//
	// Given
	LookRegistry registry;

	// When
	const LookPluginReport report = registry.LoadPlugins(NGUI_TEST_LOOK_PLUGIN_DIR);
	const auto look = registry.Find("plugin-look", registry.BackendCaps());
	const auto missing = registry.LoadPlugin(Plugin("test_look_plugin_missing"));
	const auto again = registry.LoadPlugin(Plugin("test_look_plugin"));

	// Then
	EXPECT_EQ(report.ids, (std::vector<std::string>{"plugin-look"}));
	ASSERT_EQ(report.warnings.size(), 4u);
	const auto mentions = [&report](std::size_t index, const char* text) {
		return report.warnings[index].find(text) != std::string::npos;
	};
	EXPECT_TRUE(mentions(0, "test_look_plugin_none")) << report.warnings[0];
	EXPECT_TRUE(mentions(0, "is no look plugin")) << report.warnings[0];
	EXPECT_TRUE(mentions(1, "test_look_plugin_old")) << report.warnings[1];
	EXPECT_TRUE(mentions(1, "declines the look interface version")) << report.warnings[1];
	EXPECT_TRUE(mentions(2, "test_look_plugin_other_build")) << report.warnings[2];
	EXPECT_TRUE(mentions(2, "ngui 0.0; another compiler")) << report.warnings[2];
	EXPECT_TRUE(mentions(3, "test_look_plugin_twin")) << report.warnings[3];
	EXPECT_TRUE(mentions(3, "is empty or taken")) << report.warnings[3];
	ASSERT_TRUE(look.has_value()) << look.error().message;
	EXPECT_EQ((*look)->Info().displayName, "Plugin look");
	EXPECT_EQ(FrameLeft(**look), (gfx::Rect{1, 1, 99, 39}));
	ASSERT_FALSE(missing.has_value());
	EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
	ASSERT_FALSE(again.has_value());
	EXPECT_EQ(again.error().code, ErrorCode::InvalidArgument);
}


TEST(LookPluginTest, ThePluginStaysLoadedWhileItsLookLives)
{
	// What is tested: The look of the valid test plugin, taken from a
	// registry that is then destroyed, used afterwards; then released.
	// Success criteria: The look still answers Info() and draws after its
	// registry is gone; releasing it destroys it in the plugin and unloads
	// the plugin without a crash or a sanitizer report.
	// Why it matters: The look's deleter holds the library (specification
	// 18.6); unloaded too early, its code would be gone while a window
	// still draws with it.
	//
	// Given
	std::shared_ptr<const ControlLook> kept;
	{
		LookRegistry registry;
		ASSERT_TRUE(registry.LoadPlugin(Plugin("test_look_plugin")).has_value());
		kept = *registry.Find("plugin-look", registry.BackendCaps());
	}

	// When
	const std::string id(kept->Info().id);
	const gfx::Rect left = FrameLeft(*kept);
	kept.reset();

	// Then
	EXPECT_EQ(id, "plugin-look");
	EXPECT_EQ(left, (gfx::Rect{1, 1, 99, 39}));
}


TEST(LookPluginTest, TheEnvironmentNamesPluginDirectoriesAtStart)
{
	// What is tested: A BApplication made with NGUI_LOOK_PATH naming a
	// directory that does not exist and the test plugins' directory, and
	// NGUI_CONTROL_LOOK naming the plugin's look.
	// Success criteria: The plugin's look is registered in the registry of
	// the process and is current: the plugins are loaded before the look
	// is chosen; the missing directory only gives a warning.
	// Why it matters: Users add looks without the program asking for them,
	// as decided for the plugins; nothing is loaded without the variable.
	//
	// Given
	LookRegistry& registry = LookRegistry::Default();
	std::string current;
#if defined(_WIN32)
	const std::string path = std::string("C:\\nonexistent-ngui-looks;") + NGUI_TEST_LOOK_PLUGIN_DIR;
#else
	const std::string path = std::string("/nonexistent-ngui-looks:") + NGUI_TEST_LOOK_PLUGIN_DIR;
#endif

	// When
	{
		const test::ScopedEnvironment plugins("NGUI_LOOK_PATH", path.c_str());
		const test::ScopedEnvironment look("NGUI_CONTROL_LOOK", "plugin-look");
		const BApplication app("application/x-vnd.ngui-look-plugin-test");
		current = std::string(registry.Current()->Info().id);
	}
	ASSERT_TRUE(registry.SetCurrent("be").has_value());

	// Then
	EXPECT_EQ(current, "plugin-look");
	const auto ids = registry.Ids();
	EXPECT_NE(std::find(ids.begin(), ids.end(), "plugin-look"), ids.end());
}


} // namespace
