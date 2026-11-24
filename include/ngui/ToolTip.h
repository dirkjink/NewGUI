/*
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string>
#include <string_view>

#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/gfx/Point.h>


namespace ngui {


class BTextView;
class BView;
class ToolTipPopup;


/*!	A tooltip: a view shown near the mouse when it rests over a view
	(Haiku's BToolTip). BView::SetToolTip() gives a view one;
	BWindow shows it in a popup of the window after
	BToolTipManager::ShowDelay() and hides it when the mouse moves or a key
	is pressed.

	Differences to Haiku: the tooltip owns its content view, given to the
	constructor (Haiku's subclasses override View()); views share a tooltip
	through std::shared_ptr instead of BReferenceable. A tooltip is shown in
	one window at a time; its content is changed from the thread of that
	window, or while it is not shown (Haiku locked the looper).
*/
class BToolTip {
public:
	explicit					BToolTip(std::unique_ptr<BView> view);
									/*!< A tooltip showing \a view, which it owns: below
										and right of the mouse (alignment Right, Bottom),
										at the big spacing from it, not sticky. */
	virtual						~BToolTip();
									/*!< Destroys the content view. */

								BToolTip(const BToolTip&) = delete;
			BToolTip&			operator=(const BToolTip&) = delete;

			BView*				View() const;
									/*!< The content view; while shown, a child of the
										popup. */

	virtual	void				SetSticky(bool enable);
									/*!< A sticky tooltip follows the mouse and stays
										when a key is pressed; it hides when the mouse
										leaves the view. */
			bool				IsSticky() const;
									/*!< It is sticky. */
			void				SetMouseRelativeLocation(gfx::Point location);
									/*!< The distance from the mouse in both
										directions. */
			gfx::Point			MouseRelativeLocation() const;
									/*!< The distance from the mouse. */
			void				SetAlignment(BAlignment alignment);
									/*!< Which side of the mouse it prefers: Left, Center
										or Right; Top, Middle or Bottom. Where it does not
										fit on the screen, it goes to the other side. */
			BAlignment			Alignment() const;
									/*!< The preferred side. */

	virtual	void				AttachedToWindow();
									/*!< Hook: it is about to be shown. */
	virtual	void				DetachedFromWindow();
									/*!< Hook: it was hidden. */

protected:
			void				ContentChanged();
									/*!< Tells the popup that shows it that its size may
										have changed; it is placed anew. */

private:
	friend class ToolTipPopup;

			std::unique_ptr<BView> fOwnedView;
									// while not shown
			BView*				fView;
			ToolTipPopup*		fShownIn = nullptr;
			gfx::Point			fRelativeLocation;
			BAlignment			fAlignment = {BHorizontalAlignment::Right,
									BVerticalAlignment::Bottom};
			bool				fIsSticky = false;
};


/*!	A tooltip of text (Haiku's BTextToolTip): a read-only BTextView without
	word wrap, in the palette's tooltip colors. */
class BTextToolTip : public BToolTip {
public:
	explicit					BTextToolTip(std::string_view text);
									/*!< A tooltip showing \a text; lines break at
										'\\n'. */

			std::string			Text() const;
									/*!< The text. */
			void				SetText(std::string_view text);
									/*!< Changes the text; a shown tooltip takes the new
										size. */

private:
			BTextView*			fTextView;
};


/*!	The settings of tooltips (Haiku's BToolTipManager): how long the mouse
	rests before a tooltip shows, how long a hidden one stays. Thread-safe.

	Differences to Haiku: the tooltip is a popup of the window of its view,
	not a window of its own with its own thread; ShowTip() and HideTip()
	act on the window of the calling thread.
*/
class BToolTipManager {
public:
	static	BToolTipManager*	Manager();
									/*!< The manager of the application. */

			void				ShowTip(std::shared_ptr<BToolTip> tip, gfx::Point where,
									const BView* owner);
									/*!< Shows \a tip for \a owner at \a where on the
										screen, in a popup of \a owner's window; from that
										window's thread. A tip already shown for the same
										tooltip only moves. */
			void				HideTip();
									/*!< Hides the tooltip of the calling thread's window,
										after HideDelay(). */

			void				SetShowDelay(Clock::Time delay);
									/*!< The rest before a tooltip shows, 10 ms .. 3 s
										(750 ms by default). */
			Clock::Time			ShowDelay() const;
									/*!< The rest before a tooltip shows. */
			void				SetHideDelay(Clock::Time delay);
									/*!< How long a hidden tooltip stays before it closes,
										0 .. 0.5 s (50 ms by default); shown again in that
										time it stays open. */
			Clock::Time			HideDelay() const;
									/*!< How long a hidden tooltip stays. */

private:
								BToolTipManager() = default;
};


} // namespace ngui
