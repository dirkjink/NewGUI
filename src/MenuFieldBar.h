/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The menu bar inside a BMenuField (Haiku's _BMCMenuBar_ in BMCPrivate.h):
// one item, whose submenu is the field's menu. Internal.


#include <ngui/MenuBar.h>


namespace ngui {


class BMenuField;


// Haiku's kVMargin: the space between the menu field's frame and its bar.
inline constexpr float kMenuFieldMargin = 2.0f;


class MenuFieldBar : public BMenuBar {
public:
								MenuFieldBar(gfx::Rect frame, bool fixedSize,
									BMenuField* menuField);
	explicit					MenuFieldBar(BMenuField* menuField);

			void				AttachedToWindow() override;
			void				Draw(DrawContext& context) override;
			void				MouseDown(gfx::Point where) override;
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
			void				FrameResized(float width, float height) override;
			void				MakeFocus(bool focused = true) override;
			void				SetMaxContentWidth(float width) override;
			// part of its menu field, which is the accessible node
			void				GetAccessibleInfo(AccessibleInfo* info) const override;
			void				SetEnabled(bool enabled) override;

			void				TogglePopUpMarker(bool show)
									{ fShowPopUpMarker = show; }
			bool				IsPopUpMarkerShown() const
									{ return fShowPopUpMarker; }

			gfx::Size			MinSize() override;
			gfx::Size			MaxSize() override;

private:
			void				_Init();

			BMenuField*			fMenuField;
			bool				fFixedSize;
			bool				fShowPopUpMarker = true;
			bool				fIsInside = false;
};


} // namespace ngui
