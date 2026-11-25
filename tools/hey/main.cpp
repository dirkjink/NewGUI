/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// ngui-hey: scripting messages to a running NewGUI application from the
// command line (specification 20.7), in the language of Haiku's hey. The
// application must allow it (BApplication::EnableRemoteControl() or
// NGUI_REMOTE_CONTROL=1).


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/PropertyInfo.h>
#include <ngui/RemoteControl.h>

#include "remote/Hey.h"


namespace {


using namespace ngui;


constexpr const char* kUsage =
	"usage: ngui-hey [-s] [-o] <app> <verb> <specifier> [of <specifier>]*\n"
	"                [to <value>] [with name=<value> [and name=<value>]*]\n"
	"       ngui-hey -l\n"
	"where  <app>       : a process id, a signature, or the end of one (newgui-demo)\n"
	"       <verb>      : do | get | set | count | create | delete | getsuites\n"
	"       <specifier> : [the] <property> [ <index> | -<index> | [<index>] | [-<index>]\n"
	"                     | [<index> to <end>] | <name> | \"<name>\" ]\n"
	"       <value>     : \"string\" | <integer> | <float> | true | false | int8(v)\n"
	"                     | int16(v) | int32(v) | int64(v) | bool(v) | float(v)\n"
	"                     | double(v) | BPoint(x,y) | BRect(x0,y0,x1,y1)\n"
	"                     | rgb_color(r,g,b,a)\n"
	"options: -s: silent\n"
	"         -o: output the result only, for scripts\n"
	"         -l: list the applications that allow remote control\n"
	"The application must allow remote control: BApplication::EnableRemoteControl()\n"
	"or NGUI_REMOTE_CONTROL=1.\n";


std::string
Lower(std::string_view text)
{
	std::string lower(text);
	std::ranges::transform(lower, lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower;
}


// The applications \a name stands for: its process id, its signature, or
// the end of its signature, ignoring case.
std::vector<remote::Endpoint>
Matching(const std::vector<remote::Endpoint>& endpoints, std::string_view name)
{
	std::vector<remote::Endpoint> matching;
	const bool digits = !name.empty() && std::ranges::all_of(name,
		[](char c) { return c >= '0' && c <= '9'; });
	const std::string wanted = Lower(name);
	for (const remote::Endpoint& endpoint : endpoints) {
		const std::string signature = Lower(endpoint.signature);
		if ((digits && std::to_string(endpoint.process) == name) || signature == wanted
			|| signature.ends_with(wanted)) {
			matching.push_back(endpoint);
		}
	}
	return matching;
}


} // namespace


int
main(int argc, char** argv)
{
	bool silent = false;
	bool output = false;
	int at = 1;
	for (; at < argc && argv[at][0] == '-'; at++) {
		const std::string_view option = argv[at];
		if (option == "-s")
			silent = true;
		else if (option == "-o")
			output = true;
		else if (option == "-l") {
			for (const remote::Endpoint& endpoint : remote::ListApplications())
				std::printf("%lld\t%s\n", static_cast<long long>(endpoint.process),
					endpoint.signature.c_str());
			return EXIT_SUCCESS;
		} else if (option == "-h" || option == "--help") {
			std::fputs(kUsage, stdout);
			return EXIT_SUCCESS;
		} else {
			std::fputs(kUsage, stderr);
			return 2;
		}
	}
	if (argc - at < 2) {
		std::fputs(kUsage, stderr);
		return 2;
	}

	const std::vector<std::string> words(argv + at + 1, argv + argc);
	const auto message = remote::detail::ParseHeyCommand(words);
	if (!message.has_value()) {
		if (!silent)
			std::fprintf(stderr, "ngui-hey: %s\n", message.error().message.c_str());
		return 2;
	}
	const std::vector<remote::Endpoint> targets = Matching(remote::ListApplications(),
		argv[at]);
	if (targets.empty()) {
		if (!silent) {
			std::fprintf(stderr, "ngui-hey: no application \"%s\" allows remote control "
				"(see ngui-hey -l)\n", argv[at]);
		}
		return 1;
	}

	// as hey 1.2.7: on to the next application of the name while one fails
	int status = 1;
	for (const remote::Endpoint& target : targets) {
		auto client = remote::Client::Connect(target.path);
		if (!client.has_value())
			continue;
		const auto reply = client->Send(*message);
		if (!reply.has_value()) {
			if (!silent)
				std::fprintf(stderr, "ngui-hey: %s\n", reply.error().message.c_str());
			continue;
		}
		if (!silent) {
			const std::string text = output ? remote::detail::ResultText(*reply)
				: remote::detail::DescribeReply(*reply);
			std::fputs(text.c_str(), stdout);
		}
		status = ScriptingResult(*reply).has_value() ? EXIT_SUCCESS : 1;
		if (status == EXIT_SUCCESS)
			break;
	}
	return status;
}
