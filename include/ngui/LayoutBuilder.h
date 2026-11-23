/*
 * Copyright 2009-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: headers/os/interface/LayoutBuilder.h (commit
 * b1d497bb69), the Group, Grid and Cards builders.
 */
#pragma once


#include <memory>

#include <ngui/CardLayout.h>
#include <ngui/CardView.h>
#include <ngui/GridLayout.h>
#include <ngui/GridView.h>
#include <ngui/GroupLayout.h>
#include <ngui/GroupView.h>
#include <ngui/Palette.h>
#include <ngui/SpaceLayoutItem.h>
#include <ngui/View.h>
#include <ngui/Window.h>


// Documented in docs/api/ngui.dox.
namespace ngui::BLayoutBuilder {


template<typename ParentBuilder> class Base;
template<typename ParentBuilder = void*> class Group;
template<typename ParentBuilder = void*> class Grid;
template<typename ParentBuilder = void*> class Cards;


namespace internal {


// Haiku's AdoptSystemColors() for a view with the default (white) color.
inline void
AdoptPanelColor(BView* view)
{
	if (view->ViewColor() == gfx::Color{1, 1, 1, 1})
		view->SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));
}


} // namespace internal


/*!	What all builders share: the builder they came from. */
template<typename ParentBuilder>
class Base {
protected:
								Base() = default;
									/*!< A builder without parent. */

public:
			void				SetParent(ParentBuilder* parent)
									{ fParent = parent; }
									/*!< Conceptually private: set by the parent. */
			ParentBuilder&		End()
									{ return *fParent; }
									/*!< The builder this one came from. */

protected:
			ParentBuilder*		fParent = nullptr;
									/*!< The builder this one came from. */
};


/*!	Builds a BGroupLayout: a row or a column. */
template<typename ParentBuilder>
class Group : public Base<ParentBuilder> {
public:
	typedef Group<ParentBuilder>	ThisBuilder;	/*!< This builder. */
	typedef Group<ThisBuilder>		GroupBuilder;	/*!< For a nested group. */
	typedef Grid<ThisBuilder>		GridBuilder;	/*!< For a nested grid. */
	typedef Cards<ThisBuilder>		CardBuilder;	/*!< For nested cards. */

public:
								Group(BWindow* window,
									BOrientation orientation = BOrientation::Horizontal,
									float spacing = kUseDefaultSpacing)
									:
									fLayout(static_cast<BGroupLayout*>(window->SetLayout(
										std::make_unique<BGroupLayout>(orientation,
											spacing))))
									{}
									/*!< A group as the layout of \a window. */
								Group(BView* view,
									BOrientation orientation = BOrientation::Horizontal,
									float spacing = kUseDefaultSpacing)
									:
									fLayout(nullptr)
									{
										internal::AdoptPanelColor(view);
										fLayout = static_cast<BGroupLayout*>(view->SetLayout(
											std::make_unique<BGroupLayout>(orientation,
												spacing)));
									}
									/*!< A group as the layout of \a view. */
								Group(BGroupLayout* layout)
									:
									fLayout(layout)
									{}
									/*!< Builds on \a layout, which needs a target view. */
								Group(BGroupView* view)
									:
									fLayout(view->GroupLayout())
									{}
									/*!< Builds on the layout of \a view. */

			BGroupLayout*		Layout() const
									{ return fLayout; }
									/*!< The layout built. */
			BView*				View() const
									{ return fLayout->Owner(); }
									/*!< Its owner view; nullptr for a nested layout. */
			ThisBuilder&		GetLayout(BGroupLayout** _layout)
									{ *_layout = fLayout; return *this; }
									/*!< Stores Layout() in \a _layout. */
			ThisBuilder&		GetView(BView** _view)
									{ *_view = fLayout->Owner(); return *this; }
									/*!< Stores View() in \a _view. */

			ThisBuilder&		Add(std::unique_ptr<BView> view)
									{ fLayout->AddView(std::move(view)); return *this; }
									/*!< Adds \a view. */
			ThisBuilder&		Add(std::unique_ptr<BView> view, float weight)
									{
										fLayout->AddView(std::move(view), weight);
										return *this;
									}
									/*!< Adds \a view with \a weight. */
			ThisBuilder&		Add(std::unique_ptr<BLayoutItem> item)
									{ fLayout->AddItem(std::move(item)); return *this; }
									/*!< Adds \a item. */
			ThisBuilder&		Add(std::unique_ptr<BLayoutItem> item, float weight)
									{
										fLayout->AddItem(std::move(item), weight);
										return *this;
									}
									/*!< Adds \a item with \a weight. */

