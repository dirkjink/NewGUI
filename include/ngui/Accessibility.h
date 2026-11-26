/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Flags.h>
#include <ngui/Message.h>
#include <ngui/gfx/Rect.h>


/*!	\file
	Accessibility (toolkit scope 11.3): what assistive technology such as a
	screen reader learns of a window. Each view says what it is
	(BView::GetAccessibleInfo()): a role, a name, a value, its states and the
	actions it takes; the window makes a tree of them
	(BWindow::GetAccessibleTree(), test::WindowRef::AccessibleTree()). Views
	whose role is None (layout containers, the text view inside a text
	control) are left out, their children are not. A platform passes the
	tree to the system's accessibility interface and the actions back to the
	window as kAccessibleAction, which the view does
	(BView::PerformAccessibleAction()) on the window's thread.

	Haiku has no accessibility; the roles and actions follow AccessKit's,
	which maps them to AT-SPI, UI Automation and NSAccessibility.
*/


namespace ngui {


/*!	An action for a window from assistive technology: "node" (int64) the
	node's id, "action" (int32) an AccessibleAction, for SetValue "value"
	(string) or "number" (double). A reply, if asked for, has "error" as
	scripting replies have (0 for done). */
inline constexpr std::uint32_t kAccessibleAction = FourCharCode("_ACA");


/*!	What a view is to assistive technology. */
enum class AccessibleRole : std::uint8_t {
	None,				/*!< Not exposed; its children are (layout containers). */
	Window,				/*!< A window (the root). */
	Dialog,				/*!< A modal window. */
	Alert,				/*!< An alert (BAlert). */
	Group,				/*!< Views that belong together, e.g. a BBox. */
	Label,				/*!< Static text (BStringView). */
	Button,				/*!< A push button. */
	CheckBox,			/*!< A check box. */
	RadioButton,		/*!< A radio button. */
	Slider,				/*!< A slider. */
	TextInput,			/*!< A single-line text field (BTextControl). */
	MultilineTextInput,	/*!< A text view of several lines. */
	PasswordInput,		/*!< A text field that hides typing; its text is never
							exposed. */
	ComboBox,			/*!< A pop-up menu field (BMenuField). */
	List,				/*!< A list of items to choose from (BListView). */
	ListItem,			/*!< An item of a list. */
	Tree,				/*!< An outline list (BOutlineListView). */
	TreeItem,			/*!< An item of an outline list. */
	MenuBar,			/*!< A menu bar. */
	Menu,				/*!< An open menu. */
	MenuItem,			/*!< An item of a menu. */
	ScrollBar,			/*!< A scroll bar. */
	ScrollView,			/*!< A view that scrolls its target. */
	TabList,			/*!< The tabs of a tab view. */
	Tab,				/*!< A tab. */
	TabPanel,			/*!< The view of the selected tab. */
	ProgressIndicator,	/*!< A status bar. */
	Splitter,			/*!< The divider of a split view. */
	Image,				/*!< An image or icon. */
	Generic				/*!< Something else with a name or actions. */
};


/*!	A state of an accessible view. */
enum class AccessibleState : std::uint32_t {
	Disabled = 1u << 0,		/*!< It takes no input. */
	Focusable = 1u << 1,	/*!< It can take the keyboard focus. */
	Focused = 1u << 2,		/*!< It has the keyboard focus (in the active window). */
	Checked = 1u << 3,		/*!< A check box or radio button that is on. */
	Mixed = 1u << 4,		/*!< A check box that is partly on. */
	Selected = 1u << 5,		/*!< A selected item. */
	Expanded = 1u << 6,		/*!< An item whose children show. */
	Collapsed = 1u << 7,	/*!< An item whose children are hidden. */
	ReadOnly = 1u << 8,		/*!< A text that cannot be edited. */
	Default = 1u << 9,		/*!< The default button of its window. */
	Modal = 1u << 10		/*!< A window that blocks the others. */
};
NGUI_FLAG_ENUM(AccessibleState)

/*!	A set of AccessibleState. */
using AccessibleStates = Flags<AccessibleState>;


/*!	What assistive technology can ask an accessible view to do. */
enum class AccessibleAction : std::uint32_t {
	Click = 1u << 0,		/*!< Activate it, as Space does (BControl::KeyDown()). */
	Focus = 1u << 1,		/*!< Give it the keyboard focus. */
	Increment = 1u << 2,	/*!< Step its value up (sliders, scroll bars). */
	Decrement = 1u << 3,	/*!< Step its value down. */
	SetValue = 1u << 4		/*!< Set its value: a text ("value") or a number
								("number"). */
};
NGUI_FLAG_ENUM(AccessibleAction)

/*!	A set of AccessibleAction. */
using AccessibleActions = Flags<AccessibleAction>;


/*!	What a view tells assistive technology (BView::GetAccessibleInfo()). */
struct AccessibleInfo {
			AccessibleRole		role = AccessibleRole::None;
									/*!< What it is; None leaves it out. */
			std::string			name;		/*!< What it is called: a label or a title. */
			std::string			description;
									/*!< More about it, e.g. its tooltip. */
			std::string			value;		/*!< Its value as text (a text field's text, a
												menu field's choice); never a password. */
			std::optional<double> numericValue;
									/*!< Its value as a number (sliders, progress). */
			std::optional<double> minimum;	/*!< The smallest number. */
			std::optional<double> maximum;	/*!< The largest number. */
			std::optional<double> step;		/*!< What Increment() adds. */
			AccessibleStates	states;		/*!< Its states. */
			AccessibleActions	actions;	/*!< What it can be asked to do. */
};


/*!	A node of a window's accessibility tree. */
struct AccessibleNode {
			std::uint64_t		id = 0;		/*!< Stable while the view lives: its token. */
			AccessibleInfo		info;		/*!< What it is. */
			gfx::Rect			bounds;		/*!< Where it is, in window coordinates
												(logical pixels). */
			std::vector<std::uint64_t> children;
									/*!< Its children's ids, in order. */
};


/*!	The accessibility tree of a window: its nodes, the window first. */
struct AccessibleTree {
			std::vector<AccessibleNode> nodes;
									/*!< All nodes, parents before their children. */
			std::uint64_t		focus = 0;	/*!< The node with the focus, the window's if no
												view has it. */

			const AccessibleNode* Find(std::uint64_t id) const;
									/*!< The node \a id, nullptr if there is none. */
			const AccessibleNode* FindByName(std::string_view name) const;
									/*!< The first node called \a name, or nullptr. */
			std::string			Text() const;
									/*!< A line per node, indented by depth: role, name,
										value, states and actions (the accessibility
										snapshot). Stable across platforms. */
};


/*!	The name of \a role in AccessibleTree::Text(), e.g. "checkbox". */
std::string_view				AccessibleRoleName(AccessibleRole role);


} // namespace ngui
