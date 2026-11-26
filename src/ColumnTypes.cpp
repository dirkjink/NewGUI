/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ColumnTypes.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <iterator>
#include <string>
#include <utility>

#include <ngui/ControlLook.h>
#include <ngui/Palette.h>
#include <ngui/gfx/Canvas.h>

#include "ColumnListViewPrivate.h"


namespace ngui {


namespace {


// The forms of a date BDateColumn tries, longest first.
constexpr int kDateForms = 3;


float
TextWidth(std::string_view text)
{
	return TextSystem::Default().StringWidth(text, FontRole::Plain);
}


template<typename T>
int
CompareValues(T a, T b)
{
	return a < b ? -1 : (a > b ? 1 : 0);
}


unsigned char
FoldCase(char c)
{
	const auto byte = static_cast<unsigned char>(c);
	return byte >= 'A' && byte <= 'Z' ? static_cast<unsigned char>(byte - 'A' + 'a') : byte;
}


} // namespace


// #pragma mark - BTitledColumn


BTitledColumn::BTitledColumn(std::string_view title, float width, float minWidth,
	float maxWidth, BHorizontalAlignment alignment)
	:
	BColumn(width, minWidth, maxWidth, alignment),
	fTitle(title)
{
}


void
BTitledColumn::DrawTitle(BView* owner, DrawContext& context, gfx::Rect rect,
	const gfx::Color& textColor)
{
	DrawString(fTitle, owner, context, rect, textColor);
}


void
BTitledColumn::GetColumnName(std::string* into) const
{
	if (into != nullptr)
		*into = fTitle;
}


float
BTitledColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	if (field != nullptr)
		return BColumn::GetPreferredWidth(field, parent);
	return TextWidth(fTitle) + 2 * kTextMargin;
}


void
BTitledColumn::DrawString(std::string_view string, BView* /*owner*/, DrawContext& context,
	gfx::Rect rect, const gfx::Color& color)
{
	const TextSystem& text = context.text;
	const float available = rect.Width() - 2 * kTextMargin;
	if (string.empty() || available <= 0)
		return;

	std::string shown(string);
	if (text.StringWidth(shown, FontRole::Plain) > available)
		shown = text.Truncate(string, available, TruncationMode::End, FontRole::Plain);
	const float width = text.StringWidth(shown, FontRole::Plain);

	float x = rect.x0 + kTextMargin;
	switch (Alignment()) {
		case BHorizontalAlignment::Right:
			x = rect.x1 - kTextMargin - width;
			break;
		case BHorizontalAlignment::Center:
			x = rect.x0 + (rect.Width() - width) / 2;
			break;
		case BHorizontalAlignment::Left:
		case BHorizontalAlignment::Unset:
		case BHorizontalAlignment::UseFullWidth:
			break;
	}

	text.Shape(shown, FontRole::Plain).Draw(context.canvas,
		{std::round(x), ColumnTextBaseline(rect, text)}, gfx::Paint{.color = color});
}


void
BTitledColumn::SetTitle(std::string_view title)
{
	fTitle = title;
}


const std::string&
BTitledColumn::Title() const
{
	return fTitle;
}


// #pragma mark - BStringField


BStringField::BStringField(std::string_view string)
	:
	fString(string)
{
}


void
BStringField::SetString(std::string_view string)
{
	fString = string;
}


const std::string&
BStringField::String() const
{
	return fString;
}


// #pragma mark - BStringColumn


BStringColumn::BStringColumn(std::string_view title, float width, float minWidth,
	float maxWidth, TruncationMode truncate, BHorizontalAlignment alignment)
	:
	BTitledColumn(title, width, minWidth, maxWidth, alignment),
	fTruncate(truncate)
{
}


void
BStringColumn::DrawField(BField* field, BView* owner, DrawContext& context, gfx::Rect rect,
	const gfx::Color& textColor)
{
	const std::string& string = static_cast<BStringField*>(field)->String();
	const float available = rect.Width() - 2 * kTextMargin;
	if (available <= 0)
		return;

	if (context.text.StringWidth(string, FontRole::Plain) <= available)
		DrawString(string, owner, context, rect, textColor);
	else {
		DrawString(context.text.Truncate(string, available, fTruncate, FontRole::Plain), owner,
			context, rect, textColor);
	}
}


