/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ColumnTypes.h>

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Bitmap.h>
#include <ngui/ColumnListView.h>
#include <ngui/DateFormat.h>
#include <ngui/NumberFormat.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;


TEST(ColumnTypesTest, SizesAreWrittenInBinaryUnits)
{
	// What is tested: BSizeColumn::FormatSize() of 0, 1023, 1536, 5 MiB,
	// 3 GiB + 512 MiB and 3 PiB.
	// Success criteria: Below 1024 the bytes as a whole number with
	// "bytes"; above it the value in the largest unit up to TiB with two
	// decimals as the user's locale writes them (BNumberFormat), and the
	// unit.
	// Why it matters: Sizes in file lists must be short and readable.
	//
	// Given
	const BNumberFormat number;
	const std::uint64_t kMiB = 1024 * 1024;

	// When / Then
	EXPECT_EQ(BSizeColumn::FormatSize(0), "0 bytes");
	EXPECT_EQ(BSizeColumn::FormatSize(1023), "1023 bytes");
	EXPECT_EQ(BSizeColumn::FormatSize(1536), number.Format(1.5, 2) + " KiB");
	EXPECT_EQ(BSizeColumn::FormatSize(5 * kMiB), number.Format(5.0, 2) + " MiB");
	EXPECT_EQ(BSizeColumn::FormatSize(3584 * kMiB), number.Format(3.5, 2) + " GiB");
	EXPECT_EQ(BSizeColumn::FormatSize(3 * kMiB * kMiB * 1024),
		number.Format(3072.0, 2) + " TiB");
}


