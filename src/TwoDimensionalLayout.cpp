/*
 * Copyright 2006-2010, Ingo Weinhold <ingo_weinhold@gmx.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/TwoDimensionalLayout.cpp (commit
 * b1d497bb69), without archiving and debug output. NewGUI:
 * - Sizes are pixel counts: the "- 1" with the insets is gone.
 * - Insets and spacings are kept as given and resolved by
 *   BLayoutUtils::ComposeSpacing() when they are used.
 * - The compound layouters are shared with std::shared_ptr (Haiku:
 *   BReferenceable); layouters and layout infos are owned by unique_ptr;
 *   the local and vertical compound layouters leave the layout contexts
 *   they listen to when they are destroyed.
 * - A column or row constraint with only one of minimum and maximum set
 *   has no limit on the other side; Haiku passed the unset maximum (-2) on,
 *   which made it the minimum.
 */


#include <ngui/TwoDimensionalLayout.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <ngui/LayoutContext.h>
#include <ngui/LayoutItem.h>
#include <ngui/LayoutUtils.h>

#include "layout/CollapsingLayouter.h"


namespace ngui {


using layout::CollapsingLayouter;
using layout::LayoutInfo;
using layout::Layouter;


// Some words of explanation:
//
// This class is the base class for BLayouts that organize their items
// on a grid, with each item covering one or more grid cells (always a
// rectangular area). The derived classes only need to implement the
// hooks reporting the constraints for the items and additional constraints
// for the rows and columns. This class does all the layouting.
//
// The basic idea of the layout process is simple. The horizontal and the
// vertical dimensions are laid out independently and the items are set to the
// resulting locations and sizes. The "height for width" feature makes the
// height depend on the width, which makes things a bit more complicated.
// The horizontal dimension must be laid out first and and the results are
// fed into the vertical layout process.
//
// The AlignLayoutWith() feature, which allows to align layouts for different
// views with each other, causes the need for the three inner *Layouter classes.
// For each set of layouts aligned with each other with respect to one
// dimension that dimension must be laid out together. The class responsible
// is CompoundLayouter; one instance exists per such set. The derived class
// VerticalCompoundLayouter is a specialization for the vertical dimension
// which additionally takes care of the "height for width" feature. Per layout
// a single LocalLayouter exists, which comprises the required glue layout
// code and serves as a proxy for the layout, providing service methods
// needed by the CompoundLayouter.


// CompoundLayouter
class BTwoDimensionalLayout::CompoundLayouter
	: public std::enable_shared_from_this<CompoundLayouter> {
public:
								CompoundLayouter(BOrientation orientation);
	virtual						~CompoundLayouter();

			BOrientation		Orientation();

	virtual	Layouter*			GetLayouter(bool minMax);

			LayoutInfo*			GetLayoutInfo();

			void				AddLocalLayouter(LocalLayouter* localLayouter);
			void				RemoveLocalLayouter(
									LocalLayouter* localLayouter);

			void				AbsorbCompoundLayouter(
									std::shared_ptr<CompoundLayouter> other);

	virtual	void				InvalidateLayout();
			bool				IsMinMaxValid();
			void				ValidateMinMax();
			void				Layout(float size, LocalLayouter* localLayouter,
									BLayoutContext* context);

protected:
	virtual	void				DoLayout(float size,
									LocalLayouter* localLayouter,
									BLayoutContext* context);

			std::unique_ptr<Layouter> fLayouter;
			std::unique_ptr<LayoutInfo> fLayoutInfo;
			BOrientation		fOrientation;
			std::vector<LocalLayouter*> fLocalLayouters;
			BLayoutContext*		fLayoutContext;
			float				fLastLayoutSize;

			void				_PrepareItems();

			int32				_CountElements();
			bool				_HasMultiElementItems();

			void				_AddConstraints(Layouter* layouter);

			float				_Spacing();
};


// VerticalCompoundLayouter
class BTwoDimensionalLayout::VerticalCompoundLayouter
	: public CompoundLayouter, private BLayoutContextListener {
public:
								VerticalCompoundLayouter();
								~VerticalCompoundLayouter() override;

			Layouter*			GetLayouter(bool minMax) override;

			void				InvalidateLayout() override;

			void				InvalidateHeightForWidth();

			void				InternalGetHeightForWidth(
									LocalLayouter* localLayouter,
									BLayoutContext* context,
									bool realLayout, float* minHeight,
									float* maxHeight, float* preferredHeight);

protected:
			void				DoLayout(float size,
									LocalLayouter* localLayouter,
									BLayoutContext* context) override;

private:
			std::unique_ptr<Layouter> fHeightForWidthLayouter;
			float				fCachedMinHeightForWidth;
			float				fCachedMaxHeightForWidth;
			float				fCachedPreferredHeightForWidth;
			BLayoutContext*		fHeightForWidthLayoutContext;

			bool				_HasHeightForWidth();

			bool				_SetHeightForWidthLayoutContext(
									BLayoutContext* context);

	// BLayoutContextListener
			void				LayoutContextLeft(BLayoutContext* context) override;
};


// LocalLayouter
class BTwoDimensionalLayout::LocalLayouter : private BLayoutContextListener {
public:
								LocalLayouter(BTwoDimensionalLayout* layout);
								~LocalLayouter() override;

