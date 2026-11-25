/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/BackendRegistry.h>

#include <algorithm>
#include <string>
#include <utility>

#include <ngui/gfx/DumpBackend.h>

#include "Environment.h"


namespace ngui::gfx {


BackendRegistry&
BackendRegistry::Default()
{
	// Never destroyed, like ImageRegistry::Default().
	static BackendRegistry* const sRegistry = [] {
		auto* registry = new BackendRegistry();
		registry->Register("dump", 0, [] { return std::make_unique<DumpBackend>(); });
		return registry;
	}();
	return *sRegistry;
}


bool
BackendRegistry::Register(std::string name, int priority, Factory factory)
{
	if (name.empty() || !factory)
		return false;
	const std::scoped_lock lock(fLock);
	if (std::ranges::find(fEntries, name, &Entry::name) != fEntries.end())
		return false;
	fEntries.push_back({std::move(name), priority, std::move(factory)});
	// Highest priority first; equal priorities keep the order of
	// registration.
	std::ranges::stable_sort(fEntries, std::greater<>(), &Entry::priority);
	return true;
}


std::vector<std::string>
BackendRegistry::Names() const
{
	const std::scoped_lock lock(fLock);
	std::vector<std::string> names;
	for (const Entry& entry : fEntries)
		names.push_back(entry.name);
	return names;
}


std::expected<std::unique_ptr<Backend>, Error>
BackendRegistry::Create(std::string_view name) const
{
	Factory factory;
	{
		const std::scoped_lock lock(fLock);
		const auto found = std::ranges::find(fEntries, name, &Entry::name);
		if (found == fEntries.end())
			return Failure(ErrorCode::NotFound, "no backend named " + std::string(name));
		factory = found->factory;
	}
	// Outside the lock: a factory may take long (GPU setup) or register.
	std::unique_ptr<Backend> backend = factory();
	if (backend == nullptr)
		return Failure(ErrorCode::NotSupported, "backend " + std::string(name) + " failed");
	return backend;
}


std::expected<std::unique_ptr<Backend>, Error>
BackendRegistry::CreatePreferred(std::string_view preferred) const
{
	std::string name;
	// the user's choice, as NGUI_PLATFORM for the platform
	std::string wanted(preferred);
	if (wanted.empty())
		wanted = detail::EnvironmentVariable("NGUI_BACKEND").value_or("");
	{
		const std::scoped_lock lock(fLock);
		if (fEntries.empty())
			return Failure(ErrorCode::NotFound, "no backend registered");
		const auto found = std::ranges::find(fEntries, wanted, &Entry::name);
		name = found != fEntries.end() ? found->name : fEntries.front().name;
	}
	return Create(name);
}


} // namespace ngui::gfx
