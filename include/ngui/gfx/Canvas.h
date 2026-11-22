/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Line.h>
#include <ngui/gfx/Paint.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/RRect.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Region.h>


namespace ngui::gfx {


class DisplayListBuilder;


/*!	Records the drawing of one View into a DisplayList (specification 6).
	The Canvas replaces the drawing calls of Haiku's BView: it has no drawing
	state besides translation and clip, every operation gets its Paint.

	- **State:** Save() and Restore() keep translation and clip on a stack;
	  Translate() moves all following coordinates (no scaling or rotation in
	  the core profile).
	- **Clip:** a Region in whole pixels, initially the rounded bounds.
	  ClipRect() intersects, ClipOutRect() subtracts; the rectangle is
	  translated first and then rounded with `floor(v + 0.5)`
	  (specification 7). While the clip is empty, drawing operations are
	  dropped.
	- **Invalid input** (NaN or infinity, negative widths, invalid paints,
	  too few points, an empty span, an image handle 0 …) drops the
	  operation, deterministically on every backend (specification 6.2); in
	  debug builds with a warning on stderr. Operations on empty rectangles
	  have no effect and are dropped as well (specification 4.1).
	- **Recording:** coordinates are stored translated, gradients included;
	  rounded rectangles are stored normalized (RRect::Normalized()), so that
	  every backend draws the same radii.

	Not thread-safe: a Canvas belongs to the thread that records, normally
	the looper of its window.
*/
class Canvas {
public:
	explicit					Canvas(const Rect& bounds);
									/*!< A Canvas for a View with the given bounds; bounds
										that are not finite give an empty Canvas. */
								~Canvas();

								Canvas(const Canvas&) = delete;
									/*!< Not copyable. */
			Canvas&				operator=(const Canvas&) = delete;
									/*!< Not copyable. */

			void				Save();
									/*!< Pushes translation and clip. */
			void				Restore();
									/*!< Pops translation and clip; without a matching Save()
										a programming error (an assertion in debug builds,
										ignored otherwise). */
			void				Translate(float dx, float dy);
									/*!< Moves all following coordinates by `(dx, dy)`. */

			void				ClipRect(const Rect& rect);
									/*!< `clip := clip ∩ round(rect)`. */
			void				ClipOutRect(const Rect& rect);
									/*!< `clip := clip \ round(rect)`. */

			void				FillRect(const Rect& rect, const Paint& paint);
									/*!< Fills \a rect (rule A1). */
			void				FillRRect(const RRect& rrect, const Paint& paint);
									/*!< Fills the rounded rectangle. */
			void				FillRing(const RRect& outer, const RRect& inner,
									const Paint& paint);
									/*!< Fills between \a outer and \a inner, which must lie
										inside \a outer. */
			void				FillEllipse(const Rect& rect, const Paint& paint);
									/*!< Fills the ellipse inscribed in \a rect. */
			void				FillConvexPolygon(std::span<const Point> points,
									const Paint& paint);
									/*!< Fills a convex polygon of at least 3 points; debug
										builds warn about a non-convex one. */

			void				StrokeRect(const Rect& rect, const Paint& paint);
									/*!< Strokes inside \a rect, the outer edge flush with it. */
			void				StrokeRRect(const RRect& rrect, const Paint& paint);
									/*!< Strokes inside the rounded rectangle. */
			void				StrokePolyline(std::span<const Point> points, bool closed,
									const Paint& paint);
									/*!< Strokes at least 2 points, centered on the line. */
			void				StrokeArc(const Rect& oval, float startDeg, float sweepDeg,
									const Paint& paint);
									/*!< Strokes an arc of the ellipse inscribed in \a oval;
										degrees, 0° at 3 o'clock, positive clockwise. */
			void				DrawLines(std::span<const Line> lines, float width,
									Paint::Cap cap);
									/*!< Independent lines, each with its color (rule A3). */

			void				DrawImage(ImageHandle image, const Rect& src, const Rect& dst,
									float alpha = 1.0f);
									/*!< Draws the part \a src of \a image scaled to \a dst;
										\a alpha in `[0, 1]`. */
			void				DrawGlyphRun(const GlyphRun& run, Point origin,
									const Paint& paint);
									/*!< Draws the shaped \a run at \a origin in the color of
										\a paint (a gradient is ignored); copies the spans. */
			void				DrawList(const DisplayListRef& child, Point offset);
									/*!< Inserts \a child at \a offset under the current clip. */

			void				SetRecordGlyphText(bool record);
									/*!< Whether DrawGlyphRun() keeps the text and clusters of
										a run (default true; specification 10). */

			DisplayListRef		Finish();
									/*!< The recorded list; closes open Save() calls. The
										Canvas records nothing afterwards, and a second call
										returns nullptr. */

			std::uint32_t		DiscardedOperations() const;
									/*!< How many operations were dropped as invalid so far;
										operations on empty rectangles or under an empty clip
										do not count. */

private:
			struct State {
				Point			translation;
				Region			clip;
			};

			bool				_Accept(bool valid, const char* operation);
			bool				_CanDraw() const;
			std::uint32_t		_Clip();
			Point				_Moved(Point point) const;
			Rect				_Moved(const Rect& rect) const;
			RRect				_Moved(const RRect& rrect) const;
			Paint				_Moved(const Paint& paint) const;

			Rect				fBounds;
			std::unique_ptr<DisplayListBuilder> fBuilder;
			State				fState;
			std::vector<State>	fStack;
			std::vector<Point>	fScratchPoints;
			std::vector<Line>	fScratchLines;
			std::uint32_t		fDiscarded = 0;
			bool				fRecordGlyphText = true;
};


} // namespace ngui::gfx
