/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/LayoutContext.cpp (commit
 * b1d497bb69). BList became std::vector.
 */


#include <ngui/LayoutContext.h>

#include <algorithm>


namespace ngui {


// constructor
BLayoutContextListener::BLayoutContextListener()
{
}


// destructor
BLayoutContextListener::~BLayoutContextListener()
{
}


// #pragma mark -


// constructor
BLayoutContext::BLayoutContext()
{
}


// destructor
BLayoutContext::~BLayoutContext()
{
	// notify the listeners
	for (BLayoutContextListener* listener : fListeners)
		listener->LayoutContextLeft(this);
}


// AddListener
void
BLayoutContext::AddListener(BLayoutContextListener* listener)
{
	if (listener)
		fListeners.push_back(listener);
}


// RemoveListener
void
BLayoutContext::RemoveListener(BLayoutContextListener* listener)
{
	if (listener) {
		const auto found = std::find(fListeners.begin(), fListeners.end(), listener);
		if (found != fListeners.end())
			fListeners.erase(found);
	}
}


} // namespace ngui
