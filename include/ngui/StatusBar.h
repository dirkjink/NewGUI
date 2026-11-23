/*
 * Copyright 2006-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <optional>
#include <string>
#include <string_view>

#include <ngui/View.h>
#include <ngui/gfx/Color.h>


namespace ngui {


/*!	A gauge of how much of something is done (Haiku's BStatusBar): a bar
	filled from 0 up to MaxValue(), with a label and a text on the left and
	a trailing text and label on the right above it. It also takes
	kUpdateStatusBar and kResetStatusBar messages.

	Differences to Haiku: strings instead of `const char*` (Update() keeps a
	text for std::nullopt, as Haiku for NULL); the bar color follows the
	palette until SetBarColor() is called, so there is no B_COLORS_UPDATED;
	the view color stays unset and the text and the bar use the parent's
	view color as background. Sizes are pixel counts, one more than Haiku's.
*/
class BStatusBar : public BView {
public:
								BStatusBar(gfx::Rect frame, std::string_view name,
									std::string_view label = {},
									std::string_view trailingLabel = {});
									/*!< A status bar at \a frame; it always has room
										for the texts, as in BeOS. */
								BStatusBar(std::string_view name,
									std::string_view label = {},
									std::string_view trailingLabel = {});
									/*!< A status bar for a layout. */
								~BStatusBar() override;
									/*!< Destroys the status bar. */

			void				AttachedToWindow() override;
									/*!< Takes its preferred height. */
			void				FrameResized(float width, float height) override;
									/*!< Draws it anew. */
			void				Draw(DrawContext& context) override;
									/*!< The texts, shortened to fit, and the bar. */
			void				MessageReceived(BMessage* message) override;
									/*!< kUpdateStatusBar and kResetStatusBar. */

	virtual	void				SetBarColor(gfx::Color color);
									/*!< A bar color of its own instead of the palette's. */
	virtual	void				SetBarHeight(float height);
									/*!< The height of the bar. */

	virtual	void				SetText(std::string_view text);
									/*!< The text after the label. */
	virtual	void				SetTrailingText(std::string_view text);
									/*!< The text before the trailing label. */
	virtual	void				SetMaxValue(float max);
									/*!< The value of a full bar (100 by default); does
										not draw anew, as in BeOS. */

	virtual	void				Update(float delta,
									std::optional<std::string_view> text = std::nullopt,
									std::optional<std::string_view> trailingText
										= std::nullopt);
									/*!< Adds \a delta to the value; the texts stay for
										std::nullopt. */
	virtual	void				Reset(std::string_view label = {},
									std::string_view trailingLabel = {});
									/*!< New labels, no texts, value 0 of 100. */
	virtual	void				SetTo(float value, std::string_view text = {},
									std::string_view trailingText = {});
									/*!< Sets the value (kept in 0 .. MaxValue()) and the
										texts. */

			float				CurrentValue() const;
									/*!< The value. */
			float				MaxValue() const;
									/*!< The value of a full bar. */
			gfx::Color			BarColor() const;
									/*!< Its own bar color, or the palette's. */
			float				BarHeight() const;
									/*!< The height of the bar; by default the height of
										the text plus 5. */

			const std::string&	Text() const;
									/*!< The text after the label. */
			const std::string&	TrailingText() const;
									/*!< The text before the trailing label. */
			const std::string&	Label() const;
									/*!< The label on the left. */
			const std::string&	TrailingLabel() const;
									/*!< The label on the right. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< Room for all texts and the bar. */
			gfx::Size			MinSize() override;
									/*!< The preferred size. */
			gfx::Size			MaxSize() override;
									/*!< Unlimited width, the preferred height. */
			gfx::Size			PreferredSize() override;
									/*!< Room for all texts and the bar. */

private:
			void				_SetTextData(std::string& text, std::string_view source);
			gfx::Rect			_BarFrame() const;
			float				_BarPosition(const gfx::Rect& barFrame) const;
			bool				_HasText() const;
			gfx::Color			_Background(const DrawContext& context) const;

			std::string			fLabel;
			std::string			fTrailingLabel;
			std::string			fText;
			std::string			fTrailingText;
			float				fMax = 100.0f;
			float				fCurrent = 0.0f;
	mutable	float				fBarHeight = -1.0f;
			std::optional<gfx::Color> fBarColor;
			bool				fCustomBarHeight = false;
};


} // namespace ngui
