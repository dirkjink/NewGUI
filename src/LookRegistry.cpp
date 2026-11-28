/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/LookRegistry.h>

#include <algorithm>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/BeLook.h>
#include <ngui/FlatLook.h>
#include <ngui/HaikuLook.h>


#include "WindowList.h"

namespace ngui {


bool
IsCompatible(const LookInfo& info, const gfx::Caps& caps)
{
	if (!caps.core)
		return false;
	if (info.required != ConformanceLevel::Extended)
		return true;
	const gfx::Caps& need = info.extendedCaps;
	const auto covered = [](bool needed, bool offered) { return !needed || offered; };
	return covered(need.gpuSurfaces, caps.gpuSurfaces)
		&& covered(need.affineTransform, caps.affineTransform)
		&& covered(need.pathClip, caps.pathClip)
		&& covered(need.bezierPaths, caps.bezierPaths)
		&& covered(need.concavePolygons, caps.concavePolygons)
		&& covered(need.radialGradient, caps.radialGradient)
		&& covered(need.conicGradient, caps.conicGradient)
		&& covered(need.blendModes, caps.blendModes)
		&& covered(need.offscreenTargets, caps.offscreenTargets)
		&& covered(need.blurFilter, caps.blurFilter)
		&& covered(need.linearBlending, caps.linearBlending);
}


LookRegistry&
LookRegistry::Default()
{
	static LookRegistry registry;
	static const bool initialized = [] {
		// The built-in looks (18.6).
		registry.Register(std::string(kFallbackId), CreateBeLook);
		registry.Register("haiku", CreateHaikuLook);
		registry.Register("flat", CreateFlatLook);
		const bool selected = registry.SetCurrent(kFallbackId).has_value();
		// from now on, changes reach the windows
		registry.fTellsWindows = true;
		return selected;
	}();
	(void)initialized;
	return registry;
}


bool
LookRegistry::Register(std::string id, Factory factory)
{
	if (id.empty() || !factory)
		return false;
	std::lock_guard lock(fLock);
	const bool taken = std::any_of(fEntries.begin(), fEntries.end(),
		[&](const Entry& entry) { return entry.id == id; });
	if (taken)
		return false;
	fEntries.push_back({std::move(id), std::move(factory), nullptr});
	return true;
}


std::vector<std::string>
LookRegistry::Ids() const
{
	std::lock_guard lock(fLock);
	std::vector<std::string> ids;
	for (const Entry& entry : fEntries)
		ids.push_back(entry.id);
	return ids;
}


void
LookRegistry::SetBackendCaps(const gfx::Caps& caps)
{
	std::lock_guard lock(fLock);
	fCaps = caps;
}


gfx::Caps
LookRegistry::BackendCaps() const
{
	std::lock_guard lock(fLock);
	return fCaps;
}


std::shared_ptr<const ControlLook>
LookRegistry::_Look(Entry& entry) const
{
	// Called with fLock held. A look whose Info().id is not its registered
	// id is refused, so that Available() and Current() agree.
	if (entry.look == nullptr) {
		std::shared_ptr<const ControlLook> look = entry.factory();
		if (look == nullptr || look->Info().id != entry.id)
			return nullptr;
		entry.look = std::move(look);
	}
	return entry.look;
}


std::vector<LookInfo>
LookRegistry::Available() const
{
	return Available(BackendCaps());
}


std::vector<LookInfo>
LookRegistry::Available(const gfx::Caps& caps) const
{
	std::lock_guard lock(fLock);
	std::vector<LookInfo> available;
	for (Entry& entry : fEntries) {
		const std::shared_ptr<const ControlLook> look = _Look(entry);
		if (look != nullptr && IsCompatible(look->Info(), caps))
			available.push_back(look->Info());
	}
	return available;
}


std::shared_ptr<const ControlLook>
LookRegistry::Current() const
{
	const std::scoped_lock lock(fCurrentLock);
	return fCurrent;
}


std::shared_ptr<const ControlLook>
LookRegistry::DefaultLook()
{
	static const std::shared_ptr<const ControlLook> kFallback = CreateBeLook();
	std::shared_ptr<const ControlLook> current = Default().Current();
	return current ? current : kFallback;
}


std::expected<std::shared_ptr<const ControlLook>, Error>
LookRegistry::Find(std::string_view id, const gfx::Caps& caps) const
{
	std::lock_guard lock(fLock);
	const auto entry = std::find_if(fEntries.begin(), fEntries.end(),
		[&](const Entry& candidate) { return candidate.id == id; });
	if (entry == fEntries.end())
		return Failure(ErrorCode::NotFound, "no look \"" + std::string(id) + "\"");
	std::shared_ptr<const ControlLook> look = _Look(*entry);
	if (look == nullptr) {
		return Failure(ErrorCode::NotSupported,
			"the look \"" + std::string(id) + "\" cannot be made");
	}
	if (!IsCompatible(look->Info(), caps)) {
		return Failure(ErrorCode::NotSupported, "the look \"" + std::string(id)
			+ "\" needs capabilities the backend does not have");
	}
	return look;
}


std::expected<void, Error>
LookRegistry::SetCurrent(std::string_view id)
{
	auto look = Find(id, BackendCaps());
	if (!look.has_value())
		return std::unexpected(look.error());
	std::shared_ptr<const ControlLook> previous;
	bool changed = false;
	{
		const std::scoped_lock lock(fCurrentLock);
		previous = std::exchange(fCurrent, std::move(*look));
		changed = previous != fCurrent;
	}
	// 18.5, step 3: the windows lay out and draw anew (L11)
	if (fTellsWindows && changed)
		detail::PostToWindows(kLookChanged);
	return {};
}


std::expected<LookSelection, Error>
LookRegistry::SelectAtStart(std::string_view setting)
{
	const std::string_view wanted = setting.empty() ? kFallbackId : setting;
	const std::expected<void, Error> selected = SetCurrent(wanted);
	if (selected.has_value())
		return LookSelection{std::string(wanted), {}};

	const std::expected<void, Error> fallback = SetCurrent(kFallbackId);
	if (!fallback.has_value())
		return std::unexpected(fallback.error());
	return LookSelection{std::string(kFallbackId), std::string(kSettingName) + " = \""
		+ std::string(wanted) + "\": " + selected.error().message + "; using \""
		+ std::string(kFallbackId) + "\""};
}


} // namespace ngui
