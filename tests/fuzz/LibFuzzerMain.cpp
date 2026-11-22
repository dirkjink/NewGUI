/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Entry point of a libFuzzer executable: one per fuzz target, built from the
// target file and this file (NGUI_FUZZING, Clang only).


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>


namespace {


ngui::test::FuzzFunction sTarget = nullptr;


} // namespace


namespace ngui::test {


FuzzTargetRegistration::FuzzTargetRegistration(const char* name,
	FuzzFunction function)
{
	if (sTarget != nullptr) {
		std::fprintf(stderr, "fuzz target %s: only one target per executable\n", name);
		std::abort();
	}
	sTarget = function;
}


void
FuzzCheckFailed(const char* expression, const char* file, int line)
{
	std::fprintf(stderr, "%s:%d: fuzz invariant failed: %s\n", file, line, expression);
	std::abort();
}


} // namespace ngui::test


extern "C" int
LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
	sTarget(std::span<const std::uint8_t>(data, size));
	return 0;
}
