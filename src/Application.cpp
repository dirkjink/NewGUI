/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Application.h>

#include <mutex>

#include <ngui/AppDefs.h>


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
	std::scoped_lock lock(sLock);
	if (!sExists) {
		sExists = true;
		sMessenger = BMessenger(this);
		fRegistered = true;
	}
}


BApplication::~BApplication()
{
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
		PlatformEvents events(*fPlatform);
		_RunOnThisThread(&events);
		std::scoped_lock lock(sLock);
		sPlatform = nullptr;
	}
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
	return true;
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


platform::Platform*
platform::Current()
{
	std::scoped_lock lock(sLock);
	return sPlatform;
}


} // namespace ngui
