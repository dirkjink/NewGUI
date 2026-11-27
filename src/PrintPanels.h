/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The windows of BPrintJob::ConfigPage() and ConfigJob(): each edits a copy
// of the job's settings and reports them once, when it closes.


#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <ngui/PrintJob.h>
#include <ngui/Window.h>


namespace ngui {


/*!	What a print panel reports: the new settings, or none if it was
	cancelled or closed. Runs on the panel's thread. */
using PrintPanelDone = std::function<void(std::optional<BPrintSettings> settings)>;


/*!	The page setup panel "Page setup": paper ("paper"), orientation
	("portrait", "landscape") and scale in percent ("scale"); "ok" and
	"cancel". */
std::unique_ptr<BWindow> MakePageSetupPanel(const BPrintSettings& settings,
	PrintPanelDone done);

/*!	The print panel "Print": the printer ("printer", from \a printers), the
	file for a printer that writes files ("file"), copies ("copies"), all
	pages ("all") or a range ("range", "from", "to"); "print" and
	"cancel". */
std::unique_ptr<BWindow> MakePrintPanel(const BPrintSettings& settings,
	std::vector<BPrinterInfo> printers, PrintPanelDone done);


} // namespace ngui
