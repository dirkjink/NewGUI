/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The layout lint and the layout snapshot of specification 20.5 and 20.6,
// on the window's thread (RunOnLooper()).

#include <ngui/test/Lint.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <typeinfo>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

#include <ngui/Catalog.h>
#include <ngui/LocaleRoster.h>
#include <ngui/LookRegistry.h>
#include <ngui/Palette.h>
#include <ngui/Window.h>

#include "LooperPort.h"


namespace ngui::test {


namespace {


using namespace std::chrono_literals;

// How far a view may reach out or be too small before it counts.
constexpr float kSlack = 0.5f;


// The class of \a view without namespaces, the same with every compiler.
std::string
ClassName(const BView& view)
{
	std::string name = typeid(view).name();
#if defined(__GNUC__) || defined(__clang__)
	int status = 0;
	char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
	if (status == 0 && demangled != nullptr)
		name = demangled;
	std::free(demangled);
#endif
	for (const std::string_view prefix : {"class ", "struct "}) {
		if (name.starts_with(prefix))
			name.erase(0, prefix.size());
	}
	if (const std::size_t colons = name.rfind("::"); colons != std::string::npos)
		name.erase(0, colons + 2);
	return name;
}


std::string
Number(float value)
{
	char buffer[32];
	(void)std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
	return buffer;
}


std::string
RectText(const gfx::Rect& rect)
{
	return "(" + Number(rect.x0) + ", " + Number(rect.y0) + ", " + Number(rect.x1) + ", "
		+ Number(rect.y1) + ")";
}


// A view the lint looks at.
struct Entry {
	BView*			view;
	std::string		path;
	gfx::Rect		inWindow;
};


bool
Excluded(const BView& view, const std::string& path, const LintOptions& options)
{
	return std::ranges::any_of(options.exclude, [&](const std::string& excluded) {
		return excluded == view.Name() || excluded == path;
	});
}


// The visible views below \\a view (or the window's top views), depth first.
void
Collect(BView* view, const std::string& path, const LintOptions& options,
	std::vector<Entry>* entries)
{
	if (view->IsHidden() || Excluded(*view, path, options))
		return;
	entries->push_back({view, path, view->ConvertToWindow(view->Bounds())});
	for (std::int32_t i = 0; i < view->CountChildren(); i++) {
		BView* child = view->ChildAt(i);
		Collect(child, path + "/" + child->Name(), options, entries);
	}
}


bool
Overlap(const gfx::Rect& a, const gfx::Rect& b)
{
	return std::min(a.x1, b.x1) - std::max(a.x0, b.x0) > kSlack
		&& std::min(a.y1, b.y1) - std::max(a.y0, b.y0) > kSlack;
}


// Whether \\a after reads after \\a before: on the same row (they share
// most of their height) to its right, or below it.
bool
ReadsAfter(const gfx::Rect& before, const gfx::Rect& after)
{
	const float shared = std::min(before.y1, after.y1) - std::max(before.y0, after.y0);
	const float lower = std::min(before.Height(), after.Height());
	if (shared > lower / 2)
		return after.x0 >= before.x0 - kSlack;
	return after.y0 >= before.y0 - kSlack;
}


// The checks of one window, on its thread.
std::vector<LintFinding>
LintWindow(BWindow& window, const LintOptions& options)
{
	window.UpdateIfNeeded();
	const std::string title(window.Title());
	std::vector<Entry> entries;
	for (std::int32_t i = 0; i < window.CountChildren(); i++) {
		BView* top = window.ChildAt(i);
		Collect(top, top->Name(), options, &entries);
	}
	std::vector<LintFinding> findings;
	const auto add = [&](LintCheck check, LintSeverity severity, const std::string& path,
		std::string message) {
		findings.push_back({check, severity, {}, title, path, std::move(message)});
	};
	const auto pathOf = [&](const BView* view) -> const std::string* {
		for (const Entry& entry : entries) {
			if (entry.view == view)
				return &entry.path;
		}
		return nullptr;
	};

	// texts cut to fit first: such a view is also below its minimum, for
	// the same reason, which is not reported twice
	std::set<const BView*> truncated;
	// texts cut to fit: each view drawn anew, its children's lists kept,
	// so that a report belongs to the view that drew
	const gfx::Caps caps;
	const DrawEnvironment environment{caps, Palette::Default(), TextSystem::Default(), 1};
	for (const Entry& entry : entries) {
		std::set<std::string> cut;
		TextSystem::SetTruncationObserver([&](std::string_view text, std::string_view shown) {
			truncated.insert(entry.view);
			if (cut.insert(std::string(text)).second) {
				add(LintCheck::Truncated, LintSeverity::Error, entry.path,
					"it cuts \"" + std::string(text) + "\" to \"" + std::string(shown) + "\"");
			}
		});
		entry.view->Invalidate();
		(void)entry.view->RecordTree(environment);
		TextSystem::SetTruncationObserver(nullptr);
	}
	// the window's next frame records them in its own environment again
	for (const Entry& entry : entries)
		entry.view->Invalidate();

	const gfx::Rect windowBounds = window.Bounds();
	for (const Entry& entry : entries) {
		BView* view = entry.view;
		const gfx::Rect frame = view->Frame();
		// beyond the parent or the window
		const gfx::Rect outer = view->Parent() != nullptr ? view->Parent()->Bounds()
			: windowBounds;
		if (frame.x0 < outer.x0 - kSlack || frame.y0 < outer.y0 - kSlack
			|| frame.x1 > outer.x1 + kSlack || frame.y1 > outer.y1 + kSlack) {
			add(LintCheck::Outside, LintSeverity::Error, entry.path,
				"its frame " + RectText(frame) + " reaches beyond "
				+ (view->Parent() != nullptr ? "its parent " : "the window ")
				+ RectText(outer));
		}
		// smaller than its minimum
		const gfx::Size minimum = view->MinSize();
		if (!truncated.contains(view)
			&& (frame.Width() + kSlack < minimum.w || frame.Height() + kSlack < minimum.h)) {
			add(LintCheck::BelowMinimum, LintSeverity::Error, entry.path,
				"it is " + Number(frame.Width()) + " x " + Number(frame.Height())
				+ ", its minimum " + Number(minimum.w) + " x " + Number(minimum.h));
		}
		// overlapping visible siblings, each pair once: reported at the
		// earlier child, compared with the later ones (in child order, not
		// by address, so that the report is the same on every platform)
		const auto children = [&](const BView* parent) {
			std::vector<BView*> list;
			if (parent == nullptr) {
				for (std::int32_t i = 0; i < window.CountChildren(); i++)
					list.push_back(window.ChildAt(i));
			} else {
				for (std::int32_t i = 0; i < parent->CountChildren(); i++)
					list.push_back(parent->ChildAt(i));
			}
			return list;
		};
		bool later = false;
		for (BView* sibling : children(view->Parent())) {
			if (!later) {
				later = sibling == view;
				continue;
			}
			const std::string* siblingPath = pathOf(sibling);
			if (siblingPath != nullptr && Overlap(frame, sibling->Frame())) {
				add(LintCheck::Overlap, LintSeverity::Error, entry.path,
					"it overlaps " + *siblingPath);
			}
		}
	}

	// the focus order against the reading order
	if (options.focusOrder) {
		const Entry* previous = nullptr;
		for (const Entry& entry : entries) {
			if (!entry.view->Flags().Has(BViewFlag::Navigable))
				continue;
			if (previous != nullptr && !ReadsAfter(previous->inWindow, entry.inWindow)) {
				add(LintCheck::FocusOrder, LintSeverity::Warning, entry.path,
					"Tab comes here after " + previous->path + ", which reads after it");
			}
			previous = &entry;
		}
	}
	return findings;
}


void
LayoutLines(const BView& view, int depth, std::ostringstream& out)
{
	out << std::string(static_cast<std::size_t>(depth) * 2, ' ') << ClassName(view) << " \""
		<< view.Name() << "\" " << RectText(view.Frame());
	if (view.IsHidden())
		out << " hidden";
	out << "\n";
	for (std::int32_t i = 0; i < view.CountChildren(); i++)
		LayoutLines(*view.ChildAt(i), depth + 1, out);
}


const char*
CheckName(LintCheck check)
{
	switch (check) {
		case LintCheck::Overlap:
			return "overlap";
		case LintCheck::Truncated:
			return "truncated";
		case LintCheck::BelowMinimum:
			return "below minimum";
		case LintCheck::Outside:
			return "outside";
		case LintCheck::FocusOrder:
			return "focus order";
	}
	return "";
}


} // namespace


bool
LintReport::HasErrors() const
{
	return Count(LintSeverity::Error) > 0;
}


std::size_t
LintReport::Count(LintSeverity severity) const
{
	return static_cast<std::size_t>(std::ranges::count(findings, severity,
		&LintFinding::severity));
}


std::string
LintReport::Text() const
{
	std::ostringstream out;
	for (const LintFinding& finding : findings) {
		out << (finding.severity == LintSeverity::Error ? "error" : "warning") << ": ";
		if (!finding.configuration.empty())
			out << "[" << finding.configuration << "] ";
		out << finding.window << ": " << finding.path << " (" << CheckName(finding.check)
			<< "): " << finding.message << "\n";
	}
	return out.str();
}


LintReport
Lint(const WindowRef& window, const LintOptions& options)
{
	LintReport report;
	platform::HeadlessWindow* platformWindow = window.PlatformWindow();
	if (platformWindow == nullptr)
		return report;
	(void)RunOnLooper(platformWindow->Target(), [&](BLooper& looper) {
		if (auto* target = dynamic_cast<BWindow*>(&looper))
			report.findings = LintWindow(*target, options);
	}, 30s);
	return report;
}


std::string
LayoutText(const WindowRef& window)
{
	std::ostringstream out;
	platform::HeadlessWindow* platformWindow = window.PlatformWindow();
	if (platformWindow == nullptr)
		return {};
	(void)RunOnLooper(platformWindow->Target(), [&](BLooper& looper) {
		auto* target = dynamic_cast<BWindow*>(&looper);
		if (target == nullptr)
			return;
		target->UpdateIfNeeded();
		out << "BWindow \"" << target->Title() << "\" " << RectText(target->Bounds()) << "\n";
		for (std::int32_t i = 0; i < target->CountChildren(); i++)
			LayoutLines(*target->ChildAt(i), 1, out);
	}, 30s);
	return out.str();
}


std::vector<LintConfiguration>
DefaultLintMatrix()
{
	return {{"1x", 1, false, nullptr, {}}, {"1.5x", 1.5f, false, nullptr, {}},
		{"2x", 2, false, nullptr, {}}, {"pseudo", 1, true, nullptr, {}}};
}


LintReport
LintMatrix(std::span<const LintConfiguration> configurations,
	const std::function<std::unique_ptr<BApplication>()>& makeApplication,
	const std::function<void(Driver&)>& openWindows, const LintOptions& options)
{
	LintReport report;
	for (const LintConfiguration& configuration : configurations) {
		const TextSystem* text = &TextSystem::Default();
		const std::shared_ptr<const BCatalog> catalog = BLocaleRoster::Default()->Catalog();
		const std::string look(LookRegistry::Default().Current()->Info().id);
		if (configuration.text != nullptr)
			TextSystem::SetDefault(configuration.text);
		if (configuration.pseudoLocalized)
			BLocaleRoster::Default()->SetCatalog(BCatalog::Pseudo());
		if (!configuration.look.empty())
			(void)LookRegistry::Default().SetCurrent(configuration.look);
		{
			std::unique_ptr<BApplication> app = makeApplication();
			platform::HeadlessPlatform::Options screen;
			screen.scale = configuration.scale;
			(void)app->UsePlatform(std::make_unique<platform::HeadlessPlatform>(screen));
			Driver driver(*app);
			openWindows(driver);
			(void)driver.WaitForIdle();
			for (const WindowRef& window : driver.Windows()) {
				platform::HeadlessWindow* platformWindow = window.PlatformWindow();
				if (platformWindow == nullptr || platformWindow->IsPopup())
					continue;
				for (LintFinding& finding : Lint(window, options).findings) {
					finding.configuration = configuration.name;
					report.findings.push_back(std::move(finding));
				}
			}
		}
		(void)LookRegistry::Default().SetCurrent(look);
		BLocaleRoster::Default()->SetCatalog(catalog);
		TextSystem::SetDefault(text);
	}
	return report;
}


} // namespace ngui::test
