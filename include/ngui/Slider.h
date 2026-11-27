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

#include <ngui/Control.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/gfx/Color.h>


namespace ngui {


/*!	The thumb of a slider (Haiku's thumb_style). */
enum class BThumbStyle : std::uint8_t {
	Block,		/*!< B_BLOCK_THUMB */
	Triangle	/*!< B_TRIANGLE_THUMB */
};


/*!	A value between two limits chosen with a thumb on a bar (Haiku's
	BSlider), horizontal or vertical, with a label, limit labels, a text
	that follows the value (UpdateText()), hash marks and a fill color.
	It sends its modification message while the thumb moves and its message
	when the user lets go; the arrow keys, Home and End move it as well.

	Differences to Haiku: the mouse is always followed asynchronously; the
	drawing hooks get the DrawContext (Haiku's draw on the view); no
	off-screen view, no icons; UpdateText() returns a string, empty for
	none. The geometry is Haiku's, computed with inclusive rectangles and
	given out half-open: BarFrame() and ThumbFrame() cover the same pixels
	as in Haiku. Sizes are pixel counts, one more than Haiku's.
*/
class BSlider : public BControl {
public:
								BSlider(gfx::Rect frame, std::string_view name,
									std::string_view label,
									std::unique_ptr<BMessage> message,
									int32 minValue, int32 maxValue,
									BThumbStyle thumbType = BThumbStyle::Block,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::Navigable
										| BViewFlag::WillDraw | BViewFlag::FrameEvents);
									/*!< A horizontal slider at \a frame from \a minValue
										to \a maxValue. */
								BSlider(gfx::Rect frame, std::string_view name,
									std::string_view label,
									std::unique_ptr<BMessage> message,
									int32 minValue, int32 maxValue,
									BOrientation posture,
									BThumbStyle thumbType = BThumbStyle::Block,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::Navigable
										| BViewFlag::WillDraw | BViewFlag::FrameEvents);
									/*!< A slider at \a frame in \a posture. */
								BSlider(std::string_view name, std::string_view label,
									std::unique_ptr<BMessage> message,
									int32 minValue, int32 maxValue,
									BOrientation posture,
									BThumbStyle thumbType = BThumbStyle::Block,
									BViewFlags flags = BViewFlag::Navigable
										| BViewFlag::WillDraw | BViewFlag::FrameEvents);
									/*!< A slider for a layout. */
								~BSlider() override;
									/*!< Destroys the slider and its messages. */

			void				AttachedToWindow() override;
									/*!< Takes the panel colors, its preferred size and
										checks its value. */
			void				FrameResized(float width, float height) override;
									/*!< Draws it anew. */
			void				KeyDown(std::string_view bytes) override;
									/*!< Arrows, Home and End move the value. */
			void				KeyUp(std::string_view bytes) override;
									/*!< Sends the message after a key moved the value. */
			void				MessageReceived(BMessage* message) override;
									/*!< NewGUI: kColorsUpdated gives the default bar the
										application's new colors; then as BControl's. */
			void				MouseDown(gfx::Point where) override;
									/*!< Moves the thumb there and follows the mouse. */
			void				MouseUp(gfx::Point where) override;
									/*!< Sends the message if the value changed. */
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Moves the thumb while following the mouse. */

			void				SetLabel(std::string_view label) override;
									/*!< Sets the label above the bar. */
	virtual	void				SetLimitLabels(std::string_view minLabel,
									std::string_view maxLabel);
									/*!< Labels at both ends; empty for none. */
			const std::string&	MinLimitLabel() const;
									/*!< The label at the minimum. */
			const std::string&	MaxLimitLabel() const;
									/*!< The label at the maximum. */

			void				SetValue(int32 value) override;
									/*!< Sets the value, kept within the limits. */
	virtual	int32				ValueForPoint(gfx::Point point) const;
									/*!< The value for the thumb at \a point. */
	virtual	void				SetPosition(float position);
									/*!< Sets the value from 0 (minimum) to 1 (maximum). */
			float				Position() const;
									/*!< The value from 0 to 1. */
			void				GetLimits(int32* minimum, int32* maximum) const;
									/*!< The limits; each pointer may be nullptr. */
	virtual	void				SetLimits(int32 minimum, int32 maximum);
									/*!< Sets the limits (ignored if minimum > maximum). */

			void				Draw(DrawContext& context) override;
									/*!< DrawSlider(). */
	virtual	void				DrawSlider(DrawContext& context);
									/*!< Bar, hash marks, thumb, focus mark and text. */
	virtual	void				DrawBar(DrawContext& context);
									/*!< The bar, filled up to the value. */
	virtual	void				DrawHashMarks(DrawContext& context);
									/*!< The hash marks. */
	virtual	void				DrawThumb(DrawContext& context);
									/*!< The thumb in its style. */
	virtual	void				DrawFocusMark(DrawContext& context);
									/*!< The focus mark on the thumb. */
	virtual	void				DrawText(DrawContext& context);
									/*!< The label, the update text and the limit labels. */

