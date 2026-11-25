/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/InterfaceDefs.h>
#include <ngui/SupportDefs.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>


namespace ngui {


namespace gfx {
class Canvas;
}


/*!	A part of a TextLayout's text in one font and color: from \a start to
	the next run's start. */
struct TextStyleRun {
			std::size_t			start;	/*!< Byte offset where the run begins. */
			FontSpec			font;	/*!< Its font at its size. */
			std::optional<gfx::Color> color = std::nullopt;
										/*!< Its color; none: the color Draw() is given. */
};


/*!	How a TextLayout lays its text out. */
struct TextLayoutOptions {
			float				width = 0;
									/*!< The width of the text rectangle in pixels: the
										lines wrap within it (with #wrap), and #alignment
										places them in it. */
			bool				wrap = true;
									/*!< Whether lines wrap at #width (Haiku's
										SetWordWrap()); otherwise only after line breaks. */
			float				tabWidth = 28;
									/*!< The distance of tab stops (Haiku's SetTabWidth()). */
			BHorizontalAlignment alignment = BHorizontalAlignment::Left;
									/*!< Left, Center or Right (Haiku's SetAlignment()). */
};


/*!	Text laid out in lines (module X6): where the lines break, where a
	caret stands, which offset a point hits, where the caret moves; the
	ground of BTextView. Coordinates are relative to the top left corner of
	the text rectangle; offsets are byte offsets into the UTF-8 text.

	The lines follow Haiku's BTextView (_FindLineBreak()): a line ends at a
	line break opportunity of TextSystem::Boundaries() whose text reaches
	the width, white space at the end hangs over, a word wider than a line
	is broken between graphemes, and a line break ends a line. The next
	line begins at ceil(top + ascent + descent) + 1, the descent with the
	leading; text ending with a line break has an empty last line.

	NewGUI: the boundaries come from the text system (Unicode's rules in
	the text subsystem, Haiku: its own character classes), tab stops count
	from the line's start (Haiku: from each measured piece), and the widths
	from shaping each line. A line is drawn in the visual order of its runs
	(UAX #9, module X3): each paragraph takes the direction of its first
	strong character, right-to-left runs are shaped right to left; the
	alignment stays absolute, as in Haiku (Left is left). In a line with
	right-to-left runs the caret before a character stands at its leading
	edge (the right one right to left); where two directions meet, at the
	edge of the character in the paragraph's direction (the "strong"
	caret), and after the line's text at its end in that direction.
	CaretLeft() and CaretRight() move over the grapheme left or right on
	the screen, OffsetAt() hits the nearer edge of a grapheme, and
	SelectionRects() gives a rectangle per piece of the range on the
	screen. Lines without right-to-left runs behave as Haiku's.
*/
class TextLayout {
public:
	/*!	A line: its offsets and vertical metrics (Haiku's STELine). */
	struct Line {
			std::size_t			start = 0;	/*!< Offset of its first byte. */
			std::size_t			end = 0;	/*!< Offset after it: after trailing white
												space and its line break. */
			float				top = 0;	/*!< Its top (Haiku's origin). */
			float				ascent = 0;	/*!< Above the baseline. */
			float				descent = 0;
											/*!< Below the baseline, with the leading. */
			float				width = 0;	/*!< Width of its text without the line
												break (Haiku's LineWidth()). */
	};

								TextLayout();
									/*!< An empty text, one empty plain line. */
								TextLayout(const TextSystem& system, std::string_view text,
									FontRole role, const TextLayoutOptions& options = {});
									/*!< \a text in one font role. */
								TextLayout(const TextSystem& system, std::string_view text,
									std::span<const TextStyleRun> runs,
									const TextLayoutOptions& options = {});
									/*!< \a text in the fonts of \a runs (sorted by
										start; before the first, the plain font). A run
										that begins inside a code point begins at its
										start. */

			std::string_view	Text() const;
									/*!< The text laid out. */
			const TextLayoutOptions& Options() const;
									/*!< How it was laid out. */

			int32				CountLines() const;
									/*!< The number of lines, at least one. */
			const Line&			LineAt(int32 index) const;
									/*!< Line \a index (clamped). */
			int32				LineAtOffset(std::size_t offset) const;
									/*!< The line of \a offset; an offset where a wrapped
										line ends belongs to the next one (Haiku's
										LineAt()). */
			int32				LineAtY(float y) const;
									/*!< The line at \a y (clamped). */
			float				LineHeight(int32 index) const;
									/*!< From the top of line \a index to the next. */
			float				Baseline(int32 index) const;
									/*!< Where line \a index is drawn: top + ascent + 1, as
										Haiku's _DrawLine(). */
			float				Height() const;
									/*!< All lines, as Haiku's TextHeight(). */
			float				MaxWidth() const;
									/*!< The widest line. */
			float				AlignmentOffset(int32 index) const;
									/*!< How far line \a index is moved right by the
										alignment. */

