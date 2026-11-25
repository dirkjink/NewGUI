/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/PropertyInfo.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Handler.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace {


using namespace ngui;
using namespace std::chrono_literals;


// A handler with a property of its own, "Value", an int32 to get and set
// (as an application adds properties, specification 20.7).
class Counter : public BHandler {
public:
	Counter()
		:
		BHandler("counter")
	{
	}

	BHandler* ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& specifier,
		std::uint32_t form, std::string_view property) override
	{
		if (BPropertyInfo(kProperties).FindMatch(*message, index, form, property) >= 0)
			return this;
		return BHandler::ResolveSpecifier(message, index, specifier, form, property);
	}

	std::expected<void, Error> GetSupportedSuites(BMessage* data) override
	{
		(void)data->AddString("suites", "suite/x-vnd.test-counter");
		(void)data->AddMessage("messages", BPropertyInfo(kProperties).ToMessage());
		return BHandler::GetSupportedSuites(data);
	}

	void MessageReceived(BMessage* message) override
	{
		const auto specifier = message->GetCurrentSpecifier();
		if (specifier.has_value() && specifier->property == "Value") {
			BMessage reply(kReply);
			if (message->what == kSetProperty)
				fValue = message->GetInt32("data", fValue);
			else
				(void)reply.AddInt32("result", fValue);
			(void)reply.AddInt32("error", 0);
			(void)message->SendReply(reply);
			return;
		}
		BHandler::MessageReceived(message);
	}

private:
	static inline const std::vector<BPropertyInfoEntry> kProperties = {
		{"Value", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "The count.", 0,
			{BTypeCode::Int32}},
	};

	std::int32_t fValue = 0;
};


// A looper with the counter as its second handler (index 1).
class CounterLooper : public BLooper {
public:
	CounterLooper()
		:
		BLooper("counters")
	{
		AddHandler(&fCounter);
	}

	~CounterLooper() override
	{
		RemoveHandler(&fCounter);
	}

private:
	Counter fCounter;
};


// \a message sent to \a target, the reply.
BMessage
Ask(const BMessenger& target, const BMessage& message)
{
	BMessage reply;
	EXPECT_TRUE(target.SendMessage(message, &reply, 5s).has_value());
	return reply;
}


