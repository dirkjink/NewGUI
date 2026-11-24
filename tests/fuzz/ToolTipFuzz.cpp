/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <ngui/InterfaceDefs.h>
#include <ngui/ToolTip.h>
#include <ngui/View.h>
#include <ngui/Window.h>


namespace {


using namespace ngui;


// The bytes of a fuzz input, taken one after the other; zeros at the end.
class Bytes {
public:
	explicit Bytes(std::span<const std::uint8_t> data)
		:
		fData(data)
	{
	}

	bool AtEnd() const
	{
		return fNext >= fData.size();
	}

	std::uint8_t Next()
	{
		return fNext < fData.size() ? fData[fNext++] : 0;
	}

	std::uint16_t Next16()
	{
		const std::uint16_t high = Next();
		return static_cast<std::uint16_t>((high << 8) | Next());
	}

private:
	std::span<const std::uint8_t> fData;
	std::size_t fNext = 0;
};


// A tooltip whose content has a size that can change.
class SizedToolTip : public BToolTip {
public:
	SizedToolTip()
		:
		BToolTip(std::make_unique<BView>(gfx::Rect{0, 0, 10, 10}, "content", BResizingMode{},
			BViewFlags{}))
	{
	}

	void Resize(float width, float height)
	{
		View()->SetExplicitMinSize({width, height});
		View()->SetExplicitPreferredSize({width, height});
		View()->SetExplicitMaxSize({width, height});
		ContentChanged();
	}
};


// A size of 0 to 2999.75 pixels in quarters; a large one sometimes, wider
// than the screen.
float
SizeFrom(std::uint16_t value)
{
	if (value >= 0xff00)
		return 70000.0f + static_cast<float>(value & 0xff);
	return static_cast<float>(value % 12000) / 4.0f;
}


} // namespace


NGUI_FUZZ_TARGET(ToolTip)
{
	// What is tested: A tooltip of a view in a window without a platform
	// (one screen of 65536 × 65536): its content sized, aligned (every
	// horizontal and vertical alignment), at a distance from the mouse and
	// sticky or not as the bytes say; then shown at mouse positions all
	// over the screen, resized and shown again, in any order.
	// Success criteria: No crash or sanitizer report. While shown, the
	// tooltip is one popup of the window, a pixel of border around its
	// content; the content lies on the screen, at least 2 pixels inside its
	// edges (Haiku's ResetWindowFrame()), and is never of negative size.
	// Why it matters: Tooltips are placed from the mouse, the screen and
	// sizes and distances applications choose; Haiku's algorithm moves and
	// cuts them, and a tooltip off the screen cannot be read.
	//
	Bytes bytes(data);
	BWindow window({0, 0, 400, 300}, "fuzz");
	BView* view = window.AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 400, 300}, "view",
		BResizingMode{}, BViewFlags{}));
	// The size in two steps: the order of a call's arguments is unspecified.
	const auto resize = [&](SizedToolTip& sized) {
		const float width = SizeFrom(bytes.Next16());
		const float height = SizeFrom(bytes.Next16());
		sized.Resize(width, height);
	};
	auto tip = std::make_shared<SizedToolTip>();
	resize(*tip);
	tip->SetMouseRelativeLocation({static_cast<float>(bytes.Next() % 64),
		static_cast<float>(bytes.Next() % 64)});
	const std::uint8_t alignment = bytes.Next();
	tip->SetAlignment({static_cast<BHorizontalAlignment>(alignment % 5),
		static_cast<BVerticalAlignment>((alignment / 5) % 5)});
	tip->SetSticky((bytes.Next() & 1) != 0);
	view->SetToolTip(tip);

	const auto check = [&] {
		NGUI_FUZZ_CHECK(window.CountPopups() <= 1);
		if (window.CountPopups() == 0)
			return;
		const gfx::Rect frame = window.PopupAt(0)->Frame();
		NGUI_FUZZ_CHECK(std::isfinite(frame.x0) && std::isfinite(frame.y0)
			&& std::isfinite(frame.x1) && std::isfinite(frame.y1));
		// the content, inside the border
		const gfx::Rect content{frame.x0 + 1, frame.y0 + 1, frame.x1 - 1, frame.y1 - 1};
		NGUI_FUZZ_CHECK(content.x1 >= content.x0 && content.y1 >= content.y0);
		NGUI_FUZZ_CHECK(content.x0 >= 2 && content.y0 >= 2);
		NGUI_FUZZ_CHECK(content.x1 <= 65534 && content.y1 <= 65534);
	};

	while (!bytes.AtEnd()) {
		switch (bytes.Next() % 4) {
			case 0:
			case 1:
			{
				// the mouse at a place on the screen: the window moves there
				const float x = static_cast<float>(bytes.Next16());
				const float y = static_cast<float>(bytes.Next16());
				window.MoveTo({x, y});
				view->ShowToolTip(tip);
				break;
			}
			case 2:
				resize(*tip);
				break;
			case 3:
				view->HideToolTip();
				break;
		}
		check();
	}
}
