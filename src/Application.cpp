/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Application.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ngui/AppDefs.h>
#include <ngui/Cursor.h>
#include <ngui/LocaleRoster.h>
#include <ngui/LookRegistry.h>
#include <ngui/PropertyInfo.h>
#include <ngui/Recording.h>
#include <ngui/RemoteControl.h>
#include <ngui/Window.h>
#include <ngui/platform/PlatformRegistry.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "record/Recorder.h"
#include "CursorState.h"
#include "Environment.h"
#include "LooperPort.h"
#include "remote/RemoteServer.h"
#include "ScriptingSupport.h"
#include "WindowList.h"


namespace ngui {


namespace {


// The one application of the process, as its messenger; the object itself
// is not reachable from other threads.
std::mutex sLock;
bool sExists = false;
BMessenger sMessenger;
platform::Platform* sPlatform = nullptr;


bool
IsApplicationSignature(std::string_view signature)
{
	constexpr std::string_view kPrefix = "application/";
	return signature.size() > kPrefix.size() && signature.starts_with(kPrefix);
}


} // namespace


// The platform as event source of the application's loop.
class BApplication::PlatformEvents : public BLooper::EventSource {
public:
	explicit PlatformEvents(platform::Platform& platform)
		:
		fPlatform(platform)
	{
	}

	void ProcessEvents() override
	{
		fPlatform.ProcessEvents();
	}

	void WaitForEvents() override
	{
		fPlatform.WaitForEvents();
	}

