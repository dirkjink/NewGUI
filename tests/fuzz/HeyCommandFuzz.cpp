/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <string>
#include <vector>

#include <ngui/Message.h>

#include "remote/Hey.h"


NGUI_FUZZ_TARGET(HeyCommand)
{
	// What is tested: ParseHeyCommand() of arbitrary words (the bytes split
	// at spaces), and DescribeReply() and ResultText() of what it makes.
	// Success criteria: Nothing crashes (checked by the sanitizers); a
	// parsed command is a scripting message that flattens; a failure is
	// BadScriptSyntax with a message.
	// Why it matters: ngui-hey takes whatever a user or a script types
	// (specification 20.7); no command line may crash it.
	//
	// Given
	std::vector<std::string> words;
	std::string word;
	for (const std::uint8_t byte : data) {
		if (byte == ' ') {
			if (!word.empty())
				words.push_back(word);
			word.clear();
		} else
			word += static_cast<char>(byte);
	}
	if (!word.empty())
		words.push_back(word);

	// When
	const auto message = ngui::remote::detail::ParseHeyCommand(words);

	// Then
	if (!message.has_value()) {
		NGUI_FUZZ_CHECK(message.error().code == ngui::ErrorCode::BadScriptSyntax);
		NGUI_FUZZ_CHECK(!message.error().message.empty());
		return;
	}
	NGUI_FUZZ_CHECK(message->Flatten().has_value());
	NGUI_FUZZ_CHECK(!ngui::remote::detail::DescribeReply(*message).empty());
	(void)ngui::remote::detail::ResultText(*message);
}
