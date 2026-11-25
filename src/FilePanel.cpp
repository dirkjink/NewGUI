/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Haiku's BFilePanel (headers/os/storage/FilePanel.h, the Tracker's
// TFilePanel, commit b1d497bb69) over the system's file dialog
// (Platform::ShowFileDialog()). The messages are TFilePanel's: the choice,
// then B_CANCEL with "old_what" and "source" as the panel closes
// (TFilePanel::QuitRequested(), after a choice when it hides when done).
// See FilePanel.h for the differences.

#include <ngui/FilePanel.h>

#include <filesystem>

#include <ngui/AppDefs.h>
#include <ngui/Window.h>
#include <ngui/platform/Platform.h>


namespace ngui {


// Between the panel and its dialog's callback, which may outlive the panel.
struct BFilePanel::Shared {
	std::atomic<bool>	dropped{false};
	std::atomic<bool>	done{false};
};


namespace {


// \a path as std::filesystem::path, read as UTF-8 on every system.
std::filesystem::path
PathOf(std::string_view path)
{
	return std::filesystem::path(std::u8string(path.begin(), path.end()));
}


std::string
Utf8(const std::filesystem::path& path)
{
	const std::u8string text = path.u8string();
	return std::string(text.begin(), text.end());
}


} // namespace


BFilePanel::BFilePanel(BFilePanelMode mode, BMessenger target, std::string_view directory,
	BNodeFlavors nodeFlavors, bool allowMultipleSelection, std::unique_ptr<BMessage> message,
	bool modal, bool hideWhenDone)
	:
	fMode(mode),
	fTarget(std::move(target)),
	fDirectory(directory),
	fFlavors(nodeFlavors),
	fMultiple(allowMultipleSelection),
	fMessage(std::move(message)),
	fModal(modal),
	fHideWhenDone(hideWhenDone)
{
}


BFilePanel::~BFilePanel() = default;


std::expected<void, Error>
BFilePanel::Show()
{
	std::scoped_lock lock(fLock);
	if (fShowing != nullptr && !fShowing->done)
		return {};

	platform::Platform* platform = platform::Current();
	if (platform == nullptr)
		return Failure(ErrorCode::NotSupported, "no platform");

	platform::FileDialogSpec spec;
	if (fMode == BFilePanelMode::Save)
		spec.kind = platform::FileDialogSpec::Kind::Save;
	else if (fFlavors == BNodeFlavors(BNodeFlavor::Directory))
		spec.kind = platform::FileDialogSpec::Kind::Folder;
	spec.title = fTitle;
	spec.acceptLabel = fAcceptLabel;
	spec.cancelLabel = fCancelLabel;
	spec.location = fDirectory;
	if (fMode == BFilePanelMode::Save && !fSaveText.empty()) {
		spec.location = fDirectory.empty() ? fSaveText
			: Utf8(PathOf(fDirectory) / PathOf(fSaveText));
	}
	spec.filters = fFilters;
	spec.many = fMode == BFilePanelMode::Open && fMultiple;
	// Haiku's modal panel: modal to the window whose thread shows it
	if (fModal) {
		if (BWindow* window = BWindow::_DispatchingWindow())
			spec.parent = window->fPlatformWindow.get();
	}

	BMessage message = fMessage != nullptr ? *fMessage
		: BMessage(fMode == BFilePanelMode::Save ? kSaveRequested : kRefsReceived);
	auto shared = std::make_shared<Shared>();
	const BMessenger target = fTarget;
	const BFilePanelMode mode = fMode;
	const void* source = this;
	auto done = [shared, target, message, mode, source](platform::FileDialogResult result) {
		const bool dropped = shared->dropped;
		shared->done = true;
		if (dropped || !target.IsValid())
			return;

		if (result.has_value() && !result->empty()) {
			BMessage chosen = message;
			if (mode == BFilePanelMode::Save) {
				const std::filesystem::path path = PathOf(result->front());
				(void)chosen.AddString("directory", Utf8(path.parent_path()));
				(void)chosen.AddString("name", Utf8(path.filename()));
			} else {
				for (const std::string& path : *result)
					(void)chosen.AddString("paths", path);
			}
			(void)target.SendMessage(chosen);
		}

		// TFilePanel::QuitRequested(): B_CANCEL as the panel closes
		BMessage cancel = message;
		cancel.what = kCancel;
		(void)cancel.AddInt32("old_what", static_cast<std::int32_t>(message.what));
		(void)cancel.AddPointer("source", source);
		if (!result.has_value())
			(void)cancel.AddString("error", result.error().message);
		(void)target.SendMessage(cancel);
	};

	fShowing = shared;
	if (auto shown = platform->ShowFileDialog(spec, std::move(done)); !shown.has_value()) {
		fShowing.reset();
		return shown;
	}
	return {};
}


void
BFilePanel::Hide()
{
	std::scoped_lock lock(fLock);
	if (fShowing != nullptr) {
		fShowing->dropped = true;
		fShowing.reset();
	}
}


bool
BFilePanel::IsShowing() const
{
	std::scoped_lock lock(fLock);
	return fShowing != nullptr && !fShowing->done;
}


BFilePanelMode
BFilePanel::PanelMode() const
{
	return fMode;
}


void
BFilePanel::SetTarget(BMessenger target)
{
	std::scoped_lock lock(fLock);
	fTarget = std::move(target);
}


void
BFilePanel::SetMessage(std::unique_ptr<BMessage> message)
{
	std::scoped_lock lock(fLock);
	fMessage = std::move(message);
}


void
BFilePanel::SetSaveText(std::string_view text)
{
	std::scoped_lock lock(fLock);
	fSaveText = text;
}


void
BFilePanel::SetButtonLabel(BFilePanelButton button, std::string_view label)
{
	std::scoped_lock lock(fLock);
	(button == BFilePanelButton::Default ? fAcceptLabel : fCancelLabel) = label;
}


void
BFilePanel::SetNodeFlavors(BNodeFlavors flavors)
{
	std::scoped_lock lock(fLock);
	fFlavors = flavors;
}


void
BFilePanel::SetPanelDirectory(std::string_view directory)
{
	std::scoped_lock lock(fLock);
	fDirectory = directory;
}


std::string
BFilePanel::PanelDirectory() const
{
	std::scoped_lock lock(fLock);
	return fDirectory;
}


void
BFilePanel::SetHideWhenDone(bool hideWhenDone)
{
	std::scoped_lock lock(fLock);
	fHideWhenDone = hideWhenDone;
}


bool
BFilePanel::HidesWhenDone() const
{
	std::scoped_lock lock(fLock);
	return fHideWhenDone;
}


void
BFilePanel::SetTitle(std::string_view title)
{
	std::scoped_lock lock(fLock);
	fTitle = title;
}


void
BFilePanel::AddFilter(std::string_view name, std::string_view patterns)
{
	std::scoped_lock lock(fLock);
	fFilters.emplace_back(name, patterns);
}


void
BFilePanel::ClearFilters()
{
	std::scoped_lock lock(fLock);
	fFilters.clear();
}


} // namespace ngui