	void Wake() override
	{
		fPlatform.Wake();
	}

private:
	platform::Platform& fPlatform;
};


BApplication::BApplication(std::string_view signature, int argc, char** argv)
	:
	BLooper("application"),
	fSignature(signature),
	fValidSignature(IsApplicationSignature(signature))
{
	for (int i = 1; i < argc && argv != nullptr; i++) {
		if (argv[i] != nullptr)
			fArguments.emplace_back(argv[i]);
	}
	{
		std::scoped_lock lock(sLock);
		if (!sExists) {
			sExists = true;
			sMessenger = BMessenger(this);
			fRegistered = true;
		}
	}
	// Look plugins from the directories in NGUI_LOOK_PATH (specification
	// 18.6), before the look is chosen, so that the setting may name one;
	// separated as PATH is (";" under Windows, ":" elsewhere).
	if (const std::optional<std::string> path = detail::EnvironmentVariable(
			"NGUI_LOOK_PATH"); path.has_value() && !path->empty()) {
#if defined(_WIN32)
		constexpr char kSeparator = ';';
#else
		constexpr char kSeparator = ':';
#endif
		std::size_t start = 0;
		while (start <= path->size()) {
			const std::size_t end = std::min(path->find(kSeparator, start), path->size());
			if (end > start) {
				const LookPluginReport report = LookRegistry::Default().LoadPlugins(
					std::filesystem::path(path->substr(start, end - start)));
				for (const std::string& warning : report.warnings)
					std::fprintf(stderr, "ngui: look plugin: %s\n", warning.c_str());
			}
			start = end + 1;
		}
	}
	// The look of the user's setting ngui.controlLook (specification 18.4),
	// until there are settings the environment variable NGUI_CONTROL_LOOK;
	// a look that does not fit the backend falls back to "be" (L9).
	if (const std::optional<std::string> look = detail::EnvironmentVariable(
			"NGUI_CONTROL_LOOK"); look.has_value() && !look->empty()) {
		const auto selection = LookRegistry::Default().SelectAtStart(*look);
		if (selection.has_value() && !selection->warning.empty())
			std::fprintf(stderr, "ngui: %s\n", selection->warning.c_str());
	}
	// Remote control only when asked (rule T4): here by the environment.
	if (detail::EnvironmentVariable("NGUI_REMOTE_CONTROL").value_or("") == "1") {
		if (const auto path = EnableRemoteControl(); !path.has_value())
			std::fprintf(stderr, "ngui: remote control: %s\n", path.error().message.c_str());
	}
	// Recording only when asked (rule T5): here by the environment, which
	// whoever sets it agrees to.
	if (const std::optional<std::string> file = detail::EnvironmentVariable("NGUI_RECORD");
			file.has_value() && !file->empty()) {
		if (const auto started = StartRecording(*file); !started.has_value())
			std::fprintf(stderr, "ngui: recording: %s\n", started.error().message.c_str());
	}
}


BApplication::~BApplication()
{
	// no requests any more, before the looper goes
	fRemote.reset();
	StopRecording();
	fPulseRunner.reset();
	std::scoped_lock lock(sLock);
	if (fRegistered) {
		sExists = false;
		sMessenger = BMessenger();
	}
}


std::expected<void, Error>
BApplication::UsePlatform(std::unique_ptr<platform::Platform> platform)
{
	if (platform == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no platform");
	if (Thread() != std::thread::id())
		return Failure(ErrorCode::InvalidArgument, "the application runs already");
	fPlatform = std::move(platform);
	return {};
}


std::expected<void, Error>
BApplication::UsePlatform(std::string_view name)
{
	auto platform = platform::PlatformRegistry::Default().CreatePreferred(name);
	if (!platform.has_value())
		return std::unexpected(platform.error());
	return UsePlatform(std::move(*platform));
}


platform::Platform*
BApplication::Platform() const
{
	return fPlatform.get();
}


std::expected<void, Error>
BApplication::InitCheck() const
{
	if (!fValidSignature) {
		return Failure(ErrorCode::InvalidArgument,
			"not an application signature: \"" + fSignature + "\"");
	}
	if (!fRegistered)
		return Failure(ErrorCode::InvalidArgument, "there is another application");
	return {};
}


int
BApplication::Run()
{
	if (!InitCheck().has_value())
		return 1;
	if (!fArguments.empty()) {
		BMessage arguments(kArgvReceived);
		(void)arguments.AddInt32("argc", static_cast<std::int32_t>(fArguments.size() + 1));
		for (const std::string& argument : fArguments)
			(void)arguments.AddString("argv", argument);
		(void)PostMessage(arguments, this);
	}
	(void)PostMessage(BMessage(kReadyToRun), this);
	if (fPlatform == nullptr) {
		_RunOnThisThread();
	} else {
		{
			std::scoped_lock lock(sLock);
			sPlatform = fPlatform.get();
		}
		// the process's cursor on the new platform
		detail::CursorState::Default().PlatformChanged();
		PlatformEvents events(*fPlatform);
		_RunOnThisThread(&events);
	}
	// The windows go before the application, and before its platform:
	// quit without asking, as Haiku's _QuitAllWindows(true).
	for (const BMessenger& window : Windows()) {
		if (window.SendMessage(kQuitNow).has_value())
			(void)window.WaitForLooperQuit();
	}
	{
		std::scoped_lock lock(sLock);
		sPlatform = nullptr;
	}
	detail::CursorState::Default().PlatformChanged();
	fPulseRunner.reset();
	return 0;
}


void
BApplication::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (handler != this) {
		BLooper::DispatchMessage(message, handler);
		return;
	}
	switch (message->what) {
		case kArgvReceived:
		{
			std::vector<std::string> arguments;
			for (std::int32_t i = 0; message->HasString("argv", i); i++)
				arguments.emplace_back(*message->FindString("argv", i));
			ArgvReceived(arguments);
			break;
		}
		case kReadyToRun:
			ReadyToRun();
			fLaunching = false;
			// The pulse starts once the application has launched.
			SetPulseRate(fPulseRate);
			break;
		case kPulse:
			Pulse();
			break;
		case kAboutRequested:
			AboutRequested();
			break;
		default:
			BLooper::DispatchMessage(message, handler);
	}
}


void
BApplication::ArgvReceived(std::span<const std::string>)
{
}


void
BApplication::ReadyToRun()
{
}


void
BApplication::Pulse()
{
}


void
BApplication::AboutRequested()
{
}


bool
BApplication::QuitRequested()
{
	// As Haiku's _WindowQuitLoop(): a window that agrees quits on its own
	// (BLooper::DispatchMessage()), so the answer is all that is needed.
	for (const BMessenger& window : Windows()) {
		BMessage reply;
		if (!window.SendMessage(BMessage(kQuitRequested), &reply).has_value())
			continue;
		if (reply.what == kReply && !reply.GetBool("result", true))
			return false;
	}
	return true;
}


std::int32_t
BApplication::CountWindows() const
{
	return static_cast<std::int32_t>(Windows().size());
}


std::vector<BMessenger>
BApplication::Windows() const
{
	return BWindow::_Windows();
}


void
BApplication::SetPulseRate(Clock::Time rate)
{
	fPulseRate = rate > Clock::Time::zero() ? rate : Clock::Time::zero();
	fPulseRunner.reset();
	if (fPulseRate > Clock::Time::zero() && !IsLaunching()) {
		fPulseRunner = std::make_unique<BMessageRunner>(BMessenger(this), BMessage(kPulse),
			fPulseRate);
	}
}


Clock::Time
BApplication::PulseRate() const
{
	return fPulseRate;
}


bool
BApplication::IsLaunching() const
{
	return fLaunching;
}


const std::string&
BApplication::Signature() const
{
	return fSignature;
}


BMessenger
AppMessenger()
{
	std::scoped_lock lock(sLock);
	return sMessenger;
}



void
platform::PlatformWindow::Activate()
{
}


void
platform::PlatformWindow::SetTextInputArea(gfx::Rect /*rect*/, int32 /*cursor*/)
{
}


void
platform::PlatformWindow::ClearComposition()
{
}


bool
platform::PlatformWindow::IsAccessibilityActive() const
{
	return false;
}


bool
platform::PlatformWindow::DrawsLists() const
{
	return false;
}


std::expected<void, Error>
platform::PlatformWindow::PresentList(gfx::DisplayListRef /*frame*/,
	const gfx::Color& /*clear*/)
{
	return Failure(ErrorCode::NotSupported, "the platform does not render lists");
}


std::expected<platform::ShownFrame, Error>
platform::PlatformWindow::ReadShownFrame()
{
	return Failure(ErrorCode::NotSupported, "the platform does not render lists");
}


bool
platform::PlatformWindow::CanDragToSystem() const
{
	return false;
}


std::expected<void, Error>
platform::PlatformWindow::StartSystemDrag(std::vector<DragItem> /*items*/)
{
	return Failure(ErrorCode::NotSupported, "the platform has no drags to other applications");
}


bool
platform::PlatformWindow::HasSystemMenuBar() const
{
	return false;
}


void
platform::PlatformWindow::SetSystemMenuBar(std::vector<SystemMenuItem> /*menus*/)
{
}


void
platform::PlatformWindow::UpdateAccessibility(const AccessibleTree& /*tree*/)
{
}


void
BApplication::SetCursor(const BCursor& cursor)
{
	if (cursor.InitCheck().has_value())
		detail::CursorState::Default().SetAppCursor(cursor.Shape());
}


void
BApplication::ShowCursor()
{
	detail::CursorState::Default().Show();
}


void
BApplication::HideCursor()
{
	detail::CursorState::Default().Hide();
}


void
BApplication::ObscureCursor()
{
	detail::CursorState::Default().Obscure();
}


bool
BApplication::IsCursorHidden()
{
	return detail::CursorState::Default().IsHidden();
}


bool
platform::CursorShape::operator==(const CursorShape& other) const
{
	if (image != nullptr || other.image != nullptr) {
		return image != nullptr && other.image != nullptr
			&& (image == other.image || *image == *other.image);
	}
	return system == other.system;
}


void
platform::Platform::SetCursor(const CursorShape& /*cursor*/)
{
}


void
platform::Platform::SetCursorVisible(bool /*visible*/)
{
}


std::expected<void, Error>
platform::Platform::ShowFileDialog(const FileDialogSpec& /*spec*/,
	std::function<void(FileDialogResult)> /*done*/)
{
	return Failure(ErrorCode::NotSupported, "no file dialogs");
}


std::expected<void, Error>
platform::Platform::SetClipboard(const ClipboardData& /*data*/)
{
	return Failure(ErrorCode::NotSupported, "no clipboard");
}


std::expected<platform::ClipboardData, Error>
platform::Platform::Clipboard() const
{
	return Failure(ErrorCode::NotSupported, "no clipboard");
}

platform::Platform*
platform::Current()
{
	std::scoped_lock lock(sLock);
	return sPlatform;
}


// #pragma mark - Scripting


namespace {


// Ported from Haiku: src/kits/app/Application.cpp (commit b1d497bb69),
// sPropertyInfo without the loopers and the messenger lists, which cannot
// be flattened for remote control.
enum : std::uint32_t {
	kWindowByIndex = 1,
	kWindowByName,
	kApplicationItself
};

const std::vector<BPropertyInfoEntry> kApplicationProperties = {
	{"Window", {}, {kIndexSpecifier, kReverseIndexSpecifier},
		"A window by index; leads on to its properties.", kWindowByIndex, {}},
	{"Window", {}, {kNameSpecifier}, "A window by title; leads on to its properties.",
		kWindowByName, {}},
	{"Name", {kGetProperty}, {kDirectSpecifier}, "The application's signature.",
		kApplicationItself, {BTypeCode::String}},
	{"Window", {kCountProperties}, {kDirectSpecifier}, "How many windows it has.",
		kApplicationItself, {BTypeCode::Int32}},
};


} // namespace


void
BApplication::MessageReceived(BMessage* message)
{
	if (message->HasSpecifiers()) {
		const auto specifier = message->GetCurrentSpecifier();
		if (specifier.has_value()) {
			const std::int32_t match = BPropertyInfo(kApplicationProperties).FindMatch(*message,
				specifier->index, specifier->form, specifier->property);
			BMessage reply(kReply);
			// the result first: the reply is passed by value, and compilers
			// evaluate arguments in different orders
			if (match == 2) {
				const auto added = reply.AddString("result", fSignature);
				detail::SendScriptingReply(*message, std::move(reply), added);
				return;
			}
			if (match == 3) {
				const auto added = reply.AddInt32("result", CountWindows());
				detail::SendScriptingReply(*message, std::move(reply), added);
				return;
			}
		}
	}
	BLooper::MessageReceived(message);
}


BHandler*
BApplication::ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& specifier,
	std::uint32_t form, std::string_view property)
{
	std::uint32_t data = 0;
	if (BPropertyInfo(kApplicationProperties).FindMatch(*message, 0, form, property, &data) < 0)
		return BLooper::ResolveSpecifier(message, index, specifier, form, property);
	if (data == kApplicationItself)
		return this;

	// The window is another looper: the message goes there itself, with
	// its reply still owed to the sender.
	const std::vector<detail::WindowEntry> windows = detail::WindowEntries();
	std::optional<BMessenger> window;
	if (data == kWindowByIndex) {
		const std::int32_t at = detail::SpecifiedIndex(specifier, form,
			static_cast<std::int32_t>(windows.size()));
		if (at >= 0 && static_cast<std::size_t>(at) < windows.size())
			window = windows[static_cast<std::size_t>(at)].messenger;
	} else {
		const std::string_view title = specifier.GetString("name", "");
		for (const detail::WindowEntry& entry : windows) {
			if (entry.title == title) {
				window = entry.messenger;
				break;
			}
		}
	}
	if (!window.has_value()) {
		ReplyScriptingError(*message, data == kWindowByIndex
			? Error{ErrorCode::OutOfRange, "window index out of range"}
			: Error{ErrorCode::NotFound, "no window \"" + std::string(specifier.GetString("name",
				"")) + "\""});
		return nullptr;
	}
	(void)message->PopSpecifier();
	std::unique_ptr<BMessage> forwarded = DetachCurrentMessage();
	if (forwarded != nullptr)
		(void)window->ForwardMessage(std::move(forwarded));
	return nullptr;
}


