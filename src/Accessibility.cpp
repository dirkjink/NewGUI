/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Accessibility (toolkit scope 11.3): the tree of a window and its text.


#include <ngui/Accessibility.h>

#include <format>
#include <functional>
#include <string>
#include <utility>


namespace ngui {


namespace {


struct Name {
	std::uint32_t		flag;
	const char*			name;
};


constexpr Name kStateNames[] = {
	{static_cast<std::uint32_t>(AccessibleState::Disabled), "disabled"},
	{static_cast<std::uint32_t>(AccessibleState::Focusable), "focusable"},
	{static_cast<std::uint32_t>(AccessibleState::Focused), "focused"},
	{static_cast<std::uint32_t>(AccessibleState::Checked), "checked"},
	{static_cast<std::uint32_t>(AccessibleState::Mixed), "mixed"},
	{static_cast<std::uint32_t>(AccessibleState::Selected), "selected"},
	{static_cast<std::uint32_t>(AccessibleState::Expanded), "expanded"},
	{static_cast<std::uint32_t>(AccessibleState::Collapsed), "collapsed"},
	{static_cast<std::uint32_t>(AccessibleState::ReadOnly), "readonly"},
	{static_cast<std::uint32_t>(AccessibleState::Default), "default"},
	{static_cast<std::uint32_t>(AccessibleState::Modal), "modal"}};

constexpr Name kActionNames[] = {
	{static_cast<std::uint32_t>(AccessibleAction::Click), "click"},
	{static_cast<std::uint32_t>(AccessibleAction::Focus), "focus"},
	{static_cast<std::uint32_t>(AccessibleAction::Increment), "increment"},
	{static_cast<std::uint32_t>(AccessibleAction::Decrement), "decrement"},
	{static_cast<std::uint32_t>(AccessibleAction::SetValue), "setvalue"},
	{static_cast<std::uint32_t>(AccessibleAction::Expand), "expand"},
	{static_cast<std::uint32_t>(AccessibleAction::Collapse), "collapse"}};


// The names of the flags \a bits, separated by ", ".
std::string
Names(std::uint32_t bits, std::span<const Name> names)
{
	std::string text;
	for (const Name& name : names) {
		if ((bits & name.flag) != 0)
			text += (text.empty() ? "" : ", ") + std::string(name.name);
	}
	return text;
}


std::string
Number(double value)
{
	return std::format("{:g}", value);
}


std::string
NodeLine(const AccessibleNode& node)
{
	const AccessibleInfo& info = node.info;
	std::string line(AccessibleRoleName(info.role));
	if (!info.name.empty())
		line += " \"" + info.name + "\"";
	if (!info.value.empty())
		line += " = \"" + info.value + "\"";
	if (info.numericValue.has_value()) {
		line += " = " + Number(*info.numericValue);
		if (info.minimum.has_value() || info.maximum.has_value()) {
			line += " (" + (info.minimum ? Number(*info.minimum) : std::string("?")) + ".."
				+ (info.maximum ? Number(*info.maximum) : std::string("?"));
			if (info.step.has_value())
				line += " by " + Number(*info.step);
			line += ")";
		}
	}
	if (info.level > 0)
		line += " level " + std::to_string(info.level);
	if (!info.description.empty())
		line += " \"" + info.description + "\"";
	const std::string states = Names(info.states.Raw(), kStateNames);
	if (!states.empty())
		line += " [" + states + "]";
	const std::string actions = Names(info.actions.Raw(), kActionNames);
	if (!actions.empty())
		line += " {" + actions + "}";
	return line;
}


} // namespace


std::string_view
AccessibleRoleName(AccessibleRole role)
{
	switch (role) {
		case AccessibleRole::None:
			return "none";
		case AccessibleRole::Window:
			return "window";
		case AccessibleRole::Dialog:
			return "dialog";
		case AccessibleRole::Alert:
			return "alert";
		case AccessibleRole::Group:
			return "group";
		case AccessibleRole::Label:
			return "label";
		case AccessibleRole::Button:
			return "button";
		case AccessibleRole::CheckBox:
			return "checkbox";
		case AccessibleRole::RadioButton:
			return "radiobutton";
		case AccessibleRole::Slider:
			return "slider";
		case AccessibleRole::TextInput:
			return "textinput";
		case AccessibleRole::MultilineTextInput:
			return "multilinetextinput";
		case AccessibleRole::PasswordInput:
			return "passwordinput";
		case AccessibleRole::ComboBox:
			return "combobox";
		case AccessibleRole::List:
			return "list";
		case AccessibleRole::ListItem:
			return "listitem";
		case AccessibleRole::Tree:
			return "tree";
		case AccessibleRole::TreeItem:
			return "treeitem";
		case AccessibleRole::MenuBar:
			return "menubar";
		case AccessibleRole::Menu:
			return "menu";
		case AccessibleRole::MenuItem:
			return "menuitem";
		case AccessibleRole::ScrollBar:
			return "scrollbar";
		case AccessibleRole::ScrollView:
			return "scrollview";
		case AccessibleRole::TabList:
			return "tablist";
		case AccessibleRole::Tab:
			return "tab";
		case AccessibleRole::TabPanel:
			return "tabpanel";
		case AccessibleRole::ProgressIndicator:
			return "progressindicator";
		case AccessibleRole::Splitter:
			return "splitter";
		case AccessibleRole::Image:
			return "image";
		case AccessibleRole::Generic:
			return "generic";
	}
	return "none";
}


const AccessibleNode*
AccessibleTree::Find(std::uint64_t id) const
{
	for (const AccessibleNode& node : nodes) {
		if (node.id == id)
			return &node;
	}
	return nullptr;
}


const AccessibleNode*
AccessibleTree::FindByName(std::string_view name) const
{
	for (const AccessibleNode& node : nodes) {
		if (node.info.name == name)
			return &node;
	}
	return nullptr;
}


std::string
AccessibleTree::Text() const
{
	std::string text;
	if (nodes.empty())
		return text;
	const std::function<void(const AccessibleNode&, int)> add
		= [&](const AccessibleNode& node, int depth) {
			text += std::string(static_cast<std::size_t>(depth) * 2, ' ') + NodeLine(node) + "\n";
			for (const std::uint64_t child : node.children) {
				if (const AccessibleNode* found = Find(child))
					add(*found, depth + 1);
			}
		};
	add(nodes.front(), 0);
	return text;
}


} // namespace ngui
