/*
 * Copyright 2002-2006, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>


namespace ngui {


class BBitmap;


/*!	A MIME type, "supertype/subtype" or a supertype alone (Haiku's
	BMimeType), with what Haiku's MIME database says of it: a short
	description, the file extensions, the icon.

	The database is Haiku's own (src/data/mime_db), built into the library
	(177 types); the icons are Haiku's, from the icon directory
	(BIconUtils::SetSystemIconDirectory()).

	Differences to Haiku: the database is fixed, there is no registrar:
	nothing is installed, deleted, set or watched, and there are no preferred
	or supporting applications, attribute infos or sniffer rules. A file's
	type is guessed by its name only (GuessMimeType()), not by its content.
	Types are compared without regard to case, as in Haiku.
*/
class BMimeType {
public:
								BMimeType() = default;
									/*!< No type. */
	explicit					BMimeType(std::string_view type);
									/*!< \a type, if IsValid(type); else no type, and
										InitCheck() fails. */

			std::expected<void, Error> SetTo(std::string_view type);
									/*!< \a type; InvalidArgument and no type for an invalid
										one. */
			void				Unset();
									/*!< No type. */
			std::expected<void, Error> InitCheck() const;
									/*!< InvalidArgument without a type (Haiku: B_NO_INIT,
										B_BAD_VALUE after an invalid one). */

			const std::string&	Type() const;
									/*!< The type as given, empty for none. */
			bool				IsValid() const;
									/*!< Whether it holds a type. */
			bool				IsSupertypeOnly() const;
									/*!< Whether the type has no "/" ("image"). */
			std::expected<BMimeType, Error> GetSupertype() const;
									/*!< "image" for "image/png"; InvalidArgument for a
										supertype or no type. */
			bool				Contains(const BMimeType& type) const;
									/*!< Whether \a type is this one or this one is its
										supertype. */

			bool				IsInstalled() const;
									/*!< Whether the database knows the type. */
			std::string			ShortDescription() const;
									/*!< META:S:DESC ("PNG image"), empty if none. */
			std::vector<std::string> FileExtensions() const;
									/*!< The extensions without the dot ("png"). */
			std::expected<void, Error> GetIcon(BBitmap* icon) const;
									/*!< Draws the type's own icon into all of \a icon
										(BIconUtils::GetVectorIcon()); NotFound for a type
										without one. BIconUtils::GetTypeIcon() falls back
										as Tracker does. */

	static	bool				IsValid(std::string_view type);
									/*!< Haiku's rules: 1 to 239 characters, printable
										ASCII without ()<>@,;:\\"[]?= and spaces, at most
										one "/", neither first nor last. */
	static	BMimeType			GuessMimeType(std::string_view filename);
									/*!< The type the database gives the extension of
										\a filename (without regard to case), else
										application/octet-stream (B_FILE_MIME_TYPE). By the
										name only, the file need not exist. */

			bool				operator==(const BMimeType& other) const;
									/*!< Equal without regard to case; two without a type
										are equal. */
			bool				operator==(std::string_view type) const;
									/*!< Equal to BMimeType(\a type). */

private:
			std::string			fType;
			bool				fInvalid = false;
};


inline constexpr std::string_view kFileMimeType = "application/octet-stream";
									/*!< B_FILE_MIME_TYPE: a file of no known type. */
inline constexpr std::string_view kDirectoryMimeType = "application/x-vnd.Be-directory";
									/*!< B_DIRECTORY_MIME_TYPE */
inline constexpr std::string_view kSymlinkMimeType = "application/x-vnd.Be-symlink";
									/*!< B_SYMLINK_MIME_TYPE */
inline constexpr std::string_view kAppMimeType = "application/x-vnd.Be-elfexecutable";
									/*!< B_APP_MIME_TYPE: an executable program. */


} // namespace ngui
