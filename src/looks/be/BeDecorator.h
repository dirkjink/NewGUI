/*
 * Copyright 2001-2013 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The window frame of the look "be": Haiku's BeDecorator, the yellow tab of
// BeOS R5, ported line by line (K3). The buttons' bitmaps are drawn directly,
// pixel by pixel, instead of through a cache of bitmaps.


#include "decor/Decorator.h"


namespace ngui::detail {


class BeDecorator : public Decorator {
public:
	explicit					BeDecorator(const TextSystem& text);

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
			void				_GetButtonSizeAndOffset(const compat::BRect& tabRect,
									float* offset, float* size, float* inset) const override;

private:
			void				_GetComponentColors(Component component,
									ComponentColors colors, const Tab* tab) const;
			void				_DrawBevelRect(compat::CompatView& view, compat::BRect rect,
									bool down, compat::rgb_color light,
									compat::rgb_color shadow);
};


} // namespace ngui::detail
