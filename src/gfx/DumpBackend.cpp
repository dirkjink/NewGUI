/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/DumpBackend.h>

#include <cmath>
#include <format>
#include <memory>
#include <utility>

#include <ngui/gfx/Dump.h>
#include <ngui/gfx/ImageRegistry.h>

#include "DumpNumber.h"


namespace ngui::gfx {


DumpSurface::DumpSurface(Size device, float scale)
	:
	fDevice(device),
	fScale(scale)
{
}


Size
DumpSurface::DeviceSize() const
{
	return fDevice;
}


float
DumpSurface::Scale() const
{
	return fScale;
}


SurfaceKind
DumpSurface::Kind() const
{
	return SurfaceKind::CpuRaster;
}


std::optional<CpuPixels>
DumpSurface::Pixels() const
{
	return std::nullopt;
}


const std::string&
DumpSurface::Text() const
{
	return fText;
}


DumpBackend::~DumpBackend()
{
	ImageRegistry::Default().DetachBackend(*this);
}


Caps
DumpBackend::Capabilities() const
{
	return Caps{};
}


ImageHandle
DumpBackend::CreateImage(const PixelData& pixels)
{
	if (!pixels.IsValid())
		return 0;
	return ++fLastImage;
}


void
DumpBackend::ReleaseImage(ImageHandle)
{
}


SurfaceRef
DumpBackend::CreateWindowSurface(const NativeWindow&, SurfaceKind)
{
	return nullptr;
}


SurfaceRef
DumpBackend::CreateOffscreenSurface(Size device, float scale)
{
	if (device.IsEmpty() || !device.IsFinite() || !(scale > 0) || !std::isfinite(scale))
		return nullptr;
	return std::make_shared<DumpSurface>(device, scale);
}


void
DumpBackend::ResizeSurface(Surface& surface, Size device, float scale)
{
	auto* dump = dynamic_cast<DumpSurface*>(&surface);
	if (dump == nullptr || device.IsEmpty() || !device.IsFinite() || !(scale > 0)
		|| !std::isfinite(scale)) {
		return;
	}
	dump->fDevice = device;
	dump->fScale = scale;
}


void
DumpBackend::BeginFrame(Surface& surface, const Color& clear)
{
	fCurrent = dynamic_cast<DumpSurface*>(&surface);
	if (fCurrent == nullptr)
		return;
	fCurrent->fFrame = std::format("frame device=[{} {}] scale={} clear=({} {} {} {})\n",
		DumpNumber(fCurrent->fDevice.w), DumpNumber(fCurrent->fDevice.h),
		DumpNumber(fCurrent->fScale),
		DumpNumber(clear.r), DumpNumber(clear.g), DumpNumber(clear.b), DumpNumber(clear.a));
}


void
DumpBackend::Render(const DisplayList& list, Point offset)
{
	if (fCurrent == nullptr)
		return;
	fCurrent->fFrame += std::format("render offset=({} {})\n", DumpNumber(offset.x),
		DumpNumber(offset.y));
	fCurrent->fFrame += DumpText(list);
}


void
DumpBackend::EndFrame(Surface& surface)
{
	if (fCurrent == nullptr || fCurrent != &surface)
		return;
	fCurrent->fText = std::move(fCurrent->fFrame);
	fCurrent->fFrame.clear();
	fCurrent = nullptr;
}


} // namespace ngui::gfx
