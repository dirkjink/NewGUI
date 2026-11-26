/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>
#include <string_view>

#include <ngui/ColumnListView.h>
#include <ngui/TextSystem.h>


namespace ngui {


/*!	A column with a title (Haiku's BTitledColumn, an own implementation
	after its API): DrawTitle() draws the title, GetColumnName() gives it.
	DrawString() draws text in a field as the derived columns do: aligned,
	with a margin, on the baseline of the plain font centered vertically. */
class BTitledColumn : public BColumn {
public:
								BTitledColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column titled \a title. */

			void				DrawTitle(BView* owner, DrawContext& context, gfx::Rect rect,
									const gfx::Color& textColor) override;
									/*!< The title, shortened to fit. */
			void				GetColumnName(std::string* into) const override;
									/*!< The title. */

			void				DrawString(std::string_view string, BView* owner,
									DrawContext& context, gfx::Rect rect,
									const gfx::Color& color);
									/*!< Draws \a string in \a rect as described above. */
			void				SetTitle(std::string_view title);
									/*!< Sets the title. */
			const std::string&	Title() const;
									/*!< The title. */

	static	constexpr float		kTextMargin = 8.0f;
									/*!< The space left and right of text. */

private:
			std::string			fTitle;
};


/*!	A field with a string (Haiku's BStringField). */
class BStringField : public BField {
public:
	explicit					BStringField(std::string_view string);
									/*!< A field with \a string. */

			void				SetString(std::string_view string);
									/*!< Sets the string. */
			const std::string&	String() const;
									/*!< The string. */

private:
			std::string			fString;
};


/*!	A column of BStringFields (Haiku's BStringColumn): each string is
	shortened to fit the column; they sort without regard to the case of
	ASCII letters, then by bytes.

	Differences to Haiku: the shortened string is not kept in the field
	(Haiku's SetClippedString() and HasClippedString() do not exist).
*/
class BStringColumn : public BTitledColumn {
public:
								BStringColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									TruncationMode truncate = TruncationMode::Middle,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column titled \a title, shortening strings with
										\a truncate. */

			void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor) override;
									/*!< The string, shortened to fit. */
			int					CompareFields(BField* field1, BField* field2) override;
									/*!< Compares the strings as described above. */
			bool				AcceptsField(const BField* field) const override;
									/*!< Whether \a field is a BStringField. */

private:
			TruncationMode		fTruncate;
};


} // namespace ngui
