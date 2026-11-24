/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Control.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/LookRegistry.h>
#include <ngui/Message.h>
#include <ngui/RadioButton.h>
#include <ngui/Slider.h>
#include <ngui/StatusBar.h>
#include <ngui/StringView.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Dump.h>

#include "support/FixedTextSystem.h"
#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kPressed = FourCharCode("PRSS");
constexpr std::uint32_t kModified = FourCharCode("MDFY");


// A looper that receives the controls' messages and keeps their what and
// "be:value".
class Target {
public:
	Target()
	{
		auto looper = std::make_unique<RecordingLooper>(fRecord);
		looper->action = [this](BMessage& message) {
			{
				std::scoped_lock guard(fLock);
				fValues.push_back(message.GetInt32("be:value", -1));
				fHasSource.push_back(message.FindPointer("source").has_value());
			}
			fChanged.notify_all();
		};
		fMessenger = *BLooper::Run(std::move(looper));
	}

	~Target()
	{
		(void)fMessenger.SendMessage(kQuitRequested);
		(void)fMessenger.WaitForLooperQuit(5s);
	}

	const BMessenger& Messenger() const
	{
		return fMessenger;
	}

	// The "be:value"s of the first \a count messages; empty if they do not
	// come. It waits for the values themselves: the record has an entry
	// before the action has stored the value.
	std::vector<std::int32_t> Values(std::size_t count)
	{
		std::unique_lock guard(fLock);
		if (!fChanged.wait_for(guard, 5s, [&] { return fValues.size() >= count; }))
			return {};
		return fValues;
	}

	std::vector<std::string> Received()
	{
		return fRecord->Received();
	}

	bool AllHaveASource()
	{
		std::scoped_lock guard(fLock);
		for (bool has : fHasSource) {
			if (!has)
				return false;
		}
		return true;
	}

private:
	std::shared_ptr<LooperRecord> fRecord = std::make_shared<LooperRecord>();
	std::mutex fLock;
	std::condition_variable fChanged;
	std::vector<std::int32_t> fValues;
	std::vector<bool> fHasSource;
	BMessenger fMessenger;
};


std::unique_ptr<BMessage>
Pressed()
{
	return std::make_unique<BMessage>(kPressed);
}


