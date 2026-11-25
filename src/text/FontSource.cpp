/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontSource.h>

#include <utility>


namespace ngui::text {


std::vector<SourceFont>
FontSource::List() const
{
	return {};
}


std::unique_ptr<FontSource>
SystemFontSource()
{
	// each is NotSupported on the systems it is not for
	if (auto directWrite = CreateDirectWriteSource())
		return std::move(*directWrite);
	if (auto fontconfig = CreateFontconfigSource())
		return std::move(*fontconfig);
	return nullptr;
}


} // namespace ngui::text
