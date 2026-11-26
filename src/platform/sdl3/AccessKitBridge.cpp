/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The bridge to AccessKit (toolkit scope 11.3): a window's accessibility
// tree as AccessKit's tree updates, AccessKit's action requests as
// kAccessibleAction for the window. Modelled on AccessKit's SDL example
// (accesskit-c, examples/sdl/hello_world.c):
// - The adapter is made on the main thread while the window is still
//   hidden (the Windows adapter requires it).
// - Assistive technology that begins to listen gets the tree the window
//   passed on last, or the window alone, and the window is asked for its
//   whole tree (kAccessibilityRequested).
// - Bounds are in logical pixels, which the window's node scales to device
//   pixels, as AccessKit wants them.


#include "platform/sdl3/AccessKitBridge.h"
#include "platform/sdl3/AccessKitTree.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <ngui/Version.h>


namespace ngui::platform::sdl3 {


namespace {


// Far from the tokens of views and windows: the window alone, before it
// passed its tree on.
constexpr std::uint64_t kPlaceholderId = std::uint64_t{1} << 62;


accesskit_role
RoleOf(const AccessibleInfo& info)
{
	switch (info.role) {
		case AccessibleRole::Window:
			return ACCESSKIT_ROLE_WINDOW;
		case AccessibleRole::Dialog:
			return ACCESSKIT_ROLE_DIALOG;
		case AccessibleRole::Alert:
			return ACCESSKIT_ROLE_ALERT_DIALOG;
		case AccessibleRole::Group:
			return ACCESSKIT_ROLE_GROUP;
		case AccessibleRole::Label:
			return ACCESSKIT_ROLE_LABEL;
		case AccessibleRole::Button:
			return info.states.Has(AccessibleState::Default) ? ACCESSKIT_ROLE_DEFAULT_BUTTON
				: ACCESSKIT_ROLE_BUTTON;
		case AccessibleRole::CheckBox:
			return ACCESSKIT_ROLE_CHECK_BOX;
		case AccessibleRole::RadioButton:
			return ACCESSKIT_ROLE_RADIO_BUTTON;
		case AccessibleRole::Slider:
			return ACCESSKIT_ROLE_SLIDER;
		case AccessibleRole::TextInput:
			return ACCESSKIT_ROLE_TEXT_INPUT;
		case AccessibleRole::MultilineTextInput:
			return ACCESSKIT_ROLE_MULTILINE_TEXT_INPUT;
		case AccessibleRole::PasswordInput:
			return ACCESSKIT_ROLE_PASSWORD_INPUT;
		case AccessibleRole::ComboBox:
			return ACCESSKIT_ROLE_COMBO_BOX;
		case AccessibleRole::List:
			return ACCESSKIT_ROLE_LIST_BOX;
		case AccessibleRole::ListItem:
			return ACCESSKIT_ROLE_LIST_BOX_OPTION;
		case AccessibleRole::Tree:
			return ACCESSKIT_ROLE_TREE;
		case AccessibleRole::TreeItem:
			return ACCESSKIT_ROLE_TREE_ITEM;
		case AccessibleRole::MenuBar:
			return ACCESSKIT_ROLE_MENU_BAR;
		case AccessibleRole::Menu:
			return ACCESSKIT_ROLE_MENU;
		case AccessibleRole::MenuItem:
			return ACCESSKIT_ROLE_MENU_ITEM;
		case AccessibleRole::ScrollBar:
			return ACCESSKIT_ROLE_SCROLL_BAR;
		case AccessibleRole::ScrollView:
			return ACCESSKIT_ROLE_SCROLL_VIEW;
		case AccessibleRole::TabList:
			return ACCESSKIT_ROLE_TAB_LIST;
		case AccessibleRole::Tab:
			return ACCESSKIT_ROLE_TAB;
		case AccessibleRole::TabPanel:
			return ACCESSKIT_ROLE_TAB_PANEL;
		case AccessibleRole::ProgressIndicator:
			return ACCESSKIT_ROLE_PROGRESS_INDICATOR;
		case AccessibleRole::Splitter:
			return ACCESSKIT_ROLE_SPLITTER;
		case AccessibleRole::Image:
			return ACCESSKIT_ROLE_IMAGE;
		case AccessibleRole::TextRun:
			return ACCESSKIT_ROLE_TEXT_RUN;
		case AccessibleRole::Table:
			return ACCESSKIT_ROLE_TABLE;
		case AccessibleRole::TreeTable:
			return ACCESSKIT_ROLE_TREE_GRID;
		case AccessibleRole::Row:
			return ACCESSKIT_ROLE_ROW;
		case AccessibleRole::Cell:
			return ACCESSKIT_ROLE_CELL;
		case AccessibleRole::ColumnHeader:
			return ACCESSKIT_ROLE_COLUMN_HEADER;
		case AccessibleRole::None:
		case AccessibleRole::Generic:
			break;
	}
	return ACCESSKIT_ROLE_GENERIC_CONTAINER;
}


struct ActionPair {
	AccessibleAction	ours;
	accesskit_action	theirs;
};


constexpr ActionPair kActions[] = {
	{AccessibleAction::Click, ACCESSKIT_ACTION_CLICK},
	{AccessibleAction::Focus, ACCESSKIT_ACTION_FOCUS},
	{AccessibleAction::Increment, ACCESSKIT_ACTION_INCREMENT},
	{AccessibleAction::Decrement, ACCESSKIT_ACTION_DECREMENT},
	{AccessibleAction::SetValue, ACCESSKIT_ACTION_SET_VALUE},
	{AccessibleAction::Expand, ACCESSKIT_ACTION_EXPAND},
	{AccessibleAction::Collapse, ACCESSKIT_ACTION_COLLAPSE},
	{AccessibleAction::SetTextSelection, ACCESSKIT_ACTION_SET_TEXT_SELECTION},
	{AccessibleAction::ReplaceSelectedText, ACCESSKIT_ACTION_REPLACE_SELECTED_TEXT}};


// The node of item \a item of the node \a owner (Window.cpp gives items such
// ids).
std::uint64_t
ItemId(std::uint64_t owner, std::int32_t item)
{
	return (owner << 32) | (static_cast<std::uint64_t>(item) + 1);
}


accesskit_tree_info*
TreeInfo(std::uint64_t root)
{
	accesskit_tree_info* info = accesskit_tree_info_new(root);
	accesskit_tree_info_set_toolkit_name(info, "NewGUI");
	accesskit_tree_info_set_toolkit_version(info, kVersionString);
	return info;
}


} // namespace


accesskit_node*
MakeNode(const AccessibleNode& node, bool root, double scale)
{
	const AccessibleInfo& info = node.info;
	accesskit_node* made = accesskit_node_new(RoleOf(info));
	if (!info.name.empty())
		accesskit_node_set_label(made, info.name.c_str());
	if (!info.description.empty())
		accesskit_node_set_description(made, info.description.c_str());
	// a text run always has a value, empty for an empty line (AccessKit
	// panics without one when assistive technology reads its text)
	if (!info.value.empty() || info.role == AccessibleRole::TextRun)
		accesskit_node_set_value(made, info.value.c_str());
	else if (info.role == AccessibleRole::Label && !info.name.empty())
		// AccessKit reads static text from the value (AT-SPI's name and text)
		accesskit_node_set_value(made, info.name.c_str());
	if (info.numericValue.has_value())
		accesskit_node_set_numeric_value(made, *info.numericValue);
	if (info.minimum.has_value())
		accesskit_node_set_min_numeric_value(made, *info.minimum);
	if (info.maximum.has_value())
		accesskit_node_set_max_numeric_value(made, *info.maximum);
	if (info.step.has_value())
		accesskit_node_set_numeric_value_step(made, *info.step);
	if (info.level > 0)
		accesskit_node_set_level(made, static_cast<std::size_t>(info.level));
	// tables
	const auto count = [](std::int32_t value) {
		return static_cast<std::size_t>(std::max(value, 0));
	};
	if (info.rowCount.has_value())
		accesskit_node_set_row_count(made, count(*info.rowCount));
	if (info.columnCount.has_value())
		accesskit_node_set_column_count(made, count(*info.columnCount));
	if (info.rowIndex.has_value())
		accesskit_node_set_row_index(made, count(*info.rowIndex));
	if (info.columnIndex.has_value())
		accesskit_node_set_column_index(made, count(*info.columnIndex));
	// text runs and the selection in them
	if (!info.characterLengths.empty()) {
		accesskit_node_set_character_lengths(made, info.characterLengths.size(),
			info.characterLengths.data());
	}
	if (!info.wordStarts.empty())
		accesskit_node_set_word_starts(made, info.wordStarts.size(), info.wordStarts.data());
	if (!info.characterPositions.empty()) {
		accesskit_node_set_character_positions(made, info.characterPositions.size(),
			info.characterPositions.data());
	}
	if (!info.characterWidths.empty()) {
		accesskit_node_set_character_widths(made, info.characterWidths.size(),
			info.characterWidths.data());
	}
	if (info.textSelection.has_value()) {
		const AccessibleTextSelection& selection = *info.textSelection;
		accesskit_node_set_text_selection(made, {
			{ItemId(node.id, selection.anchor.item),
				static_cast<std::size_t>(std::max(selection.anchor.character, 0))},
			{ItemId(node.id, selection.focus.item),
				static_cast<std::size_t>(std::max(selection.focus.character, 0))}});
	}
	if (info.live == AccessibleLive::Polite)
		accesskit_node_set_live(made, ACCESSKIT_LIVE_POLITE);
	else if (info.live == AccessibleLive::Assertive)
		accesskit_node_set_live(made, ACCESSKIT_LIVE_ASSERTIVE);

	const AccessibleStates states = info.states;
	if (states.Has(AccessibleState::Disabled))
		accesskit_node_set_disabled(made);
	if (states.Has(AccessibleState::Mixed))
		accesskit_node_set_toggled(made, ACCESSKIT_TOGGLED_MIXED);
	else if (states.Has(AccessibleState::Checked))
		accesskit_node_set_toggled(made, ACCESSKIT_TOGGLED_TRUE);
	else if (info.role == AccessibleRole::CheckBox || info.role == AccessibleRole::RadioButton)
		accesskit_node_set_toggled(made, ACCESSKIT_TOGGLED_FALSE);
	if (states.Has(AccessibleState::Selected))
		accesskit_node_set_selected(made, true);
	if (states.Has(AccessibleState::Expanded))
		accesskit_node_set_expanded(made, true);
	else if (states.Has(AccessibleState::Collapsed))
		accesskit_node_set_expanded(made, false);
	if (states.Has(AccessibleState::ReadOnly))
		accesskit_node_set_read_only(made);
	if (states.Has(AccessibleState::Modal))
		accesskit_node_set_modal(made);
	for (const ActionPair& action : kActions) {
		if (info.actions.Has(action.ours))
			accesskit_node_add_action(made, action.theirs);
	}

	accesskit_node_set_bounds(made, {node.bounds.x0, node.bounds.y0, node.bounds.x1,
		node.bounds.y1});
	if (root)
		accesskit_node_set_transform(made, accesskit_affine_scale(scale));
	for (const std::uint64_t child : node.children)
		accesskit_node_push_child(made, child);
	return made;
}


accesskit_tree_update*
MakeTreeUpdate(const AccessibleTree& tree, double scale)
{
	if (tree.nodes.empty())
		return MakePlaceholderUpdate(kPlaceholderId, {});
	accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(
		tree.nodes.size(), tree.focus);
	for (std::size_t i = 0; i < tree.nodes.size(); i++) {
		accesskit_tree_update_push_node(update, tree.nodes[i].id,
			MakeNode(tree.nodes[i], i == 0, scale));
	}
	accesskit_tree_update_set_tree_info(update, TreeInfo(tree.nodes.front().id));
	return update;
}


accesskit_tree_update*
MakePlaceholderUpdate(std::uint64_t id, const std::string& title)
{
	accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(1, id);
	accesskit_node* window = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
	if (!title.empty())
		accesskit_node_set_label(window, title.c_str());
	accesskit_tree_update_push_node(update, id, window);
	accesskit_tree_update_set_tree_info(update, TreeInfo(id));
	return update;
}


std::optional<BMessage>
ActionMessage(const accesskit_action_request& request)
{
	const ActionPair* pair = nullptr;
	for (const ActionPair& action : kActions) {
		if (action.theirs == request.action)
			pair = &action;
	}
	constexpr auto kLargest = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	if (pair == nullptr || request.target_node > kLargest)
		return std::nullopt;
	BMessage message(kAccessibleAction);
	(void)message.AddInt64("node", static_cast<std::int64_t>(request.target_node));
	(void)message.AddInt32("action", static_cast<std::int32_t>(pair->ours));
	if (request.data.has_value) {
		const accesskit_action_data& data = request.data.value;
		if (data.tag == ACCESSKIT_ACTION_DATA_VALUE && data.value != nullptr)
			(void)message.AddString("value", data.value);
		else if (data.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE)
			(void)message.AddDouble("number", data.numeric_value);
		else if (data.tag == ACCESSKIT_ACTION_DATA_SET_TEXT_SELECTION) {
			// positions in text runs: items of the target's node
			const accesskit_text_selection& selection = data.set_text_selection;
			const auto item = [&](accesskit_node_id id) -> std::int32_t {
				if (id >> 32 != request.target_node)
					return -1;
				return static_cast<std::int32_t>(id & 0xffffffffu) - 1;
			};
			const auto character = [](std::size_t index) {
				return static_cast<std::int32_t>(std::min<std::size_t>(index,
					static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
			};
			(void)message.AddInt32("anchorItem", item(selection.anchor.node));
			(void)message.AddInt32("anchorCharacter", character(selection.anchor.character_index));
			(void)message.AddInt32("focusItem", item(selection.focus.node));
			(void)message.AddInt32("focusCharacter", character(selection.focus.character_index));
		}
	}
	return message;
}


// #pragma mark - AccessKitBridge


AccessKitBridge::AccessKitBridge(BMessenger target, std::string title)
	:
	fTarget(std::move(target)),
	fTitle(std::move(title))
{
}


std::unique_ptr<AccessKitBridge>
AccessKitBridge::Create(SDL_Window* window, BMessenger target, std::string title)
{
	if constexpr (!kHasAccessKitAdapter)
		return nullptr;
	std::unique_ptr<AccessKitBridge> bridge(new AccessKitBridge(std::move(target),
		std::move(title)));
#if defined(_WIN32)
	auto* hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
		SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
	if (hwnd == nullptr)
		return nullptr;
	bridge->fAdapter = accesskit_windows_subclassing_adapter_new(hwnd, &_Activated,
		bridge.get(), &_Action, bridge.get());
#elif defined(__linux__)
	(void)window;
	bridge->fAdapter = accesskit_unix_adapter_new(&_Activated, bridge.get(), &_Action,
		bridge.get(), &_Deactivated, bridge.get());
#else
	(void)window;
#endif
	return bridge;
}


AccessKitBridge::~AccessKitBridge()
{
#if defined(_WIN32)
	if (fAdapter != nullptr)
		accesskit_windows_subclassing_adapter_free(fAdapter);
#elif defined(__linux__)
	if (fAdapter != nullptr)
		accesskit_unix_adapter_free(fAdapter);
#endif
}


bool
AccessKitBridge::IsActive() const
{
	return fActive.load();
}


void
AccessKitBridge::SetTree(const AccessibleTree& tree, double scale)
{
	std::scoped_lock lock(fLock);
	fTree = tree;
	fScale = scale;
}


void
AccessKitBridge::Push()
{
	// without the lock: the adapter asks _Update() for the tree
#if defined(_WIN32)
	if (fAdapter == nullptr)
		return;
	if (accesskit_windows_queued_events* events
			= accesskit_windows_subclassing_adapter_update_if_active(fAdapter, &_Update, this)) {
		accesskit_windows_queued_events_raise(events);
	}
#elif defined(__linux__)
	if (fAdapter != nullptr)
		accesskit_unix_adapter_update_if_active(fAdapter, &_Update, this);
#endif
}


void
AccessKitBridge::SetFocused(bool focused)
{
#if defined(__linux__)
	if (fAdapter != nullptr)
		accesskit_unix_adapter_update_window_focus_state(fAdapter, focused);
#else
	// the subclassing adapter follows the window's focus itself
	(void)focused;
#endif
}


void
AccessKitBridge::SetBounds(double x0, double y0, double x1, double y1)
{
#if defined(__linux__)
	if (fAdapter != nullptr) {
		const accesskit_rect bounds{x0, y0, x1, y1};
		accesskit_unix_adapter_set_root_window_bounds(fAdapter, bounds, bounds);
	}
#else
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
#endif
}


accesskit_tree_update*
AccessKitBridge::_CurrentUpdate()
{
	std::scoped_lock lock(fLock);
	if (fTree.has_value() && !fTree->nodes.empty())
		return MakeTreeUpdate(*fTree, fScale);
	return MakePlaceholderUpdate(kPlaceholderId, fTitle);
}


// Assistive technology begins to listen (on Linux on AccessKit's thread,
// on Windows on the window's).
accesskit_tree_update*
AccessKitBridge::_Activated(void* userdata)
{
	auto* bridge = static_cast<AccessKitBridge*>(userdata);
	bridge->fActive = true;
	BMessenger target;
	{
		std::scoped_lock lock(bridge->fLock);
		target = bridge->fTarget;
	}
	(void)target.SendMessage(kAccessibilityRequested);
	return bridge->_CurrentUpdate();
}


void
AccessKitBridge::_Action(accesskit_action_request* request, void* userdata)
{
	auto* bridge = static_cast<AccessKitBridge*>(userdata);
	std::optional<BMessage> message = ActionMessage(*request);
	accesskit_action_request_free(request);
	if (!message.has_value())
		return;
	BMessenger target;
	{
		std::scoped_lock lock(bridge->fLock);
		target = bridge->fTarget;
	}
	(void)target.SendMessage(*message);
}


void
AccessKitBridge::_Deactivated(void* userdata)
{
	static_cast<AccessKitBridge*>(userdata)->fActive = false;
}


accesskit_tree_update*
AccessKitBridge::_Update(void* userdata)
{
	return static_cast<AccessKitBridge*>(userdata)->_CurrentUpdate();
}


} // namespace ngui::platform::sdl3