BMessage
Key(std::string_view bytes, BModifierFlags modifiers = {})
{
	BMessage message(kKeyDown);
	(void)message.AddString("bytes", bytes);
	(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
	return message;
}


TEST(ControlTest, StateAndTheLooksFlags)
{
	// What is tested: A control's label, value and enabled state, and the
	// flags the look derives from them: off, on, partially on, disabled,
	// and inside a parent that draws on its children.
	// Success criteria: The flags are IsControl, plus Activated for on,
	// PartiallyActivated for partially on, Disabled when disabled, and
	// BlendFrame in such a parent. Disabling takes Navigable away and
	// enabling gives it back; an empty label is no label.
	// Why it matters: The look draws every control by these flags (Haiku's
	// BControlLook::Flags()); a disabled control must not get the focus.
	//
	// Given
	BView parent({0, 0, 100, 100}, "parent", kFollowLeftTop,
		BViewFlag::WillDraw | BViewFlag::DrawOnChildren);
	auto* control = static_cast<BControl*>(parent.AddChild(std::make_unique<BControl>(
		gfx::Rect{0, 0, 50, 20}, "control", "Label", Pressed(), kFollowLeftTop,
		BViewFlag::WillDraw | BViewFlag::Navigable)));
	BControl lone({0, 0, 50, 20}, "lone", "", nullptr, kFollowLeftTop, BViewFlag::Navigable);
	const ControlLook& look = *LookRegistry::DefaultLook();

	// When
	const ControlStateFlags off = look.Flags(lone);
	lone.SetValue(kControlOn);
	const ControlStateFlags on = look.Flags(lone);
	lone.SetValue(kControlPartiallyOn);
	const ControlStateFlags partially = look.Flags(lone);
	lone.SetEnabled(false);
	const ControlStateFlags disabled = look.Flags(lone);
	const bool navigableWhileDisabled = lone.Flags().Has(BViewFlag::Navigable);
	lone.SetEnabled(true);

	// Then
	EXPECT_EQ(control->Label(), "Label");
	EXPECT_TRUE(lone.Label().empty());
	EXPECT_EQ(off, ControlStateFlags(ControlState::IsControl));
	EXPECT_EQ(on, ControlState::IsControl | ControlState::Activated);
	EXPECT_EQ(partially, ControlState::IsControl | ControlState::PartiallyActivated);
	EXPECT_EQ(disabled, ControlState::IsControl | ControlState::PartiallyActivated
		| ControlState::Disabled);
	EXPECT_FALSE(navigableWhileDisabled);
	EXPECT_TRUE(lone.Flags().Has(BViewFlag::Navigable));
	EXPECT_EQ(look.Flags(*control), ControlState::IsControl | ControlState::BlendFrame);
}


TEST(ControlTest, InvokeSendsTheValue)
{
	// What is tested: A control with a message and a target: Invoke() with
	// the value on, then space and Enter typed, then space while disabled;
	// and Invoke() of a control without a message.
	// Success criteria: Three messages arrive, each with "source" and
	// "be:value": 1, then 0 and 1 (each key toggles the value before it
	// invokes); the disabled control sends nothing; without a message
	// Invoke() fails with InvalidArgument.
	// Why it matters: Applications learn of the user's changes from these
	// messages (BInvoker); the fields are Haiku's.
	//
	// Given
	Target target;
	BControl control({0, 0, 50, 20}, "control", "Label", Pressed(), kFollowLeftTop,
		BViewFlag::WillDraw);
	control.SetTarget(target.Messenger());
	BControl silent({0, 0, 50, 20}, "silent", "", nullptr, kFollowLeftTop, BViewFlags());

	// When
	control.SetValue(kControlOn);
	const auto invoked = control.Invoke();
	control.KeyDown(" ");
	control.KeyDown("\n");
	control.SetEnabled(false);
	control.KeyDown(" ");
	const auto failed = silent.Invoke();

	// Then
	EXPECT_TRUE(invoked.has_value());
	EXPECT_EQ(target.Values(3), (std::vector<std::int32_t>{1, 0, 1}));
	EXPECT_TRUE(target.AllHaveASource());
	EXPECT_EQ(target.Received().size(), 3u);
	EXPECT_EQ(failed.error().code, ErrorCode::InvalidArgument);
}


TEST(ButtonTest, PreferredSizeFollowsHaikusFormula)
{
	// What is tested: The preferred size of buttons with the BeOS look and
	// the 12 px placeholder font (7.2 px per character, ascent 9, descent
	// 3): "OK", a long label, "OK" as default button, the minimum height of
	// the frame constructor, and an explicit minimum.
	// Success criteria: Haiku's formula with insets 3, label spacing 6:
	// width max(11, 20) + ceil(14.4) = 35, at least 6 · 12.5 = 75; height
	// ceil(12 · 1.8) = 22 plus 2 up to the margins 3 + 3 + 6: 24. In pixel
	// counts 76 × 25. The long label (24 characters, 172.8 px) gives
	// 20 + 173 + 1 = 194. As default button the insets are 6: height
	// 22 + 8 + 1 = 31. A button made 80 × 10 is raised to 12 + 12 + 1 =
	// 25. An explicit minimum wins.
	// Why it matters: Buttons must come out as large as in Haiku so that
	// ported windows lay out the same (W2).
	//
	// Given
	BButton ok("OK");
	BButton longButton("A very long button label");
	BButton defaultButton("OK");
	BButton framed({0, 0, 80, 10}, "framed", "Frame", nullptr);
	BButton explicitMin("OK");

	// When
	defaultButton.MakeDefault(true);
	explicitMin.SetExplicitMinSize({100, kSizeUnset});

	// Then
	EXPECT_EQ(ok.PreferredSize(), (gfx::Size{76, 25}));
	EXPECT_EQ(ok.MinSize(), (gfx::Size{76, 25}));
	EXPECT_EQ(ok.MaxSize(), (gfx::Size{76, 25}));
	EXPECT_EQ(longButton.PreferredSize(), (gfx::Size{194, 25}));
	EXPECT_TRUE(defaultButton.IsDefault());
	EXPECT_EQ(defaultButton.PreferredSize(), (gfx::Size{76, 31}));
	EXPECT_EQ(framed.Frame(), (gfx::Rect{0, 0, 80, 25}));
	EXPECT_EQ(explicitMin.MinSize(), (gfx::Size{100, 25}));
}


TEST(ButtonTest, AClickInsideInvokesOnce)
{
	// What is tested: A push button pressed, moved out, moved back in and
	// released inside; pressed and released outside; a toggle button
	// clicked twice; a disabled button clicked.
	// Success criteria: The push button is on while the mouse is inside and
	// pressed, off outside; released inside it sends once with "be:value"
	// 1 and is off again; released outside it sends nothing. The toggle
	// button sends 1, then 0, and keeps its value. The disabled one stays
	// off and sends nothing.
	// Why it matters: That is how BButton tracks a click (Haiku's
	// asynchronous controls); a click dragged away is a cancel.
	//
	// Given
	Target target;
	BWindow window({0, 0, 300, 100}, "buttons");
	auto* push = static_cast<BButton*>(window.AddChild(std::make_unique<BButton>(
		gfx::Rect{10, 10, 90, 40}, "push", "Push", Pressed())));
	auto* toggle = static_cast<BButton*>(window.AddChild(std::make_unique<BButton>(
		gfx::Rect{100, 10, 180, 40}, "toggle", "Toggle", Pressed())));
	auto* disabled = static_cast<BButton*>(window.AddChild(std::make_unique<BButton>(
		gfx::Rect{190, 10, 270, 40}, "disabled", "Disabled", Pressed())));
	push->SetTarget(target.Messenger());
	toggle->SetTarget(target.Messenger());
	disabled->SetTarget(target.Messenger());
	toggle->SetBehavior(BButton::BBehavior::Toggle);
	disabled->SetEnabled(false);

	// When
	push->MouseDown({5, 5});
	const int32 pressed = push->Value();
	push->MouseMoved({200, 5}, BMouseTransit::Exited, nullptr);
	const int32 outside = push->Value();
	push->MouseMoved({6, 6}, BMouseTransit::Entered, nullptr);
	const int32 back = push->Value();
	push->MouseUp({6, 6});
	const int32 released = push->Value();
	push->MouseDown({5, 5});
	push->MouseMoved({200, 5}, BMouseTransit::Exited, nullptr);
	push->MouseUp({200, 5});
	toggle->MouseDown({5, 5});
	toggle->MouseUp({5, 5});
	const int32 toggledOn = toggle->Value();
	toggle->MouseDown({5, 5});
	toggle->MouseUp({5, 5});
	disabled->MouseDown({5, 5});
	disabled->MouseUp({5, 5});

	// Then
	EXPECT_EQ(pressed, kControlOn);
	EXPECT_EQ(outside, kControlOff);
	EXPECT_EQ(back, kControlOn);
	EXPECT_EQ(released, kControlOff);
	EXPECT_EQ(toggledOn, kControlOn);
	EXPECT_EQ(toggle->Value(), kControlOff);
	EXPECT_EQ(disabled->Value(), kControlOff);
	EXPECT_EQ(target.Values(3), (std::vector<std::int32_t>{1, 1, 0}));
	EXPECT_EQ(target.Received().size(), 3u);
}


TEST(ButtonTest, EnterGoesToTheDefaultButton)
{
	// What is tested: A window with buttons a and b; a made the default
	// button, b given the focus; Enter, space and Shift+Enter sent to the
	// window; then b made the default button; the window activated.
	// Success criteria: Enter invokes a, the default button; space invokes
	// b, the focus; Shift+Enter goes to the focus as well (the window keeps
	// Command keys for its shortcuts). Making b the
	// default takes it from a. b has the Focused flag only once the window
	// is active.
	// Why it matters: Dialogs are confirmed with Enter, whatever has the
	// focus (Haiku's _DetermineTarget()).
	//
	// Given
	Target aTarget;
	Target bTarget;
	BWindow window({0, 0, 300, 100}, "dialog");
	auto* a = static_cast<BButton*>(window.AddChild(std::make_unique<BButton>(
		gfx::Rect{10, 10, 90, 40}, "a", "A", Pressed())));
	auto* b = static_cast<BButton*>(window.AddChild(std::make_unique<BButton>(
		gfx::Rect{100, 10, 180, 40}, "b", "B", Pressed())));
	a->SetTarget(aTarget.Messenger());
	b->SetTarget(bTarget.Messenger());
	a->MakeDefault(true);
	b->MakeFocus(true);
	const bool focusedInactive = a->Look().Flags(*b).Has(ControlState::Focused);

	// When
	BMessage enter = Key("\n");
	window.DispatchMessage(&enter, &window);
	BMessage space = Key(" ");
	window.DispatchMessage(&space, &window);
	BMessage shiftEnter = Key("\n", BModifier::Shift);
	window.DispatchMessage(&shiftEnter, &window);
	BButton* defaultBefore = window.DefaultButton();
	b->MakeDefault(true);
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	window.DispatchMessage(&activated, &window);

	// Then
	EXPECT_EQ(defaultBefore, a);
	EXPECT_EQ(aTarget.Values(1), (std::vector<std::int32_t>{1}));
	EXPECT_EQ(bTarget.Values(2), (std::vector<std::int32_t>{1, 1}));
	EXPECT_EQ(window.DefaultButton(), b);
	EXPECT_FALSE(a->IsDefault());
	EXPECT_TRUE(b->IsDefault());
	EXPECT_FALSE(focusedInactive);
	EXPECT_TRUE(window.Look().Flags(*b).Has(ControlState::Focused));
}


TEST(StringViewTest, SizesAndAlignment)
{
	// What is tested: A string view of "Hello\nWorld!" with the placeholder
	// font; its text changed; aligned right; resized to its preferred size
	// while wider.
	// Success criteria: The preferred width is the widest line, "World!",
	// 6 · 7.2 + 1 = 44.2; the height two lines of ceil(9 + 3) plus 1 = 25.
	// A new text changes the width. Its layout alignment is its alignment,
	// vertically centered. Aligned right, ResizeToPreferred() keeps a
	// larger width and sets the height.
	// Why it matters: Labels in layouts are string views; Haiku's sizes
	// decide how forms line up.
	//
	// Given
	BStringView view({0, 0, 200, 10}, "view", "Hello\nWorld!");

	// When
	const gfx::Size preferred = view.PreferredSize();
	const BAlignment left = view.LayoutAlignment();
	view.SetText("Hi");
	const float shortWidth = view.PreferredSize().w;
	view.SetAlignment(BHorizontalAlignment::Right);
	view.ResizeToPreferred();

	// Then
	EXPECT_EQ(preferred, (gfx::Size{44.2f, 25}));
	EXPECT_EQ(left, (BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle}));
	EXPECT_NEAR(shortWidth, 2 * 7.2f + 1, 1e-4f);
	EXPECT_EQ(view.LayoutAlignment(),
		(BAlignment{BHorizontalAlignment::Right, BVerticalAlignment::Middle}));
	EXPECT_EQ(view.Frame(), (gfx::Rect{0, 0, 200, 13}));
	EXPECT_EQ(view.Text(), "Hi");
}


