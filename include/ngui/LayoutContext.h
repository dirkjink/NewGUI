/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <vector>


namespace ngui {


class BLayoutContext;


/*!	Learns when a layout pass ends (Haiku's BLayoutContextListener); layouts
	use it to drop what they computed for one pass. */
class BLayoutContextListener {
public:
								BLayoutContextListener();
	virtual						~BLayoutContextListener();
									/*!< Does not remove itself from contexts; a listener
										must outlive the contexts it listens to. */

	virtual	void				LayoutContextLeft(BLayoutContext* context) = 0;
									/*!< \a context ends: the layout pass is over. */
};


/*!	One layout pass (Haiku's BLayoutContext): made by BView::Layout(), given
	to every view and layout laid out in it, destroyed at its end. */
class BLayoutContext {
public:
								BLayoutContext();
								BLayoutContext(const BLayoutContext&) = delete;
			BLayoutContext&		operator=(const BLayoutContext&) = delete;
								~BLayoutContext();
									/*!< Tells the listeners that the pass is over. */

			void				AddListener(BLayoutContextListener* listener);
									/*!< Adds \a listener; nullptr is ignored. */
			void				RemoveListener(BLayoutContextListener* listener);
									/*!< Removes \a listener. */

private:
			std::vector<BLayoutContextListener*> fListeners;
};


} // namespace ngui
