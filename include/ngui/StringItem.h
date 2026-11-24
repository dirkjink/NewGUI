/*
 * Copyright 2006-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>
#include <string_view>

#include <ngui/ListItem.h>


namespace ngui {


/*!	A list item that shows a line of text (Haiku's BStringItem). */
class BStringItem : public BListItem {
public:
	explicit					BStringItem(std::string_view text, uint32 outlineLevel = 0,
									bool expanded = true);
									/*!< An item showing \a text. */
								~BStringItem() override;
									/*!< Destroys the item. */

			void				DrawItem(BView* owner, DrawContext& context, gfx::Rect frame,
									const gfx::Color& textColor,
									bool complete = false) override;
									/*!< The text, on the selection color if selected. */
	virtual	void				SetText(std::string_view text);
									/*!< The text it shows. */
			const std::string&	Text() const;
									/*!< The text. */
			void				Update(BView* owner, const TextSystem& text) override;
									/*!< As wide as the text plus the label spacing, a
										line and 4 pixels high. */

protected:
			float				BaselineOffset() const;
									/*!< From its top to the text's baseline. */

private:
			std::string			fText;
			float				fBaselineOffset = 0;
};


} // namespace ngui
