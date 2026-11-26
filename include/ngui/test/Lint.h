/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <ngui/Application.h>
#include <ngui/TextSystem.h>
#include <ngui/test/Driver.h>


namespace ngui::test {


/*!	What the layout lint checks (specification 20.6). */
enum class LintCheck : std::uint8_t {
	Overlap,		/*!< Two visible siblings overlap. */
	Truncated,		/*!< A view cut a text to fit (the text system's report). */
	BelowMinimum,	/*!< A view is smaller than its minimum size. */
	Outside,		/*!< A view reaches beyond its parent or the window. */
	FocusOrder,		/*!< Tab goes against the reading order. */
	Unnamed			/*!< A control has no name for assistive technology (its
						label is empty and BView::SetAccessibleName() not set). */
};


/*!	How bad a finding is. */
enum class LintSeverity : std::uint8_t {
	Warning,	/*!< Worth a look (the focus order). */
	Error		/*!< A broken layout. */
};


/*!	A finding of the layout lint. */
struct LintFinding {
			LintCheck			check = LintCheck::Overlap;	/*!< What failed. */
			LintSeverity		severity = LintSeverity::Error;	/*!< How bad. */
			std::string			configuration;	/*!< The matrix configuration, or empty. */
			std::string			window;			/*!< The window's title. */
			std::string			path;			/*!< The view, as names from the window's
													top views down ("box/ok"). */
			std::string			message;		/*!< What is wrong, in words. */
};


/*!	What the lint leaves out. */
struct LintOptions {
			std::vector<std::string> exclude;
									/*!< Views by name or path: they and their children
										are not checked. */
			bool				focusOrder = true;
									/*!< Whether to warn about the focus order. */
			bool				accessibleNames = true;
									/*!< Whether to warn about controls without a name
										for assistive technology. */
};


/*!	The findings of a lint run. */
struct LintReport {
			std::vector<LintFinding> findings;	/*!< All of them, in order. */

			bool				HasErrors() const;
									/*!< Whether a finding is an error. */
			std::size_t			Count(LintSeverity severity) const;
									/*!< How many findings of \a severity. */
			std::string			Text() const;
									/*!< One line per finding, for test output. */
};


/*!	A configuration of the lint's matrix (specification 20.6). */
struct LintConfiguration {
			std::string			name;		/*!< Names the findings, e.g. "2x". */
			float				scale = 1;	/*!< The virtual screen's scale. */
			bool				pseudoLocalized = false;
									/*!< Texts through BCatalog::Pseudo(): a third longer
										(the application's texts must use B_TRANSLATE). */
			const TextSystem*	text = nullptr;
									/*!< A text system to use, e.g. a larger font
										(FontTextSystem::CreateBundled(18)); nullptr for
										the default. */
			std::string			look;		/*!< A look by id; empty for the current. */
};


/*!	Checks the views of \a window (specification 20.6): visible siblings
	that overlap, texts cut to fit, views smaller than their minimum size,
	views beyond their parent or the window, and (warnings) navigable views
	whose focus order goes against the reading order (left to right, top to
	bottom) and controls without a name for assistive technology (buttons,
	check boxes, sliders, text fields, menu fields, lists). Runs on the
	window's thread; hidden views are left out. The truncation check draws
	each view anew; a view that cuts a text is not reported below its
	minimum as well (the same cause). */
LintReport Lint(const WindowRef& window, const LintOptions& options = {});

/*!	The layout snapshot of \a window (specification 20.5): a line per view,
	indented by depth, with its class, name, frame in its parent and
	whether it is hidden. Stable across compilers. */
std::string LayoutText(const WindowRef& window);

/*!	The default matrix: scales 1, 1.5 and 2, and pseudo-localized at 1. A
	larger font needs a text system of the application's, given as a
	configuration's text. */
std::vector<LintConfiguration> DefaultLintMatrix();

/*!	Lints an application in each configuration: \a makeApplication makes a
	new one (on the headless platform at the configuration's scale),
	\a openWindows opens its windows through the driver; then every window
	that is no popup is linted. Restores the default text system, look and
	catalog afterwards. */
LintReport LintMatrix(std::span<const LintConfiguration> configurations,
	const std::function<std::unique_ptr<BApplication>()>& makeApplication,
	const std::function<void(Driver&)>& openWindows, const LintOptions& options = {});


} // namespace ngui::test