TEST(StringViewTest, LinesAreDrawnWhereHaikuDrawsThem)
{
	// What is tested: A string view of 100 × 30 with "Hi\nYou", aligned
	// right, drawn with fixed metrics (7 px per character, ascent 9,
	// descent 3, leading 1); and "Hello" truncated in a view 30 wide.
	// Success criteria: Haiku's baselines: the last line at 29 - 3 = 26, the
	// one above 13 higher at 13; right aligned at 99 minus the width: "Hi"
	// at 85, "You" at 78 (Haiku's last column is 99). The truncated line
	// fits the 29 pixels: "Hell" at (0, 19 - 3 = 16).
	// Why it matters: Text must sit on the same pixels as in Haiku for the
	// looks to match screenshots (specification 21).
	//
	// Given
	FixedTextSystem text;
	const gfx::Caps caps;
	BStringView lines({0, 0, 100, 30}, "lines", "Hi\nYou");
	lines.SetAlignment(BHorizontalAlignment::Right);
	BStringView narrow({0, 0, 30, 20}, "narrow", "Hello");
	narrow.SetTruncation(TruncationMode::End);

	// When
	gfx::Canvas canvas({0, 0, 100, 30});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	lines.Draw(context);
	const std::string linesDump = gfx::DumpText(*canvas.Finish());
	gfx::Canvas narrowCanvas({0, 0, 30, 20});
	DrawContext narrowContext{narrowCanvas, caps, Palette::Default(), text, 1};
	narrow.Draw(narrowContext);
	const std::string narrowDump = gfx::DumpText(*narrowCanvas.Finish());

	// Then
	EXPECT_NE(linesDump.find("origin=(85 13)"), std::string::npos) << linesDump;
	EXPECT_NE(linesDump.find("origin=(78 26)"), std::string::npos) << linesDump;
	EXPECT_NE(narrowDump.find("origin=(0 16)"), std::string::npos) << narrowDump;
	EXPECT_NE(narrowDump.find("glyphs=72@(0 0),101@(7 0),108@(14 0),108@(21 0)\n"),
		std::string::npos) << narrowDump;
}


