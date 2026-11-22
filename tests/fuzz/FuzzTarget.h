/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <span>


namespace ngui::test {


/*!	The body of a fuzz target: receives one input and checks the invariants
	of the code under test with NGUI_FUZZ_CHECK.
*/
using FuzzFunction = void (*)(std::span<const std::uint8_t> data);


/*!	Registers a fuzz target. Defined twice: in the regression build
	(FuzzRegressionTest.cpp) it adds a GoogleTest test, in the libFuzzer build
	(LibFuzzerMain.cpp) it becomes the function libFuzzer calls.
*/
class FuzzTargetRegistration {
public:
								FuzzTargetRegistration(const char* name,
									FuzzFunction function);
};


/*!	Reports a broken invariant: a test failure in the regression build, an
	abort (and thus a finding) in the libFuzzer build.
*/
void							FuzzCheckFailed(const char* expression,
									const char* file, int line);


} // namespace ngui::test


/*!	Defines the fuzz target \a Name; the function body follows the macro.
	The target file is tests/fuzz/<Name>Fuzz.cpp (CONVENTIONS.md 8.3).
*/
#define NGUI_FUZZ_TARGET(Name) \
	static void NguiFuzz##Name(std::span<const std::uint8_t> data); \
	static const ::ngui::test::FuzzTargetRegistration \
		kNguiFuzzRegistration##Name(#Name, &NguiFuzz##Name); \
	static void NguiFuzz##Name(std::span<const std::uint8_t> data)


/*!	Checks an invariant inside a fuzz target. */
#define NGUI_FUZZ_CHECK(condition) \
	do { \
		if (!(condition)) \
			::ngui::test::FuzzCheckFailed(#condition, __FILE__, __LINE__); \
	} while (false)
