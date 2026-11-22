/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Regression mode of the fuzz targets, part of every test run: each target
// becomes the test FuzzRegressionTest.<Name>, which runs the empty input and
// every checked-in input of tests/fuzz/corpus/<Name>/.


#include "FuzzTarget.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>


namespace {


class FuzzRegressionTest : public testing::Test {
public:
								FuzzRegressionTest(std::string name,
									ngui::test::FuzzFunction function);

			void				TestBody() override;

private:
			std::string			fName;
			ngui::test::FuzzFunction fFunction;
};


FuzzRegressionTest::FuzzRegressionTest(std::string name,
	ngui::test::FuzzFunction function)
	:
	fName(std::move(name)),
	fFunction(function)
{
}


void
FuzzRegressionTest::TestBody()
{
	fFunction({});

	const std::filesystem::path corpus
		= std::filesystem::path(NGUI_FUZZ_CORPUS_DIR) / fName;
	if (!std::filesystem::is_directory(corpus))
		return;

	std::vector<std::filesystem::path> inputs;
	for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
		if (entry.is_regular_file())
			inputs.push_back(entry.path());
	}
	std::ranges::sort(inputs);

	for (const auto& input : inputs) {
		SCOPED_TRACE(input.string());
		std::ifstream stream(input, std::ios::binary);
		const std::vector<std::uint8_t> data(
			(std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		fFunction(data);
	}
}


} // namespace


namespace ngui::test {


FuzzTargetRegistration::FuzzTargetRegistration(const char* name,
	FuzzFunction function)
{
	const std::string testName = name;
	testing::RegisterTest("FuzzRegressionTest", name, nullptr, nullptr,
		__FILE__, __LINE__,
		[testName, function]() -> testing::Test* {
			return new FuzzRegressionTest(testName, function);
		});
}


void
FuzzCheckFailed(const char* expression, const char* file, int line)
{
	ADD_FAILURE_AT(file, line) << "fuzz invariant failed: " << expression;
}


} // namespace ngui::test
