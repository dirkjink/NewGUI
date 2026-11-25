/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Error.h>


namespace ngui {


const char*
ErrorCodeName(ErrorCode code)
{
	switch (code) {
		case ErrorCode::InvalidArgument:
			return "InvalidArgument";
		case ErrorCode::OutOfRange:
			return "OutOfRange";
		case ErrorCode::NotSupported:
			return "NotSupported";
		case ErrorCode::NotFound:
			return "NotFound";
		case ErrorCode::OutOfMemory:
			return "OutOfMemory";
		case ErrorCode::BadData:
			return "BadData";
		case ErrorCode::IoError:
			return "IoError";
		case ErrorCode::TimedOut:
			return "TimedOut";
		case ErrorCode::BadType:
			return "BadType";
		case ErrorCode::BadScriptSyntax:
			return "BadScriptSyntax";
	}
	return "Unknown";
}


} // namespace ngui
