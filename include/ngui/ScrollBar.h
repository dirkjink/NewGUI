/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>

#include <ngui/ControlLook.h>
#include <ngui/Error.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/SupportDefs.h>
#include <ngui/View.h>


namespace ngui {


/*!	How scroll bars look and behave, for all of them (Haiku's
	scroll_bar_info). Haiku's defaults. */
struct BScrollBarInfo {
			bool				proportional = true;
									/*!< The thumb shows the visible part. */
			bool				doubleArrows = false;
									/*!< Two arrows at each end. */
			KnobStyle			knob = KnobStyle::None;
									/*!< The grip on the thumb. */
			std::int32_t		minKnobSize = 15;
									/*!< The smallest thumb at a 12 pixel font, 9 .. 50. */

			bool				operator==(const BScrollBarInfo& other) const = default;
									/*!< All fields equal. */
};


/*!	The settings of scroll bars made from now on (Haiku's
	get_scroll_bar_info()). Thread-safe. */
BScrollBarInfo					GetScrollBarInfo();

/*!	Changes the settings of scroll bars made from now on (Haiku's
	set_scroll_bar_info()); InvalidArgument if minKnobSize is not within
	9 .. 50. Thread-safe. */
std::expected<void, Error>		SetScrollBarInfo(const BScrollBarInfo& info);


/*!	A scroll bar (Haiku's BScrollBar): a value from a minimum to a maximum,
	shown by a thumb between arrow buttons, that scrolls its target view.
	Dragging the thumb, clicking the arrows (with Shift a large step) or
	the empty area (a large step), holding them down (repeated after
	300 ms, every 25 ms) and the mouse wheel change it; the secondary
	button moves the thumb where it is clicked.

	Differences to Haiku: buttons repeat with a BMessageRunner on the
	window's clock (Clock::Default()) instead of a thread of its own, so a
	repeat needs the bar in a window; the mouse wheel over the bar is not
	passed on to its parents as well. Sizes are pixel counts, one more than
	Haiku's.
*/
class BScrollBar : public BView {
public:
								BScrollBar(gfx::Rect frame, std::string_view name,
									BView* target, float min, float max,
									BOrientation direction);
									/*!< A scroll bar at \a frame for \a target (may be
										nullptr), following its parent's right or bottom
										side. */
								BScrollBar(std::string_view name, BView* target,
									float min, float max, BOrientation direction);
									/*!< A scroll bar for a layout. */
								~BScrollBar() override;
									/*!< Stops scrolling its target. */

			void				Draw(DrawContext& context) override;
									/*!< Border, arrows, background and thumb. */
			void				FrameResized(float width, float height) override;
									/*!< Moves the thumb to the new size. */
			void				MessageReceived(BMessage* message) override;
									/*!< The mouse wheel and the repeat of a held button. */
			void				MouseDown(gfx::Point where) override;
									/*!< Starts dragging the thumb or scrolling by a step. */
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Drags the thumb, or pauses a repeat outside its
										button. */
			void				MouseUp(gfx::Point where) override;
									/*!< Ends dragging or repeating. */
			void				WindowActivated(bool active) override;
									/*!< Works only in an active window, as in Haiku. */

			void				SetValue(float value);
									/*!< Sets the value, rounded and kept within the range,
										and calls ValueChanged(). */
			float				Value() const;
									/*!< The value. */
	virtual	void				ValueChanged(float newValue);
									/*!< Hook: scrolls the target to \a newValue. */

			void				SetProportion(float value);
									/*!< The visible part of the target, 0 to 1; the size of
										a proportional thumb. */
			float				Proportion() const;
									/*!< The visible part of the target. */

			void				SetRange(float min, float max);
									/*!< The range of the value, rounded; invalid ranges
										become 0 .. 0. */
			void				GetRange(float* min, float* max) const;
									/*!< The range; each pointer may be nullptr. */

			void				SetSteps(float smallStep, float largeStep);
									/*!< The steps of an arrow and of a page, rounded. */
			void				GetSteps(float* smallStep, float* largeStep) const;
									/*!< The steps; each pointer may be nullptr. */

			void				SetTarget(BView* target);
									/*!< The view it scrolls, or nullptr. */
			void				SetTarget(std::string_view targetName);
									/*!< The view of its window named \a targetName; nothing
										happens without a window or such a view. */
			BView*				Target() const;
									/*!< The view it scrolls. */

			void				SetOrientation(BOrientation direction);
									/*!< Horizontal or vertical. */
			BOrientation		Orientation() const;
									/*!< Horizontal or vertical. */

			void				SetBorderHighlighted(bool highlight);
									/*!< Draws the border as focused (for a BScrollView). */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< The look's scroll bar width across, the minimum
										along. */
			gfx::Size			MinSize() override;
									/*!< Room for two arrows and two minimal thumbs. */
			gfx::Size			MaxSize() override;
									/*!< Unlimited along the bar. */
			gfx::Size			PreferredSize() override;
									/*!< GetPreferredSize(). */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role ScrollBar: the value in its range, the small step;
										Increment and Decrement. */
			std::expected<void, Error> PerformAccessibleAction(AccessibleAction action,
									const BMessage& data) override;
									/*!< Increment and Decrement by the small step. */

private:
			class Private;
			friend class Private;

			bool				_DoubleArrows() const;
			void				_UpdateThumbFrame();
			float				_ValueFor(gfx::Point where) const;
			int32				_ButtonFor(gfx::Point where) const;
			gfx::Rect			_ButtonRectFor(int32 button) const;
			void				_UpdateTargetValue(gfx::Point where);
			void				_UpdateArrowButtons();
			void				_Repeat();
			gfx::Size			_MinSize() const;

			float				fMin;
			float				fMax;
			float				fSmallStep = 1.0f;
			float				fLargeStep = 10.0f;
			float				fValue = 0.0f;
			float				fProportion = 0.0f;
			BView*				fTarget = nullptr;
			BOrientation		fOrientation;

			std::unique_ptr<Private> fPrivateData;
};


} // namespace ngui