	virtual	std::string			UpdateText() const;
									/*!< A text for the current value; empty by default. */
			void				UpdateTextChanged();
									/*!< UpdateText() changed: draws it anew. */

	virtual	gfx::Rect			BarFrame() const;
									/*!< Where the bar is. */
	virtual	gfx::Rect			HashMarksFrame() const;
									/*!< Where the hash marks are. */
	virtual	gfx::Rect			ThumbFrame() const;
									/*!< Where the thumb is. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< The preferred size, never smaller than its current
										length (as Haiku's, for compatibility). */

	virtual	void				SetModificationMessage(std::unique_ptr<BMessage> message);
									/*!< The message sent while the value changes. */
			BMessage*			ModificationMessage() const;
									/*!< The modification message, or nullptr. */

	virtual	void				SetSnoozeAmount(int32 microSeconds);
									/*!< Kept for ported code (10 ms to 1 s); the mouse is
										followed by events. */
			int32				SnoozeAmount() const;
									/*!< The snooze amount. */

	virtual	void				SetKeyIncrementValue(int32 value);
									/*!< How far an arrow key moves the value. */
			int32				KeyIncrementValue() const;
									/*!< How far an arrow key moves the value. */

	virtual	void				SetHashMarkCount(int32 count);
									/*!< The number of hash marks. */
			int32				HashMarkCount() const;
									/*!< The number of hash marks. */
	virtual	void				SetHashMarks(BHashMarkLocation where);
									/*!< On which sides the hash marks are. */
			BHashMarkLocation	HashMarks() const;
									/*!< On which sides the hash marks are. */

	virtual	void				SetStyle(BThumbStyle style);
									/*!< Block or triangle thumb. */
			BThumbStyle			Style() const;
									/*!< The thumb style. */

	virtual	void				SetBarColor(gfx::Color color);
									/*!< The color of the bar. */
			gfx::Color			BarColor() const;
									/*!< The color of the bar. */
	virtual	void				UseFillColor(bool useFill,
									const gfx::Color* color = nullptr);
									/*!< Fills the bar up to the value in \a color. */
			bool				FillColor(gfx::Color* color) const;
									/*!< Whether a fill color is used; stores it in
										\a color. */

			BOrientation		Orientation() const;
									/*!< Horizontal or vertical. */
	virtual	void				SetOrientation(BOrientation orientation);
									/*!< Makes it horizontal or vertical. */

			float				BarThickness() const;
									/*!< The thickness of the bar (6 by default). */
	virtual	void				SetBarThickness(float thickness);
									/*!< Sets the thickness, at least 1, rounded. */

	virtual	float				MaxUpdateTextWidth();
									/*!< The width of UpdateText() at the maximum. */

			gfx::Size			MinSize() override;
									/*!< Room for the bar, the labels and the texts. */
			gfx::Size			MaxSize() override;
									/*!< Unlimited along the bar. */
			gfx::Size			PreferredSize() override;
									/*!< At least 101 along the bar. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Slider: the value with its limits, the key
										increment as step, UpdateText() as text;
										Increment, Decrement and SetValue instead of
										Click. */
			std::expected<void, Error> PerformAccessibleAction(AccessibleAction action,
									const BMessage& data) override;
									/*!< Increment and Decrement as the arrow keys do, SetValue
										("number") as SetValue() and Invoke(). */

protected:
			void				LayoutInvalidated(bool descendants) override;
									/*!< Drops the cached minimum size. */

private:
			void				_DrawBlockThumb(DrawContext& context);
			void				_DrawTriangleThumb(DrawContext& context);

			gfx::Point			_Location() const;
			void				_SetLocationForValue(int32 value);

			float				_MinPosition() const;
			float				_MaxPosition() const;
			bool				_ConstrainPoint(gfx::Point& point,
									gfx::Point compare) const;

			gfx::Size			_ValidateMinSize();

			void				_InitBarColor();
			void				_InitObject();

			std::unique_ptr<BMessage> fModificationMessage;
			int32				fSnoozeAmount = 20000;

			gfx::Color			fBarColor;
			gfx::Color			fFillColor;
			bool				fUseFillColor = false;
			bool				fDefaultBarColor = true;

			std::string			fMinLimitLabel;
			std::string			fMaxLimitLabel;
			std::string			fUpdateText;

			int32				fMinValue;
			int32				fMaxValue;
			int32				fKeyIncrementValue = 1;

			int32				fHashMarkCount = 0;
			BHashMarkLocation	fHashMarks = BHashMarkLocation::None;

			BThumbStyle			fStyle;

			gfx::Point			fLocation = {0, 0};
			gfx::Point			fInitialLocation = {0, 0};

			BOrientation		fOrientation;
			float				fBarThickness = 6.0f;

			gfx::Size			fMinSize = {-1, -1};
									// Haiku's size convention (pixels - 1)
			float				fMaxUpdateTextWidth = -1.0f;
};


} // namespace ngui