	// interface for the BTwoDimensionalLayout class

			gfx::Size			MinSize();
			gfx::Size			MaxSize();
			gfx::Size			PreferredSize();

			void				InvalidateLayout();
			void				Layout(gfx::Size size);

			gfx::Rect			ItemFrame(Dimensions itemDimensions);

			void				ValidateMinMax();

			void				DoHorizontalLayout(float width);

			void				InternalGetHeightForWidth(float width,
									float* minHeight, float* maxHeight,
									float* preferredHeight);

			void				AlignWith(LocalLayouter* other,
									BOrientation orientation);

	// interface for the compound layout context

			void				PrepareItems(
									CompoundLayouter* compoundLayouter);
			int32				CountElements(
									CompoundLayouter* compoundLayouter);
			bool				HasMultiElementItems(
									CompoundLayouter* compoundLayouter);

			void				AddConstraints(
									CompoundLayouter* compoundLayouter,
									Layouter* layouter);

			float				Spacing(CompoundLayouter* compoundLayouter);

			bool				HasHeightForWidth();

			bool				AddHeightForWidthConstraints(
									VerticalCompoundLayouter* compoundLayouter,
									Layouter* layouter,
									BLayoutContext* context);
			void				SetHeightForWidthConstraintsAdded(bool added);

			void				SetCompoundLayouter(
									std::shared_ptr<CompoundLayouter> compoundLayouter,
									BOrientation orientation);

			void				InternalInvalidateLayout(
									CompoundLayouter* compoundLayouter);

	// implementation private
private:
			BTwoDimensionalLayout* fLayout;
			std::shared_ptr<CompoundLayouter> fHLayouter;
			std::shared_ptr<VerticalCompoundLayouter> fVLayouter;
			std::vector<BLayoutItem*> fHeightForWidthItems;

	// active layout context when doing last horizontal layout
			BLayoutContext*		fHorizontalLayoutContext;
			float				fHorizontalLayoutWidth;
			bool				fHeightForWidthConstraintsAdded;

			void				_SetHorizontalLayoutContext(
									BLayoutContext* context, float width);

	// BLayoutContextListener
			void				LayoutContextLeft(BLayoutContext* context) override;
};


// #pragma mark -


BTwoDimensionalLayout::BTwoDimensionalLayout()
	:
	fLocalLayouter(std::make_unique<LocalLayouter>(this))
{
}


BTwoDimensionalLayout::~BTwoDimensionalLayout()
{
}


void
BTwoDimensionalLayout::SetInsets(float left, float top, float right,
	float bottom)
{
	fLeftInset = left;
	fTopInset = top;
	fRightInset = right;
	fBottomInset = bottom;

	InvalidateLayout();
}


void
BTwoDimensionalLayout::SetInsets(float horizontal, float vertical)
{
	fLeftInset = horizontal;
	fRightInset = fLeftInset;

	fTopInset = vertical;
	fBottomInset = fTopInset;

	InvalidateLayout();
}


void
BTwoDimensionalLayout::SetInsets(float insets)
{
	fLeftInset = insets;
	fRightInset = fLeftInset;
	fTopInset = fLeftInset;
	fBottomInset = fLeftInset;

	InvalidateLayout();
}


void
BTwoDimensionalLayout::GetInsets(float* left, float* top, float* right,
	float* bottom) const
{
	if (left)
		*left = BLayoutUtils::ComposeSpacing(fLeftInset);
	if (top)
		*top = BLayoutUtils::ComposeSpacing(fTopInset);
	if (right)
		*right = BLayoutUtils::ComposeSpacing(fRightInset);
	if (bottom)
		*bottom = BLayoutUtils::ComposeSpacing(fBottomInset);
}


void
BTwoDimensionalLayout::AlignLayoutWith(BTwoDimensionalLayout* other,
	BOrientation orientation)
{
	if (!other || other == this)
		return;

	fLocalLayouter->AlignWith(other->fLocalLayouter.get(), orientation);

	InvalidateLayout();
}


gfx::Size
BTwoDimensionalLayout::BaseMinSize()
{
	_ValidateMinMax();
	return AddInsets(fLocalLayouter->MinSize());
}


gfx::Size
BTwoDimensionalLayout::BaseMaxSize()
{
	_ValidateMinMax();
	return AddInsets(fLocalLayouter->MaxSize());
}


gfx::Size
BTwoDimensionalLayout::BasePreferredSize()
{
	_ValidateMinMax();
	return AddInsets(fLocalLayouter->PreferredSize());
}


BAlignment
BTwoDimensionalLayout::BaseAlignment()
{
	return BAbstractLayout::BaseAlignment();
}


bool
BTwoDimensionalLayout::HasHeightForWidth()
{
	_ValidateMinMax();
	return fLocalLayouter->HasHeightForWidth();
}


void
BTwoDimensionalLayout::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	if (!HasHeightForWidth())
		return;

