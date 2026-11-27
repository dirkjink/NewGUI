/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <atomic>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/ControlLook.h>
#include <ngui/Error.h>
#include <ngui/gfx/Backend.h>


namespace ngui {


/*!	Whether a look with \a info runs on a backend with \a caps (rule L9).
	Levels C1 and C2 need the core profile only, which every backend
	implements completely, linear gradients included (R3); the conformance
	levels of 21.4 describe how far a backend is tested, not what it
	reports. Level Extended needs every capability set in
	LookInfo::extendedCaps. */
bool							IsCompatible(const LookInfo& info, const gfx::Caps& caps);


/*!	The result of LookRegistry::SelectAtStart(). */
struct LookSelection {
			std::string			id;			/*!< The look that is current now. */
			std::string			warning;	/*!< Why the setting was not used; empty if
												it was. For the caller to log. */
};


/*!	The result of LookRegistry::LoadPlugins(). */
struct LookPluginReport {
			std::vector<std::string> ids;		/*!< The looks registered, in order. */
			std::vector<std::string> warnings;	/*!< The plugins refused and why, for the
													caller to log. */
};


/*!	The looks a program can choose from, and the current one
	(specification 18.4, module M17a). Built-in looks and plugins register
	a factory; the registry makes each look once, when it is first needed,
	and keeps it, since looks are immutable (L4).

	Current() is read atomically and may be called from any thread; a
	window takes a snapshot at the start of each layout and drawing cycle
	(L10). SetCurrent() replaces it; the registry of the process then tells
	every window (kLookChanged, 18.5, module M17b), and a window without a
	look of its own (BWindow::SetControlLook()) lays out and draws anew.
*/
class LookRegistry {
public:
	/*!	Makes a new look. */
	using Factory = std::function<std::shared_ptr<const ControlLook>()>;

	/*!	The look chosen when the setting cannot be used: "be", level C1,
		runs on every backend (L9). */
	static constexpr std::string_view kFallbackId = "be";

	/*!	The name of the setting that names the look at start. */
	static constexpr std::string_view kSettingName = "ngui.controlLook";

								LookRegistry() = default;
									/*!< Empty, for a backend with the core profile, without
										a current look. */

								LookRegistry(const LookRegistry&) = delete;
									/*!< Not copyable. */
			LookRegistry&		operator=(const LookRegistry&) = delete;
									/*!< Not copyable. */

	static	LookRegistry&		Default();
									/*!< The registry of the process, with the built-in looks
										and "be" as the current one. */

			bool				Register(std::string id, Factory factory);
									/*!< Adds a look; false if \a id is taken or empty or
										\a factory is empty. */
			std::vector<std::string> Ids() const;
									/*!< All registered ids, in order of registration. */

			void				SetBackendCaps(const gfx::Caps& caps);
									/*!< The capabilities of the active backend, for L9. */
			gfx::Caps			BackendCaps() const;
									/*!< As set; the core profile by default. */

			std::vector<LookInfo> Available() const;
									/*!< The looks compatible with BackendCaps(). */
			std::vector<LookInfo> Available(const gfx::Caps& caps) const;
									/*!< The looks compatible with \a caps (L9). A look whose
										factory fails is left out. */

			std::expected<std::shared_ptr<const ControlLook>, Error> Find(
									std::string_view id, const gfx::Caps& caps) const;
									/*!< The look \a id for a backend with \a caps: NotFound
										if it is not registered, NotSupported if it is
										incompatible with \a caps (L9) or its factory fails or
										makes a look with another id. */
			std::shared_ptr<const ControlLook> Current() const;
									/*!< The current look, atomically; nullptr until one is
										selected. */
	static	std::shared_ptr<const ControlLook> DefaultLook();
									/*!< The current look of Default(), the BeOS look if none
										is selected: what windows take at the start of each
										layout and drawing cycle (L10). */
			std::expected<void, Error> SetCurrent(std::string_view id);
									/*!< Makes \a id current: NotFound if it is not
										registered, NotSupported if it is incompatible with
										BackendCaps() (L9) or its factory fails or makes a
										look with another id. The current look is unchanged
										on failure. In Default(), a change is sent to every
										window as kLookChanged (18.5). */
			std::expected<LookSelection, Error> SelectAtStart(std::string_view setting);
									/*!< The selection at start (18.4): the look named by
										the setting, kFallbackId if it is empty. If that
										fails, kFallbackId with a warning (L9). Fails only if
										the fallback fails as well. */

			std::expected<std::string, Error> LoadPlugin(const std::filesystem::path& file);
									/*!< Loads the look plugin \a file (18.6, made with
										NGUI_LOOK_PLUGIN(); `dlopen`, under Windows
										`LoadLibrary`), makes its look and registers it;
										gives its id. The plugin stays loaded while its look
										lives. Fails with NotFound if the file cannot be
										loaded, InvalidArgument if it is no look plugin or
										its id is taken, NotSupported if it was built
										otherwise (LookPluginBuildKey()) or declines the
										interface version (L12). Plugins run with the rights
										of the program: load only what the program or its
										user chose. */
			LookPluginReport	LoadPlugins(const std::filesystem::path& directory);
									/*!< LoadPlugin() for every shared library in
										\a directory (.so, under Windows .dll, under macOS
										.dylib), in the order of their names, not in
										subdirectories. */

private:
			struct Entry {
				std::string		id;
				Factory			factory;
				std::shared_ptr<const ControlLook> look;
			};

			std::shared_ptr<const ControlLook> _Look(Entry& entry) const;

	mutable	std::mutex			fLock;
	mutable	std::vector<Entry>	fEntries;
			gfx::Caps			fCaps;
	// Not std::atomic<std::shared_ptr>: libc++ (Xcode's) does not have it.
	mutable	std::mutex			fCurrentLock;
			std::shared_ptr<const ControlLook> fCurrent;
			bool				fTellsWindows = false;
									// Default(): SetCurrent() sends kLookChanged.
};


} // namespace ngui
