/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Message.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>


namespace {


using namespace ngui;


constexpr std::uint32_t kWhat = FourCharCode("TEST");


// A message with one field of every flattenable type, the integers at
// their limits, and a nested message.
BMessage
EveryType()
{
	BMessage nested(FourCharCode("NEST"));
	(void)nested.AddString("inner", "ä");

	BMessage message(kWhat);
	(void)message.AddBool("bool", true);
	(void)message.AddInt8("int8", std::numeric_limits<std::int8_t>::min());
	(void)message.AddUInt8("uint8", 255);
	(void)message.AddInt16("int16", -32768);
	(void)message.AddUInt16("uint16", 65535);
	(void)message.AddInt32("int32", std::numeric_limits<std::int32_t>::min());
	(void)message.AddUInt32("uint32", 0xdeadbeef);
	(void)message.AddInt64("int64", std::numeric_limits<std::int64_t>::min());
	(void)message.AddUInt64("uint64", std::numeric_limits<std::uint64_t>::max());
	(void)message.AddFloat("float", -0.0f);
	(void)message.AddDouble("double", std::numeric_limits<double>::quiet_NaN());
	(void)message.AddString("string", "Grüße\n");
	(void)message.AddPoint("point", {1.5f, -2});
	(void)message.AddRect("rect", {0, 0, 10, 20});
	(void)message.AddSize("size", {3, 4});
	(void)message.AddColor("color", {0.25f, 0.5f, 0.75f, 1});
	(void)message.AddMessage("message", nested);
	const std::array<std::byte, 3> raw = {std::byte{1}, std::byte{0}, std::byte{255}};
	(void)message.AddData("raw", static_cast<BTypeCode>(FourCharCode("MyTy")), raw);
	return message;
}


TEST(MessageTest, TypedFieldsKeepTheirItems)
{
	// What is tested: The Add, Find, Get, Replace and Has methods of several
	// types, with several items in one field.
	// Success criteria: Items come back in the order they were added and
	// with their exact values; Get gives the item or the default; Replace
	// changes one item; Has tells whether an item of that type exists.
	// Why it matters: BMessage is how every part of the toolkit talks
	// (module G2); ported code uses exactly these method families.
	//
	// Given
	BMessage message(kWhat);

	// When
	ASSERT_TRUE(message.AddInt32("value", 1).has_value());
	ASSERT_TRUE(message.AddInt32("value", 2).has_value());
	ASSERT_TRUE(message.AddString("label", "OK").has_value());
	ASSERT_TRUE(message.AddRect("frame", {1, 2, 30, 40}).has_value());
	ASSERT_TRUE(message.ReplaceInt32("value", 1, 3).has_value());
	ASSERT_TRUE(message.AddPointer("target", &message).has_value());

	// Then
	EXPECT_EQ(message.what, kWhat);
	EXPECT_EQ(*message.FindInt32("value"), 1);
	EXPECT_EQ(*message.FindInt32("value", 1), 3);
	EXPECT_EQ(*message.FindString("label"), "OK");
	EXPECT_EQ(*message.FindRect("frame"), (gfx::Rect{1, 2, 30, 40}));
	EXPECT_EQ(*message.FindPointer("target"), &message);
	EXPECT_EQ(message.GetInt32("value", 7), 1);
	EXPECT_EQ(message.GetInt32("missing", 7), 7);
	EXPECT_EQ(message.GetString("value", "fallback"), "fallback");
	EXPECT_TRUE(message.HasInt32("value", 1));
	EXPECT_FALSE(message.HasInt32("value", 2));
	EXPECT_FALSE(message.HasString("value"));
	EXPECT_TRUE(message.HasData("value"));
	EXPECT_TRUE(message.HasData("value", BTypeCode::Int32, 1));
	EXPECT_FALSE(message.HasData("value", BTypeCode::String));
}


TEST(MessageTest, ErrorsMatchHaikusStatusCodes)
{
	// What is tested: Find, Add and Replace with a missing field, a missing
	// index, another type and an empty name.
	// Success criteria: NotFound (B_NAME_NOT_FOUND), OutOfRange
	// (B_BAD_INDEX), BadType (B_BAD_TYPE) and InvalidArgument; a failed Add
	// changes nothing.
	// Why it matters: Haiku code distinguishes these cases, e.g. a missing
	// optional field from a wrong one; the codes map them one to one.
	//
	// Given
	BMessage message;
	(void)message.AddInt32("value", 1);

	// When / Then
	EXPECT_EQ(message.FindInt32("missing").error().code, ErrorCode::NotFound);
	EXPECT_EQ(message.FindInt32("value", 1).error().code, ErrorCode::OutOfRange);
	EXPECT_EQ(message.FindInt32("value", -1).error().code, ErrorCode::OutOfRange);
	EXPECT_EQ(message.FindString("value").error().code, ErrorCode::BadType);
	EXPECT_EQ(message.AddString("value", "x").error().code, ErrorCode::BadType);
	EXPECT_EQ(message.AddInt32("", 1).error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(message.ReplaceInt32("missing", 1).error().code, ErrorCode::NotFound);
	EXPECT_EQ(message.ReplaceInt32("value", 1, 5).error().code, ErrorCode::OutOfRange);
	EXPECT_EQ(message.ReplaceFloat("value", 1.0f).error().code, ErrorCode::BadType);
	EXPECT_EQ(message.GetInfo("value")->count, 1);
}


TEST(MessageTest, FieldsCanBeListedRenamedAndRemoved)
{
	// What is tested: CountNames(), Fields(), GetInfo(), Rename(),
	// RemoveData(), RemoveName(), MakeEmpty() and IsEmpty().
	// Success criteria: Fields are listed in the order they were added,
	// filtered by type; a rename keeps the position and refuses an empty or
	// taken name; removing the last item removes the field; MakeEmpty()
	// keeps what.
	// Why it matters: Generic code (scripting, the test driver, diagnostics)
	// walks the fields of a message it does not know.
	//
	// Given
	BMessage message(kWhat);
	(void)message.AddInt32("a", 1);
	(void)message.AddString("b", "x");
	(void)message.AddInt32("c", 2);
	(void)message.AddInt32("c", 3);

	// When / Then
	EXPECT_EQ(message.CountNames(), 3);
	EXPECT_EQ(message.CountNames(BTypeCode::Int32), 2);
	EXPECT_EQ(message.Fields(BTypeCode::Int32),
		(std::vector<BFieldInfo>{{"a", BTypeCode::Int32, 1}, {"c", BTypeCode::Int32, 2}}));
	EXPECT_EQ(*message.GetInfo("b"), (BFieldInfo{"b", BTypeCode::String, 1}));
	EXPECT_EQ(message.GetInfo("z").error().code, ErrorCode::NotFound);

	EXPECT_TRUE(message.Rename("a", "first").has_value());
	EXPECT_EQ(message.Rename("first", "b").error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(message.Rename("first", "").error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(message.Fields().front().name, "first");

	EXPECT_TRUE(message.RemoveData("c", 0).has_value());
	EXPECT_EQ(*message.FindInt32("c"), 3);
	EXPECT_TRUE(message.RemoveData("c").has_value());
	EXPECT_FALSE(message.HasData("c"));
	EXPECT_EQ(message.RemoveData("c").error().code, ErrorCode::NotFound);
	EXPECT_TRUE(message.RemoveName("b").has_value());
	EXPECT_EQ(message.CountNames(), 1);

	message.MakeEmpty();
	EXPECT_TRUE(message.IsEmpty());
	EXPECT_EQ(message.what, kWhat);
}


TEST(MessageTest, NestedMessagesAndRawDataAreValues)
{
	// What is tested: AddMessage() followed by a change of the original;
	// copying a message; AddData(), FindData() and ReplaceData() with an
	// application's own type, and with a type that has typed methods.
	// Success criteria: A nested message and a copy are independent values;
	// raw bytes come back exactly; AddData() refuses Int32 and Any with
	// BadType, FindData() on an Int32 field gives BadType.
	// Why it matters: Messages are values that travel between threads; a
	// shared nested message changed by its sender would be a data race.
	//
	// Given
	BMessage inner(FourCharCode("INNR"));
	(void)inner.AddInt32("x", 1);
	BMessage outer;
	(void)outer.AddMessage("inner", inner);
	const auto ownType = static_cast<BTypeCode>(FourCharCode("MyTy"));
	const std::array<std::byte, 2> bytes = {std::byte{7}, std::byte{8}};
	const std::array<std::byte, 1> other = {std::byte{9}};

	// When
	(void)inner.ReplaceInt32("x", 2);
	BMessage copy = outer;
	(void)copy.AddInt32("extra", 1);
	(void)outer.AddData("raw", ownType, bytes);
	(void)outer.AddInt32("number", 1);

	// Then
	EXPECT_EQ(*outer.FindMessage("inner")->FindInt32("x"), 1);
	EXPECT_EQ(outer.FindMessage("inner")->what, FourCharCode("INNR"));
	EXPECT_FALSE(outer.HasData("extra"));
	const auto found = outer.FindData("raw", ownType);
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(std::vector<std::byte>(found->begin(), found->end()),
		std::vector<std::byte>(bytes.begin(), bytes.end()));
	EXPECT_TRUE(outer.ReplaceData("raw", ownType, 0, other).has_value());
	EXPECT_EQ(outer.FindData("raw", BTypeCode::Any)->size(), 1u);
	EXPECT_EQ(outer.AddData("n", BTypeCode::Int32, bytes).error().code, ErrorCode::BadType);
	EXPECT_EQ(outer.AddData("n", BTypeCode::Any, bytes).error().code, ErrorCode::BadType);
	EXPECT_EQ(outer.FindData("number", BTypeCode::Any).error().code, ErrorCode::BadType);
}


TEST(MessageTest, FlatteningIsCanonical)
{
	// What is tested: Flatten() and FromFlattened() of a message with every
	// flattenable type, -0.0, NaN, the integer limits, UTF-8 strings and a
	// nested message; the exact bytes of a small message; pointers.
	// Success criteria: The round trip gives an equal message and flattening
	// it again gives the same bytes; the small message has exactly the
	// documented layout; a pointer field, also nested, gives NotSupported.
	// Why it matters: Flattened messages go to the clipboard, through drag
	// and drop and to remote control (specification 20.7); a canonical form
	// makes them comparable and testable.
	//
	// Given
	const BMessage message = EveryType();
	BMessage small(FourCharCode("abcd"));
	(void)small.AddBool("b", true);
	BMessage withPointer;
	(void)withPointer.AddPointer("p", &small);
	BMessage nestedPointer;
	(void)nestedPointer.AddMessage("m", withPointer);

	// When
	const auto bytes = message.Flatten();
	ASSERT_TRUE(bytes.has_value());
	const auto restored = BMessage::FromFlattened(*bytes);

	// Then
	ASSERT_TRUE(restored.has_value()) << restored.error().message;
	EXPECT_EQ(*restored, message);
	EXPECT_EQ(*restored->Flatten(), *bytes);
	EXPECT_TRUE(std::isnan(*restored->FindDouble("double")));
	EXPECT_TRUE(std::signbit(*restored->FindFloat("float")));
	EXPECT_EQ(*restored->FindString("string"), "Grüße\n");

	const std::vector<std::uint8_t> expected = {
		'N', 'G', 'M', 'S', 1,		// magic, version
		'd', 'c', 'b', 'a',			// what, little-endian
		1, 0, 0, 0,					// one field
		1, 0, 0, 0, 'b',			// name
		'L', 'O', 'O', 'B',			// type 'BOOL', little-endian
		1, 0, 0, 0,					// one item
		1};							// true
	const auto smallBytes = *small.Flatten();
	std::vector<std::uint8_t> actual;
	for (const std::byte byte : smallBytes)
		actual.push_back(static_cast<std::uint8_t>(byte));
	EXPECT_EQ(actual, expected);

	EXPECT_EQ(withPointer.Flatten().error().code, ErrorCode::NotSupported);
	EXPECT_EQ(nestedPointer.Flatten().error().code, ErrorCode::NotSupported);
}


TEST(MessageTest, DamagedFlattenedDataIsRefused)
{
	// What is tested: Unflatten() of every proper prefix of a flattened
	// message, of the message with a trailing byte, a wrong magic, a wrong
	// version, a bool of 2, a repeated field name, a field count that the
	// data cannot hold, and 65 nested levels.
	// Success criteria: BadData for all of them, OutOfRange for the
	// nesting; the message being unflattened into is unchanged.
	// Why it matters: Flattened messages come from other processes; damaged
	// or forged data must neither crash nor allocate without bound.
	//
	// Given
	const std::vector<std::byte> good = *EveryType().Flatten();
	BMessage target(FourCharCode("KEEP"));
	(void)target.AddInt32("kept", 1);
	const BMessage before = target;
	const auto refused = [&](std::vector<std::byte> data) {
		const auto result = target.Unflatten(data);
		return result.has_value() ? ErrorCode{} : result.error().code;
	};
	const auto changed = [&](std::size_t offset, std::uint8_t value) {
		std::vector<std::byte> data = good;
		data[offset] = static_cast<std::byte>(value);
		return data;
	};

	// When / Then
	for (std::size_t length = 0; length < good.size(); length++) {
		const std::vector<std::byte> prefix(good.begin(),
			good.begin() + static_cast<std::ptrdiff_t>(length));
		ASSERT_EQ(refused(prefix), ErrorCode::BadData) << "length " << length;
	}
	std::vector<std::byte> trailing = good;
	trailing.push_back(std::byte{0});
	EXPECT_EQ(refused(trailing), ErrorCode::BadData);
	EXPECT_EQ(refused(changed(0, 'X')), ErrorCode::BadData);
	EXPECT_EQ(refused(changed(4, 2)), ErrorCode::BadData);
	// The first field is "bool": name length at 13, name at 17, type at 21,
	// count at 25, the item at 29.
	EXPECT_EQ(refused(changed(29, 2)), ErrorCode::BadData);
	EXPECT_EQ(refused(changed(9, 0xff)), ErrorCode::BadData);

	BMessage twice;
	(void)twice.AddInt32("a", 1);
	(void)twice.AddInt32("b", 2);
	std::vector<std::byte> repeated = *twice.Flatten();
	// The second field starts at 13 + (4 + 1 + 4 + 4 + 4) = 30, its name
	// after the length at 34.
	ASSERT_EQ(repeated[34], std::byte{'b'});
	repeated[34] = std::byte{'a'};
	EXPECT_EQ(refused(repeated), ErrorCode::BadData);
	EXPECT_NE(BMessage::FromFlattened(repeated).error().message.find("repeated"),
		std::string::npos);

	BMessage deep;
	for (int level = 0; level < 65; level++) {
		BMessage outer;
		(void)outer.AddMessage("m", deep);
		deep = outer;
	}
	EXPECT_EQ(refused(*deep.Flatten()), ErrorCode::OutOfRange);
	EXPECT_EQ(target, before);
}


TEST(MessageTest, ToStringListsTheFields)
{
	// What is tested: ToString() of a message with an int32, a string and a
	// nested message.
	// Success criteria: what and the types as four-character codes, every
	// item, the nested message indented.
	// Why it matters: The test driver prints pending messages when a test
	// does not become idle (specification 20.4, T3).
	//
	// Given
	BMessage nested(FourCharCode("NEST"));
	(void)nested.AddBool("on", true);
	BMessage message(FourCharCode("ABCD"));
	(void)message.AddInt32("n", -1);
	(void)message.AddInt32("n", 2);
	(void)message.AddString("s", "x");
	(void)message.AddMessage("m", nested);

	// When / Then
	EXPECT_EQ(message.ToString(),
		"BMessage('ABCD') {\n"
		"  n 'LONG': -1 2\n"
		"  s 'CSTR': \"x\"\n"
		"  m 'MSGG': BMessage('NEST') {\n"
		"    on 'BOOL': true\n"
		"  }\n"
		"}\n");
}


} // namespace