std::expected<void, Error>
BApplication::GetSupportedSuites(BMessage* data)
{
	if (data == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message for the suites");
	if (auto added = data->AddString("suites", "suite/vnd.Be-application"); !added)
		return added;
	if (auto added = data->AddMessage("messages",
			BPropertyInfo(kApplicationProperties).ToMessage()); !added) {
		return added;
	}
	return BLooper::GetSupportedSuites(data);
}


// #pragma mark - Remote control


std::expected<std::filesystem::path, Error>
BApplication::EnableRemoteControl(const std::filesystem::path& directory)
{
	std::filesystem::path where = directory;
	if (where.empty()) {
		auto found = remote::DefaultDirectory();
		if (!found.has_value())
			return std::unexpected(found.error());
		where = std::move(*found);
	}
	fRemote.reset();
#ifdef _WIN32
	const std::int64_t process = static_cast<std::int64_t>(_getpid());
#else
	const std::int64_t process = static_cast<std::int64_t>(getpid());
#endif
	auto server = remote::detail::RemoteServer::Start(remote::EndpointPath(where, fSignature,
		process), BMessenger(this));
	if (!server.has_value())
		return std::unexpected(server.error());
	fRemote = std::move(*server);
	return fRemote->Path();
}


void
BApplication::DisableRemoteControl()
{
	fRemote.reset();
}


std::filesystem::path
BApplication::RemoteControlPath() const
{
	return fRemote != nullptr ? fRemote->Path() : std::filesystem::path();
}


std::expected<void, Error>
BApplication::StartRecording(const std::filesystem::path& file)
{
	RecordingStart start;
	start.signature = fSignature;
	if (const auto look = LookRegistry::DefaultLook())
		start.look = std::string(look->Info().id);
	start.languages = BLocaleRoster::Default()->PreferredLanguages();
	for (const detail::WindowEntry& window : detail::WindowEntries())
		start.windows.push_back({window.title});
	if (auto started = detail::StartRecorder(file, start); !started.has_value())
		return started;
	fRecording = true;
	return {};
}


void
BApplication::StopRecording()
{
	if (!fRecording)
		return;
	detail::StopRecorder();
	fRecording = false;
}


bool
BApplication::IsRecording() const
{
	return fRecording;
}


} // namespace ngui
