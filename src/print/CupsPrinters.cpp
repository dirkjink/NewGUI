/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "SystemPrinters.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

#include <dlfcn.h>


namespace ngui::print {


namespace {


// cups.h, ABI of libcups 2
struct cups_option_t {
	char*			name;
	char*			value;
};

struct cups_dest_t {
	char*			name;
	char*			instance;
	int				is_default;
	int				num_options;
	cups_option_t*	options;
};

struct http_t;
constexpr int kHttpStatusContinue = 100;	// HTTP_STATUS_CONTINUE
constexpr int kIppStatusOkLast = 0x00ff;	// the successful IPP statuses


// The functions of CUPS, from the library it was loaded from.
struct Cups {
	void*			library = nullptr;
	int				(*GetDests)(cups_dest_t**) = nullptr;
	void			(*FreeDests)(int, cups_dest_t*) = nullptr;
	const char*		(*GetOption)(const char*, int, cups_option_t*) = nullptr;
	int				(*AddOption)(const char*, const char*, int, cups_option_t**) = nullptr;
	void			(*FreeOptions)(int, cups_option_t*) = nullptr;
	int				(*CreateJob)(http_t*, const char*, const char*, int, cups_option_t*)
		= nullptr;
	int				(*StartDocument)(http_t*, const char*, int, const char*, const char*, int)
		= nullptr;
	int				(*WriteRequestData)(http_t*, const char*, size_t) = nullptr;
	int				(*FinishDocument)(http_t*, const char*) = nullptr;
	int				(*CancelJob)(const char*, int) = nullptr;
	const char*		(*LastErrorString)() = nullptr;

	// Loads \a path and the functions; false if it or one is missing.
	bool Load(const std::string& path)
	{
		library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr)
			return false;
		bool complete = true;
		const auto bind = [&](auto& function, const char* name) {
			// POSIX: a function's address passes through dlsym()'s void*.
			void* symbol = dlsym(library, name);
			complete = complete && symbol != nullptr;
			function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol);
		};
		bind(GetDests, "cupsGetDests");
		bind(FreeDests, "cupsFreeDests");
		bind(GetOption, "cupsGetOption");
		bind(AddOption, "cupsAddOption");
		bind(FreeOptions, "cupsFreeOptions");
		bind(CreateJob, "cupsCreateJob");
		bind(StartDocument, "cupsStartDocument");
		bind(WriteRequestData, "cupsWriteRequestData");
		bind(FinishDocument, "cupsFinishDocument");
		bind(CancelJob, "cupsCancelJob");
		bind(LastErrorString, "cupsLastErrorString");
		return complete;
	}
};


// CUPS loaded from \a library (the system's if empty), or nullptr; each
// library is loaded once and stays.
const Cups*
Library(const std::string& library)
{
#if defined(__APPLE__)
	const std::string path = library.empty() ? "libcups.2.dylib" : library;
#else
	const std::string path = library.empty() ? "libcups.so.2" : library;
#endif
	static std::mutex lock;
	static std::map<std::string, std::unique_ptr<Cups>> loaded;
	const std::scoped_lock guard(lock);
	auto found = loaded.find(path);
	if (found == loaded.end()) {
		auto cups = std::make_unique<Cups>();
		if (!cups->Load(path))
			cups.reset();
		found = loaded.emplace(path, std::move(cups)).first;
	}
	return found->second.get();
}


std::string
LastError(const Cups& cups)
{
	const char* message = cups.LastErrorString();
	return std::string("CUPS: ") + (message != nullptr ? message : "unknown error");
}


} // namespace


std::vector<BPrinterInfo>
CupsPrinters(const std::string& library)
{
	const Cups* cups = Library(library);
	if (cups == nullptr)
		return {};
	cups_dest_t* dests = nullptr;
	const int count = cups->GetDests(&dests);
	std::vector<BPrinterInfo> printers;
	for (int i = 0; i < count; i++) {
		const cups_dest_t& dest = dests[i];
		if (dest.name == nullptr)
			continue;
		// an instance is a set of options of a printer: "name/instance"
		std::string name = dest.name;
		if (dest.instance != nullptr)
			name += std::string("/") + dest.instance;
		const char* info = cups->GetOption("printer-info", dest.num_options, dest.options);
		printers.push_back({name, info != nullptr ? info : "", dest.is_default != 0, false});
	}
	if (dests != nullptr)
		cups->FreeDests(count, dests);
	return printers;
}


std::expected<void, Error>
CupsPrint(const std::string& library, std::string_view printer, std::string_view title,
	std::span<const std::byte> pdf, int32 copies)
{
	const Cups* cups = Library(library);
	if (cups == nullptr)
		return Failure(ErrorCode::NotSupported, "CUPS is not installed");
	const std::string name(printer.substr(0, printer.find('/')));
	const std::string jobTitle(title);
	cups_option_t* options = nullptr;
	int optionCount = 0;
	if (copies > 1) {
		optionCount = cups->AddOption("copies", std::to_string(copies).c_str(), optionCount,
			&options);
	}
	const int job = cups->CreateJob(nullptr, name.c_str(), jobTitle.c_str(), optionCount,
		options);
	cups->FreeOptions(optionCount, options);
	if (job <= 0)
		return Failure(ErrorCode::IoError, LastError(*cups));

	bool sent = cups->StartDocument(nullptr, name.c_str(), job, jobTitle.c_str(),
		"application/pdf", 1) == kHttpStatusContinue;
	// in pieces, so that a large document does not need one large request
	constexpr std::size_t kPiece = 64 * 1024;
	for (std::size_t at = 0; sent && at < pdf.size(); at += kPiece) {
		const std::span<const std::byte> piece = pdf.subspan(at,
			std::min(kPiece, pdf.size() - at));
		sent = cups->WriteRequestData(nullptr, reinterpret_cast<const char*>(piece.data()),
			piece.size()) == kHttpStatusContinue;
	}
	const int status = cups->FinishDocument(nullptr, name.c_str());
	if (!sent || status > kIppStatusOkLast) {
		const std::string error = LastError(*cups);
		(void)cups->CancelJob(name.c_str(), job);
		return Failure(ErrorCode::IoError, error);
	}
	return {};
}


std::vector<BPrinterInfo>
GdiPrinters()
{
	return {};
}


std::expected<void, Error>
GdiPrint(std::string_view /*printer*/, std::string_view /*title*/,
	std::span<const BPrintPage> /*pages*/, const BPrintSettings& /*settings*/,
	const PageRasterizer& /*raster*/)
{
	return Failure(ErrorCode::NotSupported, "GDI is Windows only");
}


int32
GdiResolution(std::string_view /*printer*/)
{
	return 300;
}


} // namespace ngui::print
