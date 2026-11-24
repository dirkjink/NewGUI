/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Clipboard.h>

#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>


namespace {


using namespace ngui;


std::span<const std::byte>
Bytes(std::string_view text)
{
	return std::as_bytes(std::span(text.data(), text.size()));
}


std::string
Text(const BMessage& message)
{
	auto data = message.FindData("text/plain", BTypeCode::Mime);
	return data.has_value()
		? std::string(reinterpret_cast<const char*>(data->data()), data->size()) : "";
}


TEST(ClipboardTest, ClipboardsOfANameShareTheirContent)
{
	// What is tested: Two clipboards named "test": the first locked,
	// cleared, given "text/plain" "one" and committed; the second locked and
	// read; then the second commits "two" and the first, still holding the
	// old content, commits with failIfChanged; Data() and Clear() while
	// unlocked; a clipboard of another name.
	// Success criteria: The second sees "one" after the first's commit;
	// the counts grow with each commit (LocalCount() is that of the content
	// fetched); Commit(true) fails after the other's commit; Data() is
	// nullptr and Clear() fails while unlocked; another name stays empty.
	// Why it matters: BTextView copies and pastes through clipboards; Haiku
	// code locks, fills and commits them this way.
	//
	// Given
	BClipboard first("test");
	BClipboard second("test");
	BClipboard other("other");

	// When / Then
	ASSERT_TRUE(first.Lock());
	ASSERT_TRUE(first.Clear().has_value());
	ASSERT_TRUE(first.Data()->AddData("text/plain", BTypeCode::Mime, Bytes("one"))
		.has_value());
	ASSERT_TRUE(first.Commit().has_value());
	const uint32 countAfterFirst = first.SystemCount();
	first.Unlock();

	ASSERT_TRUE(second.Lock());
	EXPECT_EQ(Text(*second.Data()), "one");
	EXPECT_EQ(second.LocalCount(), countAfterFirst);
	ASSERT_TRUE(first.Lock());
	ASSERT_TRUE(second.Clear().has_value());
	ASSERT_TRUE(second.Data()->AddData("text/plain", BTypeCode::Mime, Bytes("two"))
		.has_value());
	ASSERT_TRUE(second.Commit().has_value());
	second.Unlock();
	EXPECT_EQ(second.SystemCount(), countAfterFirst + 1);
	EXPECT_FALSE(first.Commit(true).has_value());
	ASSERT_TRUE(first.Revert().has_value());
	EXPECT_EQ(Text(*first.Data()), "two");
	first.Unlock();

	EXPECT_EQ(first.Data(), nullptr);
	EXPECT_FALSE(first.Clear().has_value());
	ASSERT_TRUE(other.Lock());
	EXPECT_FALSE(other.Data()->HasData("text/plain", BTypeCode::Mime));
	other.Unlock();
	EXPECT_EQ(BClipboard::System().Name(), "system");
}


} // namespace
