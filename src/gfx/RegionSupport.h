/*
 * Copyright 2026, The NewGUI Authors.
 *
 * Ported from Haiku: headers/private/interface/RegionSupport.h (commit b1d497bb69),
 * which takes it from X11. The original notices follow; NewGUI's changes
 * are class names, includes and what the compilers flag at NewGUI's warning
 * level.
 */
/* $Xorg: region.h,v 1.4 2001/02/09 02:03:40 xorgcvs Exp $ */
/************************************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

************************************************************************/

#ifndef __REGION_SUPPORT_H
#define __REGION_SUPPORT_H

#include <ngui/gfx/Region.h>


namespace ngui::gfx {


class Region::Support {
 public:
	static	int					XUnionRegion(const Region* reg1,
									const Region* reg2, Region* newReg);

	static	int					XIntersectRegion(const Region* reg1,
									const Region* reg2, Region* newReg);

	static	int					XSubtractRegion(const Region* regM,
									const Region* regS, Region* regD);

	static	int					XXorRegion(const Region* sra,
									const Region* srb, Region* dr);

	static	bool				XPointInRegion(const Region* region,
									int x, int y);

	enum {
		RectangleOut = 0,
		RectanglePart = 1,
		RectangleIn = 2
	};

	static	int					XRectInRegion(const Region* region,
									const clipping_rect& rect);

 private:
	static	Region*			CreateRegion();
	static	void				DestroyRegion(Region* r);

	static	void				XOffsetRegion(Region* pRegion, int x, int y);

	static	void				miSetExtents(Region* pReg);
	static	int					miIntersectO(Region* pReg,
									clipping_rect* r1, clipping_rect* r1End,
									clipping_rect* r2, clipping_rect* r2End,
									int top, int bottom);
	static	void				miRegionCopy(Region* dstrgn, const Region* rgn);
	static	int					miCoalesce(Region* pReg,
									int prevStart, int curStart);
	static	int					miUnionNonO(Region* pReg, clipping_rect*	r,
									clipping_rect* rEnd, int top, int bottom);
	static	int					miUnionO(Region*	pReg,
									clipping_rect* r1, clipping_rect* r1End,
									clipping_rect* r2, clipping_rect* r2End,
									int	top, int bottom);
	static	int					miSubtractO(Region* pReg,
									clipping_rect* r1, clipping_rect* r1End,
									clipping_rect* r2, clipping_rect* r2End,
									int top, int bottom);
	static	int					miSubtractNonO1(Region* pReg,
									clipping_rect* r, clipping_rect* rEnd,
									int top, int bottom);



	typedef	int (*overlapProcp)(
		Region* pReg,
		clipping_rect* r1,
		clipping_rect* r1End,
		clipping_rect* r2,
		clipping_rect* r2End,
		int top,
		int bottom);

	typedef int (*nonOverlapProcp)(
		Region* pReg,
		clipping_rect* r,
		clipping_rect* rEnd,
		int top,
		int bottom);

	static	void				miRegionOp(Region* newReg,
									const Region* reg1, const Region* reg2,
    int (*overlapFunc)(
        Region*     pReg,
        clipping_rect*     r1,
        clipping_rect*              r1End,
        clipping_rect*     r2,
        clipping_rect*              r2End,
        int               top,
        int               bottom),
    int (*nonOverlap1Func)(
        Region*     pReg,
        clipping_rect*     r,
        clipping_rect*              rEnd,
        int      top,
        int      bottom),

    int (*nonOverlap2Func)(
        Region*     pReg,
        clipping_rect*     r,
        clipping_rect*              rEnd,
        int      top,
        int      bottom));

};

} // namespace ngui::gfx

#endif // __REGION_SUPPORT_H
