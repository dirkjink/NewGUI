/*
 * Copyright 2010-2017, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <ngui/Bitmap.h>
#include <ngui/Clock.h>
#include <ngui/Error.h>
#include <ngui/Message.h>


namespace ngui {


/*!	How a notification matters (Haiku's notification_type). */
enum class BNotificationType : std::uint8_t {
	Information,	/*!< B_INFORMATION_NOTIFICATION: something to know. */
	Important,		/*!< B_IMPORTANT_NOTIFICATION: something to see soon. */
	Error,			/*!< B_ERROR_NOTIFICATION: something that went wrong. */
	Progress		/*!< B_PROGRESS_NOTIFICATION: how far a task has come. */
};


/*!	A notification of the system (module P5, Haiku's BNotification): a
	headline, a text and an icon that the system shows outside the
	application's windows for a while, in a group; one of type Progress
	with how far a task has come. Send() hands it to the platform's
	notifications: those of the desktop over D-Bus on Linux.

	Differences to Haiku: no archiving; instead of starting an application
	or opening files (OnClickApp(), OnClickFile(), OnClickRef(),
	OnClickArg()), a click sends a message to the application
	(SetOnClickMessage()), as the user may have clicked long after; Send()
	takes the time it shows as a Clock::Time and returns an Error instead of
	a status_t. SourceName() is the end of the application's signature.
	Without an icon of its own a notification shows Haiku's notification
	icon of its type (Notify_Notice for information and progress,
	Notify_Achtung for important ones, Notify_Danger for errors; 64 × 64,
	with a vector icon renderer) where Haiku shows the application's icon.
	Strings are UTF-8.
*/
class BNotification {
public:
	explicit					BNotification(BNotificationType type);
									/*!< An empty notification of \a type, from the
										running application. */
								BNotification(const BNotification& other);
									/*!< A copy, with a copy of the icon. */
			BNotification&		operator=(const BNotification& other);
									/*!< Becomes a copy of \a other. */
								BNotification(BNotification&& other) noexcept;
									/*!< Takes over \a other. */
			BNotification&		operator=(BNotification&& other) noexcept;
									/*!< Takes over \a other. */
								~BNotification();
									/*!< Default. */

			const std::string&	SourceSignature() const;
									/*!< The signature of the application that made it,
										empty without one. */
			const std::string&	SourceName() const;
									/*!< The application's name: the end of its signature
										("application/x-vnd.example-mail" gives
										"example-mail"). */
			BNotificationType	Type() const;
									/*!< How it matters. */

			const std::string&	Group() const;
									/*!< The group it shows in, empty for the
										application's. */
			void				SetGroup(std::string_view group);
									/*!< Sets the group. */
			const std::string&	Title() const;
									/*!< The headline. */
			void				SetTitle(std::string_view title);
									/*!< Sets the headline. */
			const std::string&	Content() const;
									/*!< The text. */
			void				SetContent(std::string_view content);
									/*!< Sets the text. */
			const std::string&	MessageID() const;
									/*!< Its id: sent again with the same id, it takes the
										place of the one shown before (a progress, say). */
			void				SetMessageID(std::string_view id);
									/*!< Sets the id; empty for a new notification each
										time. */
			float				Progress() const;
									/*!< How far the task has come, from 0 to 1 (type
										Progress). */
			void				SetProgress(float progress);
									/*!< Sets the progress, limited to 0 to 1. */

			const BMessage*		OnClickMessage() const;
									/*!< What a click sends to the application, nullptr
										for none. */
			void				SetOnClickMessage(const BMessage& message);
									/*!< NewGUI: a click on it sends \a message to the
										application (AppMessenger()). */
			void				ClearOnClickMessage();
									/*!< NewGUI: no message for a click. */

			const BBitmap*		Icon() const;
									/*!< The icon, nullptr for none (Send() then shows
										Haiku's notification icon of the type). */
			std::expected<void, Error> SetIcon(const BBitmap* icon);
									/*!< Takes a copy of \a icon; nullptr for none.
										InvalidArgument for an invalid bitmap. */

			std::expected<void, Error> Send(Clock::Time timeout = Clock::Time(-1)) const;
									/*!< Hands it to the system's notifications, to show
										for \a timeout (negative: as long as the system
										likes). NotSupported without a platform that has
										them, or without a running application. */

private:
			BNotificationType	fType;
			std::string			fSourceSignature;
			std::string			fSourceName;
			std::string			fGroup;
			std::string			fTitle;
			std::string			fContent;
			std::string			fID;
			float				fProgress = 0;
			std::optional<BMessage> fOnClick;
			std::unique_ptr<BBitmap> fIcon;
};


} // namespace ngui