	float outerSpacing = BLayoutUtils::ComposeSpacing(fLeftInset)
		+ BLayoutUtils::ComposeSpacing(fRightInset);
	fLocalLayouter->InternalGetHeightForWidth(BLayoutUtils::SubtractDistances(
		width, outerSpacing), min, max, preferred);
	AddInsets(min, max, preferred);
}


void
BTwoDimensionalLayout::SetFrame(gfx::Rect frame)
{
	BAbstractLayout::SetFrame(frame);
}


void
BTwoDimensionalLayout::LayoutInvalidated(bool /*children*/)
{
	fLocalLayouter->InvalidateLayout();
}


void
BTwoDimensionalLayout::DoLayout()
{
	_ValidateMinMax();

	// layout the horizontal/vertical elements
	const gfx::Rect area = LayoutArea();
	gfx::Size size = SubtractInsets({area.Width(), area.Height()});

	fLocalLayouter->Layout(size);

	// layout the items
	const float left = BLayoutUtils::ComposeSpacing(fLeftInset);
	const float top = BLayoutUtils::ComposeSpacing(fTopInset);
	int itemCount = CountItems();
	for (int i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->IsVisible()) {
			Dimensions itemDimensions;
			GetItemDimensions(item, &itemDimensions);
			gfx::Rect frame = fLocalLayouter->ItemFrame(itemDimensions);
			frame.x0 += left + area.x0;
			frame.y0 += top + area.y0;
			frame.x1 += left + area.x0;
			frame.y1 += top + area.y0;

			item->AlignInFrame(frame);
		}
	}
}


gfx::Size
BTwoDimensionalLayout::AddInsets(gfx::Size size)
{
	size.w = BLayoutUtils::AddDistances(size.w,
		BLayoutUtils::ComposeSpacing(fLeftInset)
			+ BLayoutUtils::ComposeSpacing(fRightInset));
	size.h = BLayoutUtils::AddDistances(size.h,
		BLayoutUtils::ComposeSpacing(fTopInset)
			+ BLayoutUtils::ComposeSpacing(fBottomInset));
	return size;
}


void
BTwoDimensionalLayout::AddInsets(float* minHeight, float* maxHeight,
	float* preferredHeight)
{
	float insets = BLayoutUtils::ComposeSpacing(fTopInset)
		+ BLayoutUtils::ComposeSpacing(fBottomInset);
	if (minHeight)
		*minHeight = BLayoutUtils::AddDistances(*minHeight, insets);
	if (maxHeight)
		*maxHeight = BLayoutUtils::AddDistances(*maxHeight, insets);
	if (preferredHeight)
		*preferredHeight = BLayoutUtils::AddDistances(*preferredHeight, insets);
}


gfx::Size
BTwoDimensionalLayout::SubtractInsets(gfx::Size size)
{
	size.w = BLayoutUtils::SubtractDistances(size.w,
		BLayoutUtils::ComposeSpacing(fLeftInset)
			+ BLayoutUtils::ComposeSpacing(fRightInset));
	size.h = BLayoutUtils::SubtractDistances(size.h,
		BLayoutUtils::ComposeSpacing(fTopInset)
			+ BLayoutUtils::ComposeSpacing(fBottomInset));
	return size;
}


