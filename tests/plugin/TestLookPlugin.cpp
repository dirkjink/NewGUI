/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Look plugins for LookPluginTest, one source for every kind of plugin the
// loader must handle; NGUI_TEST_PLUGIN_VARIANT chooses:
// 0 a look plugin with the look "plugin-look" (the StubLook, which shrinks
//   each frame by 1 on every side),
// 1 a plugin for another interface version: it declines every version,
// 2 a plugin built otherwise: another build key,
// 3 a shared library without the functions of a look plugin,
// 4 a look plugin whose look takes the id "plugin-look" again.


#include <ngui/LookPlugin.h>

#include "look/StubLook.h"


namespace {


class PluginLook : public ngui::test::StubLook {
public:
	ngui::LookInfo Info() const override
	{
		return {"plugin-look", "Plugin look", 1, ngui::ConformanceLevel::C1, {}};
	}
};


#if NGUI_TEST_PLUGIN_VARIANT != 1 && NGUI_TEST_PLUGIN_VARIANT != 3
ngui::ControlLook*
CreatePluginLook()
{
	return new PluginLook();
}
#endif


} // namespace


#if NGUI_TEST_PLUGIN_VARIANT == 0 || NGUI_TEST_PLUGIN_VARIANT == 4

NGUI_LOOK_PLUGIN(CreatePluginLook)

#elif NGUI_TEST_PLUGIN_VARIANT == 1

NGUI_LOOK_PLUGIN_EXPORT ngui::ControlLook*
ngui_create_control_look(std::uint32_t /*interfaceVersion*/)
{
	// built for an interface version the toolkit does not have (L12)
	return nullptr;
}


NGUI_LOOK_PLUGIN_EXPORT void
ngui_destroy_control_look(ngui::ControlLook* look)
{
	delete look;
}


NGUI_LOOK_PLUGIN_EXPORT const char*
ngui_look_plugin_build_key()
{
	static const std::string key = ngui::LookPluginBuildKey();
	return key.c_str();
}

#elif NGUI_TEST_PLUGIN_VARIANT == 2

NGUI_LOOK_PLUGIN_EXPORT ngui::ControlLook*
ngui_create_control_look(std::uint32_t /*interfaceVersion*/)
{
	return CreatePluginLook();
}


NGUI_LOOK_PLUGIN_EXPORT void
ngui_destroy_control_look(ngui::ControlLook* look)
{
	delete look;
}


NGUI_LOOK_PLUGIN_EXPORT const char*
ngui_look_plugin_build_key()
{
	return "ngui 0.0; another compiler";
}

#elif NGUI_TEST_PLUGIN_VARIANT == 3

NGUI_LOOK_PLUGIN_EXPORT int
ngui_test_not_a_look_plugin()
{
	return 3;
}

#endif
