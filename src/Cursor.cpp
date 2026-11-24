/*
 * Copyright 2006-2009, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/kits/app/Cursor.cpp (commit b1d497bb69), without
// the app_server: a cursor is a platform::CursorShape. The R5 cursor data
// are turned into an image as Haiku's ServerCursor did (a set color bit is
// black, a clear one white, the mask decides what is shown). CursorState
// takes the part of Haiku's ServerApp that decides which cursor shows.

#include <ngui/Cursor.h>

#include <algorithm>
#include <cmath>

#include <ngui/Bitmap.h>

#include "CursorState.h"


namespace ngui {


namespace {


constexpr std::size_t kR5CursorSize = 68;


} // namespace


BCursor::BCursor(BCursorID id)
{
	fShape.system = static_cast<std::uint8_t>(id);
}


BCursor::BCursor(const BBitmap& bitmap, gfx::Point hotspot)
{
	if (!bitmap.IsValid()) {
		fValid = false;
		return;
	}
	const gfx::Rect bounds = bitmap.Bounds();
	const auto width = static_cast<std::int32_t>(bounds.Width());
	const auto height = static_cast<std::int32_t>(bounds.Height());
	// the pixels as B_RGBA32 (B, G, R, A), then as R, G, B, A
	BBitmap rgba({0, 0, bounds.Width(), bounds.Height()}, BColorSpace::RGBA32);
	if (!rgba.IsValid() || !rgba.ImportBits(bitmap.Bits(), bitmap.BytesPerRow(), 0,
			bitmap.ColorSpace()).has_value()) {
		fValid = false;
		return;
	}
	auto image = std::make_shared<platform::CursorImage>();
	image->width = width;
	image->height = height;
	const auto clamp = [](float value, std::int32_t size) {
		if (!std::isfinite(value))
			return 0.0f;
		return std::clamp(std::floor(value), 0.0f, static_cast<float>(size - 1));
	};
	image->hotspot = {clamp(hotspot.x, width), clamp(hotspot.y, height)};
	image->rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
	const std::span<const std::uint8_t> bits = rgba.Bits();
	for (std::int32_t y = 0; y < height; y++) {
		const std::uint8_t* from = bits.data() + static_cast<std::size_t>(y)
			* static_cast<std::size_t>(rgba.BytesPerRow());
		std::uint8_t* to = image->rgba.data() + static_cast<std::size_t>(y)
			* static_cast<std::size_t>(width) * 4;
		for (std::int32_t x = 0; x < width; x++) {
			to[0] = from[2];
			to[1] = from[1];
			to[2] = from[0];
			to[3] = from[3];
			from += 4;
			to += 4;
		}
	}
	fShape.image = std::move(image);
}


BCursor::BCursor(std::span<const std::uint8_t> cursorData)
{
	// R5's format: size (16), depth (1), hot spot y and x, 32 bytes of
	// colors, 32 bytes of mask, rows of two bytes, the leftmost pixel in the
	// top bit
	if (cursorData.size() != kR5CursorSize || cursorData[0] != 16 || cursorData[1] != 1
		|| cursorData[2] > 15 || cursorData[3] > 15) {
		fValid = false;
		return;
	}
	auto image = std::make_shared<platform::CursorImage>();
	image->width = 16;
	image->height = 16;
	image->hotspot = {static_cast<float>(cursorData[3]), static_cast<float>(cursorData[2])};
	image->rgba.resize(16 * 16 * 4);
	for (std::size_t y = 0; y < 16; y++) {
		for (std::size_t x = 0; x < 16; x++) {
			const std::size_t byte = y * 2 + x / 8;
			const auto bit = static_cast<std::uint8_t>(0x80 >> (x % 8));
			const bool black = (cursorData[4 + byte] & bit) != 0;
			const bool shown = (cursorData[36 + byte] & bit) != 0;
			std::uint8_t* pixel = image->rgba.data() + (y * 16 + x) * 4;
			const auto level = static_cast<std::uint8_t>(shown && !black ? 255 : 0);
			pixel[0] = level;
			pixel[1] = level;
			pixel[2] = level;
			pixel[3] = static_cast<std::uint8_t>(shown ? 255 : 0);
		}
	}
	fShape.image = std::move(image);
}


std::expected<void, Error>
BCursor::InitCheck() const
{
	if (!fValid)
		return Failure(ErrorCode::InvalidArgument, "no valid cursor data");
	return {};
}


bool
BCursor::operator==(const BCursor& other) const
{
	return fValid == other.fValid && fShape == other.fShape;
}


const platform::CursorShape&
BCursor::Shape() const
{
	return fShape;
}


// #pragma mark - CursorState


namespace detail {


CursorState&
CursorState::Default()
{
	static CursorState state;
	return state;
}


void
CursorState::SetAppCursor(const platform::CursorShape& cursor)
{
	std::scoped_lock lock(fLock);
	fApp = cursor;
	_Apply();
}


void
CursorState::SetViewCursor(const std::optional<platform::CursorShape>& cursor)
{
	std::scoped_lock lock(fLock);
	fView = cursor;
	_Apply();
}


void
CursorState::Hide()
{
	std::scoped_lock lock(fLock);
	fHideLevel++;
	_Apply();
}


void
CursorState::Show()
{
	std::scoped_lock lock(fLock);
	if (fHideLevel > 0)
		fHideLevel--;
	_Apply();
}


void
CursorState::Obscure()
{
	std::scoped_lock lock(fLock);
	fObscured = true;
	_Apply();
}


void
CursorState::MouseMoved()
{
	std::scoped_lock lock(fLock);
	if (!fObscured)
		return;
	fObscured = false;
	_Apply();
}


bool
CursorState::IsHidden() const
{
	std::scoped_lock lock(fLock);
	return fHideLevel > 0;
}


void
CursorState::PlatformChanged()
{
	std::scoped_lock lock(fLock);
	fShownOn = nullptr;
	fShown.reset();
	fVisible.reset();
	_Apply();
}


platform::CursorShape
CursorState::Shown() const
{
	std::scoped_lock lock(fLock);
	return fView.value_or(fApp);
}


bool
CursorState::IsVisible() const
{
	std::scoped_lock lock(fLock);
	return fHideLevel == 0 && !fObscured;
}


void
CursorState::_Apply()
{
	platform::Platform* platform = platform::Current();
	if (platform == nullptr)
		return;
	if (platform != fShownOn) {
		// a new platform shows nothing of ours yet
		fShownOn = platform;
		fShown.reset();
		fVisible.reset();
	}
	const platform::CursorShape shape = fView.value_or(fApp);
	if (!fShown.has_value() || !(*fShown == shape)) {
		fShown = shape;
		platform->SetCursor(shape);
	}
	const bool visible = fHideLevel == 0 && !fObscured;
	if (fVisible != visible) {
		fVisible = visible;
		platform->SetCursorVisible(visible);
	}
}


} // namespace detail


} // namespace ngui