TEST(ColumnTypesTest, FieldsSortAndMeasureByTheirValues)
{
	// What is tested: With the fixed text system: a list with a date, a
	// size, an integer, a graph and a bitmap column and three rows; sorted
	// by each of the first three; the preferred widths of the titles and
	// of some fields; which fields each column accepts.
	// Success criteria: Dates sort by time, sizes and integers by value,
	// in both directions. A title wants its width and the margins (16), a
	// date its longest form (the long date and the time), a size and an
	// integer their text, a bitmap its width, each with the margins. A
	// column accepts only its own kind of field; the graph column takes
	// integer fields.
	// Why it matters: Each column type must order and fit its data the way
	// it shows it.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 400, 300}, "columns");
	auto& list = static_cast<BColumnListView&>(*window.AddChild(
		std::make_unique<BColumnListView>(gfx::Rect{0, 0, 300, 200}, "list", kFollowLeftTop,
			BViewFlags{})));
	auto date = std::make_unique<BDateColumn>("Date", 100.0f, 20.0f, 400.0f);
	auto size = std::make_unique<BSizeColumn>("Size", 100.0f, 20.0f, 400.0f);
	auto integer = std::make_unique<BIntegerColumn>("Count", 100.0f, 20.0f, 400.0f);
	auto graph = std::make_unique<GraphColumn>("Done", 100.0f, 20.0f, 400.0f);
	auto bitmap = std::make_unique<BBitmapColumn>("Icon", 100.0f, 20.0f, 400.0f);
	BColumn* dateColumn = date.get();
	BColumn* sizeColumn = size.get();
	BColumn* integerColumn = integer.get();
	BColumn* graphColumn = graph.get();
	BColumn* bitmapColumn = bitmap.get();
	list.AddColumn(std::move(date), 0);
	list.AddColumn(std::move(size), 1);
	list.AddColumn(std::move(integer), 2);
	list.AddColumn(std::move(graph), 2);
	list.AddColumn(std::move(bitmap), 3);
	auto icon = std::make_shared<BBitmap>(gfx::Rect{0, 0, 12, 10}, BColorSpace::RGBA32);
	const std::time_t times[] = {2'000'000'000, 1'000'000'000, 1'500'000'000};
	const std::uint64_t sizes[] = {10, 30'000, 2'000};
	const std::int32_t counts[] = {-5, 7, 3};
	for (int i = 0; i < 3; i++) {
		auto row = std::make_unique<BRow>();
		row->SetField(std::make_unique<BDateField>(times[i]), 0);
		row->SetField(std::make_unique<BSizeField>(sizes[i]), 1);
		row->SetField(std::make_unique<BIntegerField>(counts[i]), 2);
		row->SetField(std::make_unique<BBitmapField>(icon), 3);
		ASSERT_TRUE(list.AddRow(std::move(row)));
	}
	auto order = [&list](int32 field) {
		std::vector<std::int64_t> values;
		for (int32 i = 0; i < list.CountRows(); i++) {
			const BField* value = list.RowAt(i)->GetField(field);
			if (const auto* time = dynamic_cast<const BDateField*>(value))
				values.push_back(time->UnixTime());
			else if (const auto* bytes = dynamic_cast<const BSizeField*>(value))
				values.push_back(static_cast<std::int64_t>(bytes->Size()));
			else if (const auto* count = dynamic_cast<const BIntegerField*>(value))
				values.push_back(count->Value());
		}
		return values;
	};
	BDateField dateField(times[0]);
	BSizeField sizeField(30'000);
	BIntegerField integerField(-1234);
	BBitmapField bitmapField(icon);
	BStringField stringField("x");

	// When
	list.SetSortColumn(dateColumn, false, true);
	const std::vector<std::int64_t> byDate = order(0);
	list.SetSortColumn(sizeColumn, false, false);
	const std::vector<std::int64_t> bySize = order(1);
	list.SetSortColumn(integerColumn, false, true);
	const std::vector<std::int64_t> byCount = order(2);

	// Then
	EXPECT_EQ(byDate, (std::vector<std::int64_t>{1'000'000'000, 1'500'000'000,
		2'000'000'000}));
	EXPECT_EQ(bySize, (std::vector<std::int64_t>{30'000, 2'000, 10}));
	EXPECT_EQ(byCount, (std::vector<std::int64_t>{-5, 3, 7}));
	EXPECT_EQ(dateColumn->GetPreferredWidth(nullptr, &list), 4 * 7 + 16);
	const std::string longest = BDateFormat().Format(times[0], BDateFormatStyle::Long) + " "
		+ BTimeFormat().Format(times[0], BTimeFormatStyle::Short);
	EXPECT_EQ(dateColumn->GetPreferredWidth(&dateField, &list),
		7 * static_cast<float>(longest.size()) + 16);
	EXPECT_EQ(sizeColumn->GetPreferredWidth(&sizeField, &list),
		7 * static_cast<float>(BSizeColumn::FormatSize(30'000).size()) + 16);
	EXPECT_EQ(integerColumn->GetPreferredWidth(&integerField, &list),
		7 * static_cast<float>(BNumberFormat().Format(std::int64_t{-1234}).size()) + 16);
	EXPECT_EQ(bitmapColumn->GetPreferredWidth(&bitmapField, &list), 12 + 16);
	EXPECT_TRUE(dateColumn->AcceptsField(&dateField));
	EXPECT_FALSE(dateColumn->AcceptsField(&sizeField));
	EXPECT_TRUE(sizeColumn->AcceptsField(&sizeField));
	EXPECT_FALSE(sizeColumn->AcceptsField(&integerField));
	EXPECT_TRUE(integerColumn->AcceptsField(&integerField));
	EXPECT_TRUE(graphColumn->AcceptsField(&integerField));
	EXPECT_FALSE(integerColumn->AcceptsField(&stringField));
	EXPECT_TRUE(bitmapColumn->AcceptsField(&bitmapField));
	EXPECT_FALSE(bitmapColumn->AcceptsField(&stringField));
	EXPECT_EQ(bitmapColumn->CompareFields(&bitmapField, &bitmapField), 0);
	TextSystem::SetDefault(nullptr);
}


} // namespace
