/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Flags.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace ngui {


/*!	What a file panel is for (Haiku's file_panel_mode). */
enum class BFilePanelMode : std::uint8_t {
	Open,	/*!< B_OPEN_PANEL: existing files (or folders). */
	Save	/*!< B_SAVE_PANEL: a file to save to. */
};


/*!	A button of a file panel (Haiku's file_panel_button). */
enum class BFilePanelButton : std::uint8_t {
	Cancel,		/*!< B_CANCEL_BUTTON */
	Default		/*!< B_DEFAULT_BUTTON: Open or Save. */
};


/*!	The kinds of nodes an open panel picks (Haiku's node_flavor); the
	values are Haiku's. */
enum class BNodeFlavor : std::uint32_t {
	File = 0x01,		/*!< B_FILE_NODE */
	Symlink = 0x02,		/*!< B_SYMLINK_NODE */
	Directory = 0x04	/*!< B_DIRECTORY_NODE */
};
NGUI_FLAG_ENUM(BNodeFlavor)

/*!	A set of BNodeFlavor. */
using BNodeFlavors = Flags<BNodeFlavor>;


/*!	A panel to choose files to open or a file to save (Haiku's BFilePanel),
	shown as the system's own file dialog (module P2). Show() returns at
	once; the choice arrives at the target as a message, as with Haiku:

	- Open: the panel's message (kRefsReceived by default) with the chosen
	  paths in "paths" (UTF-8; Haiku: entry_refs in "refs").
	- Save: the panel's message (kSaveRequested by default) with the folder
	  in "directory" and the file name in "name" (strings; Haiku: an
	  entry_ref and a string).
	- Then, as the dialog closes, kCancel: a copy of the panel's message
	  with what kCancel, "old_what" (its what) and "source" (a pointer to
	  the panel), also after a choice, as Haiku's panel sends it whenever
	  it is hidden.

	Differences to Haiku: the dialog is the system's (under Linux through
	the xdg-desktop-portal), so there is no BRefFilter, Window() or
	SelectionChanged(), no Rewind() or GetNextSelectedRef(); filters by
	file name pattern (AddFilter()) take the place of BRefFilter. A dialog
	cannot be closed by the program: after Hide() its result is dropped.
	The dialog always closes when done (hideWhenDone is kept for the API).
	An open panel whose only flavor is BNodeFlavor::Directory picks
	folders. The panel may be destroyed while its dialog shows; the result
	then still goes to the target. Show() returns NotSupported without a
	platform that has file dialogs. Thread-safe.
*/
class BFilePanel {
public:
								BFilePanel(BFilePanelMode mode = BFilePanelMode::Open,
									BMessenger target = {}, std::string_view directory = {},
									BNodeFlavors nodeFlavors = {},
									bool allowMultipleSelection = true,
									std::unique_ptr<BMessage> message = nullptr,
									bool modal = false, bool hideWhenDone = true);
									/*!< A panel that sends its choice to \a target
										(nothing without one), starting in \a directory
										(the platform's choice if empty). \a modal makes the
										dialog modal to the window whose thread calls
										Show(). */
								~BFilePanel();
									/*!< Destroys it; a dialog still showing still answers
										its target. */

								BFilePanel(const BFilePanel&) = delete;
			BFilePanel&			operator=(const BFilePanel&) = delete;

			std::expected<void, Error> Show();
									/*!< Shows the dialog and returns at once; nothing if it
										shows already. NotSupported without file dialogs. */
			void				Hide();
									/*!< Drops the result of the dialog showing (it cannot
										be closed from here). */
			bool				IsShowing() const;
									/*!< A dialog of this panel shows. */

			BFilePanelMode		PanelMode() const;
									/*!< Open or Save. */
			void				SetTarget(BMessenger target);
									/*!< Where the choice goes. */
			void				SetMessage(std::unique_ptr<BMessage> message);
									/*!< The message the choice comes in; nullptr for the
										default (kRefsReceived, kSaveRequested). */
			void				SetSaveText(std::string_view text);
									/*!< The file name a save panel suggests. */
			void				SetButtonLabel(BFilePanelButton button, std::string_view label);
									/*!< The label of a button; empty for the system's. */
			void				SetNodeFlavors(BNodeFlavors flavors);
									/*!< What an open panel picks: Directory alone picks
										folders, anything else files. */
			void				SetPanelDirectory(std::string_view directory);
									/*!< The folder the next dialog starts in. */
			std::string			PanelDirectory() const;
									/*!< The folder the next dialog starts in (Haiku's
										GetPanelDirectory()). */
			void				SetHideWhenDone(bool hideWhenDone);
									/*!< Kept for Haiku's API: system dialogs always close
										when done. */
			bool				HidesWhenDone() const;
									/*!< See SetHideWhenDone(). */

			void				SetTitle(std::string_view title);
									/*!< NewGUI: the dialog's title (Haiku:
										Window()->SetTitle()). */
			void				AddFilter(std::string_view name, std::string_view patterns);
									/*!< NewGUI: a choice of files by their extension:
										\a patterns like "png;jpg", "*" for all. Instead of
										Haiku's BRefFilter. */
			void				ClearFilters();
									/*!< NewGUI: removes the filters. */

private:
			struct Shared;

			mutable std::mutex	fLock;
			BFilePanelMode		fMode;
			BMessenger			fTarget;
			std::string			fDirectory;
			BNodeFlavors		fFlavors;
			bool				fMultiple;
			std::unique_ptr<BMessage> fMessage;
			bool				fModal;
			bool				fHideWhenDone;
			std::string			fSaveText;
			std::string			fTitle;
			std::string			fAcceptLabel;
			std::string			fCancelLabel;
			std::vector<std::pair<std::string, std::string>> fFilters;
			std::shared_ptr<Shared> fShowing;
									// the dialog showing, shared with its callback
};


} // namespace ngui
