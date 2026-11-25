/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/SvgExportBackend.h>


namespace {


// Whether \a xml is well-formed as far as the SVG export writes it: tags
// nested and closed in order, attributes quoted, no stray '<' or '&'
// outside markup and entities, text only from the allowed entities.
bool
IsWellFormed(std::string_view xml)
{
	std::vector<std::string_view> open;
	std::size_t at = 0;
	while (at < xml.size()) {
		if (xml[at] == '&') {
			const std::size_t end = xml.find(';', at);
			if (end == std::string_view::npos)
				return false;
			const std::string_view entity = xml.substr(at, end - at + 1);
			if (entity != "&amp;" && entity != "&lt;" && entity != "&gt;"
				&& entity != "&quot;") {
				return false;
			}
			at = end + 1;
			continue;
		}
		if (xml[at] != '<') {
			if (xml[at] == '>')
				return false;
			at++;
			continue;
		}
		const bool closing = at + 1 < xml.size() && xml[at + 1] == '/';
		std::size_t i = at + (closing ? 2 : 1);
		const std::size_t nameStart = i;
		while (i < xml.size() && xml[i] != ' ' && xml[i] != '>' && xml[i] != '/'
			&& xml[i] != '\n') {
			i++;
		}
		const std::string_view name = xml.substr(nameStart, i - nameStart);
		if (name.empty())
			return false;
		// the attributes: name="value", values without '<' or a raw quote
		bool selfClosing = false;
		while (i < xml.size() && xml[i] != '>') {
			if (xml[i] == '"') {
				const std::size_t end = xml.find('"', i + 1);
				if (end == std::string_view::npos
					|| xml.substr(i + 1, end - i - 1).find('<') != std::string_view::npos) {
					return false;
				}
				i = end + 1;
				continue;
			}
			if (xml[i] == '<')
				return false;
			selfClosing = xml[i] == '/';
			i++;
		}
		if (i >= xml.size())
			return false;
		if (closing) {
			if (open.empty() || open.back() != name)
				return false;
			open.pop_back();
		} else if (!selfClosing)
			open.push_back(name);
		at = i + 1;
	}
	return open.empty();
}


} // namespace


NGUI_FUZZ_TARGET(SvgExport)
{
	// What is tested: The SVG export of every list Deserialize() accepts
	// from arbitrary bytes, compact and pretty, with glyphs as text and as
	// paths (the placeholder text system has no outlines, so they fall
	// back to text), over the list's bounds.
	// Success criteria: Nothing crashes (checked by the sanitizers); each
	// export is well-formed XML with <svg> around everything, and the same
	// list gives the same bytes twice (E1).
	// Why it matters: Lists come from outside (a remote-rendering stream,
	// a saved document); an export must neither crash on them nor write a
	// file other programs cannot read, whatever text or numbers it holds.
	//
	// Given
	const auto list = ngui::gfx::DisplayList::Deserialize(std::as_bytes(data));
	if (!list.has_value())
		return;
	ngui::gfx::Rect bounds = (*list)->Bounds();
	if (bounds.IsEmpty())
		bounds = {0, 0, 1, 1};
	const bool pretty = !data.empty() && (data[0] & 1) != 0;
	const ngui::gfx::ExportOptions options{
		.text = !data.empty() && (data[0] & 2) != 0
			? ngui::gfx::ExportOptions::TextMode::Text
			: ngui::gfx::ExportOptions::TextMode::Paths,
		.pretty = pretty};

	// When
	const auto first = ngui::gfx::ExportSvg(**list, bounds, options);
	const auto second = ngui::gfx::ExportSvg(**list, bounds, options);

	// Then
	if (!first.has_value()) {
		// only for bounds that are not finite
		NGUI_FUZZ_CHECK(!bounds.IsFinite());
		return;
	}
	NGUI_FUZZ_CHECK(second.has_value() && *first == *second);
	NGUI_FUZZ_CHECK(first->starts_with("<svg "));
	NGUI_FUZZ_CHECK(first->find("</svg>") != std::string::npos);
	NGUI_FUZZ_CHECK(IsWellFormed(*first));
}