			GroupBuilder		AddGroup(BOrientation orientation,
									float spacing = kUseDefaultSpacing,
									float weight = 1.0f)
									{
										return AddGroup(std::make_unique<BGroupLayout>(
											orientation, spacing), weight);
									}
									/*!< Adds a nested group and builds it. */
			GroupBuilder		AddGroup(std::unique_ptr<BGroupView> groupView,
									float weight = 1.0f)
									{
										GroupBuilder builder(groupView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(groupView), weight);
										return builder;
									}
									/*!< Adds \a groupView and builds its layout. */
			GroupBuilder		AddGroup(std::unique_ptr<BGroupLayout> groupLayout,
									float weight = 1.0f)
									{
										GroupBuilder builder(static_cast<BGroupLayout*>(
											fLayout->AddItem(std::move(groupLayout),
												weight)));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a groupLayout nested and builds it. */

			GridBuilder			AddGrid(float horizontal = kUseDefaultSpacing,
									float vertical = kUseDefaultSpacing,
									float weight = 1.0f)
									{
										return AddGrid(std::make_unique<BGridLayout>(
											horizontal, vertical), weight);
									}
									/*!< Adds a nested grid and builds it. */
			GridBuilder			AddGrid(std::unique_ptr<BGridLayout> gridLayout,
									float weight = 1.0f)
									{
										GridBuilder builder(static_cast<BGridLayout*>(
											fLayout->AddItem(std::move(gridLayout),
												weight)));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a gridLayout nested and builds it. */
			GridBuilder			AddGrid(std::unique_ptr<BGridView> gridView,
									float weight = 1.0f)
									{
										GridBuilder builder(gridView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(gridView), weight);
										return builder;
									}
									/*!< Adds \a gridView and builds its layout. */

			CardBuilder			AddCards(float weight = 1.0f)
									{
										return AddCards(std::make_unique<BCardView>(),
											weight);
									}
									/*!< Adds a card view and builds its layout. */
			CardBuilder			AddCards(std::unique_ptr<BCardLayout> cardLayout,
									float weight = 1.0f)
									{
										CardBuilder builder(static_cast<BCardLayout*>(
											fLayout->AddItem(std::move(cardLayout),
												weight)));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a cardLayout nested and builds it (Haiku
										added its owner, which it does not have). */
			CardBuilder			AddCards(std::unique_ptr<BCardView> cardView,
									float weight = 1.0f)
									{
										CardBuilder builder(cardView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(cardView), weight);
										return builder;
									}
									/*!< Adds \a cardView and builds its layout. */

			ThisBuilder&		AddGlue(float weight = 1.0f)
									{
										fLayout->AddItem(BSpaceLayoutItem::CreateGlue(),
											weight);
										return *this;
									}
									/*!< Adds glue with \a weight. */
			ThisBuilder&		AddStrut(float size)
									{
										if (fLayout->Orientation() == BOrientation::Horizontal)
											fLayout->AddItem(
												BSpaceLayoutItem::CreateHorizontalStrut(size));
										else
											fLayout->AddItem(
												BSpaceLayoutItem::CreateVerticalStrut(size));
										return *this;
									}
									/*!< Adds a strut of \a size along the group. */

			ThisBuilder&		SetInsets(float left, float top, float right,
									float bottom)
									{
										fLayout->SetInsets(left, top, right, bottom);
										return *this;
									}
									/*!< Sets the insets. */
			ThisBuilder&		SetInsets(float horizontal, float vertical)
									{ fLayout->SetInsets(horizontal, vertical); return *this; }
									/*!< Sets the insets. */
			ThisBuilder&		SetInsets(float insets)
									{ fLayout->SetInsets(insets); return *this; }
									/*!< Sets the insets. */

			ThisBuilder&		SetExplicitMinSize(gfx::Size size)
									{ fLayout->SetExplicitMinSize(size); return *this; }
									/*!< Sets the explicit minimum. */
			ThisBuilder&		SetExplicitMaxSize(gfx::Size size)
									{ fLayout->SetExplicitMaxSize(size); return *this; }
									/*!< Sets the explicit maximum. */
			ThisBuilder&		SetExplicitPreferredSize(gfx::Size size)
									{ fLayout->SetExplicitPreferredSize(size); return *this; }
									/*!< Sets the explicit preferred size. */
			ThisBuilder&		SetExplicitAlignment(BAlignment alignment)
									{ fLayout->SetExplicitAlignment(alignment); return *this; }
									/*!< Sets the explicit alignment. */

								operator BGroupLayout*()
									{ return fLayout; }
									/*!< The layout built. */

private:
			BGroupLayout*		fLayout;
};


/*!	Builds a BGridLayout. */
template<typename ParentBuilder>
class Grid : public Base<ParentBuilder> {
public:
	typedef Grid<ParentBuilder>		ThisBuilder;	/*!< This builder. */
	typedef Group<ThisBuilder>		GroupBuilder;	/*!< For a nested group. */
	typedef Grid<ThisBuilder>		GridBuilder;	/*!< For a nested grid. */
	typedef Cards<ThisBuilder>		CardBuilder;	/*!< For nested cards. */

public:
								Grid(BWindow* window, float horizontal = kUseDefaultSpacing,
									float vertical = kUseDefaultSpacing)
									:
									fLayout(static_cast<BGridLayout*>(window->SetLayout(
										std::make_unique<BGridLayout>(horizontal,
											vertical))))
									{}
									/*!< A grid as the layout of \a window. */
								Grid(BView* view, float horizontal = kUseDefaultSpacing,
									float vertical = kUseDefaultSpacing)
									:
									fLayout(nullptr)
									{
										internal::AdoptPanelColor(view);
										fLayout = static_cast<BGridLayout*>(view->SetLayout(
											std::make_unique<BGridLayout>(horizontal,
												vertical)));
									}
									/*!< A grid as the layout of \a view. */
								Grid(BGridLayout* layout)
									:
									fLayout(layout)
									{}
									/*!< Builds on \a layout, which needs a target view. */
								Grid(BGridView* view)
									:
									fLayout(view->GridLayout())
									{}
									/*!< Builds on the layout of \a view. */

			BGridLayout*		Layout() const
									{ return fLayout; }
									/*!< The layout built. */
			BView*				View() const
									{ return fLayout->Owner(); }
									/*!< Its owner view; nullptr for a nested layout. */
			ThisBuilder&		GetLayout(BGridLayout** _layout)
									{ *_layout = fLayout; return *this; }
									/*!< Stores Layout() in \a _layout. */
			ThisBuilder&		GetView(BView** _view)
									{ *_view = fLayout->Owner(); return *this; }
									/*!< Stores View() in \a _view. */

			ThisBuilder&		Add(std::unique_ptr<BView> view, int32 column, int32 row,
									int32 columnCount = 1, int32 rowCount = 1)
									{
										fLayout->AddView(std::move(view), column, row,
											columnCount, rowCount);
										return *this;
									}
									/*!< Adds \a view covering these cells. */
			ThisBuilder&		Add(std::unique_ptr<BLayoutItem> item, int32 column,
									int32 row, int32 columnCount = 1, int32 rowCount = 1)
									{
										fLayout->AddItem(std::move(item), column, row,
											columnCount, rowCount);
										return *this;
									}
									/*!< Adds \a item covering these cells. */

			GroupBuilder		AddGroup(BOrientation orientation, float spacing,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										return AddGroup(std::make_unique<BGroupLayout>(
											orientation, spacing), column, row, columnCount,
											rowCount);
									}
									/*!< Adds a nested group in these cells. */
			GroupBuilder		AddGroup(std::unique_ptr<BGroupView> groupView,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										GroupBuilder builder(groupView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(groupView), column, row,
											columnCount, rowCount);
										return builder;
									}
									/*!< Adds \a groupView in these cells. */
			GroupBuilder		AddGroup(std::unique_ptr<BGroupLayout> groupLayout,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										GroupBuilder builder(static_cast<BGroupLayout*>(
											fLayout->AddItem(std::move(groupLayout), column,
												row, columnCount, rowCount)));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a groupLayout nested in these cells. */

			GridBuilder			AddGrid(float horizontalSpacing, float verticalSpacing,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										return AddGrid(std::make_unique<BGridLayout>(
											horizontalSpacing, verticalSpacing), column, row,
											columnCount, rowCount);
									}
									/*!< Adds a nested grid in these cells. */
			GridBuilder			AddGrid(std::unique_ptr<BGridLayout> gridLayout,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										GridBuilder builder(static_cast<BGridLayout*>(
											fLayout->AddItem(std::move(gridLayout), column,
												row, columnCount, rowCount)));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a gridLayout nested in these cells. */
			GridBuilder			AddGrid(std::unique_ptr<BGridView> gridView,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										GridBuilder builder(gridView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(gridView), column, row,
											columnCount, rowCount);
										return builder;
									}
									/*!< Adds \a gridView in these cells. */

			CardBuilder			AddCards(int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										return AddCards(std::make_unique<BCardView>(), column,
											row, columnCount, rowCount);
									}
									/*!< Adds a card view in these cells. */
			CardBuilder			AddCards(std::unique_ptr<BCardLayout> cardLayout,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										CardBuilder builder(static_cast<BCardLayout*>(
											fLayout->AddItem(std::move(cardLayout), column,
												row, columnCount, rowCount)));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a cardLayout nested in these cells. */
			CardBuilder			AddCards(std::unique_ptr<BCardView> cardView,
									int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										CardBuilder builder(cardView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(cardView), column, row,
											columnCount, rowCount);
										return builder;
									}
									/*!< Adds \a cardView in these cells. */

			ThisBuilder&		AddGlue(int32 column, int32 row, int32 columnCount = 1,
									int32 rowCount = 1)
									{
										fLayout->AddItem(BSpaceLayoutItem::CreateGlue(),
											column, row, columnCount, rowCount);
										return *this;
									}
									/*!< Adds glue in these cells. */

			ThisBuilder&		SetHorizontalSpacing(float spacing)
									{ fLayout->SetHorizontalSpacing(spacing); return *this; }
									/*!< Sets the spacing between columns. */
			ThisBuilder&		SetVerticalSpacing(float spacing)
									{ fLayout->SetVerticalSpacing(spacing); return *this; }
									/*!< Sets the spacing between rows. */
			ThisBuilder&		SetSpacing(float horizontal, float vertical)
									{ fLayout->SetSpacing(horizontal, vertical); return *this; }
									/*!< Sets both spacings. */

			ThisBuilder&		SetColumnWeight(int32 column, float weight)
									{ fLayout->SetColumnWeight(column, weight); return *this; }
									/*!< Sets the weight of \a column. */
			ThisBuilder&		SetRowWeight(int32 row, float weight)
									{ fLayout->SetRowWeight(row, weight); return *this; }
									/*!< Sets the weight of \a row. */

			ThisBuilder&		SetInsets(float left, float top, float right,
									float bottom)
									{
										fLayout->SetInsets(left, top, right, bottom);
										return *this;
									}
									/*!< Sets the insets. */
			ThisBuilder&		SetInsets(float horizontal, float vertical)
									{ fLayout->SetInsets(horizontal, vertical); return *this; }
									/*!< Sets the insets. */
			ThisBuilder&		SetInsets(float insets)
									{ fLayout->SetInsets(insets); return *this; }
									/*!< Sets the insets. */

			ThisBuilder&		SetExplicitMinSize(gfx::Size size)
									{ fLayout->SetExplicitMinSize(size); return *this; }
									/*!< Sets the explicit minimum. */
			ThisBuilder&		SetExplicitMaxSize(gfx::Size size)
									{ fLayout->SetExplicitMaxSize(size); return *this; }
									/*!< Sets the explicit maximum. */
			ThisBuilder&		SetExplicitPreferredSize(gfx::Size size)
									{ fLayout->SetExplicitPreferredSize(size); return *this; }
									/*!< Sets the explicit preferred size. */
			ThisBuilder&		SetExplicitAlignment(BAlignment alignment)
									{ fLayout->SetExplicitAlignment(alignment); return *this; }
									/*!< Sets the explicit alignment. */

								operator BGridLayout*()
									{ return fLayout; }
									/*!< The layout built. */

private:
			BGridLayout*		fLayout;
};


/*!	Builds a BCardLayout: pages of which one is shown. */
template<typename ParentBuilder>
class Cards : public Base<ParentBuilder> {
public:
	typedef Cards<ParentBuilder>	ThisBuilder;	/*!< This builder. */
	typedef Group<ThisBuilder>		GroupBuilder;	/*!< For a nested group. */
	typedef Grid<ThisBuilder>		GridBuilder;	/*!< For a nested grid. */
	typedef Cards<ThisBuilder>		CardBuilder;	/*!< For nested cards. */

public:
								Cards(BWindow* window)
									:
									fLayout(static_cast<BCardLayout*>(window->SetLayout(
										std::make_unique<BCardLayout>())))
									{}
									/*!< Cards as the layout of \a window. */
								Cards(BView* view)
									:
									fLayout(nullptr)
									{
										internal::AdoptPanelColor(view);
										fLayout = static_cast<BCardLayout*>(view->SetLayout(
											std::make_unique<BCardLayout>()));
									}
									/*!< Cards as the layout of \a view. */
								Cards(BCardLayout* layout)
									:
									fLayout(layout)
									{}
									/*!< Builds on \a layout, which needs a target view. */
								Cards(BCardView* view)
									:
									fLayout(view->CardLayout())
									{}
									/*!< Builds on the layout of \a view. */

			BCardLayout*		Layout() const
									{ return fLayout; }
									/*!< The layout built. */
			BView*				View() const
									{ return fLayout->Owner(); }
									/*!< Its owner view; nullptr for a nested layout. */
			ThisBuilder&		GetLayout(BCardLayout** _layout)
									{ *_layout = fLayout; return *this; }
									/*!< Stores Layout() in \a _layout. */
			ThisBuilder&		GetView(BView** _view)
									{ *_view = fLayout->Owner(); return *this; }
									/*!< Stores View() in \a _view. */

			ThisBuilder&		Add(std::unique_ptr<BView> view)
									{ fLayout->AddView(std::move(view)); return *this; }
									/*!< Adds \a view as a page. */
			ThisBuilder&		Add(std::unique_ptr<BLayoutItem> item)
									{ fLayout->AddItem(std::move(item)); return *this; }
									/*!< Adds \a item as a page. */

			GroupBuilder		AddGroup(BOrientation orientation,
									float spacing = kUseDefaultSpacing)
									{
										return AddGroup(std::make_unique<BGroupLayout>(
											orientation, spacing));
									}
									/*!< Adds a nested group as a page. */
			GroupBuilder		AddGroup(std::unique_ptr<BGroupView> groupView)
									{
										GroupBuilder builder(groupView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(groupView));
										return builder;
									}
									/*!< Adds \a groupView as a page. */
			GroupBuilder		AddGroup(std::unique_ptr<BGroupLayout> groupLayout)
									{
										GroupBuilder builder(static_cast<BGroupLayout*>(
											fLayout->AddItem(std::move(groupLayout))));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a groupLayout nested as a page. */

			GridBuilder			AddGrid(float horizontal = kUseDefaultSpacing,
									float vertical = kUseDefaultSpacing)
									{
										return AddGrid(std::make_unique<BGridLayout>(
											horizontal, vertical));
									}
									/*!< Adds a nested grid as a page. */
			GridBuilder			AddGrid(std::unique_ptr<BGridLayout> gridLayout)
									{
										GridBuilder builder(static_cast<BGridLayout*>(
											fLayout->AddItem(std::move(gridLayout))));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a gridLayout nested as a page. */
			GridBuilder			AddGrid(std::unique_ptr<BGridView> gridView)
									{
										GridBuilder builder(gridView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(gridView));
										return builder;
									}
									/*!< Adds \a gridView as a page. */

			CardBuilder			AddCards()
									{ return AddCards(std::make_unique<BCardView>()); }
									/*!< Adds a card view as a page. */
			CardBuilder			AddCards(std::unique_ptr<BCardLayout> cardLayout)
									{
										CardBuilder builder(static_cast<BCardLayout*>(
											fLayout->AddItem(std::move(cardLayout))));
										builder.SetParent(this);
										return builder;
									}
									/*!< Adds \a cardLayout nested as a page. */
			CardBuilder			AddCards(std::unique_ptr<BCardView> cardView)
									{
										CardBuilder builder(cardView.get());
										builder.SetParent(this);
										fLayout->AddView(std::move(cardView));
										return builder;
									}
									/*!< Adds \a cardView as a page. */

			ThisBuilder&		SetExplicitMinSize(gfx::Size size)
									{ fLayout->SetExplicitMinSize(size); return *this; }
									/*!< Sets the explicit minimum. */
			ThisBuilder&		SetExplicitMaxSize(gfx::Size size)
									{ fLayout->SetExplicitMaxSize(size); return *this; }
									/*!< Sets the explicit maximum. */
			ThisBuilder&		SetExplicitPreferredSize(gfx::Size size)
									{ fLayout->SetExplicitPreferredSize(size); return *this; }
									/*!< Sets the explicit preferred size. */
			ThisBuilder&		SetExplicitAlignment(BAlignment alignment)
									{ fLayout->SetExplicitAlignment(alignment); return *this; }
									/*!< Sets the explicit alignment. */

			ThisBuilder&		SetVisibleItem(int32 index)
									{ fLayout->SetVisibleItem(index); return *this; }
									/*!< Shows page \a index. */

								operator BCardLayout*()
									{ return fLayout; }
									/*!< The layout built. */

private:
			BCardLayout*		fLayout;
};


} // namespace ngui::BLayoutBuilder