TEST(ScriptingTest, SpecifiersAreAddedAndPopped)
{
	// What is tested: AddSpecifier() of each kind, a specifier of the
	// application's, SetCurrentSpecifier(), and the message flattened and
	// read back; GetCurrentSpecifier() and PopSpecifier() before delivery.
	// Success criteria: Each specifier is a message in "specifiers" with
	// its form as what, "property", and "index", "range" or "name"; the
	// last one added is current. Undelivered, GetCurrentSpecifier() is
	// BadScriptSyntax and PopSpecifier() InvalidArgument, as in Haiku. An
	// empty property and a negative range are refused. Read back from
	// Flatten() the message has its specifiers with the last current.
	// Why it matters: A scripting message names its property with a chain
	// of specifiers ("Value of View volume of Window Settings"), resolved
	// from the last added to the first (specification 20.7).
	//
	// Given
	BMessage message(kGetProperty);
	BMessage custom(kSpecifiersEnd + 1);
	(void)custom.AddString("property", "Mine");

	// When
	const auto direct = message.AddSpecifier("Value");
	const auto indexed = message.AddSpecifier("View", 2);
	const auto range = message.AddSpecifier("Item", 1, 3);
	const auto named = message.AddSpecifier("Window", "Settings");
	const auto own = message.AddSpecifier(custom);
	const auto empty = message.AddSpecifier("");
	const auto negative = message.AddSpecifier("Item", 0, -1);
	const auto flattened = message.Flatten();
	const auto readBack = BMessage::FromFlattened(*flattened);

	// Then
	EXPECT_TRUE(direct && indexed && range && named && own);
	EXPECT_EQ(empty.error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(negative.error().code, ErrorCode::InvalidArgument);
	EXPECT_TRUE(message.HasSpecifiers());
	EXPECT_EQ(message.CurrentSpecifierIndex(), 4);
	const auto window = message.FindMessage("specifiers", 3);
	ASSERT_TRUE(window.has_value());
	EXPECT_EQ(window->what, kNameSpecifier);
	EXPECT_EQ(window->GetString("property", ""), "Window");
	EXPECT_EQ(window->GetString("name", ""), "Settings");
	const auto items = message.FindMessage("specifiers", 2);
	EXPECT_EQ(items->what, kRangeSpecifier);
	EXPECT_EQ(items->GetInt32("range", 0), 3);
	EXPECT_EQ(message.GetCurrentSpecifier().error().code, ErrorCode::BadScriptSyntax);
	EXPECT_EQ(message.PopSpecifier().error().code, ErrorCode::InvalidArgument);
	EXPECT_TRUE(message.SetCurrentSpecifier(1).has_value());
	EXPECT_EQ(message.CurrentSpecifierIndex(), 1);
	EXPECT_EQ(message.SetCurrentSpecifier(5).error().code, ErrorCode::OutOfRange);
	ASSERT_TRUE(readBack.has_value());
	EXPECT_EQ(readBack->CurrentSpecifierIndex(), 4);
	EXPECT_EQ(*readBack, message);
	EXPECT_FALSE(BMessage(kGetProperty).HasSpecifiers());
}


TEST(ScriptingTest, PropertyTablesMatchLikeHaikus)
{
	// What is tested: BPropertyInfo::FindMatch() over a table with a
	// property that leads on (no commands), one with commands and forms,
	// and the table as a message.
	// Success criteria: An entry without commands matches any command but
	// only for a specifier that is not the last one (index 1), an entry
	// with commands only the last one (index 0) and only its commands; the
	// forms must fit; the first match wins and gives its extra data. The
	// message has one "property" per entry with its name, commands, forms
	// and usage.
	// Why it matters: These are Haiku's FindCommand() and FindSpecifier();
	// ported handlers rely on exactly them to tell "View 2 (of …)" from
	// "Value (of View 2)".
	//
	// Given
	const std::vector<BPropertyInfoEntry> table = {
		{"View", {}, {kIndexSpecifier, kNameSpecifier}, "A child view.", 10, {}},
		{"Value", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "The value.", 20,
			{BTypeCode::Int32}},
	};
	const BPropertyInfo properties(table);
	const BMessage get(kGetProperty);
	const BMessage execute(kExecuteProperty);
	std::uint32_t data = 0;

	// When / Then
	EXPECT_EQ(properties.FindMatch(get, 1, kIndexSpecifier, "View", &data), 0);
	EXPECT_EQ(data, 10u);
	EXPECT_EQ(properties.FindMatch(get, 0, kIndexSpecifier, "View"), 0);
	EXPECT_EQ(properties.FindMatch(get, 1, kDirectSpecifier, "View"), -1);
	EXPECT_EQ(properties.FindMatch(get, 0, kDirectSpecifier, "Value", &data), 1);
	EXPECT_EQ(data, 20u);
	EXPECT_EQ(properties.FindMatch(get, 1, kDirectSpecifier, "Value"), -1);
	EXPECT_EQ(properties.FindMatch(execute, 0, kDirectSpecifier, "Value"), -1);
	EXPECT_EQ(properties.FindMatch(get, 0, kDirectSpecifier, "Missing"), -1);
	const BMessage described = properties.ToMessage();
	const auto second = described.FindMessage("property", 1);
	ASSERT_TRUE(second.has_value());
	EXPECT_EQ(second->GetString("name", ""), "Value");
	EXPECT_EQ(second->GetUInt32("commands", 0), kGetProperty);
	EXPECT_EQ(second->GetString("usage", ""), "The value.");
}


TEST(ScriptingTest, LoopersAndHandlersAnswerScripting)
{
	// What is tested: Scripting messages to a running looper with a
	// handler "counter" of its own property "Value": get and set "Value of
	// Handler 1"; get "InternalName" of the same handler; count "Handler";
	// "Suites" and kGetSupportedSuites; a property nobody has; a handler
	// index out of range; kGetProperty without specifiers.
	// Success criteria: The looper resolves "Handler 1" to the counter,
	// which sets and returns its value (error 0); its name is "counter";
	// the looper has 2 handlers and reports the suites of looper and
	// handler, the counter its own too. A missing property is
	// kMessageNotUnderstood with BadScriptSyntax, a bad index OutOfRange
	// (ScriptingResult()); kGetProperty without specifiers is not
	// understood either (Haiku answers it with the handler's messenger,
	// which NewGUI leaves out).
	// Why it matters: This is Haiku's scripting protocol (specification
	// 20.7): what the remote control and the hey tool speak, and how
	// applications add properties of their own.
	//
	// Given
	const BMessenger looper = *BLooper::Run(std::make_unique<CounterLooper>());
	BMessage set(kSetProperty);
	(void)set.AddInt32("data", 42);
	(void)set.AddSpecifier("Value");
	(void)set.AddSpecifier("Handler", 1);
	BMessage get(kGetProperty);
	(void)get.AddSpecifier("Value");
	(void)get.AddSpecifier("Handler", 1);
	BMessage name(kGetProperty);
	(void)name.AddSpecifier("InternalName");
	(void)name.AddSpecifier("Handler", 1);
	BMessage count(kCountProperties);
	(void)count.AddSpecifier("Handler");
	BMessage suites(kGetProperty);
	(void)suites.AddSpecifier("Suites");
	BMessage counterSuites(kGetSupportedSuites);
	(void)counterSuites.AddSpecifier("Handler", 1);
	BMessage missing(kGetProperty);
	(void)missing.AddSpecifier("Colour");
	BMessage outOfRange(kGetProperty);
	(void)outOfRange.AddSpecifier("Value");
	(void)outOfRange.AddSpecifier("Handler", 7);

	// When
	const BMessage setReply = Ask(looper, set);
	const BMessage getReply = Ask(looper, get);
	const BMessage nameReply = Ask(looper, name);
	const BMessage countReply = Ask(looper, count);
	const BMessage suitesReply = Ask(looper, suites);
	const BMessage counterReply = Ask(looper, counterSuites);
	const BMessage missingReply = Ask(looper, missing);
	const BMessage rangeReply = Ask(looper, outOfRange);
	const BMessage plainReply = Ask(looper, BMessage(kGetProperty));
	(void)looper.SendMessage(kQuitRequested);
	(void)looper.WaitForLooperQuit(5s);

	// Then
	EXPECT_TRUE(ScriptingResult(setReply).has_value());
	EXPECT_EQ(getReply.GetInt32("result", 0), 42);
	EXPECT_EQ(nameReply.GetString("result", ""), "counter");
	EXPECT_EQ(countReply.GetInt32("result", 0), 2);
	EXPECT_EQ(suitesReply.GetString("suites", ""), "suite/vnd.Be-looper");
	EXPECT_EQ(suitesReply.FindString("suites", 1).value_or(""), "suite/vnd.Be-handler");
	EXPECT_EQ(counterReply.GetString("suites", ""), "suite/x-vnd.test-counter");
	EXPECT_EQ(missingReply.what, kMessageNotUnderstood);
	EXPECT_EQ(ScriptingResult(missingReply).error().code, ErrorCode::BadScriptSyntax);
	EXPECT_EQ(ScriptingResult(rangeReply).error().code, ErrorCode::OutOfRange);
	EXPECT_FALSE(ScriptingResult(plainReply).has_value()) << plainReply.ToString();
}


} // namespace