void
BTwoDimensionalLayout::PrepareItems(BOrientation /*orientation*/)
{
}


bool
BTwoDimensionalLayout::HasMultiColumnItems()
{
	return false;
}


bool
BTwoDimensionalLayout::HasMultiRowItems()
{
	return false;
}


void
BTwoDimensionalLayout::_ValidateMinMax()
{
	fLocalLayouter->ValidateMinMax();
}


// #pragma mark - CompoundLayouter


BTwoDimensionalLayout::CompoundLayouter::CompoundLayouter(
	BOrientation orientation)
	:
	fOrientation(orientation),
	fLayoutContext(nullptr),
	fLastLayoutSize(-1)
{
}


BTwoDimensionalLayout::CompoundLayouter::~CompoundLayouter()
{
}


BOrientation
BTwoDimensionalLayout::CompoundLayouter::Orientation()
{
	return fOrientation;
}


Layouter*
BTwoDimensionalLayout::CompoundLayouter::GetLayouter(bool /*minMax*/)
{
	return fLayouter.get();
}


LayoutInfo*
BTwoDimensionalLayout::CompoundLayouter::GetLayoutInfo()
{
	return fLayoutInfo.get();
}


void
BTwoDimensionalLayout::CompoundLayouter::AddLocalLayouter(
	LocalLayouter* localLayouter)
{
	if (localLayouter) {
		if (std::find(fLocalLayouters.begin(), fLocalLayouters.end(), localLayouter)
				== fLocalLayouters.end()) {
			fLocalLayouters.push_back(localLayouter);
			InvalidateLayout();
		}
	}
}


void
BTwoDimensionalLayout::CompoundLayouter::RemoveLocalLayouter(
	LocalLayouter* localLayouter)
{
	if (std::erase(fLocalLayouters, localLayouter) > 0)
		InvalidateLayout();
}


void
BTwoDimensionalLayout::CompoundLayouter::AbsorbCompoundLayouter(
	std::shared_ptr<CompoundLayouter> other)
{
	if (other.get() == this)
		return;

	// NewGUI: a copy, since SetCompoundLayouter() removes the layouters from
	// other's list.
	const std::vector<LocalLayouter*> layouters = other->fLocalLayouters;
	for (auto i = layouters.rbegin(); i != layouters.rend(); i++) {
		LocalLayouter* layouter = *i;
		AddLocalLayouter(layouter);
		layouter->SetCompoundLayouter(shared_from_this(), fOrientation);
	}

	InvalidateLayout();
}


void
BTwoDimensionalLayout::CompoundLayouter::InvalidateLayout()
{
	if (!fLayouter)
		return;

	fLayouter.reset();
	fLayoutInfo.reset();
	fLayoutContext = nullptr;

	// notify all local layouters to invalidate the respective views
	// NewGUI: a copy, the invalidation must not change the list, but should
	// not depend on that
	const std::vector<LocalLayouter*> layouters = fLocalLayouters;
	for (LocalLayouter* layouter : layouters)
		layouter->InternalInvalidateLayout(this);
}


bool
BTwoDimensionalLayout::CompoundLayouter::IsMinMaxValid()
{
	return fLayouter != nullptr;
}


void
BTwoDimensionalLayout::CompoundLayouter::ValidateMinMax()
{
	if (IsMinMaxValid())
		return;

	fLastLayoutSize = -1;

	// create the layouter
	_PrepareItems();

	int elementCount = _CountElements();

	fLayouter = std::make_unique<CollapsingLayouter>(elementCount, _Spacing());

	// tell the layouter about our constraints
	// TODO: We should probably ignore local layouters whose view is hidden.
	// It's a bit tricky to find out, whether the view is hidden, though, since
	// this doesn't necessarily mean only hidden relative to the parent, but
	// hidden relative to a common parent.
	_AddConstraints(fLayouter.get());

	fLayoutInfo = fLayouter->CreateLayoutInfo();
}


void
BTwoDimensionalLayout::CompoundLayouter::Layout(float size,
	LocalLayouter* localLayouter, BLayoutContext* context)
{
	ValidateMinMax();

	if (context != fLayoutContext || fLastLayoutSize != size) {
		DoLayout(size, localLayouter, context);
		fLayoutContext = context;
		fLastLayoutSize = size;
	}
}


