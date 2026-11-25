/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/PlatformRegistry.h>

#include <algorithm>
#include <optional>

#include <ngui/platform/HeadlessPlatform.h>

#include "../Environment.h"


namespace ngui::platform {


PlatformRegistry&
PlatformRegistry::Default()
{
	static PlatformRegistry* const registry = [] {
		auto* defaults = new PlatformRegistry();
		// only by name: a program without a display must not run unseen
		(void)defaults->Register("headless", -1,
			[]() -> std::expected<std::unique_ptr<Platform>, Error> {
				return std::make_unique<HeadlessPlatform>();
			});
		return defaults;
	}();
	return *registry;
}


bool
PlatformRegistry::Register(std::string name, int priority, Factory factory)
{
	if (name.empty() || !factory)
		return false;
	std::scoped_lock lock(fLock);
	if (std::ranges::any_of(fEntries, [&](const Entry& entry) { return entry.name == name; }))
		return false;
	fEntries.push_back({std::move(name), priority, std::move(factory)});
	std::ranges::stable_sort(fEntries, std::ranges::greater(), &Entry::priority);
	return true;
}


std::vector<std::string>
PlatformRegistry::Names() const
{
	std::scoped_lock lock(fLock);
	std::vector<std::string> names;
	for (const Entry& entry : fEntries)
		names.push_back(entry.name);
	return names;
}


std::expected<std::unique_ptr<Platform>, Error>
PlatformRegistry::Create(std::string_view name) const
{
	Factory factory;
	{
		std::scoped_lock lock(fLock);
		const auto found = std::ranges::find(fEntries, name, &Entry::name);
		if (found == fEntries.end())
			return Failure(ErrorCode::NotFound, "no such platform: " + std::string(name));
		factory = found->factory;
	}
	// not under the lock: a factory may take its time
	return factory();
}


std::expected<std::unique_ptr<Platform>, Error>
PlatformRegistry::CreatePreferred(std::string_view preferred) const
{
	if (!preferred.empty())
		return Create(preferred);
	if (const std::optional<std::string> named = detail::EnvironmentVariable("NGUI_PLATFORM");
			named.has_value() && !named->empty()) {
		return Create(*named);
	}
	std::vector<std::string> candidates;
	{
		std::scoped_lock lock(fLock);
		for (const Entry& entry : fEntries) {
			if (entry.priority >= 0)
				candidates.push_back(entry.name);
		}
	}
	std::optional<Error> last;
	for (const std::string& name : candidates) {
		auto platform = Create(name);
		if (platform.has_value())
			return platform;
		last = platform.error();
	}
	return Failure(ErrorCode::NotFound,
		last.has_value() ? last->message : std::string("no platform registered"));
}


} // namespace ngui::platform
