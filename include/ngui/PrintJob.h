/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Messenger.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


class BView;
class BWindow;


/*!	A paper size, in points (1/72 inch), standing upright. */
struct BPaper {
			std::string			name;	/*!< Its name, such as "A4". */
			gfx::Size			size;	/*!< Width and height in points, portrait. */

			bool				operator==(const BPaper& other) const = default;
									/*!< Same name and size. */

	static	BPaper				A4();		/*!< 210 × 297 mm. */
	static	BPaper				A5();		/*!< 148 × 210 mm. */
	static	BPaper				Letter();	/*!< US Letter, 8.5 × 11 inches. */
	static	BPaper				Legal();	/*!< US Legal, 8.5 × 14 inches. */
	static	std::vector<BPaper>	Standard();	/*!< The four above, A4 first. */
};


/*!	How a page lies on the paper. */
enum class BPrintOrientation : std::uint8_t {
	Portrait,	/*!< Upright: the paper's width across. */
	Landscape	/*!< On its side: the paper's height across. */
};


/*!	The margins of a page in points: what the printable rectangle leaves
	free of the paper. */
struct BPrintMargins {
			float				left = 36;		/*!< Half an inch. */
			float				top = 36;		/*!< Half an inch. */
			float				right = 36;		/*!< Half an inch. */
			float				bottom = 36;	/*!< Half an inch. */
};


/*!	The settings of a print job (Haiku keeps them in a BMessage). */
struct BPrintSettings {
			std::string			printer{};	/*!< The printer by name; empty for the print
												service's default one. */
			BPaper				paper = BPaper::A4();	/*!< The paper. */
			BPrintOrientation	orientation = BPrintOrientation::Portrait;
									/*!< How the pages lie on it. */
			BPrintMargins		margins{};	/*!< Free around the printable rectangle. */
			float				scale = 100;
									/*!< Percent of the natural size: at 100, a logical
										pixel is 0.75 points (96 per inch), as on a screen
										of 96 dpi. */
			int32				copies = 1;	/*!< Copies of the document, at least 1. */
			int32				firstPage = 1;
									/*!< The first page to print, from 1 (FirstPage()). */
			int32				lastPage = 0x7fffffff;
									/*!< The last page to print (LastPage()); the largest
										int32 for all. */
			std::string			file{};
									/*!< For a printer that writes files (the PDF
										printer): the file. */
};


/*!	A printer a print service offers. */
struct BPrinterInfo {
			std::string			name;			/*!< What BPrintSettings::printer says. */
			std::string			description{};	/*!< What a user reads; may be empty. */
			bool				isDefault = false;	/*!< The service's default printer. */
			bool				writesFile = false;
									/*!< Writes to BPrintSettings::file instead of paper. */
};


/*!	A page of a print job: DisplayLists placed on paper of a size in points
	(origin top left), as PdfDocument::Draw() takes them. */
struct BPrintPage {
	/*!	A part of a list on the page. */
	struct Placement {
			gfx::DisplayListRef	list;	/*!< What is drawn. */
			gfx::Rect			area;	/*!< The part of it, in its logical pixels. */
			gfx::Point			at;		/*!< Where its top left corner goes, in points. */
			float				pointsPerPixel = 0.75f;	/*!< Its scale. */
	};

			gfx::Size			size;	/*!< The paper as it lies, in points. */
			std::vector<Placement> placements;	/*!< In drawing order. */
};


/*!	A print system: the printers of the platform and the way pages get to
	them. The core of NewGUI has none; ngui_skia brings SkiaPrintService,
	which writes PDF files and prints through CUPS or Windows' GDI. A
	program installs one with SetDefault(); BPrintJob uses that one.
*/
class BPrintService {
public:
	virtual						~BPrintService() = default;

	virtual	std::vector<BPrinterInfo> Printers() const = 0;
									/*!< The printers there are now. */
	virtual	std::expected<void, Error> Print(std::string_view jobName,
									std::span<const BPrintPage> pages,
									const BPrintSettings& settings) = 0;
									/*!< Prints \a pages on the printer of \a settings (its
										default printer if empty), \a settings.copies times.
										NotFound for an unknown printer, NotSupported where
										it cannot print, IoError if the printer or file
										fails. */
	virtual	int32				Resolution(std::string_view printer) const;
									/*!< Dots per inch of \a printer, for
										BPrintJob::GetResolution(); 300 by default. */

