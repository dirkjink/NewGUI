/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "PrintPanels.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

#include <ngui/Button.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/Message.h>
#include <ngui/PopUpMenu.h>
#include <ngui/RadioButton.h>
#include <ngui/StringView.h>
#include <ngui/TextControl.h>


namespace ngui {


namespace {


constexpr std::uint32_t kAccept = 0x70724f4b;		// 'prOK'
constexpr std::uint32_t kCancelPanel = 0x70724378;	// 'prCx'
constexpr std::uint32_t kPrinterChosen = 0x70725072;	// 'prPr'
constexpr std::uint32_t kRangeChosen = 0x70725267;	// 'prRg'


std::string_view
Trimmed(std::string_view text)
{
	while (!text.empty() && text.front() == ' ')
		text.remove_prefix(1);
	while (!text.empty() && text.back() == ' ')
		text.remove_suffix(1);
	return text;
}


// \a text as a whole number; std::nullopt unless it is one.
std::optional<int32>
Integer(std::string_view text)
{
	text = Trimmed(text);
	int32 value = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc() || end != text.data() + text.size())
		return std::nullopt;
	return value;
}


// \a text as a decimal number ("150", "87.5", "87,5"); std::nullopt unless
// it is one. By hand: libc++ before LLVM 20 (Xcode's) has no from_chars()
// for floats, and strtof() would follow the locale.
std::optional<float>
Decimal(std::string_view text)
{
	text = Trimmed(text);
	float value = 0;
	float scale = 0;
	bool digits = false;
	for (const char c : text) {
		if (c >= '0' && c <= '9') {
			const float digit = static_cast<float>(c - '0');
			if (scale == 0)
				value = value * 10 + digit;
			else {
				scale /= 10;
				value += digit * scale;
			}
			digits = true;
		} else if ((c == '.' || c == ',') && scale == 0)
			scale = 1;
		else
			return std::nullopt;
	}
	if (!digits || !std::isfinite(value))
		return std::nullopt;
	return value;
}


std::string
Format(float value)
{
	if (value == std::floor(value) && std::fabs(value) < 1e9f)
		return std::to_string(static_cast<long long>(value));
	return std::to_string(value);
}


// What both panels share: OK and Cancel at the bottom right, reporting
// once, also when closed.
class PrintPanel : public BWindow {
public:
	PrintPanel(std::string_view title, BPrintSettings settings, PrintPanelDone done)
		:
		BWindow({100, 100, 460, 300}, title, BWindowLook::Titled, BWindowFeel::ModalApp,
			BWindowFlags{BWindowFlag::NotResizable} | BWindowFlag::AutoUpdateSizeLimits),
		fSettings(std::move(settings)),
		fDone(std::move(done))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kAccept:
				_Report(Accepted());
				Quit();
				break;
			case kCancelPanel:
				_Report(std::nullopt);
				Quit();
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

	bool QuitRequested() override
	{
		_Report(std::nullopt);
		return true;
	}

protected:
	// The settings as the panel shows them.
	virtual BPrintSettings Accepted() = 0;

	std::unique_ptr<BButton> MakeButton(std::string_view name, std::string_view label,
		std::uint32_t what)
	{
		return std::make_unique<BButton>(name, label, std::make_unique<BMessage>(what));
	}

	BPrintSettings fSettings;

private:
	void _Report(std::optional<BPrintSettings> settings)
	{
		if (fDone == nullptr)
			return;
		PrintPanelDone done = std::move(fDone);
		fDone = nullptr;
		done(std::move(settings));
	}

