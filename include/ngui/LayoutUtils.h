/*
 * Copyright 2006-2014, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/InterfaceDefs.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


/*!	A size that is not set, as Haiku's B_SIZE_UNSET. */
inline constexpr float kSizeUnset = -2;

/*!	A size without limit, as Haiku's B_SIZE_UNLIMITED: 2^30 pixels. Sums
	that reach it stay at it. */
inline constexpr float kSizeUnlimited = 1024 * 1024 * 1024;

/*!	A size whose width and height are both unset, as Haiku's BSize(). */
inline constexpr gfx::Size kUnsetSize = {kSizeUnset, kSizeUnset};


// Spacing constants: spacings and insets that depend on the look and the
// font, as Haiku's B_USE_DEFAULT_SPACING ...; BLayoutUtils::ComposeSpacing()
// resolves them. Layouts keep them as given and resolve them whenever they
// compute sizes, so that a new look or font changes the spacing with the
// next layout.
inline constexpr float kUseDefaultSpacing = -1002;	/*!< The look's item spacing. */
inline constexpr float kUseItemSpacing = -1003;		/*!< The look's item spacing. */
inline constexpr float kUseItemInsets = kUseItemSpacing;	/*!< The item spacing. */
inline constexpr float kUseHalfItemSpacing = -1004;	/*!< Half the item spacing. */
inline constexpr float kUseHalfItemInsets = kUseHalfItemSpacing;
													/*!< Half the item spacing. */
inline constexpr float kUseWindowSpacing = -1005;	/*!< The item spacing. */
inline constexpr float kUseWindowInsets = kUseWindowSpacing;
													/*!< The item spacing. */
inline constexpr float kUseSmallSpacing = -1006;	/*!< 0.7 times the item spacing. */
inline constexpr float kUseSmallInsets = kUseSmallSpacing;
													/*!< 0.7 times the item spacing. */
inline constexpr float kUseCornerSpacing = -1007;	/*!< 1.272 times the item spacing. */
inline constexpr float kUseCornerInsets = kUseCornerSpacing;
													/*!< 1.272 times the item spacing. */
inline constexpr float kUseBigSpacing = -1008;		/*!< 1.8 times the item spacing. */
inline constexpr float kUseBigInsets = kUseBigSpacing;	/*!< 1.8 times the item spacing. */
inline constexpr float kUseBorderSpacing = -1009;	/*!< A border: an eleventh of the item
														spacing, at least 1. */
inline constexpr float kUseBorderInsets = kUseBorderSpacing;
													/*!< A border. */


class BView;
class ControlLook;
class TextSystem;


/*!	Size arithmetic of the layout engine (module G10), as Haiku's
	BLayoutUtils.

	Sizes and distances are **pixel counts**, as the widths of gfx::Rect: a
	minimum width of 80 is 80 pixels. Haiku counts one less (a width of 79
	is 80 pixels, because its rectangles include their right edge) and adds
	or subtracts 1 when it combines sizes; NewGUI's layout engine is ported
	without these corrections, everything else follows Haiku.
*/
class BLayoutUtils {
public:
	static	int32				AddSizesInt32(int32 a, int32 b);
									/*!< \a a + \a b, at most kSizeUnlimited. */
	static	int32				AddSizesInt32(int32 a, int32 b, int32 c);
									/*!< \a a + \a b + \a c, at most kSizeUnlimited. */
	static	float				AddDistances(float a, float b);
									/*!< \a a + \a b, at most kSizeUnlimited. */
	static	float				AddDistances(float a, float b, float c);
									/*!< \a a + \a b + \a c, at most kSizeUnlimited. */
	static	int32				SubtractSizesInt32(int32 a, int32 b);
									/*!< \a a - \a b, at least 0. */
	static	float				SubtractDistances(float a, float b);
									/*!< \a a - \a b, at least 0. */
	static	void				FixSizeConstraints(float& min, float& max,
									float& preferred);
									/*!< Raises \a max to \a min and puts \a preferred
										between them. */
	static	void				FixSizeConstraints(gfx::Size& min, gfx::Size& max,
									gfx::Size& preferred);
									/*!< The same for width and height. */

	static	bool				IsSet(float size);
									/*!< Whether \a size is not kSizeUnset (BSize::IsWidthSet()). */
	static	gfx::Size			ComposeSize(gfx::Size size, gfx::Size layoutSize);
									/*!< \a size, with its unset parts from \a layoutSize. */
	static	BAlignment			ComposeAlignment(BAlignment alignment,
									BAlignment layoutAlignment);
									/*!< \a alignment, with its unset parts from
										\a layoutAlignment. */
	static	float				RelativeHorizontal(BAlignment alignment);
									/*!< BAlignment::RelativeHorizontal(): 0 left, 1 right,
										0.5 center; -1 unset, -2 full width. */
	static	float				RelativeVertical(BAlignment alignment);
									/*!< BAlignment::RelativeVertical(): 0 top, 1 bottom,
										0.5 middle; -1 unset, -2 full height. */

	static	gfx::Rect			AlignInFrame(gfx::Rect frame, gfx::Size maxSize,
									BAlignment alignment);
									/*!< \a frame reduced to \a maxSize where it is larger
										and the alignment does not use the full extent,
										placed by \a alignment. */
	static	void				AlignInFrame(BView* view, gfx::Rect frame);
									/*!< Moves and resizes \a view into \a frame by its
										maximum size and alignment, height for width
										included. */
	static	gfx::Rect			AlignOnRect(gfx::Rect rect, gfx::Size size,
									BAlignment alignment);
									/*!< A rectangle of \a size placed on \a rect by
										\a alignment; it may be larger than \a rect. */
	static	float				ComposeSpacing(float spacing);
									/*!< \a spacing with the spacing constants resolved by
										the current look (LookRegistry, the BeOS look if
										none is selected) and TextSystem::Default(), until
										windows have looks of their own (module M17b). */
	static	float				ComposeSpacing(float spacing, const ControlLook& look,
									const TextSystem& text);
									/*!< \a spacing with the spacing constants resolved by
										\a look for \a text, as Haiku's
										BControlLook::ComposeSpacing(); other values stay. */

	static	gfx::Rect			MoveIntoFrame(gfx::Rect rect, gfx::Size frameSize);
									/*!< \a rect moved into the frame (0, 0, \a frameSize)
										as far as it fits, left and top edges first; its
										size stays. */
};


} // namespace ngui
