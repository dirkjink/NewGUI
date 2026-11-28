/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Notification.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Application.h>
#include <ngui/Bitmap.h>
#include <ngui/IconUtils.h>
#include <ngui/platform/HeadlessPlatform.h>
#include <ngui/svg/LunaSvgRenderer.h>
#include <ngui/test/Driver.h>


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;


// The RGBA bytes of the system icon \a name at 64 × 64, as Send() hands an
// icon on.
std::vector<std::uint8_t>
RgbaOf(std::string_view name)
{
	BBitmap icon({0, 0, 64, 64}, BColorSpace::RGBA32);
	if (!BIconUtils::GetSystemIcon(name, &icon).has_value())
		return {};
	std::vector<std::uint8_t> rgba;
	const auto row = static_cast<std::size_t>(icon.BytesPerRow());
	const std::span<const std::uint8_t> bits = std::as_const(icon).Bits();
	for (std::size_t y = 0; y < 64; y++) {
		for (std::size_t x = 0; x < 64; x++) {
			const std::uint8_t* pixel = bits.data() + y * row + x * 4;
			rgba.insert(rgba.end(), {pixel[2], pixel[1], pixel[0], pixel[3]});
		}
	}
	return rgba;
}


TEST(NotificationIconTest, WithoutAnIconTheTypeGivesOne)
{
	// What is tested: With the lunasvg renderer on the headless platform:
	// notifications of each type without an icon of their own, one with
	// its own 2 × 1 icon; then, without a renderer, one without an icon.
	// Success criteria: Information and progress show Haiku's Notify_Notice,
	// important ones Notify_Achtung, errors Notify_Danger, each 64 × 64 as
	// RGBA; the one with its own icon keeps it; without a renderer there is
	// none, and the notification is still sent. Icon() stays nullptr.
	// Why it matters: A notification tells at a glance how much it matters,
	// with Haiku's own notification icons (Humdinger's grading from notice
	// to danger), where Haiku shows the application's icon NewGUI lacks.
	//
	// Given
	TestEnvironment environment;
	BApplication app("application/x-vnd.example-notify");
	auto owned = std::make_unique<HeadlessPlatform>();
	HeadlessPlatform* platform = owned.get();
	ASSERT_TRUE(app.UsePlatform(std::move(owned)).has_value());
	Driver driver(app);
	const svg::LunaSvgRenderer renderer;
	VectorIconRenderer::SetDefault(&renderer);
	BBitmap own({0, 0, 2, 1}, BColorSpace::RGBA32);
	BNotification withOwn(BNotificationType::Error);
	ASSERT_TRUE(withOwn.SetIcon(&own).has_value());
	const BNotification information(BNotificationType::Information);

	// When
	for (const BNotificationType type : {BNotificationType::Information,
			BNotificationType::Progress, BNotificationType::Important,
			BNotificationType::Error}) {
		ASSERT_TRUE(BNotification(type).Send().has_value());
	}
	ASSERT_TRUE(withOwn.Send().has_value());
	VectorIconRenderer::SetDefault(nullptr);
	const auto withoutRenderer = information.Send();
	const std::vector<NotificationSpec> shown = platform->Notifications();

	// Then
	ASSERT_TRUE(withoutRenderer.has_value());
	ASSERT_EQ(shown.size(), 6u);
	VectorIconRenderer::SetDefault(&renderer);
	const std::vector<std::uint8_t> notice = RgbaOf("Notify_Notice");
	const std::vector<std::uint8_t> achtung = RgbaOf("Notify_Achtung");
	const std::vector<std::uint8_t> danger = RgbaOf("Notify_Danger");
	VectorIconRenderer::SetDefault(nullptr);
	ASSERT_FALSE(notice.empty());
	EXPECT_EQ(shown[0].iconWidth, 64);
	EXPECT_EQ(shown[0].iconHeight, 64);
	EXPECT_EQ(shown[0].icon, notice);
	EXPECT_EQ(shown[1].icon, notice);
	EXPECT_EQ(shown[2].icon, achtung);
	EXPECT_EQ(shown[3].icon, danger);
	EXPECT_NE(achtung, notice);
	EXPECT_NE(danger, achtung);
	EXPECT_EQ(shown[4].iconWidth, 2);
	EXPECT_EQ(shown[4].iconHeight, 1);
	EXPECT_TRUE(shown[5].icon.empty());
	EXPECT_EQ(information.Icon(), nullptr);
}


} // namespace