	static	BPrintService*		Default();
									/*!< The service of the program; nullptr if none is
										installed. */
	static	void				SetDefault(BPrintService* service);
									/*!< Installs \a service (nullptr: none); it must
										outlive its use. */
};


/*!	A print job, as Haiku's BPrintJob: a program begins it, draws views
	page by page and commits it; the job then goes to the printer of its
	settings through BPrintService::Default().

	Coordinates on a page are points relative to PrintableRect(), as in
	Haiku; a view's logical pixel is BPrintSettings::scale percent of 0.75
	points (96 per inch at 100 %). Pages are kept as DisplayLists until
	CommitJob(); printing them is the service's business.

	Differences to Haiku:
	- BeginJob(), CommitJob() and DrawView() return what went wrong instead
	  of leaving it to CanContinue().
	- The settings are a BPrintSettings, not a BMessage.
	- FirstPage() and LastPage() tell the program what to draw; the job
	  does not drop pages outside them.
	- ConfigPage() and ConfigJob() are asynchronous, as BAlert::Go(): they
	  show their panel and return at once; the result comes through a
	  callback (Haiku blocked and returned a status).
	- PrinterType() is gone: printers print in color.
*/
class BPrintJob {
public:
	explicit					BPrintJob(std::string name);
									/*!< A job called \a name with the default settings:
										A4 upright, margins of half an inch, the default
										printer of the print service. */
								~BPrintJob();
								BPrintJob(const BPrintJob&) = delete;
			BPrintJob&			operator=(const BPrintJob&) = delete;

			std::expected<BMessenger, Error> ConfigPage(
									std::function<void(bool accepted)> done = nullptr);
									/*!< Shows the page setup panel (paper, orientation,
										scale) in front of the window whose thread calls,
										and returns at once with the panel's messenger. OK
										makes its choices the job's settings, then \a done
										gets true; Cancel or closing it leaves them, \a done
										gets false. \a done runs on the panel's thread, the
										job must live until then. */
			std::expected<BMessenger, Error> ConfigJob(
									std::function<void(bool accepted)> done = nullptr);
									/*!< As ConfigPage() with the print panel: the printers
										of the print service, the file for one that writes
										files, copies, all pages or a range. NotSupported
										without a print service. */

			std::expected<void, Error> BeginJob();
									/*!< Starts the job without pages. NotSupported without
										a print service, NotFound if it has no printer of
										the settings' name. */
			std::expected<void, Error> DrawView(BView* view, gfx::Rect rect,
									gfx::Point where);
									/*!< Draws \a rect of \a view (its coordinates) and its
										children on the current page, the top left corner
										at \a where (points from PrintableRect()'s top left).
										On the view's window thread. InvalidArgument without
										a begun job, a view in a window or a rectangle. */
			std::expected<void, Error> DrawList(gfx::DisplayListRef list, gfx::Rect area,
									gfx::Point where);
									/*!< As DrawView() for \a area of a DisplayList, from
										any thread. */
			void				SpoolPage();
									/*!< Ends the current page; the next drawing starts a
										new one. */
			std::expected<void, Error> CommitJob();
									/*!< Ends the current page and prints the job; it is
										over then. InvalidArgument without a begun job or
										without pages, else what the service reports. */
			void				CancelJob();
									/*!< Drops the job and its pages. */
			bool				CanContinue() const;
									/*!< Whether the job is begun and not cancelled,
										committed or failed. */

			BPrintSettings		Settings() const;
									/*!< The settings (a copy: a panel may change them on
										its thread). */
			void				SetSettings(const BPrintSettings& settings);
									/*!< Replaces them; copies below 1 become 1, a scale
										that is not positive 100. */

			gfx::Rect			PaperRect() const;
									/*!< The paper as it lies, in points: (0, 0) to its
										width and height, swapped in landscape. */
			gfx::Rect			PrintableRect() const;
									/*!< The paper inside the margins, in points. */
			void				GetResolution(int32* xDPI, int32* yDPI) const;
									/*!< The printer's dots per inch. */
			int32				FirstPage() const;
									/*!< The first page the user wants, from 1. */
			int32				LastPage() const;
									/*!< The last page the user wants. */
			int32				SpooledPages() const;
									/*!< The pages so far, the current one included. */

private:
			struct Data;

	static	std::expected<BMessenger, Error> _ShowPanel(std::unique_ptr<BWindow> panel);

			std::unique_ptr<Data> fData;
};


} // namespace ngui
