/*
 * Copyright 2009-2012, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>

#include <ngui/AppDefs.h>
#include <ngui/MessageRunner.h>
#include <ngui/Message.h>
#include <ngui/gfx/Point.h>


namespace ngui {


class BToolTip;
class BView;
class BWindow;


// The hide delay passed (Haiku's kMsgCloseToolTip): a hidden tooltip
// closes. Sent to the window.
inline constexpr std::uint32_t kToolTipClose = FourCharCode("_TTC");

// The show delay passed since the mouse last moved: the window checks
// whether it rested (then kMouseIdle to the view under it).
inline constexpr std::uint32_t kMouseIdleCheck = FourCharCode("_MIC");


// The tooltip of a window (Haiku's ToolTipWindow with its ToolTipView):
// shown in a popup of the window, at most one at a time. Used on the
// window's thread only.
class ToolTipPopup {
public:
	explicit					ToolTipPopup(BWindow* window);
								~ToolTipPopup();

								ToolTipPopup(const ToolTipPopup&) = delete;
			ToolTipPopup&		operator=(const ToolTipPopup&) = delete;

			// Shows \a tip for \a owner at \a where (screen); the tip already
			// shown only moves there and stays.
			void				Show(const std::shared_ptr<BToolTip>& tip, gfx::Point where,
									const BView* owner);
			// Closes after the hide delay, unless shown again.
			void				Hide();
			// Closes at once and gives the tip its view back.
			void				Close();
			// kToolTipClose: closes if still hidden.
			void				CloseIfHidden();

			// The mouse moved to \a where (screen) anywhere in the window: a
			// sticky tip follows, another one hides.
			void				MouseMoved(gfx::Point where);
			// The mouse moved over the tooltip itself (Haiku's B_ENTERED_VIEW
			// on the ToolTipView): a sticky tip follows, another one closes.
			void				MouseMovedOver(gfx::Point where);
			// A key went down: a tip that is not sticky hides.
			void				KeyDown();
			// \a view leaves the window: its tip closes.
			void				ViewDetached(const BView* view);
			// The tip's content changed its size.
			void				ContentChanged();

			BView*				Popup() const;
			const BView*		Owner() const;
			bool				IsHidden() const;

private:
			void				_ResetFrame(gfx::Point where);

			BWindow*			fWindow;
			std::shared_ptr<BToolTip> fTip;
			const BView*		fOwner = nullptr;
			BView*				fView = nullptr;
									// the popup's root, while shown
			gfx::Point			fWhere = {0, 0};
			bool				fHidden = false;
			std::unique_ptr<BMessageRunner> fCloser;
};


} // namespace ngui
