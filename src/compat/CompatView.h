/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The compatibility layer for Haiku code (specification 17): a BView-like
// drawing API with state (high and low color, pen size, line mode, drawing
// mode, font, pen location) over the Canvas. It exists so that Haiku's
// control looks can be ported line by line (K3); it is internal (K2), and its
// headers are never installed.
//
// Rectangles and points are Haiku's (inclusive BRect, BPoint); they are
// converted when drawing, as table 17.2 says:
//   FromBRect(r)    = {r.left, r.top, r.right + 1, r.bottom + 1}
//   PixelCenter(p)  = {p.x + 0.5, p.y + 0.5}   for lines and polygons
//
// Without B_SUBPIXEL_PRECISE (SetFlags()), Haiku's app_server rounds the
// coordinates of rectangles, lines and polygons down to whole pixels before
// it draws them (Painter::_Align()); the layer does the same, so that ported
// code with fractional coordinates hits the same pixels.


#include <optional>
#include <string>
#include <vector>

#include <ngui/ControlLook.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Line.h>
#include <ngui/gfx/Paint.h>

#include "Bitmap.h"
#include "Font.h"
#include "Gradient.h"
#include "InterfaceDefs.h"
#include "Point.h"
#include "Rect.h"
#include "Region.h"
#include "Shape.h"


namespace ngui::compat {


// Specification 17.1.
inline gfx::Rect
FromBRect(BRect rect)
{
	return {rect.left, rect.top, rect.right + 1, rect.bottom + 1};
}


inline gfx::Point
PixelCenter(BPoint point)
{
	return {point.x + 0.5f, point.y + 0.5f};
}


class CompatView {
public:
								CompatView(DrawContext& context);
								~CompatView();

								CompatView(const CompatView&) = delete;
			CompatView&			operator=(const CompatView&) = delete;

	// Colors

			void				SetHighColor(rgb_color color);
			void				SetHighColor(uint8 r, uint8 g, uint8 b, uint8 a = 255);
			rgb_color			HighColor() const;
			void				SetLowColor(rgb_color color);
			void				SetLowColor(uint8 r, uint8 g, uint8 b, uint8 a = 255);
			rgb_color			LowColor() const;
			rgb_color			UiColor(color_which which) const;

	// State

			void				PushState();
			void				PopState();
			void				SetPenSize(float size);
			float				PenSize() const;
			void				SetLineMode(cap_mode cap, join_mode join,
									float miterLimit = 10.0f);
			void				SetDrawingMode(drawing_mode mode);
			drawing_mode		DrawingMode() const;
			void				MovePenTo(BPoint point);
			BPoint				PenLocation() const;
			void				SetFont(const BFont* font);
			void				GetFont(BFont* font) const;
			uint32				Flags() const;
			void				SetFlags(uint32 flags);

	// Clipping

			void				ClipToRect(BRect rect);
			void				ClipToInverseRect(BRect rect);

	// Drawing

			void				FillRect(BRect rect, pattern_kind pattern = B_SOLID_HIGH);
			void				FillRect(BRect rect, const BGradientLinear& gradient);
			void				StrokeRect(BRect rect, pattern_kind pattern = B_SOLID_HIGH);
			void				StrokeLine(BPoint start, BPoint end,
									pattern_kind pattern = B_SOLID_HIGH);
			void				StrokeLine(BPoint end, pattern_kind pattern = B_SOLID_HIGH);
			void				BeginLineArray(int32 count);
			void				AddLine(BPoint start, BPoint end, rgb_color color);
			void				EndLineArray();
			void				FillTriangle(BPoint p1, BPoint p2, BPoint p3,
									pattern_kind pattern = B_SOLID_HIGH);
			void				StrokeTriangle(BPoint p1, BPoint p2, BPoint p3,
									pattern_kind pattern = B_SOLID_HIGH);
			void				FillEllipse(BRect rect, pattern_kind pattern = B_SOLID_HIGH);
			void				FillEllipse(BRect rect, const BGradientLinear& gradient);
			void				StrokeEllipse(BRect rect, pattern_kind pattern = B_SOLID_HIGH);
			void				StrokeArc(BRect rect, float startAngle, float arcAngle,
									pattern_kind pattern = B_SOLID_HIGH);
			void				FillRegion(const BRegion* region,
									pattern_kind pattern = B_SOLID_HIGH);
			void				StrokeRoundRect(BRect rect, float xRadius, float yRadius,
									pattern_kind pattern = B_SOLID_HIGH);
			void				StrokeShape(const BShape* shape,
									pattern_kind pattern = B_SOLID_HIGH);
			void				FillShape(const BShape* shape,
									pattern_kind pattern = B_SOLID_HIGH);
			void				FillShape(const BShape* shape, const BGradientLinear& gradient);
			void				DrawString(const char* string, BPoint where);
			float				StringWidth(const char* string) const;
			void				DrawBitmap(gfx::ImageHandle bitmap, BPoint where);
			void				DrawBitmap(const BBitmap* bitmap, BPoint where);

private:
			struct State {
				rgb_color		high;
				rgb_color		low;
				float			penSize;
				gfx::Paint::Cap	cap;
				gfx::Paint::Join join;
				drawing_mode	mode;
				BPoint			pen;
				BFont			font;
			};

			gfx::Paint			_Paint(pattern_kind pattern) const;
			std::optional<gfx::Paint> _Paint(const BGradientLinear& gradient,
									BPoint start, BPoint end) const;
			void				_DrawShape(const BShape* shape, bool fill,
									const gfx::Paint& paint);
			BPoint				_Align(BPoint point) const;
			BRect				_Align(BRect rect) const;

			DrawContext&		fContext;
			State				fState;
			std::vector<State>	fStack;
			std::vector<gfx::Line> fLineArray;
			bool				fInLineArray = false;
			uint32				fFlags = 0;
			int					fCanvasSaves = 0;
};


} // namespace ngui::compat