void
BTwoDimensionalLayout::CompoundLayouter::DoLayout(float size,
	LocalLayouter* /*localLayouter*/, BLayoutContext* /*context*/)
{
	fLayouter->Layout(*fLayoutInfo, size);
}


void
BTwoDimensionalLayout::CompoundLayouter::_PrepareItems()
{
	for (LocalLayouter* layouter : fLocalLayouters)
		layouter->PrepareItems(this);
}


int32
BTwoDimensionalLayout::CompoundLayouter::_CountElements()
{
	int32 elementCount = 0;
	for (LocalLayouter* layouter : fLocalLayouters) {
		int32 layouterCount = layouter->CountElements(this);
		elementCount = std::max(elementCount, layouterCount);
	}

	return elementCount;
}


bool
BTwoDimensionalLayout::CompoundLayouter::_HasMultiElementItems()
{
	for (LocalLayouter* layouter : fLocalLayouters) {
		if (layouter->HasMultiElementItems(this))
			return true;
	}

	return false;
}


void
BTwoDimensionalLayout::CompoundLayouter::_AddConstraints(Layouter* layouter)
{
	for (LocalLayouter* localLayouter : fLocalLayouters)
		localLayouter->AddConstraints(this, layouter);
}


float
BTwoDimensionalLayout::CompoundLayouter::_Spacing()
{
	if (!fLocalLayouters.empty())
		return fLocalLayouters.front()->Spacing(this);
	return 0;
}


// #pragma mark - VerticalCompoundLayouter


BTwoDimensionalLayout::VerticalCompoundLayouter::VerticalCompoundLayouter()
	:
	CompoundLayouter(BOrientation::Vertical),
	fCachedMinHeightForWidth(0),
	fCachedMaxHeightForWidth(0),
	fCachedPreferredHeightForWidth(0),
	fHeightForWidthLayoutContext(nullptr)
{
}


BTwoDimensionalLayout::VerticalCompoundLayouter::~VerticalCompoundLayouter()
{
	// NewGUI: leave a context that is still running
	if (fHeightForWidthLayoutContext != nullptr)
		fHeightForWidthLayoutContext->RemoveListener(this);
}


Layouter*
BTwoDimensionalLayout::VerticalCompoundLayouter::GetLayouter(bool minMax)
{
	return (minMax || !_HasHeightForWidth()
		? fLayouter.get() : fHeightForWidthLayouter.get());
}


void
BTwoDimensionalLayout::VerticalCompoundLayouter::InvalidateLayout()
{
	CompoundLayouter::InvalidateLayout();

	InvalidateHeightForWidth();
}


void
BTwoDimensionalLayout::VerticalCompoundLayouter::InvalidateHeightForWidth()
{
	if (fHeightForWidthLayouter != nullptr) {
		fHeightForWidthLayouter.reset();

		// also make sure we're not reusing the old layout info
		fLastLayoutSize = -1;

		for (LocalLayouter* layouter : fLocalLayouters)
			layouter->SetHeightForWidthConstraintsAdded(false);
	}
}


void
BTwoDimensionalLayout::VerticalCompoundLayouter::InternalGetHeightForWidth(
	LocalLayouter* localLayouter, BLayoutContext* context, bool /*realLayout*/,
	float* minHeight, float* maxHeight, float* preferredHeight)
{
	bool updateCachedInfo = false;

	if (_SetHeightForWidthLayoutContext(context)
		|| fHeightForWidthLayouter == nullptr) {
		// Either the layout context changed or we haven't initialized the
		// height for width layouter yet. We create it and init it now.

		// clone the vertical layouter
		fHeightForWidthLayouter = fLayouter->CloneLayouter();
		fLayoutInfo = fHeightForWidthLayouter->CreateLayoutInfo();

		// add the children's height for width constraints
		for (LocalLayouter* layouter : fLocalLayouters) {
			if (layouter->HasHeightForWidth()) {
				layouter->AddHeightForWidthConstraints(this,
					fHeightForWidthLayouter.get(), context);
			}
		}

		updateCachedInfo = true;
	} else if (localLayouter->HasHeightForWidth()) {
		// There is a height for width layouter and it has been initialized
		// in the current layout context. So we just add the height for width
		// constraints of the calling local layouter, if they haven't been
		// added yet.
		updateCachedInfo = localLayouter->AddHeightForWidthConstraints(this,
			fHeightForWidthLayouter.get(), context);
	}

	// update cached height for width info, if something changed
	if (updateCachedInfo) {
		// get the height for width info
		fCachedMinHeightForWidth = fHeightForWidthLayouter->MinSize();
		fCachedMaxHeightForWidth = fHeightForWidthLayouter->MaxSize();
		fCachedPreferredHeightForWidth
			= fHeightForWidthLayouter->PreferredSize();
	}

	if (minHeight)
		*minHeight = fCachedMinHeightForWidth;
	if (maxHeight)
		*maxHeight = fCachedMaxHeightForWidth;
	if (preferredHeight)
		*preferredHeight = fCachedPreferredHeightForWidth;
}


