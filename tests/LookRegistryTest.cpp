/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/LookRegistry.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/BeLook.h>

#include "look/StubLook.h"


namespace {


using namespace ngui;


// A stub look with an id and level of its own.
class TestLook : public test::StubLook {
public:
	TestLook(std::string_view id, ConformanceLevel level, gfx::Caps needs = {})
		:
		fId(id),
		fLevel(level),
		fNeeds(needs)
	{
	}

	LookInfo Info() const override
	{
		return {fId, "Test", 1, fLevel, fNeeds};
	}

private:
	std::string_view	fId;
	ConformanceLevel	fLevel;
	gfx::Caps			fNeeds;
};


gfx::Caps
WithRadialGradients()
{
	gfx::Caps caps;
	caps.radialGradient = true;
	return caps;
}


// A registry with "be", "plain" (C2) and "fancy" (Extended, radial
// gradients), on a backend with the core profile only.
void
Fill(LookRegistry& registry)
{
	registry.Register("be", CreateBeLook);
	registry.Register("plain", [] {
		return std::make_shared<const TestLook>("plain", ConformanceLevel::C2);
	});
	registry.Register("fancy", [] {
		return std::make_shared<const TestLook>("fancy", ConformanceLevel::Extended,
			WithRadialGradients());
	});
}


TEST(LookRegistryTest, CompatibilityFollowsRuleL9)
{
	// What is tested: IsCompatible() for looks of levels C1, C2 and Extended
	// on backends with and without the capability the Extended look needs.
	// Success criteria: C1 and C2 run on every backend with the core
	// profile; Extended only where every needed capability is reported;
	// nothing runs on a backend that does not claim the core profile.
	// Why it matters: Rule L9 (specification 18.4): a look must not be
	// activated on a backend that cannot draw it.
	//
	// Given
	const gfx::Caps core;
	const gfx::Caps radial = WithRadialGradients();
	gfx::Caps broken;
	broken.core = false;
	const LookInfo c1 = {"a", "A", 1, ConformanceLevel::C1, {}};
	const LookInfo c2 = {"b", "B", 1, ConformanceLevel::C2, {}};
	const LookInfo extended = {"c", "C", 1, ConformanceLevel::Extended, radial};

	// When / Then
	EXPECT_TRUE(IsCompatible(c1, core));
	EXPECT_TRUE(IsCompatible(c2, core));
	EXPECT_FALSE(IsCompatible(extended, core));
	EXPECT_TRUE(IsCompatible(extended, radial));
	EXPECT_FALSE(IsCompatible(c1, broken));
}


TEST(LookRegistryTest, RegistrationKeepsOrderAndRejectsDuplicates)
{
	// What is tested: Register() with new, duplicate and empty ids and an
	// empty factory; Ids().
	// Success criteria: Only the new ids are accepted, in their order.
	// Why it matters: The id is the stable key of the setting
	// ngui.controlLook; two looks with one id would make it ambiguous.
	//
	// Given
	LookRegistry registry;

	// When / Then
	EXPECT_TRUE(registry.Register("be", CreateBeLook));
	EXPECT_TRUE(registry.Register("stub", [] { return std::make_shared<const test::StubLook>(); }));
	EXPECT_FALSE(registry.Register("be", CreateBeLook));
	EXPECT_FALSE(registry.Register("", CreateBeLook));
	EXPECT_FALSE(registry.Register("none", nullptr));
	EXPECT_EQ(registry.Ids(), (std::vector<std::string>{"be", "stub"}));
	EXPECT_EQ(registry.Current(), nullptr);
}


TEST(LookRegistryTest, SetCurrentRefusesIncompatibleLooks)
{
	// What is tested: SetCurrent() and Available() on a backend with the
	// core profile only, then with radial gradients; an unknown id; looks
	// whose factory fails or reports another id.
	// Success criteria: "fancy" is refused with NotSupported and missing
	// from Available() until the backend reports radial gradients; an
	// unknown id gives NotFound; a failing factory and a wrong id give
	// NotSupported; on every failure the current look stays; each factory
	// runs once, its look is kept.
	// Why it matters: Rule L9 and the test it asks for (18.7: a backend with
	// deliberately reduced Caps); a look is shared by all windows (L4), so
	// it is made once.
	//
	// Given
	LookRegistry registry;
	Fill(registry);
	int made = 0;
	registry.Register("counted", [&made] {
		made++;
		return std::make_shared<const TestLook>("counted", ConformanceLevel::C1);
	});
	registry.Register("failing", [] { return std::shared_ptr<const ControlLook>(); });
	registry.Register("liar", [] {
		return std::make_shared<const TestLook>("other", ConformanceLevel::C1);
	});
	ASSERT_TRUE(registry.SetCurrent("plain").has_value());

	// When / Then: core profile only.
	EXPECT_EQ(registry.SetCurrent("fancy").error().code, ErrorCode::NotSupported);
	EXPECT_EQ(registry.SetCurrent("missing").error().code, ErrorCode::NotFound);
	EXPECT_EQ(registry.SetCurrent("failing").error().code, ErrorCode::NotSupported);
	EXPECT_EQ(registry.SetCurrent("liar").error().code, ErrorCode::NotSupported);
	EXPECT_EQ(registry.Current()->Info().id, "plain");
	std::vector<std::string> ids;
	for (const LookInfo& info : registry.Available())
		ids.emplace_back(info.id);
	EXPECT_EQ(ids, (std::vector<std::string>{"be", "plain", "counted"}));

	// When / Then: with radial gradients.
	registry.SetBackendCaps(WithRadialGradients());
	EXPECT_TRUE(registry.SetCurrent("fancy").has_value());
	EXPECT_EQ(registry.Current()->Info().id, "fancy");
	EXPECT_EQ(registry.Available().size(), 4u);

	ASSERT_TRUE(registry.SetCurrent("counted").has_value());
	const std::shared_ptr<const ControlLook> first = registry.Current();
	ASSERT_TRUE(registry.SetCurrent("be").has_value());
	ASSERT_TRUE(registry.SetCurrent("counted").has_value());
	EXPECT_EQ(registry.Current(), first);
	EXPECT_EQ(made, 1);
}


TEST(LookRegistryTest, StartFallsBackToTheBeOSLook)
{
	// What is tested: SelectAtStart() with an empty setting, a compatible
	// look, an unknown look, an incompatible look, and in a registry without
	// "be".
	// Success criteria: Empty selects "be"; "plain" is selected without a
	// warning; "missing" and "fancy" select "be" with a warning that names
	// the setting and the reason; without "be" the start fails with
	// NotFound.
	// Why it matters: At start a wrong setting must not leave a program
	// without a look: L9 prescribes the fallback "be", which runs on every
	// backend, and a logged warning.
	//
	// Given
	LookRegistry registry;
	Fill(registry);
	LookRegistry empty;

	// When
	const auto byDefault = registry.SelectAtStart("");
	const auto plain = registry.SelectAtStart("plain");
	const auto missing = registry.SelectAtStart("missing");
	const auto fancy = registry.SelectAtStart("fancy");
	const auto nothing = empty.SelectAtStart("plain");

	// Then
	EXPECT_EQ(byDefault->id, "be");
	EXPECT_TRUE(byDefault->warning.empty());
	EXPECT_EQ(plain->id, "plain");
	EXPECT_TRUE(plain->warning.empty());
	EXPECT_EQ(missing->id, "be");
	EXPECT_EQ(missing->warning,
		"ngui.controlLook = \"missing\": no look \"missing\"; using \"be\"");
	EXPECT_EQ(fancy->id, "be");
	EXPECT_NE(fancy->warning.find("capabilities"), std::string::npos) << fancy->warning;
	EXPECT_EQ(registry.Current()->Info().id, "be");
	EXPECT_EQ(nothing.error().code, ErrorCode::NotFound);
}


TEST(LookRegistryTest, DefaultRegistryStartsWithTheBeOSLook)
{
	// What is tested: LookRegistry::Default() before any selection.
	// Success criteria: "be" is registered and current.
	// Why it matters: The BeOS look is built in and the default of
	// ngui.controlLook (18.4, 18.6); code that draws before the start
	// selection still has a look.
	//
	// When
	LookRegistry& registry = LookRegistry::Default();

	// Then
	const auto ids = registry.Ids();
	EXPECT_NE(std::find(ids.begin(), ids.end(), "be"), ids.end());
	ASSERT_NE(registry.Current(), nullptr);
	EXPECT_EQ(registry.Current()->Info().id, "be");
}


TEST(LookRegistryTest, CurrentIsReadSafelyWhileItChanges)
{
	// What is tested: Four threads read Current() while one switches between
	// two looks 2,000 times.
	// Success criteria: Every read gives one of the two looks, never
	// nullptr, and no read crashes.
	// Why it matters: Window threads take their look snapshot at the start
	// of each cycle (L10) while the settings thread may switch (18.5); the
	// atomic shared_ptr keeps the old look alive for whoever still holds it.
	//
	// Given
	LookRegistry registry;
	Fill(registry);
	ASSERT_TRUE(registry.SetCurrent("be").has_value());
	std::atomic<bool> done = false;
	std::atomic<int> bad = 0;

	// When
	std::vector<std::thread> readers;
	for (int i = 0; i < 4; i++) {
		readers.emplace_back([&] {
			while (!done) {
				const std::shared_ptr<const ControlLook> look = registry.Current();
				if (look == nullptr
					|| (look->Info().id != "be" && look->Info().id != "plain")) {
					bad++;
				}
			}
		});
	}
	for (int i = 0; i < 2000; i++)
		(void)registry.SetCurrent(i % 2 == 0 ? "plain" : "be");
	done = true;
	for (std::thread& reader : readers)
		reader.join();

	// Then
	EXPECT_EQ(bad, 0);
}


} // namespace
