/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
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


/*!	A part of a TextLayout's text in one font role: from \a start to the
	next run's start. */
struct TextStyleRun {
			std::size_t			start;	/*!< Byte offset where the run begins. */
			FontRole			role;	/*!< Its font. */
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
	from shaping each line. Only left-to-right text is laid out; the
	interface is meant to stay when right-to-left text comes: positions are
	asked for by offset, CaretLeft() and CaretRight() move visually, and a
	selection is a list of rectangles.
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
									/*!< \a text in the roles of \a runs (sorted by start;
										the first applies from 0). */

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
										line's). */

			void				Draw(gfx::Canvas& canvas, gfx::Point origin,
									const gfx::Color& color, int32 firstLine = 0,
									int32 lastLine = -1) const;
									/*!< Draws lines \a firstLine to \a lastLine (-1: the
										last) with the text rectangle at \a origin. */

private:
	// A piece of a line drawn as one glyph run: no tab, one role.
	struct Segment {
			float				x = 0;
			ShapedText			shaped;
	};

	struct LineData {
			Line				line;
			std::vector<float>	carets;
									// x of each offset from start to end
			std::vector<Segment> segments;
	};

			void				_Layout(const TextSystem& system);
			std::size_t			_FindLineBreak(const TextSystem& system, std::size_t from,
									float* ascent, float* descent, float* width) const;
			float				_Measure(const TextSystem& system, std::size_t from,
									std::size_t to, float x, std::vector<float>* carets,
									std::vector<Segment>* segments) const;
			void				_Metrics(const TextSystem& system, std::size_t from,
									std::size_t to, float* ascent, float* descent) const;
			FontRole			_RoleAt(std::size_t offset) const;
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