void
BTwoDimensionalLayout::VerticalCompoundLayouter::DoLayout(float size,
	LocalLayouter* localLayouter, BLayoutContext* context)
{
	Layouter* layouter;
	if (_HasHeightForWidth()) {
		float minHeight, maxHeight, preferredHeight;
		InternalGetHeightForWidth(localLayouter, context, true, &minHeight,
			&maxHeight, &preferredHeight);
		size = std::max(size, minHeight);
		layouter = fHeightForWidthLayouter.get();
	} else
		layouter = fLayouter.get();

	layouter->Layout(*fLayoutInfo, size);
}


bool
BTwoDimensionalLayout::VerticalCompoundLayouter::_HasHeightForWidth()
{
	for (LocalLayouter* layouter : fLocalLayouters) {
		if (layouter->HasHeightForWidth())
			return true;
	}

	return false;
}


bool
BTwoDimensionalLayout::VerticalCompoundLayouter
	::_SetHeightForWidthLayoutContext(BLayoutContext* context)
{
	if (context == fHeightForWidthLayoutContext)
		return false;

	if (fHeightForWidthLayoutContext != nullptr) {
		fHeightForWidthLayoutContext->RemoveListener(this);
		fHeightForWidthLayoutContext = nullptr;
	}

	// We can ignore the whole context business, if we have no more than one
	// local layouter. We use the layout context only to recognize when calls
	// of different local layouters belong to the same context.
	if (fLocalLayouters.size() <= 1)
		return false;

	fHeightForWidthLayoutContext = context;

	if (fHeightForWidthLayoutContext != nullptr)
		fHeightForWidthLayoutContext->AddListener(this);

	InvalidateHeightForWidth();

	return true;
}


void
BTwoDimensionalLayout::VerticalCompoundLayouter::LayoutContextLeft(
	BLayoutContext* /*context*/)
{
	fHeightForWidthLayoutContext = nullptr;
}


// #pragma mark - LocalLayouter


BTwoDimensionalLayout::LocalLayouter::LocalLayouter(
		BTwoDimensionalLayout* layout)
	:
	fLayout(layout),
	fHLayouter(std::make_shared<CompoundLayouter>(BOrientation::Horizontal)),
	fVLayouter(std::make_shared<VerticalCompoundLayouter>()),
	fHorizontalLayoutContext(nullptr),
	fHorizontalLayoutWidth(0),
	fHeightForWidthConstraintsAdded(false)
{
	fHLayouter->AddLocalLayouter(this);
	fVLayouter->AddLocalLayouter(this);
}


BTwoDimensionalLayout::LocalLayouter::~LocalLayouter()
{
	if (fHLayouter != nullptr)
		fHLayouter->RemoveLocalLayouter(this);

	if (fVLayouter != nullptr)
		fVLayouter->RemoveLocalLayouter(this);

	// NewGUI: leave a context that is still running
	if (fHorizontalLayoutContext != nullptr)
		fHorizontalLayoutContext->RemoveListener(this);
}


gfx::Size
BTwoDimensionalLayout::LocalLayouter::MinSize()
{
	return {fHLayouter->GetLayouter(true)->MinSize(),
		fVLayouter->GetLayouter(true)->MinSize()};
}


gfx::Size
BTwoDimensionalLayout::LocalLayouter::MaxSize()
{
	return {fHLayouter->GetLayouter(true)->MaxSize(),
		fVLayouter->GetLayouter(true)->MaxSize()};
}


gfx::Size
BTwoDimensionalLayout::LocalLayouter::PreferredSize()
{
	return {fHLayouter->GetLayouter(true)->PreferredSize(),
		fVLayouter->GetLayouter(true)->PreferredSize()};
}


void
BTwoDimensionalLayout::LocalLayouter::InvalidateLayout()
{
	fHLayouter->InvalidateLayout();
	fVLayouter->InvalidateLayout();
}


