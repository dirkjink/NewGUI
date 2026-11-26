/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ColumnTypes.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <string>

#include <ngui/ControlLook.h>
#include <ngui/gfx/Canvas.h>

#include "ColumnListViewPrivate.h"


namespace ngui {


namespace {


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


} // namespace ngui