int
BStringColumn::CompareFields(BField* field1, BField* field2)
{
	const std::string& first = static_cast<BStringField*>(field1)->String();
	const std::string& second = static_cast<BStringField*>(field2)->String();

	const auto folded = std::lexicographical_compare_three_way(first.begin(), first.end(),
		second.begin(), second.end(), [](char a, char b) { return FoldCase(a) <=> FoldCase(b); });
	if (folded != 0)
		return folded < 0 ? -1 : 1;

	const int bytes = first.compare(second);
	return bytes < 0 ? -1 : (bytes > 0 ? 1 : 0);
}


bool
BStringColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const BStringField*>(field) != nullptr;
}


void
BStringColumn::GetFieldText(BField* field, std::string* into) const
{
	if (into != nullptr)
		*into = static_cast<BStringField*>(field)->String();
}


float
BStringColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	if (field == nullptr)
		return BTitledColumn::GetPreferredWidth(field, parent);
	return TextWidth(static_cast<BStringField*>(field)->String()) + 2 * kTextMargin;
}


// #pragma mark - BDateField


BDateField::BDateField(std::time_t time)
	:
	fTime(time)
{
}


void
BDateField::SetUnixTime(std::time_t time)
{
	fTime = time;
}


std::time_t
BDateField::UnixTime() const
{
	return fTime;
}


// #pragma mark - BDateColumn


BDateColumn::BDateColumn(std::string_view title, float width, float minWidth, float maxWidth,
	BHorizontalAlignment alignment)
	:
	BTitledColumn(title, width, minWidth, maxWidth, alignment)
{
}


void
BDateColumn::DrawField(BField* field, BView* owner, DrawContext& context, gfx::Rect rect,
	const gfx::Color& textColor)
{
	const std::time_t time = static_cast<BDateField*>(field)->UnixTime();
	const float available = rect.Width() - 2 * kTextMargin;
	for (int form = 0; form < kDateForms - 1; form++) {
		const std::string text = _Form(time, form);
		if (context.text.StringWidth(text, FontRole::Plain) <= available) {
			DrawString(text, owner, context, rect, textColor);
			return;
		}
	}
	// the shortest form, shortened by DrawString() if need be
	DrawString(_Form(time, kDateForms - 1), owner, context, rect, textColor);
}


int
BDateColumn::CompareFields(BField* field1, BField* field2)
{
	return CompareValues(static_cast<BDateField*>(field1)->UnixTime(),
		static_cast<BDateField*>(field2)->UnixTime());
}


bool
BDateColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const BDateField*>(field) != nullptr;
}


void
BDateColumn::GetFieldText(BField* field, std::string* into) const
{
	if (into != nullptr)
		*into = _Form(static_cast<BDateField*>(field)->UnixTime(), 0);
}


float
BDateColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	if (field == nullptr)
		return BTitledColumn::GetPreferredWidth(field, parent);
	return TextWidth(_Form(static_cast<BDateField*>(field)->UnixTime(), 0)) + 2 * kTextMargin;
}


std::string
BDateColumn::_Form(std::time_t time, int form) const
{
	switch (form) {
		case 0:
			return fDateFormat.Format(time, BDateFormatStyle::Long) + " "
				+ fTimeFormat.Format(time, BTimeFormatStyle::Short);
		case 1:
			return fDateFormat.Format(time, BDateFormatStyle::Short) + " "
				+ fTimeFormat.Format(time, BTimeFormatStyle::Short);
		default:
			return fDateFormat.Format(time, BDateFormatStyle::Short);
	}
}


// #pragma mark - BSizeField


BSizeField::BSizeField(std::uint64_t size)
	:
	fSize(size)
{
}


void
BSizeField::SetSize(std::uint64_t size)
{
	fSize = size;
}


std::uint64_t
BSizeField::Size() const
{
	return fSize;
}


// #pragma mark - BSizeColumn


