/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <string>
#include <utility>


namespace ngui {


/*!	The kind of an error. Replaces Haiku's status_t codes; the list names the
	Haiku code each one takes over when code is ported (CONVENTIONS.md,
	section 3, rule 2).
*/
enum class ErrorCode : std::uint8_t {
	InvalidArgument,	/*!< An argument is out of its domain (B_BAD_VALUE). */
	OutOfRange,			/*!< An index or position is out of range (B_BAD_INDEX). */
	NotSupported,		/*!< The operation is not supported here (B_NOT_SUPPORTED). */
	NotFound,			/*!< Something looked up does not exist (B_NAME_NOT_FOUND,
							B_ENTRY_NOT_FOUND). */
	OutOfMemory,		/*!< An allocation failed (B_NO_MEMORY). */
	BadData,			/*!< Input data is malformed (B_BAD_DATA). */
	IoError,			/*!< Reading or writing failed (B_IO_ERROR). */
	TimedOut,			/*!< A wait ended before it succeeded (B_TIMED_OUT,
							B_WOULD_BLOCK). */
	BadType				/*!< A value has another type than asked for (B_BAD_TYPE). */
};


/*!	The error type of the API: every fallible function returns
	`std::expected<T, ngui::Error>`, no exception crosses the API boundary
	(CONVENTIONS.md, section 3, rule 2). Failure() builds the error value.
*/
struct Error {
			ErrorCode			code;		/*!< What went wrong. */
			std::string			message;	/*!< Details for a human, may be empty. */

			bool				operator==(const Error& other) const = default;
									/*!< Same code and same message. */
};


/*!	The name of \a code as written in the source, e.g. "InvalidArgument". */
const char*						ErrorCodeName(ErrorCode code);


/*!	The error value for a `std::expected<T, ngui::Error>`:
	`return ngui::Failure(ngui::ErrorCode::InvalidArgument, "negative width");`
*/
[[nodiscard]] inline std::unexpected<Error>
Failure(ErrorCode code, std::string message = {})
{
	return std::unexpected<Error>(Error{code, std::move(message)});
}


} // namespace ngui
