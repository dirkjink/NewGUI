/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/MessageFilter.h>


namespace ngui {


class BLooper;
class BMessage;


/*!	Something that receives messages in a looper (module G3), as Haiku's
	BHandler: a looper calls MessageReceived() on its thread. A handler that
	does not handle a message passes it to its next handler.

	A looper does not own its handlers; a handler that is destroyed leaves
	its looper. It owns its filters. Scripting and observers come later.
*/
class BHandler {
public:
	explicit					BHandler(std::string_view name = {});
									/*!< A handler outside any looper. */
	virtual						~BHandler();
									/*!< Leaves its looper. */

								BHandler(const BHandler&) = delete;
									/*!< Not copyable. */
			BHandler&			operator=(const BHandler&) = delete;
									/*!< Not copyable. */

	virtual	void				MessageReceived(BMessage* message);
									/*!< Handles \a message; the default passes it to the
										next handler. */

			BLooper*			Looper() const;
									/*!< The looper it belongs to, nullptr if none. */
			const std::string&	Name() const;
									/*!< Its name. */
			void				SetName(std::string_view name);
									/*!< Renames it. */
			BHandler*			NextHandler() const;
									/*!< Where unhandled messages go; AddHandler() makes it
										the looper. */
			void				SetNextHandler(BHandler* handler);
									/*!< Changes the next handler; it must be in the same
										looper. */

			BMessageFilter*		AddFilter(std::unique_ptr<BMessageFilter> filter);
									/*!< Adds a filter for the messages to this handler, after
										the others; returns it, nullptr for nullptr. */
			std::unique_ptr<BMessageFilter> RemoveFilter(BMessageFilter* filter);
									/*!< Takes \a filter back; nullptr if it is not here. */
			std::vector<BMessageFilter*> FilterList() const;
									/*!< Its filters, in order. */

private:
	friend class BLooper;
	friend class BMessenger;

			std::string			fName;
			BLooper*			fLooper = nullptr;
			BHandler*			fNextHandler = nullptr;
			std::int32_t		fToken;
			std::vector<std::unique_ptr<BMessageFilter>> fFilters;
};


} // namespace ngui
