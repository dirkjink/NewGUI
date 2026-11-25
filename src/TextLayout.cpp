/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * The line breaking, positions and hit testing of Haiku's BTextView
 * (src/kits/interface/TextView.cpp, commit b1d497bb69: _RecalculateLineBreaks(),
 * _FindLineBreak(), PointAt(), OffsetAt(), LineWidth(), TextHeight(),
 * _ActualTabWidth()), as a layout of its own (module X6). NewGUI (see
 * TextLayout.h):
 * - Line break opportunities, graphemes and words come from
 *   TextSystem::Boundaries() instead of CanEndLine() and the character
 *   classes; a line also ends at any mandatory break, and a word broken
 *   between graphemes does not run past one.
 * - Widths are carets of the shaped pieces of a line (TextSystem::
 *   CaretPositions()), tab stops count from the line's start.
 * - Text ending with a line break has an empty last line of its own
 *   (Haiku: _IsOnEmptyLastLine()).
 * - The text width is a pixel count; Haiku compared with its width, one
 *   less.
 *
 * Authors (Haiku):
 *		Hiroshi Lockheimer (BTextView is based on his STEngine)
 *		Marc Flerackers, mflerackers@androme.be
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Stephan Aßmus, superstippi@gmx.de
 *		Oliver Tappe, zooey@hirschkaefer.de
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <ngui/TextLayout.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include <ngui/gfx/Canvas.h>


