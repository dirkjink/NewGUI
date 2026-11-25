/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <ngui/gfx/Backend.h>
#include <ngui/gfx/NativeWindow.h>


namespace ngui::test {


// A backend whose surfaces have pixel buffers but which draws nothing, so
// that a window's frames reach its platform window's presenter without
// Skia. Counts the lists it renders and records the fonts added to it in
// #addedFonts, if set (read it once the window has quit).
class PixelBackend : public gfx::Backend {
public:
	class PixelSurface : public gfx::Surface {
	public:
		PixelSurface(gfx::Size device, float scale)
			:
			fDevice{std::round(device.w), std::round(device.h)},
			fScale(scale),
			fBytes(static_cast<std::size_t>(fDevice.w * fDevice.h * 4))
		{
		}

		gfx::Size DeviceSize() const override
		{
			return fDevice;
		}

		float Scale() const override
		{
			return fScale;
		}

		gfx::SurfaceKind Kind() const override
		{
			return gfx::SurfaceKind::CpuRaster;
		}

		std::optional<gfx::CpuPixels> Pixels() const override
		{
			const auto width = static_cast<std::int32_t>(fDevice.w);
			const auto height = static_cast<std::int32_t>(fDevice.h);
			return gfx::CpuPixels{width, height, static_cast<std::size_t>(width) * 4,
				gfx::PixelFormat::Bgra8Premul, fBytes};
		}

	private:
		gfx::Size fDevice;
		float fScale;
		std::vector<std::byte> fBytes;
	};

	gfx::Caps Capabilities() const override
	{
		return {};
	}

	gfx::ImageHandle CreateImage(const gfx::PixelData&) override
	{
		return 0;
	}

	void ReleaseImage(gfx::ImageHandle) override
	{
	}

	gfx::SurfaceRef CreateWindowSurface(const gfx::NativeWindow& window,
		gfx::SurfaceKind kind) override
	{
		if (kind != gfx::SurfaceKind::CpuRaster)
			return nullptr;
		return std::make_shared<PixelSurface>(gfx::Size{window.size.w * window.scale,
			window.size.h * window.scale}, window.scale);
	}

	gfx::SurfaceRef CreateOffscreenSurface(gfx::Size device, float scale) override
	{
		return std::make_shared<PixelSurface>(device, scale);
	}

	void ResizeSurface(gfx::Surface&, gfx::Size, float) override
	{
	}

	void BeginFrame(gfx::Surface&, const gfx::Color&) override
	{
	}

	void Render(const gfx::DisplayList&, gfx::Point) override
	{
		renders++;
		// font 0 marks a frame among the added fonts
		if (addedFonts != nullptr && markRenders)
			addedFonts->push_back(0);
	}

	void EndFrame(gfx::Surface&) override
	{
	}

	bool AddFont(gfx::FontId font, std::span<const std::byte>, std::uint32_t = 0) override
	{
		if (addedFonts != nullptr)
			addedFonts->push_back(font);
		return true;
	}

	std::atomic<int> renders = 0;
	std::shared_ptr<std::vector<gfx::FontId>> addedFonts;
	bool markRenders = false;
};


} // namespace ngui::test
