/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>

#include <ngui/Bitmap.h>
#include <ngui/ColumnListView.h>
#include <ngui/DateFormat.h>
#include <ngui/NumberFormat.h>
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
			float				GetPreferredWidth(BField* field, BView* parent)
									const override;
									/*!< For the title (\a field nullptr): its width and
										the margins; otherwise the column's width. */

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
			float				GetPreferredWidth(BField* field, BView* parent)
									const override;
									/*!< The width of the string and the margins (of the
										title for nullptr). */

private:
			TruncationMode		fTruncate;
};


/*!	A field with a point in time (Haiku's BDateField). */
class BDateField : public BField {
public:
	explicit					BDateField(std::time_t time);
									/*!< A field with \a time. */

			void				SetUnixTime(std::time_t time);
									/*!< Sets the time. */
			std::time_t			UnixTime() const;
									/*!< The time, in seconds since 1970 (UTC). */

private:
			std::time_t			fTime;
};


/*!	A column of BDateFields (Haiku's BDateColumn): each time in the local
	time zone, written as the user's locale writes it (BDateFormat,
	BTimeFormat), as long as it fits: the long date with the time, the short
	date with the time, the short date, then the short date shortened. They
	sort by time. */
class BDateColumn : public BTitledColumn {
public:
								BDateColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column titled \a title. */

			void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor) override;
									/*!< The longest form that fits. */
			int					CompareFields(BField* field1, BField* field2) override;
									/*!< Earlier first. */
			bool				AcceptsField(const BField* field) const override;
									/*!< Whether \a field is a BDateField. */
			float				GetPreferredWidth(BField* field, BView* parent)
									const override;
									/*!< The width of the longest form and the margins. */

private:
			std::string			_Form(std::time_t time, int form) const;

			BDateFormat			fDateFormat;
			BTimeFormat			fTimeFormat;
};


/*!	A field with a size in bytes (Haiku's BSizeField). */
class BSizeField : public BField {
public:
	explicit					BSizeField(std::uint64_t size);
									/*!< A field with \a size bytes. */

			void				SetSize(std::uint64_t size);
									/*!< Sets the size. */
			std::uint64_t		Size() const;
									/*!< The size in bytes. */

private:
			std::uint64_t		fSize;
};


/*!	A column of BSizeFields (Haiku's BSizeColumn): "512 bytes", "1.50 KiB",
	... up to TiB, with two decimals in the user's locale (BNumberFormat).
	They sort by size. */
class BSizeColumn : public BTitledColumn {
public:
								BSizeColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column titled \a title. */

			void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor) override;
									/*!< The size, shortened to fit. */
			int					CompareFields(BField* field1, BField* field2) override;
									/*!< Smaller first. */
			bool				AcceptsField(const BField* field) const override;
									/*!< Whether \a field is a BSizeField. */
			float				GetPreferredWidth(BField* field, BView* parent)
									const override;
									/*!< The width of the size and the margins. */

	static	std::string			FormatSize(std::uint64_t size);
									/*!< \a size as the column writes it. */
};


/*!	A field with an integer (Haiku's BIntegerField). */
class BIntegerField : public BField {
public:
	explicit					BIntegerField(std::int32_t value);
									/*!< A field with \a value. */

			void				SetValue(std::int32_t value);
									/*!< Sets the value. */
			std::int32_t		Value() const;
									/*!< The value. */

private:
			std::int32_t		fValue;
};


/*!	A column of BIntegerFields (Haiku's BIntegerColumn): each number in the
	user's locale (BNumberFormat); they sort by value. */
class BIntegerColumn : public BTitledColumn {
public:
								BIntegerColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column titled \a title. */

			void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor) override;
									/*!< The number, shortened to fit. */
			int					CompareFields(BField* field1, BField* field2) override;
									/*!< Smaller first. */
			bool				AcceptsField(const BField* field) const override;
									/*!< Whether \a field is a BIntegerField. */
			float				GetPreferredWidth(BField* field, BView* parent)
									const override;
									/*!< The width of the number and the margins. */

private:
			BNumberFormat		fNumberFormat;
};


/*!	A column that shows BIntegerFields of 0 to 100 as a bar (Haiku's
	GraphColumn): filled in the control highlight color as far as the value, with
	the value and "%" centered on it. */
class GraphColumn : public BIntegerColumn {
public:
								GraphColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Center);
									/*!< A column titled \a title. */

			void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor) override;
									/*!< The bar with the value; values outside 0 to 100
										are drawn as the nearest. */
};


/*!	A field with a bitmap (Haiku's BBitmapField).

	Differences to Haiku: the field shares the bitmap (Haiku: it points to
	one the application keeps).
*/
class BBitmapField : public BField {
public:
	explicit					BBitmapField(std::shared_ptr<const BBitmap> bitmap);
									/*!< A field with \a bitmap (may be nullptr). */

			void				SetBitmap(std::shared_ptr<const BBitmap> bitmap);
									/*!< Sets the bitmap. */
			const BBitmap*		Bitmap() const;
									/*!< The bitmap, or nullptr. */

private:
			std::shared_ptr<const BBitmap> fBitmap;
};


/*!	A column of BBitmapFields (Haiku's BBitmapColumn): each bitmap aligned
	horizontally, centered vertically and scaled down if it does not fit.
	They do not sort (CompareFields() gives 0). */
class BBitmapColumn : public BTitledColumn {
public:
								BBitmapColumn(std::string_view title, float width,
									float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column titled \a title. */

			void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor) override;
									/*!< The bitmap. */
			int					CompareFields(BField* field1, BField* field2) override;
									/*!< 0. */
			bool				AcceptsField(const BField* field) const override;
									/*!< Whether \a field is a BBitmapField. */
			float				GetPreferredWidth(BField* field, BView* parent)
									const override;
									/*!< The width of the bitmap and the margins. */
};


} // namespace ngui
