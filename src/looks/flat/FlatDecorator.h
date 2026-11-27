/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The window frame of the look "flat": Haiku's FlatDecorator (made for the
// dark themes), ported line by line (K3); the buttons' bitmaps are drawn
// directly.


#include "decor/Decorator.h"


namespace ngui::detail {


class FlatDecorator : public Decorator {
public:
	explicit					FlatDecorator(const TextSystem& text);

protected:
			void				_DrawFrame(compat::CompatView& view, compat::BRect rect) override;
			void				_DrawTab(compat::CompatView& view, Tab* tab,
									compat::BRect rect) override;
			void				_DrawTitle(compat::CompatView& view, Tab* tab,
									compat::BRect rect) override;
			void				_DrawClose(compat::CompatView& view, Tab* tab,
									compat::BRect rect) override;
			void				_DrawZoom(compat::CompatView& view, Tab* tab,
									compat::BRect rect) override;

private:
			void				_GetComponentColors(Component component,
									ComponentColors colors, const Tab* tab) const;
			void				_DrawBlendedRect(compat::CompatView& view, compat::BRect rect,
									bool down, const ComponentColors& colors);
};


} // namespace ngui::detail
