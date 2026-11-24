/*
 * Copyright 2001-2005, Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <ngui/Error.h>
#include <ngui/Locker.h>
#include <ngui/Message.h>
#include <ngui/SupportDefs.h>


namespace ngui {


/*!	A clipboard (Haiku's BClipboard): a BMessage whose fields are named by
	MIME type, with data of BTypeCode::Mime, e.g. "text/plain" with UTF-8
	text. Lock() fetches its content, Data() gives the message to read or,
	after Clear(), to fill, Commit() publishes it; Unlock() ends.

	Clipboards of the same name share their content within the process.
	System() is the system clipboard (Haiku's be_clipboard): with a
	platform (BApplication::UsePlatform()) its content is the window
	system's, so it reaches other applications; without one it stays in
	the process.

	Differences to Haiku: no watching (StartWatching(), B_CLIPBOARD_CHANGED)
	and no DataSource(); the counts count the commits of this process.
*/
class BClipboard {
public:
	explicit					BClipboard(std::string_view name);
									/*!< The clipboard \a name ("system" is the system's). */
								~BClipboard();
									/*!< Default. */
								BClipboard(const BClipboard&) = delete;
			BClipboard&			operator=(const BClipboard&) = delete;

			const std::string&	Name() const;
									/*!< Its name. */
			uint32				LocalCount() const;
									/*!< The count of the content last fetched. */
			uint32				SystemCount() const;
									/*!< The count of the content now: it grows with each
										Commit() of a clipboard of this name. */

			bool				Lock();
									/*!< Locks it and fetches the content; recursive. */
			void				Unlock();
									/*!< Unlocks it. */
			bool				IsLocked() const;
									/*!< Whether the calling thread holds the lock. */

			std::expected<void, Error> Clear();
									/*!< Empties Data(); while locked. */
			std::expected<void, Error> Commit();
									/*!< Publishes Data(); while locked. */
			std::expected<void, Error> Commit(bool failIfChanged);
									/*!< As Commit(); with \a failIfChanged it fails
										(InvalidArgument) if the content changed since it was
										fetched. */
			std::expected<void, Error> Revert();
									/*!< Fetches the content again, dropping changes. */

			BMessage*			Data() const;
									/*!< The content; nullptr unless locked. */

	static	BClipboard&			System();
									/*!< The system clipboard (Haiku's be_clipboard). */

private:
	struct Store;

			void				_DownloadFromSystem();
			std::expected<void, Error> _UploadToSystem();

			std::string			fName;
			std::shared_ptr<Store> fStore;
			mutable BMessage	fData;
			BLocker				fLock;
			uint32				fCount = 0;
};


} // namespace ngui