			gfx::Point			PointAt(std::size_t offset, float* height = nullptr) const;
									/*!< The top of the caret at \a offset, rounded, and the
										line's height (Haiku's PointAt()). */
			std::size_t			OffsetAt(gfx::Point point) const;
									/*!< The caret offset nearest to \a point (Haiku's
										OffsetAt(): never after a line break, nor after
										the space where a line wraps). */
			std::size_t			OffsetAt(int32 line, float x) const;
									/*!< The caret offset of \a line nearest to \a x. */

			std::size_t			CaretRight(std::size_t offset) const;
									/*!< The caret position right of \a offset: the next
										grapheme boundary. */
			std::size_t			CaretLeft(std::size_t offset) const;
									/*!< The caret position left of \a offset. */
			std::size_t			NextBoundary(std::size_t offset, TextBoundary kind) const;
									/*!< The first boundary of \a kind after \a offset, or
										the end. */
			std::size_t			PreviousBoundary(std::size_t offset,
									TextBoundary kind) const;
									/*!< The last boundary of \a kind before \a offset, or
										0. */
			bool				IsBoundary(std::size_t offset, TextBoundary kind) const;
									/*!< Whether \a offset is a boundary of \a kind. */

			std::vector<gfx::Rect> SelectionRects(std::size_t from, std::size_t to) const;
									/*!< The parts of the lines between \a from and \a to,
										one rectangle per line (from its top to the next
										line's); in a line with right-to-left runs, one per
										piece of the range on the screen. */
			bool				HasRightToLeft() const;
									/*!< Whether a line has right-to-left runs: then carets
										move, hit and select visually (module X3). */

			/*!	Maps the color of a run as it is drawn, e.g. inverted. */
			using				ColorMap = gfx::Color (*)(const gfx::Color& color);

			void				Draw(gfx::Canvas& canvas, gfx::Point origin,
									const gfx::Color& color, int32 firstLine = 0,
									int32 lastLine = -1, ColorMap map = nullptr) const;
									/*!< Draws lines \a firstLine to \a lastLine (-1: the
										last) with the text rectangle at \a origin, the
										runs without a color in \a color; \a map, if
										given, changes each color. */

private:
	// A piece of a line drawn as one glyph run: no tab, one run.
	struct Segment {
			float				x = 0;
			ShapedText			shaped;
			std::optional<gfx::Color> color;
	};

	// A grapheme cluster of a line with right-to-left runs, where it is on
	// the screen.
	struct Cluster {
			std::size_t			start = 0;
			std::size_t			end = 0;
			float				left = 0;
			float				right = 0;
			bool				rightToLeft = false;
	};

	struct LineData {
			Line				line;
			std::vector<float>	carets;
									// x of each offset from start to end
			std::vector<Segment> segments;
			std::vector<Cluster> clusters;
									// left to right; only in lines with right-to-left
									// runs (module X3), the others are left to right
			bool				rightToLeft = false;
									// its paragraph's direction
	};

			void				_Layout(const TextSystem& system);
			std::size_t			_FindLineBreak(const TextSystem& system, std::size_t from,
									float* ascent, float* descent, float* width) const;
			float				_Measure(const TextSystem& system, std::size_t from,
									std::size_t to, float x, std::vector<float>* carets,
									std::vector<Segment>* segments,
									LineData* visual = nullptr) const;
			void				_PlaceCarets(const LineData& data, std::size_t from,
									std::size_t to, std::vector<float>* carets) const;
			std::size_t			_ClusterRightOf(const LineData& data, float x) const;
			std::size_t			_ClusterLeftOf(const LineData& data, float x) const;
			std::size_t			_EdgeOffset(int32 lineNum, std::size_t index,
									bool rightEdge) const;
			std::size_t			_OnBoundary(std::size_t offset) const;
			void				_Metrics(const TextSystem& system, std::size_t from,
									std::size_t to, float* ascent, float* descent) const;
			const TextStyleRun&	_RunAt(std::size_t offset) const;
			std::size_t			_RunEnd(std::size_t offset) const;
			std::size_t			_ContentEnd(const Line& line) const;
			float				_X(int32 line, std::size_t offset) const;
			bool				_IsOnEmptyLastLine(std::size_t offset) const;

			std::string			fText;
			std::vector<TextStyleRun> fRuns;
			TextLayoutOptions	fOptions;
			std::vector<TextBoundaryFlags> fBoundaries;
			std::vector<LineData> fLines;
			float				fBottom = 0;
			float				fNullStyleHeight = 0;
};


} // namespace ngui