TEST(CheckBoxTest, PreferredSizeFollowsHaikusFormula)
{
	// What is tested: The sizes of check boxes with the placeholder font
	// (7.2 px per character, ascent 9, descent 3) and the BeOS look (label
	// spacing 6): "Check", no label, the minimum height of the frame
	// constructor, the layout alignment.
	// Success criteria: Haiku's box frame (0, 2, 12, 14) inclusive is
	// (0, 2, 13, 15) half-open; the preferred width 12 + 0 + 6 + ceil(36)
	// = 54, the height max(14 + 2, ceil(6 + 9 + 3)) = 18, as pixel counts
	// 55 × 19; without a label 13 × 17; a check box made 10 high is raised
	// to 19; aligned left and vertically centered.
	// Why it matters: Forms with check boxes must lay out as in Haiku.
	//
	// Given
	BCheckBox check("Check");
	BCheckBox bare("");
	BCheckBox framed({0, 0, 80, 10}, "framed", "Framed", nullptr);

	// When / Then
	EXPECT_EQ(check.PreferredSize(), (gfx::Size{55, 19}));
	EXPECT_EQ(check.MaxSize(), (gfx::Size{55, 19}));
	EXPECT_EQ(bare.PreferredSize(), (gfx::Size{13, 17}));
	EXPECT_EQ(framed.Frame(), (gfx::Rect{0, 0, 80, 19}));
	EXPECT_EQ(check.LayoutAlignment(),
		(BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle}));
}


