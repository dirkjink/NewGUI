/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>
#include <string_view>


namespace ngui::svg {


/*!	\a svg with its CSS media queries resolved for an icon drawn \a width
	pixels wide: each `@media` rule whose condition holds is replaced by the
	rules inside it, each other one removed (library ngui_svg). lunasvg does
	not evaluate media queries; Haiku's icons (data/icons/haiku/) say their
	levels of detail with them, as `@media (max-width:21.07px){...}`.

	Conditions: a comma-separated list, any of which may hold; each the media
	types `all` or `screen` and the features `(min-width:Npx)` and
	`(max-width:Npx)`, joined by `and`, optionally after `only`. Anything else
	(other features or media types, `not`) does not hold, as a browser treats
	a query it does not know. A rule without its closing brace takes the rest
	of the text, as CSS reads it.
*/
std::string						ResolveMediaQueries(std::string_view svg, float width);


} // namespace ngui::svg
