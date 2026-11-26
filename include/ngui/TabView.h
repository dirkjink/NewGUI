/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/ControlLook.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/SupportDefs.h>
#include <ngui/View.h>


namespace ngui {


class BTabView;


/*!	Which tab is drawn (Haiku's tab_position). */
enum class BTabPosition : std::uint8_t {
	First,	/*!< B_TAB_FIRST */
	Front,	/*!< B_TAB_FRONT: the selected tab. */
	Any		/*!< B_TAB_ANY */
};


/*!	On which side of a tab view the tabs are (Haiku's BTabView::tab_side). */
enum class BTabSide : std::uint8_t {
	Left,	/*!< kLeftSide */
	Right,	/*!< kRightSide */
	Top,	/*!< kTopSide */
	Bottom	/*!< kBottomSide */
};


/*!	One tab of a BTabView: a label and the view shown when it is selected
	(Haiku's BTab). The label is the view's name unless set.

	Differences to Haiku: the tab owns its view while the view is not in
	the tab view (Haiku: always; it deleted the view with the tab); drawing
	hooks get the DrawContext.
*/
class BTab {
public:
	explicit					BTab(std::unique_ptr<BView> contentsView = nullptr);
									/*!< A tab showing \a contentsView. */
	virtual						~BTab();
									/*!< Destroys the tab and a view it owns. */

								BTab(const BTab&) = delete;
								BTab& operator=(const BTab&) = delete;

			const std::string&	Label() const;
									/*!< The label. */
	virtual	void				SetLabel(std::string_view label);
									/*!< Sets the label (ignored without a view, as in
										Haiku). */

			bool				IsSelected() const;
									/*!< Whether it is the selected tab. */
	virtual	void				Select(BView* owner);
									/*!< Shows its view in \a owner, the container view. */
	virtual	void				Deselect();
									/*!< Takes its view out (without a layout). */

	virtual	void				SetEnabled(bool enable);
									/*!< Enables or disables the tab. */
			bool				IsEnabled() const;
									/*!< Whether the tab is enabled. */

			void				MakeFocus(bool focus = true);
									/*!< Marks it as the focused tab. */
			bool				IsFocus() const;
									/*!< Whether it is the focused tab. */

	virtual	void				SetView(std::unique_ptr<BView> view);
									/*!< Replaces its view (and the label with the view's
										name); the old view is destroyed. */
			BView*				View() const;
									/*!< Its view, or nullptr. */

	virtual	void				DrawFocusMark(DrawContext& context, gfx::Rect frame);
									/*!< The focus line under the label. */
	virtual	void				DrawLabel(DrawContext& context, gfx::Rect frame);
									/*!< The label centered in \a frame. */
	virtual	void				DrawTab(DrawContext& context, gfx::Rect frame,
									BTabPosition position, bool full = true);
									/*!< The tab and its label. */

private:
	friend class BTabView;

			BorderFlags			_Borders(BView* owner, gfx::Rect frame) const;
			std::unique_ptr<BView> _TakeView();

			bool				fEnabled = true;
			bool				fSelected = false;
			bool				fFocus = false;
			BView*				fView = nullptr;
			std::unique_ptr<BView> fOwnedView;
			BTabView*			fTabView = nullptr;
			std::string			fLabel;
};


/*!	Views on pages with a tab each, one page shown at a time (Haiku's
	BTabView). Clicking a tab, or the arrows and Enter or Space with the
	focus, selects a page; the back and forward mouse buttons go to the
	previous and next one. For a layout, the pages are cards of a
	BCardLayout in the container view; at a frame, the selected page is
	the container's only child.

	Differences to Haiku: tabs and views are owned (AddTab() takes them as
	std::unique_ptr, RemoveTab() gives the tab back with its view); the
	labels of tabs on the left or right side are not rotated (the Canvas
	has no rotation); no scripting. Sizes are pixel counts.
*/
class BTabView : public BView {
public:
								BTabView(std::string_view name,
									BButtonWidth width = BButtonWidth::FromWidest,
									BViewFlags flags = BViewFlag::FullUpdateOnResize
										| BViewFlag::WillDraw | BViewFlag::NavigableJump
										| BViewFlag::FrameEvents | BViewFlag::Navigable);
									/*!< A tab view for a layout. */
								BTabView(gfx::Rect frame, std::string_view name,
									BButtonWidth width = BButtonWidth::AsUsual,
									BResizingMode resizingMode = kFollowAll,
									BViewFlags flags = BViewFlag::FullUpdateOnResize
										| BViewFlag::WillDraw | BViewFlag::NavigableJump
										| BViewFlag::FrameEvents | BViewFlag::Navigable);
									/*!< A tab view at \a frame. */
								~BTabView() override;
									/*!< Destroys it with its tabs and views. */

