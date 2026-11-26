/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// ngui-replay: shows what a recording of NewGUI holds (specification 20.8),
// so that one can see what was recorded before passing it on (rule T5).
// A recording is played back in a test, by test::Driver::Replay().
//
//   ngui-replay --dump <file>
//
// Exit status: 0, 1 if the file is no recording, 2 for wrong arguments.


#include <cstdio>
#include <string>
#include <string_view>

#include <ngui/Recording.h>

#include "remote/Hey.h"


namespace {


void
Usage(std::FILE* out)
{
	std::fputs("usage: ngui-replay --dump <file>\n"
		"Prints the start and the events of a NewGUI recording (NGUI_RECORD=<file>,\n"
		"BApplication::StartRecording()). Typing into password fields and private\n"
		"views is masked. Tests play recordings back with test::Driver::Replay().\n",
		out);
}


std::string
Plural(std::size_t count, const char* one, const char* many)
{
	return std::to_string(count) + " " + (count == 1 ? one : many);
}


} // namespace


int
main(int argc, char** argv)
{
	const std::string_view first = argc > 1 ? argv[1] : "";
	if (argc == 2 && (first == "-h" || first == "--help")) {
		Usage(stdout);
		return 0;
	}
	if (argc != 3 || first != "--dump") {
		Usage(stderr);
		return 2;
	}
	const auto recording = ngui::Recording::Load(argv[2]);
	if (!recording.has_value()) {
		std::fprintf(stderr, "ngui-replay: %s\n", recording.error().message.c_str());
		return 1;
	}

	using ngui::remote::detail::DescribeFields;
	using ngui::remote::detail::MessageName;
	const ngui::RecordingStart& start = recording->start;
	std::string languages;
	for (const std::string& language : start.languages)
		languages += (languages.empty() ? "" : ", ") + language;
	std::printf("Recording of %s, look \"%s\", languages %s\n", start.signature.c_str(),
		start.look.c_str(), languages.empty() ? "-" : languages.c_str());
	std::printf("%s at the start, %s:\n",
		Plural(start.windows.size(), "window", "windows").c_str(),
		Plural(recording->events.size(), "event", "events").c_str());
	for (std::size_t i = 0; i < start.windows.size(); i++)
		std::printf("   window %s: \"%s\"\n", std::to_string(i).c_str(),
			start.windows[i].title.c_str());
	for (const ngui::RecordedEvent& event : recording->events) {
		std::string where = event.window < 0 ? std::string("application")
			: "window " + std::to_string(event.window);
		if (event.popup >= 0)
			where += ", popup " + std::to_string(event.popup);
		const double seconds = static_cast<double>(event.time.count()) / 1e6;
		std::printf("%10.3f s  %-20s %s%s\n", seconds, where.c_str(),
			MessageName(event.message.what).c_str(), event.masked ? "  (masked)" : "");
		std::fputs(DescribeFields(event.message, "      ").c_str(), stdout);
	}
	return 0;
}
