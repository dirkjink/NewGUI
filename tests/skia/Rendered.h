/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Rendering with the Skia backend for its tests and those of the looks.


#include <ngui/gfx/SkiaBackend.h>

#include "support/RenderedWith.h"


namespace ngui::test {


using gfx::SkiaBackend;

using Rendered = RenderedWith<SkiaBackend>;


} // namespace ngui::test
