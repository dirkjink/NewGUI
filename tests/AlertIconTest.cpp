/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Alert.h>

#include <chrono>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Bitmap.h>
#include <ngui/IconUtils.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/svg/LunaSvgRenderer.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");


// An alert that answers kAsk with the bounds of its icon, if it has one.
class IconAlert : public BAlert {
public:
	using BAlert::BAlert;

	void MessageReceived(BMessage* message) override
	{
		if (message->what != kAsk) {
			BAlert::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		if (BBitmap* icon = Icon())
			(void)reply.AddRect("icon", icon->Bounds());
		(void)message->SendReply(reply);
	}
};


// The bounds of the icon of the alert of \a type shown by Go(), optionally
// with the icon \a given; std::nullopt for none.
std::optional<gfx::Rect>
IconOf(BAlertType type, std::unique_ptr<BBitmap> given = nullptr)
{
	auto alert = std::make_unique<IconAlert>("Title", "Text", "OK", "", "",
		BButtonWidth::AsUsual, type);
	if (given != nullptr)
		alert->SetIcon(std::move(given));
	const BMessenger shown = *BAlert::Go(std::move(alert), [](int32) {});
	BMessage reply;
	(void)shown.SendMessage(BMessage(kAsk), &reply);
	BMessage key(kKeyDown);
	(void)key.AddString("bytes", "\n");
	(void)shown.SendMessage(key);
	(void)shown.WaitForLooperQuit(5s);
	if (!reply.HasRect("icon"))
		return std::nullopt;
	return reply.GetRect("icon", {});
}


TEST(AlertIconTest, TheTypeGivesTheIcon)
{
	// What is tested: With the fixed text system (plain font 12) and the
	// lunasvg renderer: alerts of every type shown by Go(); a warning alert
	// given a 16 × 16 icon by SetIcon(); an info alert without a renderer.
	// Success criteria: Info, idea, warning and stop alerts show their
	// system icon at Haiku's large icon size, 32 × 32 for a 12 pixel font;
	// an empty alert shows none. An icon given by SetIcon() takes the type
	// icon's place at its own size. Without a renderer there is no icon,
	// and the alert still shows.
	// Why it matters: The type icon tells at a glance what an alert is
	// about, as in Haiku; NewGUI draws its own SVG icons.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	const svg::LunaSvgRenderer renderer;
	VectorIconRenderer::SetDefault(&renderer);
	const gfx::Rect large{0, 0, 32, 32};

	// When
	const auto info = IconOf(BAlertType::Info);
	const auto idea = IconOf(BAlertType::Idea);
	const auto warning = IconOf(BAlertType::Warning);
	const auto stop = IconOf(BAlertType::Stop);
	const auto empty = IconOf(BAlertType::Empty);
	const auto given = IconOf(BAlertType::Warning,
		std::make_unique<BBitmap>(gfx::Rect{0, 0, 16, 16}, BColorSpace::RGBA32));
	VectorIconRenderer::SetDefault(nullptr);
	const auto withoutRenderer = IconOf(BAlertType::Info);

	// Then
	EXPECT_EQ(info, large);
	EXPECT_EQ(idea, large);
	EXPECT_EQ(warning, large);
	EXPECT_EQ(stop, large);
	EXPECT_EQ(empty, std::nullopt);
	EXPECT_EQ(given, (gfx::Rect{0, 0, 16, 16}));
	EXPECT_EQ(withoutRenderer, std::nullopt);
	TextSystem::SetDefault(nullptr);
}


} // namespace