			void				AttachedToWindow() override;
									/*!< Selects the first tab if none is. */
			void				KeyDown(std::string_view bytes) override;
									/*!< Arrows move the focus tab; Enter and Space select
										it. */
			void				MouseDown(gfx::Point where) override;
									/*!< Selects the tab clicked, or the previous or next
										one with the back and forward buttons. */

	virtual	void				Select(int32 index);
									/*!< Shows page \a index. */
			int32				Selection() const;
									/*!< The selected page, or -1. */

			void				WindowActivated(bool active) override;
									/*!< Draws the focus mark anew. */
			void				MakeFocus(bool focused = true) override;
									/*!< Takes the focus; the selected tab is the focus tab. */
	virtual	void				SetFocusTab(int32 tab, bool focused);
									/*!< Moves the keyboard focus to \a tab. */
			int32				FocusTab() const;
									/*!< The tab with the keyboard focus, or -1. */

			void				Draw(DrawContext& context) override;
									/*!< Tabs, box and focus mark. */
	virtual	gfx::Rect			DrawTabs(DrawContext& context);
									/*!< The tab frame and the tabs; returns the selected
										tab's frame. */
	virtual	void				DrawBox(DrawContext& context, gfx::Rect selectedTabRect);
									/*!< The frame around the pages. */
	virtual	gfx::Rect			TabFrame(int32 index) const;
									/*!< Where tab \a index is; empty for an invalid index.
										Neighbouring tabs share a pixel, as in Haiku. */

			gfx::Size			MinSize() override;
									/*!< Its layout's, or the tabs and the container's. */
			gfx::Size			MaxSize() override;
									/*!< Its layout's, or the tabs and the container's. */
			gfx::Size			PreferredSize() override;
									/*!< Its layout's, or the tabs and the container's. */

	virtual	void				AddTab(std::unique_ptr<BView> target,
									std::unique_ptr<BTab> tab = nullptr);
									/*!< Adds a page with \a target in \a tab (a new BTab
										by default). */
	virtual	std::unique_ptr<BTab> RemoveTab(int32 tabIndex);
									/*!< Takes tab \a tabIndex out, with its view. */
			BTab*				TabAt(int32 index) const;
									/*!< Tab \a index, or nullptr. */

	virtual	void				SetTabWidth(BButtonWidth width);
									/*!< How wide the tabs are. */
			BButtonWidth		TabWidth() const;
									/*!< How wide the tabs are. */

	virtual	void				SetTabHeight(float height);
									/*!< How high the tabs are (Haiku's measure: the bottom
										row of the tabs). */
			float				TabHeight() const;
									/*!< How high the tabs are. */

	virtual	void				SetBorder(BBorderStyle borderStyle);
									/*!< The frame around the pages. */
			BBorderStyle		Border() const;
									/*!< The frame around the pages. */

	virtual	void				SetTabSide(BTabSide tabSide);
									/*!< On which side the tabs are. */
			BTabSide			TabSide() const;
									/*!< On which side the tabs are. */

			BView*				ContainerView() const;
									/*!< The view the pages are shown in. */
			int32				CountTabs() const;
									/*!< The number of tabs. */
			BView*				ViewForTab(int32 tabIndex) const;
									/*!< The view of tab \a tabIndex, or nullptr. */
			int32				IndexOf(const BTab* tab) const;
									/*!< The index of \a tab, or -1. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role TabList. */
			void				GetAccessibleItems(std::vector<AccessibleItem>* items)
									const override;
									/*!< Its tabs: Tab with the label as name, Selected for the
										selected one, Disabled; Click. */
			std::expected<void, Error> PerformAccessibleItemAction(int32 index,
									AccessibleAction action, const BMessage& data) override;
									/*!< Click selects the tab. */

private:
			void				_InitObject(bool layouted, BButtonWidth width);
			void				_InitContainerView(bool layouted);
			gfx::Size			_TabsMinSize() const;
			float				_BorderWidth() const;
			void				_LayoutContainerView(bool layouted);
			gfx::Size			_Size(gfx::Size containerSize) const;

			std::vector<std::unique_ptr<BTab>> fTabList;
			BView*				fContainerView = nullptr;
			BButtonWidth		fTabWidthSetting;
			float				fTabHeight = 0;
			int32				fSelection = -1;
			int32				fFocus = -1;
			BBorderStyle		fBorderStyle = BBorderStyle::Fancy;
			BTabSide			fTabSide = BTabSide::Top;
};


} // namespace ngui