TEST(CheckBoxTest, ClicksAndKeysCycleTheStates)
{
	// What is tested: A check box clicked (pressed and released inside),
	// pressed and dragged out, toggled with space and Enter, set partially
	// on and clicked, set partially on with partial-to-off and clicked, and
	// set to an unknown value.
	// Success criteria: Clicks and keys go off -> on -> off -> on and send
	// each new value; a click dragged out changes nothing; partially on
	// goes to on, with partial-to-off to off; an unknown value is on.
	// Why it matters: Haiku's _NextState() rules, and that the user can
	// cancel a click by dragging away.
	//
	// Given
	Target target;
	BWindow window({0, 0, 200, 100}, "check");
	auto* check = static_cast<BCheckBox*>(window.AddChild(std::make_unique<BCheckBox>(
		gfx::Rect{10, 10, 100, 30}, "check", "Check", Pressed())));
	check->SetTarget(target.Messenger());

	// When
	check->MouseDown({5, 5});
	check->MouseUp({5, 5});
	check->MouseDown({5, 5});
	check->MouseMoved({150, 5}, BMouseTransit::Exited, nullptr);
	check->MouseUp({150, 5});
	const int32 afterCancel = check->Value();
	check->KeyDown(" ");
	check->KeyDown("\n");
	check->SetValue(kControlPartiallyOn);
	check->MouseDown({5, 5});
	check->MouseUp({5, 5});
	const int32 partialToOn = check->Value();
	check->SetValue(kControlPartiallyOn);
	check->SetPartialStateToOff(true);
	check->MouseDown({5, 5});
	check->MouseUp({5, 5});
	const int32 partialToOff = check->Value();
	check->SetValue(5);

	// Then
	EXPECT_EQ(afterCancel, kControlOn);
	EXPECT_EQ(partialToOn, kControlOn);
	EXPECT_EQ(partialToOff, kControlOff);
	EXPECT_EQ(check->Value(), kControlOn);
	EXPECT_TRUE(check->IsPartialStateToOff());
	EXPECT_EQ(target.Values(5), (std::vector<std::int32_t>{1, 0, 1, 1, 0}));
}


TEST(RadioButtonTest, OnlyOneSiblingIsOn)
{
	// What is tested: Three radio buttons and a check box in one parent: b
	// clicked, then c set on, then c clicked again; a turned on with space;
	// Enter on a, which is on; a click on b dragged out.
	// Success criteria: Turning one on turns the other radio buttons off,
	// not the check box; a click sends 1; a click on a button that is on
	// sends nothing; space turns a on and sends; Enter on an on button does
	// nothing (it cannot be turned off); a click dragged out changes
	// nothing.
	// Why it matters: Radio buttons are a choice of one (Haiku's
	// BRadioButton::SetValue() walks the siblings).
	//
	// Given
	Target target;
	BWindow window({0, 0, 300, 100}, "radios");
	BView* parent = window.AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 300, 100},
		"group"));
	std::vector<BRadioButton*> radios;
	for (int i = 0; i < 3; i++) {
		radios.push_back(static_cast<BRadioButton*>(parent->AddChild(
			std::make_unique<BRadioButton>(gfx::Rect{10.0f + 90.0f * float(i), 10,
				90.0f + 90.0f * float(i), 30}, "radio", "Radio", Pressed()))));
		radios.back()->SetTarget(target.Messenger());
	}
	auto* check = static_cast<BCheckBox*>(parent->AddChild(std::make_unique<BCheckBox>(
		gfx::Rect{10, 50, 90, 70}, "check", "Check", nullptr)));
	check->SetValue(kControlOn);
	BRadioButton* a = radios[0];
	BRadioButton* b = radios[1];
	BRadioButton* c = radios[2];

	// When
	b->MouseDown({5, 5});
	b->MouseUp({5, 5});
	const bool bOn = b->Value() == kControlOn;
	c->SetValue(kControlOn);
	const bool bOffAfterC = b->Value() == kControlOff;
	c->MouseDown({5, 5});
	c->MouseUp({5, 5});
	a->KeyDown(" ");
	a->KeyDown("\n");
	b->MouseDown({5, 5});
	b->MouseMoved({200, 5}, BMouseTransit::Exited, nullptr);
	b->MouseUp({200, 5});

	// Then
	EXPECT_TRUE(bOn);
	EXPECT_TRUE(bOffAfterC);
	EXPECT_EQ(a->Value(), kControlOn);
	EXPECT_EQ(b->Value(), kControlOff);
	EXPECT_EQ(c->Value(), kControlOff);
	EXPECT_EQ(check->Value(), kControlOn);
	EXPECT_EQ(target.Values(2), (std::vector<std::int32_t>{1, 1}));
	EXPECT_EQ(target.Received().size(), 2u);
	EXPECT_EQ(b->PreferredSize(), (gfx::Size{55, 19}));
	EXPECT_EQ(b->LayoutAlignment(),
		(BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle}));
}


