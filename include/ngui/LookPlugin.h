/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Looks as plugins (specification 18.6, module M17c): what a plugin defines
// and what the toolkit checks before it uses one.


#include <cstdint>
#include <string>

#include <ngui/ControlLook.h>
#include <ngui/Version.h>


/*!	Marks a function a look plugin exports with C linkage. */
#if defined(_WIN32)
#define NGUI_LOOK_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define NGUI_LOOK_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif


namespace ngui {


/*!	What a look plugin and the toolkit that loads it must have in common:
	the toolkit's major and minor version, the compiler and its major
	version, the standard library and, with MSVC, the runtime library and
	the iterator debug level. The interface between them is made of C++
	vtables (specification 18.6), so they must be built alike; the loader
	refuses a plugin whose key differs (LookRegistry::LoadPlugin()).

	Each side computes the key with the compiler it is built with, which is
	why this function is inline.
*/
inline std::string
LookPluginBuildKey()
{
	std::string key = "ngui " + std::to_string(kVersionMajor) + "."
		+ std::to_string(kVersionMinor);
#if defined(__clang__)
	key += "; clang " + std::to_string(__clang_major__);
#elif defined(__GNUC__)
	key += "; gcc " + std::to_string(__GNUC__);
#elif defined(_MSC_VER)
	key += "; msvc " + std::to_string(_MSC_VER / 100);
#else
	key += "; unknown compiler";
#endif
#if defined(_LIBCPP_VERSION)
	key += "; libc++";
#elif defined(__GLIBCXX__)
	key += "; libstdc++";
#elif defined(_MSVC_STL_VERSION)
	key += "; msvc stl";
#endif
#if defined(_MSC_VER)
#if defined(_DLL)
	key += "; dynamic runtime";
#else
	key += "; static runtime";
#endif
	key += "; iterator debug " + std::to_string(_ITERATOR_DEBUG_LEVEL);
#endif
	return key;
}


} // namespace ngui


/*!	Defines the functions a look plugin exports (specification 18.6), with
	\a create, a function without arguments that returns a new look
	(`ngui::ControlLook*`, made with `new`):

	\code
	ngui::ControlLook* CreateMyLook() { return new MyLook(); }
	NGUI_LOOK_PLUGIN(CreateMyLook)
	\endcode

	- `ngui_create_control_look(interfaceVersion)` makes the look, or gives
	  nullptr if \a interfaceVersion is not ControlLook::kInterfaceVersion
	  (rule L12) or \a create throws.
	- `ngui_destroy_control_look(look)` destroys it with `delete`.
	- `ngui_look_plugin_build_key()` gives LookPluginBuildKey() as the
	  plugin was built.

	A plugin links libngui statically and so has its own copy of the code
	it uses. It must draw only through the DrawContext it is given (its
	Canvas, Palette and TextSystem), as rule L3 asks of every look: the
	toolkit's global objects (TextSystem::Default(), Palette::Default(),
	images of BBitmap) are other ones in the plugin.
*/
#define NGUI_LOOK_PLUGIN(create) \
	NGUI_LOOK_PLUGIN_EXPORT ::ngui::ControlLook* \
	ngui_create_control_look(std::uint32_t interfaceVersion) \
	{ \
		if (interfaceVersion != ::ngui::ControlLook::kInterfaceVersion) \
			return nullptr; \
		try { \
			return (create)(); \
		} catch (...) { \
			return nullptr; \
		} \
	} \
	NGUI_LOOK_PLUGIN_EXPORT void \
	ngui_destroy_control_look(::ngui::ControlLook* look) \
	{ \
		delete look; \
	} \
	NGUI_LOOK_PLUGIN_EXPORT const char* \
	ngui_look_plugin_build_key() \
	{ \
		static const std::string key = ::ngui::LookPluginBuildKey(); \
		return key.c_str(); \
	}
