/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// A stand-in for libcups 2 that SkiaPrintTest loads instead of the system's:
// the destinations Office (default, "Office printer"), Lab and its instance
// Lab/duplex, and Broken, which refuses jobs. A job is written to the
// directory of the environment variable FAKE_CUPS_DIR: job.pdf with the
// document, job.txt with the printer, title, format and copies.


#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>


namespace {


struct Option {
	char*	name;
	char*	value;
};

struct Dest {
	char*	name;
	char*	instance;
	int		is_default;
	int		num_options;
	Option*	options;
};


std::string sLastError;
std::string sPrinter;
std::string sTitle;
std::string sCopies;
std::string sFormat;
std::string sDocument;


char*
Copy(const char* text)
{
	if (text == nullptr)
		return nullptr;
	const std::size_t length = std::strlen(text) + 1;
	auto* copy = static_cast<char*>(std::malloc(length));
	std::memcpy(copy, text, length);
	return copy;
}


} // namespace


extern "C" {


int
cupsAddOption(const char* name, const char* value, int count, Option** options)
{
	auto* grown = static_cast<Option*>(std::realloc(*options,
		sizeof(Option) * static_cast<std::size_t>(count + 1)));
	grown[count] = {Copy(name), Copy(value)};
	*options = grown;
	return count + 1;
}


void
cupsFreeOptions(int count, Option* options)
{
	for (int i = 0; i < count; i++) {
		std::free(options[i].name);
		std::free(options[i].value);
	}
	std::free(options);
}


const char*
cupsGetOption(const char* name, int count, Option* options)
{
	for (int i = 0; i < count; i++) {
		if (std::strcmp(options[i].name, name) == 0)
			return options[i].value;
	}
	return nullptr;
}


int
cupsGetDests(Dest** dests)
{
	auto* list = static_cast<Dest*>(std::calloc(4, sizeof(Dest)));
	list[0] = {Copy("Office"), nullptr, 1, 0, nullptr};
	list[0].num_options = cupsAddOption("printer-info", "Office printer", 0, &list[0].options);
	list[1] = {Copy("Lab"), nullptr, 0, 0, nullptr};
	list[2] = {Copy("Lab"), Copy("duplex"), 0, 0, nullptr};
	list[3] = {Copy("Broken"), nullptr, 0, 0, nullptr};
	*dests = list;
	return 4;
}


void
cupsFreeDests(int count, Dest* dests)
{
	for (int i = 0; i < count; i++) {
		std::free(dests[i].name);
		std::free(dests[i].instance);
		cupsFreeOptions(dests[i].num_options, dests[i].options);
	}
	std::free(dests);
}


int
cupsCreateJob(void* /*http*/, const char* name, const char* title, int count, Option* options)
{
	if (std::strcmp(name, "Broken") == 0) {
		sLastError = "the printer is broken";
		return 0;
	}
	sPrinter = name;
	sTitle = title;
	const char* copies = cupsGetOption("copies", count, options);
	sCopies = copies != nullptr ? copies : "";
	sDocument.clear();
	return 7;
}


int
cupsStartDocument(void* /*http*/, const char* /*name*/, int job, const char* /*document*/,
	const char* format, int /*last*/)
{
	sFormat = format;
	return job == 7 ? 100 : 400;
}


int
cupsWriteRequestData(void* /*http*/, const char* buffer, std::size_t length)
{
	sDocument.append(buffer, length);
	return 100;
}


int
cupsFinishDocument(void* /*http*/, const char* /*name*/)
{
	const char* directory = std::getenv("FAKE_CUPS_DIR");
	if (directory == nullptr) {
		sLastError = "FAKE_CUPS_DIR is not set";
		return 0x0500;
	}
	std::ofstream(std::string(directory) + "/job.pdf", std::ios::binary) << sDocument;
	std::ofstream(std::string(directory) + "/job.txt") << "printer=" << sPrinter << "\ntitle="
		<< sTitle << "\nformat=" << sFormat << "\ncopies=" << sCopies << "\n";
	return 0;
}


int
cupsCancelJob(const char* /*name*/, int /*job*/)
{
	return 1;
}


const char*
cupsLastErrorString()
{
	return sLastError.c_str();
}


} // extern "C"
