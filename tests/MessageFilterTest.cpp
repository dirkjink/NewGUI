/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/MessageFilter.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kMarker = 99;


TEST(MessageFilterTest, FiltersSkipAndRedirect)
{
	// What is tested: A looper with a common filter that skips command 1,
	// a filter on handler "a" that redirects command 2 to handler "b", a
	// filter on "b" that skips command 3, and a subclass filter on the
	// looper counting what passes.
	// Success criteria: 1 reaches nobody; 2 sent to "a" arrives at "b"; 3
	// sent to "b" is skipped, sent to "a" arrives at "a"; the common filter
	// sees every message first; Looper() of a filter is its owner's looper.
	// Why it matters: Filters are how windows implement shortcuts and how
	// controls intercept keys (G9, W2); their order and redirection follow
	// Haiku's _TopLevelFilter().
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<RecordingLooper>(record);
	RecordingLooper* looper = owned.get();
	RecordingHandler a("a", record);
	RecordingHandler b("b", record);
	looper->AddHandler(&a);
	looper->AddHandler(&b);
	std::vector<std::uint32_t> seenByCommon;
	BMessageFilter* common = looper->AddCommonFilter(std::make_unique<BMessageFilter>(
		std::nullopt, [&](BMessage* message, BHandler**, BMessageFilter*) {
			seenByCommon.push_back(message->what);
			return message->what == 1 ? BFilterResult::Skip : BFilterResult::Dispatch;
		}));
	a.AddFilter(std::make_unique<BMessageFilter>(2u,
		[&](BMessage*, BHandler** target, BMessageFilter*) {
			*target = &b;
			return BFilterResult::Dispatch;
		}));
	BMessageFilter* skipOnB = b.AddFilter(std::make_unique<BMessageFilter>(3u,
		[](BMessage*, BHandler**, BMessageFilter*) { return BFilterResult::Skip; }));
	const BMessenger toA(&a);
	const BMessenger toB(&b);
	EXPECT_EQ(common->Looper(), looper);
	EXPECT_EQ(skipOnB->Looper(), looper);
	EXPECT_EQ(b.FilterList(), (std::vector<BMessageFilter*>{skipOnB}));

	// When
	const BMessenger messenger = *BLooper::Run(std::move(owned));
	for (const auto& [target, command] : std::vector<std::pair<BMessenger, std::uint32_t>>{
			{toA, 1}, {toA, 2}, {toB, 3}, {toA, 3}, {messenger, kMarker}}) {
		ASSERT_TRUE(target.SendMessage(command).has_value());
	}
	ASSERT_TRUE(record->WaitFor(3));
	ASSERT_TRUE(messenger.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(messenger.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"b:2", "a:3",
		"looper:" + std::to_string(kMarker)}));
	EXPECT_EQ(seenByCommon, (std::vector<std::uint32_t>{1, 2, 3, 3, kMarker, kQuitRequested}));
}


TEST(MessageFilterTest, FiltersAreOwnedAndMatched)
{
	// What is tested: RemoveFilter() and RemoveCommonFilter(), a filter
	// subclass, and Applies() for command, delivery and source.
	// Success criteria: Removing gives the filter back as std::unique_ptr
	// and nullptr for one that is not there; the subclass's Filter() is
	// called; a filter for another command, for dropped messages or for
	// remote sources does not apply to a local message.
	// Why it matters: Handlers own their filters (CONVENTIONS.md 3.3);
	// filters for drag and drop must not see ordinary messages.
	//
	// Given
	class Counting : public BMessageFilter {
	public:
		BFilterResult Filter(BMessage*, BHandler**) override
		{
			calls++;
			return BFilterResult::Dispatch;
		}

		int calls = 0;
	};
	BHandler handler("h");
	BLooper looper("l");
	auto* counting = static_cast<Counting*>(handler.AddFilter(std::make_unique<Counting>()));
	BMessageFilter* common = looper.AddCommonFilter(std::make_unique<BMessageFilter>(5u));
	const BMessage five(5);

	// When / Then
	BHandler* target = &handler;
	EXPECT_EQ(counting->Filter(nullptr, &target), BFilterResult::Dispatch);
	EXPECT_EQ(counting->calls, 1);
	EXPECT_TRUE(common->Applies(five));
	EXPECT_FALSE(common->Applies(BMessage(6)));
	EXPECT_EQ(common->Command(), 5u);
	EXPECT_FALSE(common->FiltersAnyCommand());
	EXPECT_FALSE(BMessageFilter(BMessageDelivery::Dropped, BMessageSource::Any).Applies(five));
	EXPECT_FALSE(BMessageFilter(BMessageDelivery::Any, BMessageSource::Remote).Applies(five));
	EXPECT_TRUE(BMessageFilter(BMessageDelivery::Programmed, BMessageSource::Local)
		.Applies(five));

	std::unique_ptr<BMessageFilter> back = handler.RemoveFilter(counting);
	EXPECT_EQ(back.get(), counting);
	EXPECT_EQ(handler.RemoveFilter(counting), nullptr);
	EXPECT_TRUE(handler.FilterList().empty());
	EXPECT_EQ(looper.RemoveCommonFilter(common).get(), common);
	EXPECT_TRUE(looper.CommonFilterList().empty());
}


} // namespace
