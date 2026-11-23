/*
 * Copyright 2010, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/AbstractLayout.cpp (commit
 * b1d497bb69), without archiving. OwnerChanged() goes back to the layout's
 * own data when the owner is removed; Haiku kept a proxy to no view.
 */


#include <ngui/AbstractLayout.h>

#include <ngui/LayoutUtils.h>
#include <ngui/View.h>


namespace ngui {


struct BAbstractLayout::Proxy {
	virtual ~Proxy()
	{
	}

	virtual	gfx::Size	MinSize() const = 0;
	virtual void		SetMinSize(const gfx::Size&) = 0;

	virtual	gfx::Size	MaxSize() const = 0;
	virtual void		SetMaxSize(const gfx::Size&) = 0;

	virtual	gfx::Size	PreferredSize() const = 0;
	virtual void		SetPreferredSize(const gfx::Size&) = 0;

	virtual	BAlignment	Alignment() const = 0;
	virtual	void		SetAlignment(const BAlignment&) = 0;

	virtual	gfx::Rect	Frame() const = 0;
	virtual	void		SetFrame(const gfx::Rect& frame) = 0;

	virtual bool		IsVisible(bool ancestorHidden) const = 0;
	virtual	void		SetVisible(bool visible) = 0;
};


struct BAbstractLayout::DataProxy : Proxy {
	gfx::Size MinSize() const override
	{
		return minSize;
	}

	void SetMinSize(const gfx::Size& min) override
	{
		minSize = min;
	}

	gfx::Size MaxSize() const override
	{
		return maxSize;
	}

	void SetMaxSize(const gfx::Size& max) override
	{
		maxSize = max;
	}

	gfx::Size PreferredSize() const override
	{
		return preferredSize;
	}

	void SetPreferredSize(const gfx::Size& preferred) override
	{
		preferredSize = preferred;
	}

	BAlignment Alignment() const override
	{
		return this->alignment;
	}

	void SetAlignment(const BAlignment& align) override
	{
		this->alignment = align;
	}

	gfx::Rect Frame() const override
	{
		return frame;
	}

	void SetFrame(const gfx::Rect& newFrame) override
	{
		frame = newFrame;
	}

	bool IsVisible(bool) const override
	{
		return visible;
	}

	void SetVisible(bool isVisible) override
	{
		visible = isVisible;
	}

	gfx::Size	minSize = kUnsetSize;
	gfx::Size	maxSize = kUnsetSize;
	gfx::Size	preferredSize = kUnsetSize;
	BAlignment	alignment;
	gfx::Rect	frame = {-1, -1, 0, 0};
	bool		visible = true;
};


struct BAbstractLayout::ViewProxy : Proxy {
	explicit ViewProxy(BView* target)
		:
		view(target)
	{
	}

	gfx::Size MinSize() const override
	{
		return view->ExplicitMinSize();
	}

	void SetMinSize(const gfx::Size& min) override
	{
		view->SetExplicitMinSize(min);
	}

	gfx::Size MaxSize() const override
	{
		return view->ExplicitMaxSize();
	}

	void SetMaxSize(const gfx::Size& max) override
	{
		view->SetExplicitMaxSize(max);
	}

	gfx::Size PreferredSize() const override
	{
		return view->ExplicitPreferredSize();
	}

	void SetPreferredSize(const gfx::Size& preferred) override
	{
		view->SetExplicitPreferredSize(preferred);
	}

	BAlignment Alignment() const override
	{
		return view->ExplicitAlignment();
	}

	void SetAlignment(const BAlignment& alignment) override
	{
		view->SetExplicitAlignment(alignment);
	}

	gfx::Rect Frame() const override
	{
		return view->Frame();
	}

	void SetFrame(const gfx::Rect& frame) override
	{
		view->MoveTo(frame.LeftTop());
		view->ResizeTo(frame.Width(), frame.Height());
	}

	bool IsVisible(bool ancestorsVisible) const override
	{
		int showLevel = view->_HideLevel();
		return (showLevel - (ancestorsVisible ? 0 : 1)) <= 0;
	}

	void SetVisible(bool visible) override
	{
		// No need to check that we are not re-hiding, that is done
		// for us.
		if (visible)
			view->Show();
		else
			view->Hide();
	}

	BView*	view;
};


BAbstractLayout::BAbstractLayout()
	:
	fExplicitData(std::make_unique<DataProxy>())
{
}


BAbstractLayout::~BAbstractLayout()
{
}


gfx::Size
BAbstractLayout::MinSize()
{
	return BLayoutUtils::ComposeSize(fExplicitData->MinSize(), BaseMinSize());
}


gfx::Size
BAbstractLayout::MaxSize()
{
	return BLayoutUtils::ComposeSize(fExplicitData->MaxSize(), BaseMaxSize());
}


gfx::Size
BAbstractLayout::PreferredSize()
{
	return BLayoutUtils::ComposeSize(fExplicitData->PreferredSize(),
		BasePreferredSize());
}


BAlignment
BAbstractLayout::Alignment()
{
	return BLayoutUtils::ComposeAlignment(fExplicitData->Alignment(),
		BaseAlignment());
}


void
BAbstractLayout::SetExplicitMinSize(gfx::Size size)
{
	fExplicitData->SetMinSize(size);
}


void
BAbstractLayout::SetExplicitMaxSize(gfx::Size size)
{
	fExplicitData->SetMaxSize(size);
}


void
BAbstractLayout::SetExplicitPreferredSize(gfx::Size size)
{
	fExplicitData->SetPreferredSize(size);
}


void
BAbstractLayout::SetExplicitAlignment(BAlignment alignment)
{
	fExplicitData->SetAlignment(alignment);
}


gfx::Size
BAbstractLayout::BaseMinSize()
{
	return {0, 0};
}


gfx::Size
BAbstractLayout::BaseMaxSize()
{
	return {kSizeUnlimited, kSizeUnlimited};
}


gfx::Size
BAbstractLayout::BasePreferredSize()
{
	return {0, 0};
}


BAlignment
BAbstractLayout::BaseAlignment()
{
	return {BHorizontalAlignment::UseFullWidth, BVerticalAlignment::UseFullHeight};
}


gfx::Rect
BAbstractLayout::Frame()
{
	return fExplicitData->Frame();
}


void
BAbstractLayout::SetFrame(gfx::Rect frame)
{
	if (frame != fExplicitData->Frame()) {
		fExplicitData->SetFrame(frame);
		if (!Owner())
			Relayout();
	}
}


bool
BAbstractLayout::IsVisible()
{
	return fExplicitData->IsVisible(AncestorsVisible());
}


void
BAbstractLayout::SetVisible(bool visible)
{
	if (visible != fExplicitData->IsVisible(AncestorsVisible())) {
		fExplicitData->SetVisible(visible);
		if (Layout())
			Layout()->InvalidateLayout(false);
		VisibilityChanged(visible);
	}
}


void
BAbstractLayout::OwnerChanged(BView* /*was*/)
{
	if (Owner() != nullptr)
		fExplicitData = std::make_unique<ViewProxy>(Owner());
	else
		fExplicitData = std::make_unique<DataProxy>();
}


void
BAbstractLayout::AncestorVisibilityChanged(bool shown)
{
	if (AncestorsVisible() == shown)
		return;

	if (BView* owner = Owner()) {
		if (shown)
			owner->Show();
		else
			owner->Hide();
	}
	BLayout::AncestorVisibilityChanged(shown);
}


} // namespace ngui