	PrintPanelDone fDone;
};


class PageSetupPanel final : public PrintPanel {
public:
	PageSetupPanel(const BPrintSettings& settings, PrintPanelDone done)
		:
		PrintPanel("Page setup", settings, std::move(done)),
		fPapers(BPaper::Standard())
	{
		auto menu = std::make_unique<BPopUpMenu>("paper");
		bool known = false;
		for (const BPaper& paper : fPapers)
			known = known || paper == fSettings.paper;
		if (!known)
			fPapers.push_back(fSettings.paper);
		for (const BPaper& paper : fPapers) {
			auto item = std::make_unique<BMenuItem>(paper.name, nullptr);
			item->SetMarked(paper == fSettings.paper);
			menu->AddItem(std::move(item));
		}
		auto paper = std::make_unique<BMenuField>("paper", "Paper:", std::move(menu));
		fPaper = paper.get();
		auto portrait = std::make_unique<BRadioButton>("portrait", "Portrait", nullptr);
		auto landscape = std::make_unique<BRadioButton>("landscape", "Landscape", nullptr);
		fLandscape = landscape.get();
		(fSettings.orientation == BPrintOrientation::Landscape ? landscape : portrait)
			->SetValue(kControlOn);
		auto scale = std::make_unique<BTextControl>("scale", "Scale (%):",
			Format(fSettings.scale), nullptr);
		fScale = scale.get();
		auto ok = MakeButton("ok", "OK", kAccept);
		ok->MakeDefault(true);

		BLayoutBuilder::Group<>(this, BOrientation::Vertical)
			.SetInsets(kUseWindowInsets)
			.AddGrid()
				.AddMenuField(std::move(paper), 0, 0)
				.Add(std::make_unique<BStringView>("orientation", "Orientation:"), 0, 1)
				.Add(std::move(portrait), 1, 1)
				.Add(std::move(landscape), 1, 2)
				.AddTextControl(std::move(scale), 0, 3)
			.End()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(MakeButton("cancel", "Cancel", kCancelPanel))
				.Add(std::move(ok))
			.End();
	}

protected:
	BPrintSettings Accepted() override
	{
		BPrintSettings settings = fSettings;
		const int32 index = fPaper->Menu()->FindMarkedIndex();
		if (index >= 0 && static_cast<std::size_t>(index) < fPapers.size())
			settings.paper = fPapers[static_cast<std::size_t>(index)];
		settings.orientation = fLandscape->Value() == kControlOn
			? BPrintOrientation::Landscape : BPrintOrientation::Portrait;
		const std::optional<float> scale = Decimal(fScale->Text());
		if (scale.has_value() && std::isfinite(*scale) && *scale > 0)
			settings.scale = *scale;
		return settings;
	}

private:
	std::vector<BPaper> fPapers;
	BMenuField* fPaper = nullptr;
	BRadioButton* fLandscape = nullptr;
	BTextControl* fScale = nullptr;
};


class PrintJobPanel final : public PrintPanel {
public:
	PrintJobPanel(const BPrintSettings& settings, std::vector<BPrinterInfo> printers,
		PrintPanelDone done)
		:
		PrintPanel("Print", settings, std::move(done)),
		fPrinters(std::move(printers))
	{
		auto menu = std::make_unique<BPopUpMenu>("printer");
		for (std::size_t i = 0; i < fPrinters.size(); i++) {
			const BPrinterInfo& info = fPrinters[i];
			auto message = std::make_unique<BMessage>(kPrinterChosen);
			(void)message->AddInt32("index", static_cast<int32>(i));
			auto item = std::make_unique<BMenuItem>(info.name, std::move(message));
			item->SetMarked(fSettings.printer.empty() ? info.isDefault
				: info.name == fSettings.printer);
			menu->AddItem(std::move(item));
		}
		auto printer = std::make_unique<BMenuField>("printer", "Printer:", std::move(menu));
		fPrinter = printer.get();
		auto file = std::make_unique<BTextControl>("file", "File:", fSettings.file, nullptr);
		fFile = file.get();
		auto copies = std::make_unique<BTextControl>("copies", "Copies:",
			std::to_string(fSettings.copies), nullptr);
		fCopies = copies.get();
		const bool everything = fSettings.firstPage <= 1 && fSettings.lastPage == 0x7fffffff;
		auto all = std::make_unique<BRadioButton>("all", "All pages",
			std::make_unique<BMessage>(kRangeChosen));
		auto range = std::make_unique<BRadioButton>("range", "Pages",
			std::make_unique<BMessage>(kRangeChosen));
		fRange = range.get();
		(everything ? all : range)->SetValue(kControlOn);
		auto from = std::make_unique<BTextControl>("from", "From:",
			std::to_string(std::max(fSettings.firstPage, 1)), nullptr);
		fFrom = from.get();
		auto to = std::make_unique<BTextControl>("to", "To:",
			everything ? std::string() : std::to_string(fSettings.lastPage), nullptr);
		fTo = to.get();
		auto print = MakeButton("print", "Print", kAccept);
		print->MakeDefault(true);

		BLayoutBuilder::Group<>(this, BOrientation::Vertical)
			.SetInsets(kUseWindowInsets)
			.AddGrid()
				.AddMenuField(std::move(printer), 0, 0)
				.AddTextControl(std::move(file), 0, 1)
				.AddTextControl(std::move(copies), 0, 2)
				.Add(std::make_unique<BStringView>("pages", "Pages:"), 0, 3)
				.Add(std::move(all), 1, 3)
				.Add(std::move(range), 1, 4)
				.AddTextControl(std::move(from), 0, 5)
				.AddTextControl(std::move(to), 0, 6)
			.End()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(MakeButton("cancel", "Cancel", kCancelPanel))
				.Add(std::move(print))
			.End();
		_Update();
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kPrinterChosen || message->what == kRangeChosen) {
			_Update();
			return;
		}
		PrintPanel::MessageReceived(message);
	}

protected:
	BPrintSettings Accepted() override
	{
		BPrintSettings settings = fSettings;
		if (const BPrinterInfo* printer = _Chosen(); printer != nullptr)
			settings.printer = printer->name;
		settings.file = std::string(Trimmed(fFile->Text()));
		settings.copies = std::max(Integer(fCopies->Text()).value_or(1), 1);
		settings.firstPage = 1;
		settings.lastPage = 0x7fffffff;
		if (fRange->Value() == kControlOn) {
			settings.firstPage = std::max(Integer(fFrom->Text()).value_or(1), 1);
			settings.lastPage = std::max(Integer(fTo->Text()).value_or(0x7fffffff),
				settings.firstPage);
		}
		return settings;
	}

private:
	const BPrinterInfo* _Chosen() const
	{
		const int32 index = fPrinter->Menu()->FindMarkedIndex();
		if (index < 0 || static_cast<std::size_t>(index) >= fPrinters.size())
			return nullptr;
		return &fPrinters[static_cast<std::size_t>(index)];
	}

	// The file only for a printer that writes one, the range only if chosen.
	void _Update()
	{
		const BPrinterInfo* printer = _Chosen();
		fFile->SetEnabled(printer != nullptr && printer->writesFile);
		const bool range = fRange->Value() == kControlOn;
		fFrom->SetEnabled(range);
		fTo->SetEnabled(range);
	}

	std::vector<BPrinterInfo> fPrinters;
	BMenuField* fPrinter = nullptr;
	BTextControl* fFile = nullptr;
	BTextControl* fCopies = nullptr;
	BRadioButton* fRange = nullptr;
	BTextControl* fFrom = nullptr;
	BTextControl* fTo = nullptr;
};


} // namespace


std::unique_ptr<BWindow>
MakePageSetupPanel(const BPrintSettings& settings, PrintPanelDone done)
{
	return std::make_unique<PageSetupPanel>(settings, std::move(done));
}


std::unique_ptr<BWindow>
MakePrintPanel(const BPrintSettings& settings, std::vector<BPrinterInfo> printers,
	PrintPanelDone done)
{
	return std::make_unique<PrintJobPanel>(settings, std::move(printers), std::move(done));
}


} // namespace ngui