TEST(SliderTest, SizesFollowHaikusFormula)
{
	// What is tested: The sizes of sliders with the placeholder font
	// (ascent 9, descent 3, no leading, 7.2 px per character): horizontal
	// with the label "Volume"; with limit labels "Low" and "High" (frame
	// constructor, outside a layout); vertical with the label "Vol".
	// Success criteria: Haiku's minimum: height 12 + 6 (bar) plus one row
	// of 9 + 3 + 4 = 34, width max(43.2, 32) = 43.2, as pixel counts
	// 44.2 × 35; preferred 101 wide (100 + 1), unlimited maximum width.
	// With limit labels 21.6 + 14.4 (two "M") + 28.8 = 64.8 wide and a
	// second row: 65.8 × 51, and the slider made 10 high grows to 51.
	// The vertical one is max(21.6, 18) wide and 32 + 12 high: 22.6 × 45,
	// preferred 101 high.
	// Why it matters: Sliders in Haiku's preference windows get their size
	// from these formulas.
	//
	// Given
	BSlider volume({0, 0, 200, 35}, "volume", "Volume", nullptr, 0, 100);
	BSlider limited({0, 0, 200, 10}, "limited", "Volume", nullptr, 0, 100);
	BSlider vertical("vertical", "Vol", nullptr, 0, 10, BOrientation::Vertical);

	// When
	limited.SetLimitLabels("Low", "High");

	// Then
	EXPECT_EQ(volume.MinSize(), (gfx::Size{44.2f, 35}));
	EXPECT_EQ(volume.PreferredSize(), (gfx::Size{101, 35}));
	EXPECT_EQ(volume.MaxSize(), (gfx::Size{kSizeUnlimited, 35}));
	EXPECT_EQ(limited.MinSize(), (gfx::Size{65.8f, 51}));
	EXPECT_EQ(limited.Frame(), (gfx::Rect{0, 0, 200, 51}));
	EXPECT_EQ(vertical.MinSize(), (gfx::Size{22.6f, 45}));
	EXPECT_EQ(vertical.PreferredSize(), (gfx::Size{22.6f, 101}));
	EXPECT_EQ(limited.MinLimitLabel(), "Low");
}


TEST(SliderTest, GeometryIsHaikus)
{
	// What is tested: The bar, the thumb and the value for points of a
	// slider 200 × 35 from 0 to 100 with a label, block thumb at 50; and of
	// a triangle thumb without label at 100; SetPosition().
	// Success criteria: Haiku's inclusive bar (8, 22, 191, 28) is (8, 22,
	// 192, 29) half-open; the thumb at 50 is at floor(0.5 · 181 + 9) - 8 =
	// 91: (91, 18, 109, 32). The positions run from 9 to 190: x 9 is 0, 190
	// is 100, 99.5 is 50, 55 is 25, beyond the bar the limits. The triangle
	// bar is inset by 7: (7, 6, 193, 13), its thumb at 100 is (185, 9, 198,
	// 18). SetPosition(0.25) is 25.
	// Why it matters: The thumb must be where the user clicks and where
	// Haiku draws it.
	//
	// Given
	BSlider slider({0, 0, 200, 35}, "slider", "Volume", nullptr, 0, 100);
	BSlider triangle({0, 0, 200, 35}, "triangle", "", nullptr, 0, 100,
		BThumbStyle::Triangle);

	// When
	slider.SetValue(50);
	triangle.SetValue(100);

	// Then
	EXPECT_EQ(slider.BarFrame(), (gfx::Rect{8, 22, 192, 29}));
	EXPECT_EQ(slider.ThumbFrame(), (gfx::Rect{91, 18, 109, 32}));
	EXPECT_EQ(slider.ValueForPoint({9, 0}), 0);
	EXPECT_EQ(slider.ValueForPoint({190, 0}), 100);
	EXPECT_EQ(slider.ValueForPoint({99.5f, 0}), 50);
	EXPECT_EQ(slider.ValueForPoint({55, 0}), 25);
	EXPECT_EQ(slider.ValueForPoint({-20, 0}), 0);
	EXPECT_EQ(slider.ValueForPoint({300, 0}), 100);
	EXPECT_FLOAT_EQ(slider.Position(), 0.5f);
	EXPECT_EQ(triangle.BarFrame(), (gfx::Rect{7, 6, 193, 13}));
	EXPECT_EQ(triangle.ThumbFrame(), (gfx::Rect{185, 9, 198, 18}));
	slider.SetPosition(0.25f);
	EXPECT_EQ(slider.Value(), 25);
}


