/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Region.h>


namespace ngui::gfx {


/*!	Writes a DisplayList; used by the Canvas only. Takes coordinates that
	are already translated and arguments the Canvas has already validated
	(specification 6.2); it records them as they are.
*/
class DisplayListBuilder {
public:
	explicit					DisplayListBuilder(const Rect& bounds);

			std::uint32_t		AddClip(const Region& clip);
									// Index of the clip; reuses the previous index if the
									// rectangles are the same (specification 11.2).

			void				FillRect(std::uint32_t clip, const Rect& rect,
									const Paint& paint);
			void				FillRRect(std::uint32_t clip, const RRect& rrect,
									const Paint& paint);
			void				FillRing(std::uint32_t clip, const RRect& outer,
									const RRect& inner, const Paint& paint);
			void				FillEllipse(std::uint32_t clip, const Rect& rect,
									const Paint& paint);
			void				FillConvexPolygon(std::uint32_t clip,
									std::span<const Point> points, const Paint& paint);
			void				StrokeRect(std::uint32_t clip, const Rect& rect,
									const Paint& paint);
			void				StrokeRRect(std::uint32_t clip, const RRect& rrect,
									const Paint& paint);
			void				StrokePolyline(std::uint32_t clip,
									std::span<const Point> points, bool closed,
									const Paint& paint);
			void				StrokeArc(std::uint32_t clip, const Rect& oval,
									float startDeg, float sweepDeg, const Paint& paint);
			void				DrawLines(std::uint32_t clip, std::span<const Line> lines,
									float width, Paint::Cap cap);
			void				DrawImage(std::uint32_t clip, ImageHandle image,
									const Rect& src, const Rect& dst, float alpha);
			void				DrawGlyphRun(std::uint32_t clip, const GlyphRun& run,
									Point origin, const Color& color);
			void				DrawList(std::uint32_t clip, const DisplayListRef& child,
									Point offset);

			DisplayListRef		Finish();
									// The list; the builder is empty afterwards.

private:
			template<typename Record>
			std::size_t			_BeginOp(OpCode code, std::uint32_t clip,
									const Record& record, std::size_t trailingBytes);
			void				_Write(std::size_t offset, const void* data,
									std::size_t size);

			std::unique_ptr<DisplayList> fList;
};


} // namespace ngui::gfx