namespace ngui {


TextLayout::TextLayout()
	:
	TextLayout(TextSystem::Default(), {}, FontRole::Plain)
{
}


TextLayout::TextLayout(const TextSystem& system, std::string_view text, FontRole role,
	const TextLayoutOptions& options)
	:
	fText(text),
	fRuns{TextStyleRun{0, system.RoleFont(role)}},
	fOptions(options)
{
	_Layout(system);
}


TextLayout::TextLayout(const TextSystem& system, std::string_view text,
	std::span<const TextStyleRun> runs, const TextLayoutOptions& options)
	:
	fText(text),
	fRuns(runs.begin(), runs.end()),
	fOptions(options)
{
	if (fRuns.empty() || fRuns.front().start != 0)
		fRuns.insert(fRuns.begin(), TextStyleRun{0, system.RoleFont(FontRole::Plain)});
	// NewGUI: a run that begins inside a code point begins at its start, so
	// that no character is split between runs (and their glyph runs)
	for (TextStyleRun& run : fRuns) {
		while (run.start > 0 && run.start < fText.size()
			&& (static_cast<unsigned char>(fText[run.start]) & 0xc0) == 0x80) {
			run.start--;
		}
	}
	_Layout(system);
}


std::string_view
TextLayout::Text() const
{
	return fText;
}


const TextLayoutOptions&
TextLayout::Options() const
{
	return fOptions;
}


int32
TextLayout::CountLines() const
{
	return static_cast<int32>(fLines.size());
}


const TextLayout::Line&
TextLayout::LineAt(int32 index) const
{
	index = std::clamp(index, 0, CountLines() - 1);
	return fLines[static_cast<std::size_t>(index)].line;
}


int32
TextLayout::LineAtOffset(std::size_t offset) const
{
	offset = std::min(offset, fText.size());
	const auto found = std::upper_bound(fLines.begin(), fLines.end(), offset,
		[](std::size_t value, const LineData& data) { return value < data.line.start; });
	return std::max(0, static_cast<int32>(found - fLines.begin()) - 1);
}


int32
TextLayout::LineAtY(float y) const
{
	const auto found = std::upper_bound(fLines.begin(), fLines.end(), y,
		[](float value, const LineData& data) { return value < data.line.top; });
	return std::clamp(static_cast<int32>(found - fLines.begin()) - 1, 0, CountLines() - 1);
}


float
TextLayout::LineHeight(int32 index) const
{
	index = std::clamp(index, 0, CountLines() - 1);
	const float next = index + 1 < CountLines()
		? fLines[static_cast<std::size_t>(index) + 1].line.top : fBottom;
	return next - fLines[static_cast<std::size_t>(index)].line.top;
}


float
TextLayout::Baseline(int32 index) const
{
	const Line& line = LineAt(index);
	return line.top + line.ascent + 1;
}


float
TextLayout::Height() const
{
	return std::ceil(fBottom);
}


float
TextLayout::MaxWidth() const
{
	float width = 0;
	for (const LineData& data : fLines)
		width = std::max(width, data.line.width);
	return width;
}


float
TextLayout::AlignmentOffset(int32 index) const
{
	if (fOptions.alignment != BHorizontalAlignment::Right
		&& fOptions.alignment != BHorizontalAlignment::Center) {
		return 0;
	}

	// Haiku: fTextRect.Width() + 1 - LineWidth(), the pixel width here
	float alignmentOffset = fOptions.width - LineAt(index).width;
	if (fOptions.alignment == BHorizontalAlignment::Center)
		alignmentOffset = std::round(alignmentOffset / 2);
	return alignmentOffset;
}


gfx::Point
TextLayout::PointAt(std::size_t offset, float* height) const
{
	// pin offset at reasonable values
	offset = std::min(offset, fText.size());

	const int32 lineNum = LineAtOffset(offset);
	const bool onEmptyLastLine = _IsOnEmptyLastLine(offset);

	gfx::Point result{0, LineAt(lineNum).top};
	float lineHeight = LineHeight(lineNum);
	if (fText.empty() || onEmptyLastLine) {
		// the height of an empty line (Haiku's _NullStyleHeight())
		lineHeight = fNullStyleHeight;
	} else
		result.x = _X(lineNum, offset);

	result.x += AlignmentOffset(lineNum);

	// round up
	result.x = std::round(result.x);
	result.y = std::round(result.y);
	if (height != nullptr)
		*height = lineHeight;

	return result;
}


std::size_t
TextLayout::OffsetAt(gfx::Point point) const
{
	const std::size_t textLength = fText.size();

	// should we even bother?
	if (point.y >= Height())
		return textLength;
	else if (point.y < 0)
		return 0;

	return OffsetAt(LineAtY(point.y), point.x);
}


std::size_t
TextLayout::OffsetAt(int32 lineNum, float x) const
{
	lineNum = std::clamp(lineNum, 0, CountLines() - 1);
	const Line& line = LineAt(lineNum);
	const LineData& data = fLines[static_cast<std::size_t>(lineNum)];
	const std::vector<float>& carets = data.carets;

	// convert to text rect coordinates
	x -= AlignmentOffset(lineNum);
	x = std::max(x, 0.0f);

	std::size_t offset = line.start;
	const std::size_t limit = line.end;
	if (!data.clusters.empty()) {
		// NewGUI: right-to-left runs; the edge of the grapheme hit that is
		// nearer, beyond the line the edge of its outer grapheme
		std::size_t hit = 0;
		bool rightEdge = false;
		if (x >= data.clusters.back().right) {
			hit = data.clusters.size() - 1;
			rightEdge = true;
		} else if (x >= data.clusters.front().left) {
			for (std::size_t i = 0; i < data.clusters.size(); i++) {
				if (x >= data.clusters[i].left && x < data.clusters[i].right) {
					hit = i;
					break;
				}
			}
			// the right half: its right edge
			rightEdge = x >= (data.clusters[hit].left + data.clusters[hit].right) / 2;
		}
		offset = _OnBoundary(_EdgeOffset(lineNum, hit, rightEdge));
	}
	float location = 0;
	while (offset < limit && data.clusters.empty()) {
		const std::size_t nextInitial = std::min(NextBoundary(offset, TextBoundary::Grapheme),
			limit);
		const float width = carets[nextInitial - line.start] - carets[offset - line.start];
		if (location + width > x) {
			if (std::fabs(location + width - x) < std::fabs(location - x))
				offset = nextInitial;
			break;
		}

		location += width;
		offset = nextInitial;
	}

	if (offset == line.end && offset > line.start) {
		// special case: newlines aren't visible
		// return the offset of the character preceding the newline
		if (fText[offset - 1] == '\n')
			return offset - 1;

		// special case: return the offset preceding any spaces that
		// aren't at the end of the buffer
		if (offset != fText.size() && fText[offset - 1] == ' ')
			return offset - 1;
	}

	return offset;
}


std::size_t
TextLayout::CaretRight(std::size_t offset) const
{
	offset = std::min(offset, fText.size());
	const int32 lineNum = LineAtOffset(offset);
	const LineData& data = fLines[static_cast<std::size_t>(lineNum)];
	if (data.clusters.empty() || _IsOnEmptyLastLine(offset))
		return NextBoundary(offset, TextBoundary::Grapheme);

	// NewGUI: visually, over the grapheme right of the caret
	if (const std::size_t next = _ClusterRightOf(data, _X(lineNum, offset));
			next < data.clusters.size()) {
		return _OnBoundary(_EdgeOffset(lineNum, next, true));
	}
	// at the line's right end: on to the next line left to right, back to
	// the previous one right to left
	// (at the text's start: there, the line's start is its right end)
	if (data.rightToLeft) {
		return data.line.start > 0 ? PreviousBoundary(data.line.start, TextBoundary::Grapheme)
			: data.line.start;
	}
	return NextBoundary(_ContentEnd(data.line), TextBoundary::Grapheme);
}


std::size_t
TextLayout::CaretLeft(std::size_t offset) const
{
	offset = std::min(offset, fText.size());
	const int32 lineNum = LineAtOffset(offset);
	const LineData& data = fLines[static_cast<std::size_t>(lineNum)];
	if (data.clusters.empty() || _IsOnEmptyLastLine(offset))
		return PreviousBoundary(offset, TextBoundary::Grapheme);

	// NewGUI: visually, over the grapheme left of the caret
	if (const std::size_t previous = _ClusterLeftOf(data, _X(lineNum, offset));
			previous < data.clusters.size()) {
		return _OnBoundary(_EdgeOffset(lineNum, previous, false));
	}
	// at the line's left end
	if (data.rightToLeft)
		return NextBoundary(_ContentEnd(data.line), TextBoundary::Grapheme);
	return data.line.start > 0 ? PreviousBoundary(data.line.start, TextBoundary::Grapheme)
		: data.line.start;
}


// \a offset, or the grapheme boundary before it if it is inside a grapheme:
// in malformed UTF-8 the bidirectional algorithm may begin a run at a stray
// continuation byte, which the boundaries count to the character before.
std::size_t
TextLayout::_OnBoundary(std::size_t offset) const
{
	if (offset == 0 || offset >= fText.size() || IsBoundary(offset, TextBoundary::Grapheme))
		return offset;
	return PreviousBoundary(offset, TextBoundary::Grapheme);
}


namespace {

// How close two edges must be to be the same.
constexpr float kSlack = 0.01f;

} // namespace


// The index of the grapheme right of a caret at \a x: the first that
// begins there or later and is wider than nothing; the number of graphemes
// at the line's right end.
std::size_t
TextLayout::_ClusterRightOf(const LineData& data, float x) const
{
	for (std::size_t i = 0; i < data.clusters.size(); i++) {
		if (data.clusters[i].left >= x - kSlack && data.clusters[i].right > x + kSlack)
			return i;
	}
	return data.clusters.size();
}


// The index of the grapheme left of a caret at \a x; the number of
// graphemes at the line's left end.
std::size_t
TextLayout::_ClusterLeftOf(const LineData& data, float x) const
{
	for (std::size_t i = data.clusters.size(); i-- > 0;) {
		if (data.clusters[i].right <= x + kSlack && data.clusters[i].left < x - kSlack)
			return i;
	}
	return data.clusters.size();
}


// The offset whose caret stands at the right or left edge of grapheme
// \a index of line \a lineNum: the grapheme's own (the start of a
// left-to-right grapheme's left edge, the end of a right-to-left one's)
// or, where two directions meet, that of its neighbor across the edge.
std::size_t
TextLayout::_EdgeOffset(int32 lineNum, std::size_t index, bool rightEdge) const
{
	const LineData& data = fLines[static_cast<std::size_t>(lineNum)];
	const Cluster& cluster = data.clusters[index];
	const float edge = rightEdge ? cluster.right : cluster.left;
	const std::size_t own = rightEdge != cluster.rightToLeft ? cluster.end : cluster.start;
	if (std::fabs(_X(lineNum, own) - edge) < kSlack)
		return own;
	const std::size_t neighbor = rightEdge ? index + 1 : index - 1;
	if (neighbor < data.clusters.size()) {
		const Cluster& other = data.clusters[neighbor];
		if (std::fabs((rightEdge ? other.left : other.right) - edge) < kSlack) {
			const std::size_t offset = rightEdge != other.rightToLeft ? other.start : other.end;
			if (std::fabs(_X(lineNum, offset) - edge) < kSlack)
				return offset;
		}
	}
	// at the line's ends: its start or the end of its text
	for (const std::size_t offset : {data.line.start, _ContentEnd(data.line)}) {
		if (std::fabs(_X(lineNum, offset) - edge) < kSlack)
			return offset;
	}
	return own;
}


std::size_t
TextLayout::NextBoundary(std::size_t offset, TextBoundary kind) const
{
	for (std::size_t i = offset + 1; i < fBoundaries.size(); i++) {
		if (fBoundaries[i].Has(kind))
			return i;
	}
	return fText.size();
}


std::size_t
TextLayout::PreviousBoundary(std::size_t offset, TextBoundary kind) const
{
	offset = std::min(offset, fText.size());
	for (std::size_t i = offset; i-- > 0;) {
		if (fBoundaries[i].Has(kind))
			return i;
	}
	return 0;
}


bool
TextLayout::IsBoundary(std::size_t offset, TextBoundary kind) const
{
	return offset < fBoundaries.size() && fBoundaries[offset].Has(kind);
}


std::vector<gfx::Rect>
TextLayout::SelectionRects(std::size_t from, std::size_t to) const
{
	std::vector<gfx::Rect> rects;
	if (from > to)
		std::swap(from, to);
	from = std::min(from, fText.size());
	to = std::min(to, fText.size());
	if (from == to)
		return rects;

	for (int32 i = LineAtOffset(from); i < CountLines(); i++) {
		const Line& line = LineAt(i);
		if (line.start >= to)
			break;
		const std::size_t start = std::max(from, line.start);
		const std::size_t end = std::min(to, _ContentEnd(line));
		const float align = AlignmentOffset(i);
		const float bottom = line.top + LineHeight(i);
		const LineData& data = fLines[static_cast<std::size_t>(i)];
		if (!data.clusters.empty()) {
			// NewGUI: the graphemes in the range, joined where they touch
			const std::size_t first = rects.size();
			for (const Cluster& cluster : data.clusters) {
				if (cluster.start < start || cluster.end > end)
					continue;
				const float x0 = cluster.left + align;
				const float x1 = cluster.right + align;
				if (rects.size() > first && std::fabs(rects.back().x1 - x0) < kSlack)
					rects.back().x1 = x1;
				else
					rects.push_back({x0, line.top, x1, bottom});
			}
			if (rects.size() == first) {
				const float x = _X(i, start) + align;
				rects.push_back({x, line.top, x, bottom});
			}
			continue;
		}
		const float x0 = _X(i, start) + align;
		const float x1 = _X(i, std::max(start, end)) + align;
		rects.push_back({x0, line.top, x1, bottom});
	}
	return rects;
}


bool
TextLayout::HasRightToLeft() const
{
	return std::ranges::any_of(fLines, [](const LineData& data) {
		return !data.clusters.empty();
	});
}


void
TextLayout::Draw(gfx::Canvas& canvas, gfx::Point origin, const gfx::Color& color,
	int32 firstLine, int32 lastLine, ColorMap map) const
{
	if (lastLine < 0 || lastLine >= CountLines())
		lastLine = CountLines() - 1;

	for (int32 i = std::max(firstLine, 0); i <= lastLine; i++) {
		const LineData& data = fLines[static_cast<std::size_t>(i)];
		const float x = origin.x + AlignmentOffset(i);
		const float y = origin.y + Baseline(i);
		for (const Segment& segment : data.segments) {
			if (segment.shaped.glyphs.empty())
				continue;
			const gfx::Color runColor = segment.color.value_or(color);
			segment.shaped.Draw(canvas, {x + segment.x, y},
				gfx::Paint{.color = map != nullptr ? map(runColor) : runColor});
		}
	}
}


// #pragma mark - private


void
TextLayout::_Layout(const TextSystem& system)
{
	fBoundaries = system.Boundaries(fText);
	fLines.clear();

	const std::size_t textLength = fText.size();
	float top = 0;
	std::size_t fromOffset = 0;
	do {
		float ascent;
		float descent;
		float width;
		std::size_t toOffset = _FindLineBreak(system, fromOffset, &ascent, &descent, &width);

		// we want to advance at least by one character
		const std::size_t nextOffset = NextBoundary(fromOffset, TextBoundary::Grapheme);
		if (toOffset < nextOffset && fromOffset < textLength)
			toOffset = nextOffset;

		LineData data;
		data.line = {fromOffset, toOffset, top, ascent, descent, 0};
		const std::size_t contentEnd = _ContentEnd(data.line);
		data.carets.assign(toOffset - fromOffset + 1, 0.0f);
		std::vector<float> carets;
		data.line.width = _Measure(system, fromOffset, contentEnd, 0, &carets,
			&data.segments, &data);
		std::copy(carets.begin(), carets.end(), data.carets.begin());
		// after the content (its line break): where the caret after it is
		for (std::size_t i = contentEnd - fromOffset + 1; i < data.carets.size(); i++)
			data.carets[i] = carets.back();
		fLines.push_back(std::move(data));

		top = std::ceil(top + ascent + descent) + 1;
		fromOffset = toOffset;
	} while (fromOffset < textLength);

	if (_IsOnEmptyLastLine(textLength)) {
		// NewGUI: the empty line after a final line break
		float ascent;
		float descent;
		_Metrics(system, textLength, textLength, &ascent, &descent);
		LineData data;
		data.line = {textLength, textLength, top, ascent, descent, 0};
		data.carets.assign(1, 0.0f);
		fLines.push_back(std::move(data));
		top = std::ceil(top + ascent + descent) + 1;
	}
	fBottom = top;

	// Haiku's _NullStyleHeight(): the font at the end, without leading
	const FontHeight height = system.Height(_RunAt(textLength).font);
	fNullStyleHeight = std::ceil(height.ascent + height.descent + 1);
}


std::size_t
TextLayout::_FindLineBreak(const TextSystem& system, std::size_t fromOffset, float* _ascent,
	float* _descent, float* inOutWidth) const
{
	*_ascent = 0.0;
	*_descent = 0.0;

	const std::size_t limit = fText.size();

	// is fromOffset at the end?
	if (fromOffset >= limit) {
		// try to return valid height info anyway
		_Metrics(system, fromOffset, fromOffset, _ascent, _descent);
		*inOutWidth = 0;
		return limit;
	}

	if (!fOptions.wrap || fOptions.width <= 0) {
		// Text wrapping is turned off.
		// Just find the next line break
		std::size_t offset = NextBoundary(fromOffset, TextBoundary::MandatoryLine);
		std::size_t toOffset = offset;
		if (IsBoundary(offset, TextBoundary::MandatoryLine) && fText[offset - 1] == '\n')
			toOffset--;

		*inOutWidth = _Measure(system, fromOffset, toOffset, 0, nullptr, nullptr);
		_Metrics(system, fromOffset, toOffset, _ascent, _descent);
		return offset;
	}

	// NewGUI: Haiku compared with its width of the text rectangle
	const float maxWidth = fOptions.width - 1;

	std::size_t offset = fromOffset;
	bool done = false;
	float ascent = 0.0;
	float descent = 0.0;
	float strWidth = 0.0;

	// wrap the text
	while (offset < limit && !done) {
		// find the next line break candidate; it comes after the trailing
		// whitespace, if any
		const std::size_t chunkEnd = NextBoundary(offset, TextBoundary::Line);
		std::size_t widthEnd = chunkEnd;
		if (IsBoundary(chunkEnd, TextBoundary::MandatoryLine)) {
			// found a line break, we're done!
			done = true;
			// do not include the newline into width & height calculations
			if (fText[chunkEnd - 1] == '\n')
				widthEnd--;
		}

		std::size_t beforeWhitespace = widthEnd;
		while (beforeWhitespace > offset
			&& (fText[beforeWhitespace - 1] == ' ' || fText[beforeWhitespace - 1] == '\t')) {
			beforeWhitespace--;
		}

		const float deltaWidth = _Measure(system, offset, widthEnd, strWidth, nullptr,
			nullptr) - strWidth;
		_Metrics(system, offset, widthEnd, &ascent, &descent);
		strWidth += deltaWidth;

		if (strWidth >= maxWidth) {
			// we've found where the line will wrap
			done = true;

			// we have included trailing whitespace in the width computation
			// above, but that is not being shown anyway, so we try again
			// without the trailing whitespace
			if (beforeWhitespace == chunkEnd) {
				// there is no trailing whitespace, no point in trying
				break;
			}

			// reset string width to start of current run ...
			strWidth -= deltaWidth;

			// ... and compute the resulting width (of visible characters)
			strWidth = _Measure(system, offset, beforeWhitespace, strWidth, nullptr, nullptr);
			if (strWidth >= maxWidth) {
				// width of visible characters exceeds line, we need to wrap
				// before the current "word"
				break;
			}
		}

		*_ascent = std::max(ascent, *_ascent);
		*_descent = std::max(descent, *_descent);

		offset = chunkEnd;
	}

	if (offset == fromOffset) {
		// there weren't any words that fit entirely in this line
		// force a break in the middle of a word
		*_ascent = 0.0;
		*_descent = 0.0;
		strWidth = 0.0;

		std::size_t current = fromOffset;
		for (offset = NextBoundary(current, TextBoundary::Grapheme); current < limit;
				current = offset, offset = NextBoundary(offset, TextBoundary::Grapheme)) {
			strWidth = _Measure(system, current, offset, strWidth, nullptr, nullptr);
			if (strWidth >= maxWidth) {
				offset = current;
				break;
			}

			_Metrics(system, current, offset, &ascent, &descent);
			*_ascent = std::max(ascent, *_ascent);
			*_descent = std::max(descent, *_descent);

			// NewGUI: not past a line break
			if (IsBoundary(offset, TextBoundary::MandatoryLine))
				break;
		}
	}

	*inOutWidth = strWidth;
	return std::min(offset, limit);
}


float
TextLayout::_Measure(const TextSystem& system, std::size_t from, std::size_t to, float x,
	std::vector<float>* carets, std::vector<Segment>* segments, LineData* visual) const
{
	if (carets != nullptr)
		carets->assign(to - from + 1, x);

	// A line that is drawn goes in the visual order of its runs (UAX #9,
	// in its paragraph: up to the line breaks around it); widths do not
	// depend on the order.
	std::vector<DirectionRun> runs{{from, to, TextDirection::LeftToRight}};
	if (segments != nullptr && to > from) {
		const std::size_t paragraphStart = from == 0 ? 0 : fText.rfind('\n', from - 1) + 1;
		std::size_t paragraphEnd = fText.find('\n', from);
		if (paragraphEnd == std::string::npos)
			paragraphEnd = fText.size();
		const std::string_view paragraph(fText.data() + paragraphStart,
			paragraphEnd - paragraphStart);
		std::vector<DirectionRun> ordered = system.VisualRuns(paragraph,
			from - paragraphStart, std::min(to, paragraphEnd) - paragraphStart);
		if (!ordered.empty()) {
			for (DirectionRun& run : ordered) {
				run.start += paragraphStart;
				run.end += paragraphStart;
			}
			runs = std::move(ordered);
		}
		if (visual != nullptr && (runs.size() > 1
				|| runs.front().direction == TextDirection::RightToLeft)) {
			visual->rightToLeft
				= system.ParagraphDirection(paragraph) == TextDirection::RightToLeft;
		} else
			visual = nullptr;
	} else
		visual = nullptr;
	// the clusters of a line with right-to-left runs, where they are
	std::vector<Cluster>* clusters = visual != nullptr ? &visual->clusters : nullptr;

	float position = x;
	for (const DirectionRun& run : runs) {
		const bool rightToLeft = run.direction == TextDirection::RightToLeft;
		// Its pieces: a tab, a line break, or text without them in one
		// style run; a right-to-left run's last first.
		std::vector<std::pair<std::size_t, std::size_t>> pieces;
		for (std::size_t i = run.start; i < run.end;) {
			if (fText[i] == '\t' || fText[i] == '\n') {
				pieces.emplace_back(i, i + 1);
				i++;
				continue;
			}
			const std::size_t runEnd = std::min(_RunEnd(i), run.end);
			std::size_t j = i;
			while (j < runEnd && fText[j] != '\t' && fText[j] != '\n')
				j++;
			pieces.emplace_back(i, j);
			i = j;
		}
		if (rightToLeft)
			std::ranges::reverse(pieces);

		for (const auto& [i, j] : pieces) {
			if (fText[i] == '\t') {
				// Haiku's _ActualTabWidth(), from the line's start
				float tabWidth = 0;
				if (fOptions.tabWidth > 0) {
					tabWidth = fOptions.tabWidth - std::fmod(position, fOptions.tabWidth);
					if (std::round(tabWidth) == 0)
						tabWidth = fOptions.tabWidth;
				}
				if (carets != nullptr)
					(*carets)[i - from] = rightToLeft ? position + tabWidth : position;
				if (clusters != nullptr)
					clusters->push_back({i, i + 1, position, position + tabWidth, rightToLeft});
				position += tabWidth;
				continue;
			}
			if (fText[i] == '\n') {
				if (carets != nullptr)
					(*carets)[i - from] = position;
				continue;
			}

			const std::string_view piece(fText.data() + i, j - i);
			const TextStyleRun& style = _RunAt(i);
			const std::vector<float> pieceCarets = system.CaretPositions(piece, style.font,
				run.direction);
			if (carets != nullptr) {
				for (std::size_t k = 0; k < piece.size(); k++)
					(*carets)[i - from + k] = position + pieceCarets[k];
			}
			if (clusters != nullptr) {
				// its graphemes, from their leading to their trailing edge
				for (std::size_t k = i; k < j;) {
					const std::size_t next = std::min(NextBoundary(k, TextBoundary::Grapheme), j);
					const float leading = pieceCarets[k - i];
					const float trailing = pieceCarets[next - i];
					clusters->push_back({k, next, position + std::min(leading, trailing),
						position + std::max(leading, trailing), rightToLeft});
					k = next;
				}
			}
			if (segments != nullptr) {
				segments->push_back({position, system.ShapeRun(piece, style.font, run.direction),
					style.color});
			}
			// the run's width: the caret after it, or before it right to left
			position += rightToLeft ? pieceCarets.front() : pieceCarets.back();
		}
	}

	if (carets != nullptr)
		(*carets)[to - from] = position;
	if (clusters != nullptr) {
		std::ranges::stable_sort(*clusters, {}, &Cluster::left);
		if (carets != nullptr)
			_PlaceCarets(*visual, from, to, carets);
	}
	return position;
}


// The carets between graphemes of a line with right-to-left runs, where two
// graphemes of different directions meet on two places: at the edge of the
// one in the paragraph's direction (the "strong" caret), else at the
// leading edge of the one after it. At the line's start and end a grapheme
// against the paragraph's direction puts the caret at the line's end in
// that direction (left to right the left end, then the right end): there
// the next character typed goes.
void
TextLayout::_PlaceCarets(const LineData& data, std::size_t from, std::size_t to,
	std::vector<float>* carets) const
{
	constexpr std::size_t kNone = static_cast<std::size_t>(-1);
	std::vector<std::size_t> starting(to - from + 1, kNone);
	std::vector<std::size_t> ending(to - from + 1, kNone);
	for (std::size_t i = 0; i < data.clusters.size(); i++) {
		starting[data.clusters[i].start - from] = i;
		ending[data.clusters[i].end - from] = i;
	}
	const auto leading = [](const Cluster& cluster) {
		return cluster.rightToLeft ? cluster.right : cluster.left;
	};
	const auto trailing = [](const Cluster& cluster) {
		return cluster.rightToLeft ? cluster.left : cluster.right;
	};
	const float left = data.clusters.front().left;
	const float right = data.clusters.back().right;
	for (std::size_t offset = from; offset <= to; offset++) {
		const std::size_t next = starting[offset - from];
		const std::size_t previous = ending[offset - from];
		if (next != kNone && data.clusters[next].rightToLeft == data.rightToLeft)
			(*carets)[offset - from] = leading(data.clusters[next]);
		else if (previous != kNone && data.clusters[previous].rightToLeft == data.rightToLeft)
			(*carets)[offset - from] = trailing(data.clusters[previous]);
		else if (next != kNone && previous != kNone)
			(*carets)[offset - from] = leading(data.clusters[next]);
		else if (next != kNone)
			(*carets)[offset - from] = data.rightToLeft ? right : left;
		else if (previous != kNone)
			(*carets)[offset - from] = data.rightToLeft ? left : right;
	}
}


void
TextLayout::_Metrics(const TextSystem& system, std::size_t from, std::size_t to,
	float* ascent, float* descent) const
{
	// The highest ascent and descent of the fonts from \a from to \a to,
	// or of the font at \a from if they are equal.
	*ascent = 0;
	*descent = 0;
	std::size_t offset = from;
	do {
		const FontHeight height = system.Height(_RunAt(offset).font);
		*ascent = std::max(*ascent, height.ascent);
		*descent = std::max(*descent, height.descent + height.leading);
		offset = _RunEnd(offset);
	} while (offset < to);
}


const TextStyleRun&
TextLayout::_RunAt(std::size_t offset) const
{
	// the first run begins at 0 (the constructors see to it)
	const TextStyleRun* found = &fRuns.front();
	for (const TextStyleRun& run : fRuns) {
		if (run.start > offset)
			break;
		found = &run;
	}
	return *found;
}


std::size_t
TextLayout::_RunEnd(std::size_t offset) const
{
	for (const TextStyleRun& run : fRuns) {
		if (run.start > offset)
			return std::min(run.start, fText.size());
	}
	return fText.size();
}


std::size_t
TextLayout::_ContentEnd(const Line& line) const
{
	// without the line break at its end
	if (line.end > line.start && fText[line.end - 1] == '\n')
		return line.end - 1;
	return line.end;
}


float
TextLayout::_X(int32 lineNum, std::size_t offset) const
{
	const LineData& data = fLines[static_cast<std::size_t>(lineNum)];
	offset = std::clamp(offset, data.line.start, data.line.end);
	return data.carets[offset - data.line.start];
}


bool
TextLayout::_IsOnEmptyLastLine(std::size_t offset) const
{
	return offset == fText.size() && offset > 0
		&& IsBoundary(offset, TextBoundary::MandatoryLine);
}


} // namespace ngui