BSizeColumn::BSizeColumn(std::string_view title, float width, float minWidth, float maxWidth,
	BHorizontalAlignment alignment)
	:
	BTitledColumn(title, width, minWidth, maxWidth, alignment)
{
}


void
BSizeColumn::DrawField(BField* field, BView* owner, DrawContext& context, gfx::Rect rect,
	const gfx::Color& textColor)
{
	DrawString(FormatSize(static_cast<BSizeField*>(field)->Size()), owner, context, rect,
		textColor);
}


int
BSizeColumn::CompareFields(BField* field1, BField* field2)
{
	return CompareValues(static_cast<BSizeField*>(field1)->Size(),
		static_cast<BSizeField*>(field2)->Size());
}


bool
BSizeColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const BSizeField*>(field) != nullptr;
}


void
BSizeColumn::GetFieldText(BField* field, std::string* into) const
{
	if (into != nullptr)
		*into = FormatSize(static_cast<BSizeField*>(field)->Size());
}


float
BSizeColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	if (field == nullptr)
		return BTitledColumn::GetPreferredWidth(field, parent);
	return TextWidth(FormatSize(static_cast<BSizeField*>(field)->Size())) + 2 * kTextMargin;
}


std::string
BSizeColumn::FormatSize(std::uint64_t size)
{
	if (size < 1024)
		return std::to_string(size) + " bytes";

	static constexpr const char* kUnits[] = {"KiB", "MiB", "GiB", "TiB"};
	auto value = static_cast<double>(size) / 1024;
	std::size_t unit = 0;
	while (value >= 1024 && unit + 1 < std::size(kUnits)) {
		value /= 1024;
		unit++;
	}
	return BNumberFormat().Format(value, 2) + " " + kUnits[unit];
}


// #pragma mark - BIntegerField


BIntegerField::BIntegerField(std::int32_t value)
	:
	fValue(value)
{
}


void
BIntegerField::SetValue(std::int32_t value)
{
	fValue = value;
}


std::int32_t
BIntegerField::Value() const
{
	return fValue;
}


// #pragma mark - BIntegerColumn


BIntegerColumn::BIntegerColumn(std::string_view title, float width, float minWidth,
	float maxWidth, BHorizontalAlignment alignment)
	:
	BTitledColumn(title, width, minWidth, maxWidth, alignment)
{
}


void
BIntegerColumn::DrawField(BField* field, BView* owner, DrawContext& context, gfx::Rect rect,
	const gfx::Color& textColor)
{
	DrawString(fNumberFormat.Format(std::int64_t{static_cast<BIntegerField*>(field)->Value()}),
		owner, context, rect, textColor);
}


int
BIntegerColumn::CompareFields(BField* field1, BField* field2)
{
	return CompareValues(static_cast<BIntegerField*>(field1)->Value(),
		static_cast<BIntegerField*>(field2)->Value());
}


bool
BIntegerColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const BIntegerField*>(field) != nullptr;
}


void
BIntegerColumn::GetFieldText(BField* field, std::string* into) const
{
	if (into != nullptr)
		*into = fNumberFormat.Format(std::int64_t{static_cast<BIntegerField*>(field)->Value()});
}


float
BIntegerColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	if (field == nullptr)
		return BTitledColumn::GetPreferredWidth(field, parent);
	return TextWidth(fNumberFormat.Format(
		std::int64_t{static_cast<BIntegerField*>(field)->Value()})) + 2 * kTextMargin;
}


// #pragma mark - GraphColumn


GraphColumn::GraphColumn(std::string_view title, float width, float minWidth, float maxWidth,
	BHorizontalAlignment alignment)
	:
	BIntegerColumn(title, width, minWidth, maxWidth, alignment)
{
}


