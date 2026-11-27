/*
 * Copyright 2010-2017, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Notification.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <string_view>
#include <utility>

#include <ngui/Application.h>
#include <ngui/platform/Platform.h>

#include "ApplicationPrivate.h"


namespace ngui {


namespace {


// "application/x-vnd.example-mail" gives "example-mail".
std::string
NameOf(const std::string& signature)
{
	std::string_view name = signature;
	if (const auto slash = name.rfind('/'); slash != std::string_view::npos)
		name.remove_prefix(slash + 1);
	if (name.starts_with("x-vnd."))
		name.remove_prefix(6);
	return std::string(name);
}


platform::NotificationSpec::Kind
KindOf(BNotificationType type)
{
	switch (type) {
		case BNotificationType::Important:
			return platform::NotificationSpec::Kind::Important;
		case BNotificationType::Error:
			return platform::NotificationSpec::Kind::Error;
		case BNotificationType::Progress:
			return platform::NotificationSpec::Kind::Progress;
		case BNotificationType::Information:
			break;
	}
	return platform::NotificationSpec::Kind::Information;
}


} // namespace


BNotification::BNotification(BNotificationType type)
	:
	fType(type),
	fSourceSignature(detail::AppSignature()),
	fSourceName(NameOf(fSourceSignature))
{
}


BNotification::BNotification(const BNotification& other)
	:
	fType(other.fType),
	fSourceSignature(other.fSourceSignature),
	fSourceName(other.fSourceName),
	fGroup(other.fGroup),
	fTitle(other.fTitle),
	fContent(other.fContent),
	fID(other.fID),
	fProgress(other.fProgress),
	fOnClick(other.fOnClick),
	fIcon(other.fIcon != nullptr ? std::make_unique<BBitmap>(*other.fIcon) : nullptr)
{
}


BNotification&
BNotification::operator=(const BNotification& other)
{
	if (this != &other) {
		BNotification copy(other);
		*this = std::move(copy);
	}
	return *this;
}


BNotification::BNotification(BNotification&& other) noexcept = default;


BNotification& BNotification::operator=(BNotification&& other) noexcept = default;


BNotification::~BNotification() = default;


const std::string&
BNotification::SourceSignature() const
{
	return fSourceSignature;
}


const std::string&
BNotification::SourceName() const
{
	return fSourceName;
}


BNotificationType
BNotification::Type() const
{
	return fType;
}


const std::string&
BNotification::Group() const
{
	return fGroup;
}


void
BNotification::SetGroup(std::string_view group)
{
	fGroup = group;
}


const std::string&
BNotification::Title() const
{
	return fTitle;
}


void
BNotification::SetTitle(std::string_view title)
{
	fTitle = title;
}


const std::string&
BNotification::Content() const
{
	return fContent;
}


void
BNotification::SetContent(std::string_view content)
{
	fContent = content;
}


const std::string&
BNotification::MessageID() const
{
	return fID;
}


void
BNotification::SetMessageID(std::string_view id)
{
	fID = id;
}


float
BNotification::Progress() const
{
	return fProgress;
}


void
BNotification::SetProgress(float progress)
{
	// Haiku clamps the same way; NaN counts as nothing done
	fProgress = std::isnan(progress) ? 0.0f : std::clamp(progress, 0.0f, 1.0f);
}


const BMessage*
BNotification::OnClickMessage() const
{
	return fOnClick.has_value() ? &*fOnClick : nullptr;
}


void
BNotification::SetOnClickMessage(const BMessage& message)
{
	fOnClick = message;
}


void
BNotification::ClearOnClickMessage()
{
	fOnClick.reset();
}


const BBitmap*
BNotification::Icon() const
{
	return fIcon.get();
}


std::expected<void, Error>
BNotification::SetIcon(const BBitmap* icon)
{
	if (icon == nullptr) {
		fIcon.reset();
		return {};
	}
	if (!icon->IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid icon");
	fIcon = std::make_unique<BBitmap>(*icon);
	return {};
}


std::expected<void, Error>
BNotification::Send(Clock::Time timeout) const
{
	platform::Platform* platform = platform::Current();
	if (platform == nullptr)
		return Failure(ErrorCode::NotSupported, "no running application");
	platform::NotificationSpec spec;
	spec.kind = KindOf(fType);
	spec.application = fSourceName;
	spec.group = fGroup;
	spec.title = fTitle;
	spec.content = fContent;
	spec.id = fID;
	spec.progress = fProgress;
	spec.timeout = timeout < Clock::Time::zero()
		? std::chrono::milliseconds(-1)
		: std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
	spec.target = AppMessenger();
	spec.onClick = fOnClick;
	if (fIcon != nullptr) {
		// RGBA, not premultiplied: B_RGBA32 lies as B, G, R, A in memory
		BBitmap rgba(fIcon->Bounds(), BColorSpace::RGBA32);
		if (rgba.IsValid() && rgba.ImportBits(*fIcon).has_value()) {
			const gfx::Rect bounds = rgba.Bounds();
			spec.iconWidth = static_cast<std::int32_t>(bounds.x1 - bounds.x0);
			spec.iconHeight = static_cast<std::int32_t>(bounds.y1 - bounds.y0);
			const auto row = static_cast<std::size_t>(rgba.BytesPerRow());
			const auto width = static_cast<std::size_t>(spec.iconWidth);
			spec.icon.resize(width * static_cast<std::size_t>(spec.iconHeight) * 4);
			const std::span<const std::uint8_t> bits = std::as_const(rgba).Bits();
			for (std::size_t y = 0; y < static_cast<std::size_t>(spec.iconHeight); y++) {
				for (std::size_t x = 0; x < width; x++) {
					const std::uint8_t* from = bits.data() + y * row + x * 4;
					std::uint8_t* to = spec.icon.data() + (y * width + x) * 4;
					to[0] = from[2];
					to[1] = from[1];
					to[2] = from[0];
					to[3] = from[3];
				}
			}
		}
	}
	return platform->ShowNotification(spec);
}


} // namespace ngui
