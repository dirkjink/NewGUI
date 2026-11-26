/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/AccessKitTree.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Version.h>


namespace {


using namespace ngui;
using namespace ngui::platform::sdl3;


// \a update in AccessKit's own words (Rust's Debug), freed.
std::string
Debug(accesskit_tree_update* update)
{
	char* text = accesskit_tree_update_debug(update);
	std::string result = text != nullptr ? text : "";
	accesskit_string_free(text);
	accesskit_tree_update_free(update);
	return result;
}


AccessibleNode
Node(std::uint64_t id, AccessibleRole role, std::string name, gfx::Rect bounds,
	std::vector<std::uint64_t> children = {})
{
	AccessibleNode node;
	node.id = id;
	node.info.role = role;
	node.info.name = std::move(name);
	node.bounds = bounds;
	node.children = std::move(children);
	return node;
}


TEST(AccessKitBridgeTest, TreesBecomeAccessKitsUpdates)
{
	// What is tested: MakeTreeUpdate() of a window at scale 2 with a check
	// box that is on, an unchecked radio button, a slider at 30 of 0 to 100
	// by 5, a password field, the default button and a disabled one, a
	// label, an expanded tree item at level 2, the focus on the check box;
	// and MakePlaceholderUpdate().
	// Success criteria: In AccessKit's own description of the update: the
	// window is the root with its children, scaled by 2; each node has its
	// role (the default button DefaultButton), label, bounds and actions;
	// the check box is toggled True, the radio button False; the slider has
	// its numbers (AccessKit orders the actions itself); the password
	// field no value; the disabled button is disabled; the label's text is
	// its value too (AccessKit's static text); the tree item has its level,
	// expanded, Collapse and live Polite; the focus is the check
	// box; the toolkit is NewGUI with its version. The placeholder is the
	// window alone, focused.
	// Why it matters: Screen readers get the tree as NewGUI has it, in
	// device pixels (toolkit scope 11.3).
	//
	// Given
	AccessibleTree tree;
	tree.nodes.push_back(Node(7, AccessibleRole::Window, "Settings", {0, 0, 300, 200},
		{8, 9, 10, 11, 12, 13, 14, 15}));
	AccessibleNode check = Node(8, AccessibleRole::CheckBox, "Save", {10, 10, 110, 30});
	check.info.states = AccessibleState::Checked | AccessibleState::Focusable;
	check.info.actions = AccessibleAction::Click | AccessibleAction::Focus;
	tree.nodes.push_back(check);
	tree.nodes.push_back(Node(9, AccessibleRole::RadioButton, "Small", {10, 40, 110, 60}));
	AccessibleNode slider = Node(10, AccessibleRole::Slider, "Volume", {10, 70, 210, 90});
	slider.info.numericValue = 30;
	slider.info.minimum = 0;
	slider.info.maximum = 100;
	slider.info.step = 5;
	slider.info.actions = AccessibleAction::Increment | AccessibleAction::Decrement
		| AccessibleAction::SetValue;
	tree.nodes.push_back(slider);
	tree.nodes.push_back(Node(11, AccessibleRole::PasswordInput, "Password:",
		{10, 100, 210, 120}));
	AccessibleNode ok = Node(12, AccessibleRole::Button, "OK", {200, 160, 290, 190});
	ok.info.states = AccessibleState::Default;
	tree.nodes.push_back(ok);
	AccessibleNode off = Node(13, AccessibleRole::Button, "Reset", {100, 160, 190, 190});
	off.info.states = AccessibleState::Disabled;
	tree.nodes.push_back(off);
	tree.nodes.push_back(Node(14, AccessibleRole::Label, "Not clicked yet", {10, 130, 210, 150}));
	AccessibleNode fruit = Node(15, AccessibleRole::TreeItem, "Fruit", {220, 10, 290, 30});
	fruit.info.level = 2;
	fruit.info.states = AccessibleState::Expanded;
	fruit.info.actions = AccessibleAction::Collapse;
	fruit.info.live = AccessibleLive::Polite;
	tree.nodes.push_back(fruit);
	tree.focus = 8;

	// When
	const std::string text = Debug(MakeTreeUpdate(tree, 2));
	const std::string placeholder = Debug(MakePlaceholderUpdate(5, "Main"));

	// Then
	const auto has = [&](const std::string& part) {
		return text.find(part) != std::string::npos;
	};
	EXPECT_TRUE(has("(#7, Node { role: Window, children: [#8, #9, #10, #11, #12, #13, #14, #15], "
		"label: \"Settings\", transform: Affine([2.0, 0.0, 0.0, 2.0, 0.0, 0.0]), "
		"bounds: Rect { x0: 0.0, y0: 0.0, x1: 300.0, y1: 200.0 } })")) << text;
	EXPECT_TRUE(has("(#8, Node { role: CheckBox, actions: [Click, Focus], label: \"Save\", "
		"toggled: True, bounds: Rect { x0: 10.0, y0: 10.0, x1: 110.0, y1: 30.0 } })"));
	EXPECT_TRUE(has("role: RadioButton, label: \"Small\", toggled: False"));
	EXPECT_TRUE(has("role: Slider, actions: [Decrement, Increment, SetValue], "
		"label: \"Volume\", numeric_value: 30.0, min_numeric_value: 0.0, "
		"max_numeric_value: 100.0, numeric_value_step: 5.0"));
	EXPECT_TRUE(has("(#11, Node { role: PasswordInput, label: \"Password:\", bounds"));
	EXPECT_TRUE(has("role: DefaultButton, label: \"OK\""));
	EXPECT_TRUE(has("role: Button, is_disabled: true, label: \"Reset\"")) << text;
	EXPECT_TRUE(has("tree: Some(TreeInfo { root: #7, toolkit_name: Some(\"NewGUI\"), "
		"toolkit_version: Some(\"" + std::string(kVersionString) + "\") })"));
	EXPECT_TRUE(has("role: Label, label: \"Not clicked yet\", value: \"Not clicked yet\""))
		<< text;
	EXPECT_TRUE(has("role: TreeItem, actions: [Collapse], label: \"Fruit\""));
	EXPECT_TRUE(has("level: 2")) << text;
	EXPECT_TRUE(has("expanded: true")) << text;
	EXPECT_TRUE(has("live: Polite")) << text;
	EXPECT_TRUE(has("focus: #8 }"));
	EXPECT_NE(placeholder.find("nodes: [(#5, Node { role: Window, label: \"Main\" })]"),
		std::string::npos) << placeholder;
	EXPECT_NE(placeholder.find("focus: #5 }"), std::string::npos);
}


TEST(AccessKitBridgeTest, ActionRequestsBecomeMessages)
{
	// What is tested: ActionMessage() of AccessKit's requests: Click of
	// node 8; SetValue of node 10 with the number 80; SetValue of node 11
	// with the text "Grace"; Increment; Expand; a ScrollDown, which NewGUI
	// does not take.
	// Success criteria: kAccessibleAction with "node" and "action" as
	// AccessibleAction; the number as "number", the text as "value";
	// nothing for ScrollDown.
	// Why it matters: What a screen reader asks of a control reaches its
	// window as the toolkit's own message.
	//
	// Given
	accesskit_action_request click{};
	click.action = ACCESSKIT_ACTION_CLICK;
	click.target_node = 8;
	accesskit_action_request number{};
	number.action = ACCESSKIT_ACTION_SET_VALUE;
	number.target_node = 10;
	number.data.has_value = true;
	number.data.value.tag = ACCESSKIT_ACTION_DATA_NUMERIC_VALUE;
	number.data.value.numeric_value = 80;
	std::string grace = "Grace";
	accesskit_action_request text{};
	text.action = ACCESSKIT_ACTION_SET_VALUE;
	text.target_node = 11;
	text.data.has_value = true;
	text.data.value.tag = ACCESSKIT_ACTION_DATA_VALUE;
	text.data.value.value = grace.data();
	accesskit_action_request increment{};
	increment.action = ACCESSKIT_ACTION_INCREMENT;
	increment.target_node = 10;
	accesskit_action_request expand{};
	expand.action = ACCESSKIT_ACTION_EXPAND;
	expand.target_node = 15;
	accesskit_action_request scroll{};
	scroll.action = ACCESSKIT_ACTION_SCROLL_DOWN;
	scroll.target_node = 10;

	// When
	const auto clicked = ActionMessage(click);
	const auto numbered = ActionMessage(number);
	const auto texted = ActionMessage(text);
	const auto incremented = ActionMessage(increment);
	const auto expanded = ActionMessage(expand);
	const auto scrolled = ActionMessage(scroll);

	// Then
	ASSERT_TRUE(clicked.has_value());
	EXPECT_EQ(clicked->what, kAccessibleAction);
	EXPECT_EQ(clicked->GetInt64("node", 0), 8);
	EXPECT_EQ(clicked->GetInt32("action", 0), static_cast<std::int32_t>(AccessibleAction::Click));
	ASSERT_TRUE(numbered.has_value());
	EXPECT_EQ(numbered->GetInt32("action", 0),
		static_cast<std::int32_t>(AccessibleAction::SetValue));
	EXPECT_EQ(numbered->GetDouble("number", 0), 80.0);
	ASSERT_TRUE(texted.has_value());
	EXPECT_EQ(texted->GetString("value", ""), "Grace");
	ASSERT_TRUE(incremented.has_value());
	EXPECT_EQ(incremented->GetInt32("action", 0),
		static_cast<std::int32_t>(AccessibleAction::Increment));
	ASSERT_TRUE(expanded.has_value());
	EXPECT_EQ(expanded->GetInt32("action", 0), static_cast<std::int32_t>(AccessibleAction::Expand));
	EXPECT_FALSE(scrolled.has_value());
}


} // namespace