TEST(SliderTest, DraggingAndKeysSendModificationsThenTheValue)
{
	// What is tested: A slider from 0 to 100 with a message and a
	// modification message: pressed at x 55, dragged to 190 and released;
	// then Home, End pressed and released, and another key released; the
	// limits set to 10 .. 20.
	// Success criteria: Pressing sends a modification with 25, dragging one
	// with 100, releasing the message with 100. Home sends a modification
	// with 0, the key up the message with 0; End likewise 100; a key up
	// without change sends nothing (Haiku sent the message again). New
	// limits move the value into them: 20.
	// Why it matters: Applications follow the value live (the modification
	// message) and act on the final one; both must come as in Haiku.
	//
	// Given
	Target target;
	BWindow window({0, 0, 300, 100}, "sliders");
	auto* slider = static_cast<BSlider*>(window.AddChild(std::make_unique<BSlider>(
		gfx::Rect{0, 0, 200, 35}, "slider", "Volume", Pressed(), 0, 100)));
	slider->SetModificationMessage(std::make_unique<BMessage>(kModified));
	slider->SetTarget(target.Messenger());
	slider->ResizeTo(200, 35);

	// When
	slider->MouseDown({55, 25});
	slider->MouseMoved({190, 25}, BMouseTransit::Inside, nullptr);
	slider->MouseUp({190, 25});
	slider->KeyDown(std::string(1, keys::kHome));
	slider->KeyUp(std::string(1, keys::kHome));
	slider->KeyDown(std::string(1, keys::kEnd));
	slider->KeyUp(std::string(1, keys::kEnd));
	slider->KeyUp(std::string(1, keys::kEnd));
	slider->SetLimits(10, 20);

	// Then
	EXPECT_EQ(target.Values(7), (std::vector<std::int32_t>{25, 100, 100, 0, 0, 100, 100}));
	const std::string modified = "looper:" + std::to_string(kModified);
	const std::string pressed = "looper:" + std::to_string(kPressed);
	EXPECT_EQ(target.Received(), (std::vector<std::string>{modified, modified, pressed,
		modified, pressed, modified, pressed}));
	EXPECT_EQ(slider->Value(), 20);
}