void
BTwoDimensionalLayout::LocalLayouter::Layout(gfx::Size size)
{
	DoHorizontalLayout(size.w);
	fVLayouter->Layout(size.h, this, fLayout->LayoutContext());
}


gfx::Rect
BTwoDimensionalLayout::LocalLayouter::ItemFrame(Dimensions itemDimensions)
{
	LayoutInfo* hLayoutInfo = fHLayouter->GetLayoutInfo();
	LayoutInfo* vLayoutInfo = fVLayouter->GetLayoutInfo();
	float x = hLayoutInfo->ElementLocation(itemDimensions.x);
	float y = vLayoutInfo->ElementLocation(itemDimensions.y);
	float width = hLayoutInfo->ElementRangeSize(itemDimensions.x,
		itemDimensions.width);
	float height = vLayoutInfo->ElementRangeSize(itemDimensions.y,
		itemDimensions.height);
	return {x, y, x + width, y + height};
}


void
BTwoDimensionalLayout::LocalLayouter::ValidateMinMax()
{
	if (fHLayouter->IsMinMaxValid() && fVLayouter->IsMinMaxValid())
		return;

	if (!fHLayouter->IsMinMaxValid())
		fHeightForWidthItems.clear();

	_SetHorizontalLayoutContext(nullptr, -1);

	fHLayouter->ValidateMinMax();
	fVLayouter->ValidateMinMax();
	fLayout->ResetLayoutInvalidation();
}


void
BTwoDimensionalLayout::LocalLayouter::DoHorizontalLayout(float width)
{
	BLayoutContext* context = fLayout->LayoutContext();
	if (fHorizontalLayoutContext != context
			|| width != fHorizontalLayoutWidth) {
		_SetHorizontalLayoutContext(context, width);
		fHLayouter->Layout(width, this, context);
		fVLayouter->InvalidateHeightForWidth();
	}
}


void
BTwoDimensionalLayout::LocalLayouter::InternalGetHeightForWidth(float width,
	float* minHeight, float* maxHeight, float* preferredHeight)
{
	DoHorizontalLayout(width);
	fVLayouter->InternalGetHeightForWidth(this, fHorizontalLayoutContext, false,
		minHeight, maxHeight, preferredHeight);
}


void
BTwoDimensionalLayout::LocalLayouter::AlignWith(LocalLayouter* other,
	BOrientation orientation)
{
	if (orientation == BOrientation::Horizontal)
		other->fHLayouter->AbsorbCompoundLayouter(fHLayouter);
	else
		other->fVLayouter->AbsorbCompoundLayouter(fVLayouter);
}


void
BTwoDimensionalLayout::LocalLayouter::PrepareItems(
	CompoundLayouter* compoundLayouter)
{
	fLayout->PrepareItems(compoundLayouter->Orientation());
}


int32
BTwoDimensionalLayout::LocalLayouter::CountElements(
	CompoundLayouter* compoundLayouter)
{
	if (compoundLayouter->Orientation() == BOrientation::Horizontal)
		return fLayout->InternalCountColumns();
	else
		return fLayout->InternalCountRows();
}


bool
BTwoDimensionalLayout::LocalLayouter::HasMultiElementItems(
	CompoundLayouter* compoundLayouter)
{
	if (compoundLayouter->Orientation() == BOrientation::Horizontal)
		return fLayout->HasMultiColumnItems();
	else
		return fLayout->HasMultiRowItems();
}


void
BTwoDimensionalLayout::LocalLayouter::AddConstraints(
	CompoundLayouter* compoundLayouter, Layouter* layouter)
{
	BOrientation orientation = compoundLayouter->Orientation();
	int itemCount = fLayout->CountItems();
	if (itemCount > 0) {
		for (int i = 0; i < itemCount; i++) {
			BLayoutItem* item = fLayout->ItemAt(i);
			if (item->IsVisible()) {
				Dimensions itemDimensions;
				fLayout->GetItemDimensions(item, &itemDimensions);

				gfx::Size min = item->MinSize();
				gfx::Size max = item->MaxSize();
				gfx::Size preferred = item->PreferredSize();

				if (orientation == BOrientation::Horizontal) {
					layouter->AddConstraints(
						itemDimensions.x,
						itemDimensions.width,
						min.w,
						max.w,
						preferred.w);

					if (item->HasHeightForWidth())
						fHeightForWidthItems.push_back(item);

				} else {
					layouter->AddConstraints(
						itemDimensions.y,
						itemDimensions.height,
						min.h,
						max.h,
						preferred.h);
				}
			}
		}

		// add column/row constraints
		int elementCount = CountElements(compoundLayouter);
		for (int element = 0; element < elementCount; element++) {
			ColumnRowConstraints constraints;
			fLayout->GetColumnRowConstraints(orientation, element,
				&constraints);
			// NewGUI: a part that is not set has no limit (see the top)
			float min = constraints.min;
			float max = constraints.max;
			if (BLayoutUtils::IsSet(min) || BLayoutUtils::IsSet(max)) {
				if (!BLayoutUtils::IsSet(min))
					min = 0;
				if (!BLayoutUtils::IsSet(max))
					max = kSizeUnlimited;
			}
			layouter->SetWeight(element, constraints.weight);
			layouter->AddConstraints(element, 1, min, max, min);
		}
	}
}