void
GraphColumn::DrawField(BField* field, BView* owner, DrawContext& context, gfx::Rect rect,
	const gfx::Color& textColor)
{
	const int32 value = std::clamp(static_cast<BIntegerField*>(field)->Value(), 0, 100);

	// a frame inside the margins, two pixels off the row's edges
	const gfx::Rect frame = {rect.x0 + kTextMargin / 2, rect.y0 + 2, rect.x1 - kTextMargin / 2,
		rect.y1 - 3};
	if (frame.Width() < 2 || frame.Height() < 2)
		return;

	const float filled = std::floor((frame.Width() - 2) * static_cast<float>(value) / 100);
	context.canvas.FillRect({frame.x0 + 1, frame.y0 + 1, frame.x0 + 1 + filled, frame.y1 - 1},
		gfx::Paint{.color = context.palette.Get(BColorWhich::ControlHighlight)});
	const gfx::Paint line{.color = textColor};
	context.canvas.FillRect({frame.x0, frame.y0, frame.x1, frame.y0 + 1}, line);
	context.canvas.FillRect({frame.x0, frame.y1 - 1, frame.x1, frame.y1}, line);
	context.canvas.FillRect({frame.x0, frame.y0, frame.x0 + 1, frame.y1}, line);
	context.canvas.FillRect({frame.x1 - 1, frame.y0, frame.x1, frame.y1}, line);

	DrawString(std::to_string(value) + "%", owner, context, rect, textColor);
}


void
GraphColumn::GetFieldText(BField* field, std::string* into) const
{
	if (into != nullptr) {
		*into = std::to_string(std::clamp(static_cast<BIntegerField*>(field)->Value(), 0, 100))
			+ "%";
	}
}


// #pragma mark - BBitmapField


BBitmapField::BBitmapField(std::shared_ptr<const BBitmap> bitmap)
	:
	fBitmap(std::move(bitmap))
{
}


void
BBitmapField::SetBitmap(std::shared_ptr<const BBitmap> bitmap)
{
	fBitmap = std::move(bitmap);
}


const BBitmap*
BBitmapField::Bitmap() const
{
	return fBitmap.get();
}


// #pragma mark - BBitmapColumn


BBitmapColumn::BBitmapColumn(std::string_view title, float width, float minWidth,
	float maxWidth, BHorizontalAlignment alignment)
	:
	BTitledColumn(title, width, minWidth, maxWidth, alignment)
{
}


void
BBitmapColumn::DrawField(BField* field, BView* /*owner*/, DrawContext& context, gfx::Rect rect,
	const gfx::Color& /*textColor*/)
{
	const BBitmap* bitmap = static_cast<BBitmapField*>(field)->Bitmap();
	if (bitmap == nullptr)
		return;

	const gfx::Rect pixels = bitmap->Bounds();
	const float available = rect.Width() - 2 * kTextMargin;
	if (pixels.IsEmpty() || available <= 0 || rect.Height() <= 0)
		return;

	const float scale = std::min({1.0f, available / pixels.Width(),
		rect.Height() / pixels.Height()});
	const float width = std::round(pixels.Width() * scale);
	const float height = std::round(pixels.Height() * scale);

	float x = rect.x0 + kTextMargin;
	switch (Alignment()) {
		case BHorizontalAlignment::Right:
			x = rect.x1 - kTextMargin - width;
			break;
		case BHorizontalAlignment::Center:
			x = std::round(rect.x0 + (rect.Width() - width) / 2);
			break;
		case BHorizontalAlignment::Left:
		case BHorizontalAlignment::Unset:
		case BHorizontalAlignment::UseFullWidth:
			break;
	}
	const float y = std::round(rect.y0 + (rect.Height() - height) / 2);
	context.canvas.DrawImage(bitmap->Image(), {0, 0, pixels.Width(), pixels.Height()},
		{x, y, x + width, y + height});
}


int
BBitmapColumn::CompareFields(BField* /*field1*/, BField* /*field2*/)
{
	return 0;
}


bool
BBitmapColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const BBitmapField*>(field) != nullptr;
}


float
BBitmapColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	if (field == nullptr)
		return BTitledColumn::GetPreferredWidth(field, parent);
	const BBitmap* bitmap = static_cast<BBitmapField*>(field)->Bitmap();
	return (bitmap != nullptr ? bitmap->Bounds().Width() : 0.0f) + 2 * kTextMargin;
}


} // namespace ngui