TEST(SliderTest, ArrowKeysStepWithinTheLimits)
{
	// What is tested: A slider from 0 to 100 at 50 with a key increment
	// of 30: Right, Up, Left three times, Down, an unrelated key; then
	// disabled, Right. Its bar thickness set to 10, 0.3 and 6.6, its
	// snooze amount to 5 and 2 000 000.
	// Success criteria: Right and Up add 30, stopping at 100; Left and Down
	// subtract it, stopping at 0; another key changes nothing; a disabled
	// slider ignores keys. The thickness is rounded and at least 1: 10,
	// 1, 7; the minimum height follows it (12 + thickness + a row of 16).
	// The snooze amount stays within 10 000 .. 1 000 000 microseconds.
	// Why it matters: Sliders are worked with the keyboard as in Haiku;
	// their settings stay within Haiku's bounds.
	//
	// Given
	BSlider slider({0, 0, 200, 35}, "volume", "Volume", nullptr, 0, 100);
	slider.SetValue(50);
	slider.SetKeyIncrementValue(30);
	std::vector<std::int32_t> values;
	const auto press = [&](char key) {
		slider.KeyDown(std::string(1, key));
		values.push_back(slider.Value());
	};

	// When
	press(keys::kRightArrow);
	press(keys::kUpArrow);
	press(keys::kLeftArrow);
	press(keys::kLeftArrow);
	press(keys::kLeftArrow);
	press(keys::kLeftArrow);
	press(keys::kUpArrow);
	press(keys::kDownArrow);
	press('x');
	slider.SetEnabled(false);
	press(keys::kRightArrow);
	slider.SetBarThickness(10);
	const float thick = slider.BarThickness();
	const gfx::Size thickMin = slider.MinSize();
	slider.SetBarThickness(0.3f);
	const float thin = slider.BarThickness();
	slider.SetBarThickness(6.6f);
	const float rounded = slider.BarThickness();
	slider.SetSnoozeAmount(5);
	const int32 shortest = slider.SnoozeAmount();
	slider.SetSnoozeAmount(2'000'000);
	const int32 longest = slider.SnoozeAmount();

	// Then
	EXPECT_EQ(slider.KeyIncrementValue(), 30);
	EXPECT_EQ(values, (std::vector<std::int32_t>{80, 100, 70, 40, 10, 0, 30, 0, 0, 0}));
	EXPECT_FLOAT_EQ(thick, 10);
	EXPECT_EQ(thickMin.h, 39);
	EXPECT_FLOAT_EQ(thin, 1);
	EXPECT_FLOAT_EQ(rounded, 7);
	EXPECT_EQ(shortest, 10'000);
	EXPECT_EQ(longest, 1'000'000);
}


TEST(StatusBarTest, SizesFollowHaikusFormula)
{
	// What is tested: The sizes of status bars with the placeholder font
	// (ascent 9, descent 3, 7.2 px per character): in a layout with the
	// label "Copying" and the text "file.txt"; in a layout without any text;
	// at a frame without any text; after SetBarHeight(10) at the frame.
	// Success criteria: The bar is 9 + 3 + 5 = 17 high, a text row 12 + 6 =
	// 18. With texts ceil(50.4) + ceil(57.6) + 5 = 114 wide and 18 + 17 =
	// 35 high, as pixel counts 115 × 36, maximum width unlimited. Without
	// text in a layout only the bar: 6 × 18. At a frame there is always
	// room for text, as in BeOS: 36 high; with a bar of 10 the frame
	// becomes 18 + 10 + 1 = 29 high.
	// Why it matters: Progress windows are laid out with these sizes.
	//
	// Given
	BStatusBar copying("copying", "Copying");
	BStatusBar empty("empty");
	BStatusBar framed({0, 0, 200, 20}, "framed");

	// When
	copying.SetText("file.txt");
	const gfx::Size framedPreferred = framed.PreferredSize();
	framed.SetBarHeight(10);

	// Then
	EXPECT_FLOAT_EQ(copying.BarHeight(), 17);
	EXPECT_EQ(copying.PreferredSize(), (gfx::Size{115, 36}));
	EXPECT_EQ(copying.MinSize(), (gfx::Size{115, 36}));
	EXPECT_EQ(copying.MaxSize(), (gfx::Size{kSizeUnlimited, 36}));
	EXPECT_EQ(empty.PreferredSize(), (gfx::Size{6, 18}));
	EXPECT_EQ(framedPreferred, (gfx::Size{6, 36}));
	EXPECT_EQ(framed.Frame(), (gfx::Rect{0, 0, 200, 29}));
}


TEST(StatusBarTest, ValuesAndMessagesUpdateIt)
{
	// What is tested: SetTo() beyond both ends, Update() with and without
	// texts, Reset(), the messages kUpdateStatusBar and kResetStatusBar,
	// SetBarColor().
	// Success criteria: Values are kept within 0 .. 100. Update(30, "a")
	// sets the text and keeps the trailing text; Update(10) keeps both.
	// Reset() replaces the labels, clears the texts and the value. The
	// update message adds its delta and sets its text, the reset message
	// sets the labels. The bar color is the palette's until one is set.
	// Why it matters: Programs drive the status bar with these calls and
	// messages from their worker threads.
	//
	// Given
	BStatusBar bar("bar", "Label", "Trailing");

	// When / Then
	EXPECT_EQ(bar.BarColor(), Palette::Default().Get(BColorWhich::StatusBar));
	bar.SetTo(150, "text", "trailing");
	EXPECT_FLOAT_EQ(bar.CurrentValue(), 100);
	bar.SetTo(-5, "text", "trailing");
	EXPECT_FLOAT_EQ(bar.CurrentValue(), 0);
	bar.Update(30, "a");
	EXPECT_FLOAT_EQ(bar.CurrentValue(), 30);
	EXPECT_EQ(bar.Text(), "a");
	EXPECT_EQ(bar.TrailingText(), "trailing");
	bar.Update(10);
	EXPECT_FLOAT_EQ(bar.CurrentValue(), 40);
	EXPECT_EQ(bar.Text(), "a");
	EXPECT_EQ(bar.TrailingText(), "trailing");

	BMessage update(kUpdateStatusBar);
	ASSERT_TRUE(update.AddFloat("delta", 25).has_value());
	ASSERT_TRUE(update.AddString("text", "x").has_value());
	bar.MessageReceived(&update);
	EXPECT_FLOAT_EQ(bar.CurrentValue(), 65);
	EXPECT_EQ(bar.Text(), "x");
	EXPECT_EQ(bar.TrailingText(), "trailing");

	bar.Reset("New", "Labels");
	EXPECT_FLOAT_EQ(bar.CurrentValue(), 0);
	EXPECT_FLOAT_EQ(bar.MaxValue(), 100);
	EXPECT_EQ(bar.Text(), "");
	EXPECT_EQ(bar.TrailingText(), "");
	EXPECT_EQ(bar.Label(), "New");

	BMessage reset(kResetStatusBar);
	ASSERT_TRUE(reset.AddString("label", "From").has_value());
	ASSERT_TRUE(reset.AddString("trailing_label", "Message").has_value());
	bar.MessageReceived(&reset);
	EXPECT_EQ(bar.Label(), "From");
	EXPECT_EQ(bar.TrailingLabel(), "Message");

	const gfx::Color red = gfx::Color::FromRgba8(255, 0, 0);
	bar.SetBarColor(red);
	EXPECT_EQ(bar.BarColor(), red);
}


} // namespace