float
BTwoDimensionalLayout::LocalLayouter::Spacing(
	CompoundLayouter* compoundLayouter)
{
	return BLayoutUtils::ComposeSpacing(
		compoundLayouter->Orientation() == BOrientation::Horizontal
			? fLayout->fHSpacing : fLayout->fVSpacing);
}


bool
BTwoDimensionalLayout::LocalLayouter::HasHeightForWidth()
{
	return !fHeightForWidthItems.empty();
}


bool
BTwoDimensionalLayout::LocalLayouter::AddHeightForWidthConstraints(
	VerticalCompoundLayouter* /*compoundLayouter*/, Layouter* layouter,
	BLayoutContext* context)
{
	if (context != fHorizontalLayoutContext)
		return false;

	if (fHeightForWidthConstraintsAdded)
		return false;

	LayoutInfo* hLayoutInfo = fHLayouter->GetLayoutInfo();

	// add the children's height for width constraints
	for (BLayoutItem* item : fHeightForWidthItems) {
		Dimensions itemDimensions;
		fLayout->GetItemDimensions(item, &itemDimensions);

		float minHeight, maxHeight, preferredHeight;
		item->GetHeightForWidth(
			hLayoutInfo->ElementRangeSize(itemDimensions.x,
				itemDimensions.width),
			&minHeight, &maxHeight, &preferredHeight);
		layouter->AddConstraints(
			itemDimensions.y,
			itemDimensions.height,
			minHeight,
			maxHeight,
			preferredHeight);
	}

	SetHeightForWidthConstraintsAdded(true);

	return true;
}


void
BTwoDimensionalLayout::LocalLayouter::SetHeightForWidthConstraintsAdded(
	bool added)
{
	fHeightForWidthConstraintsAdded = added;
}


void
BTwoDimensionalLayout::LocalLayouter::SetCompoundLayouter(
	std::shared_ptr<CompoundLayouter> compoundLayouter, BOrientation orientation)
{
	std::shared_ptr<CompoundLayouter> oldCompoundLayouter;
	if (orientation == BOrientation::Horizontal) {
		oldCompoundLayouter = fHLayouter;
		fHLayouter = compoundLayouter;
	} else {
		oldCompoundLayouter = fVLayouter;
		fVLayouter = std::static_pointer_cast<VerticalCompoundLayouter>(
			compoundLayouter);
	}

	if (compoundLayouter == oldCompoundLayouter)
		return;

	if (oldCompoundLayouter != nullptr)
		oldCompoundLayouter->RemoveLocalLayouter(this);

	InternalInvalidateLayout(compoundLayouter.get());
}


void
BTwoDimensionalLayout::LocalLayouter::InternalInvalidateLayout(
	CompoundLayouter* /*compoundLayouter*/)
{
	_SetHorizontalLayoutContext(nullptr, -1);

	fLayout->BLayout::InvalidateLayout();
}


void
BTwoDimensionalLayout::LocalLayouter::_SetHorizontalLayoutContext(
	BLayoutContext* context, float width)
{
	if (context != fHorizontalLayoutContext) {
		if (fHorizontalLayoutContext != nullptr)
			fHorizontalLayoutContext->RemoveListener(this);

		fHorizontalLayoutContext = context;

		if (fHorizontalLayoutContext != nullptr)
			fHorizontalLayoutContext->AddListener(this);
	}

	fHorizontalLayoutWidth = width;
}


void
BTwoDimensionalLayout::LocalLayouter::LayoutContextLeft(
	BLayoutContext* /*context*/)
{
	fHorizontalLayoutContext = nullptr;
	fHorizontalLayoutWidth = -1;
}


} // namespace ngui
