/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// A window with a button and a label in the BeOS look: a BButton tracks
// the click as Haiku's does and sends its message to the window, which
// counts the clicks in a BStringView; Enter clicks it too, as it is the
// default button. Closing the window quits the application. It shows the
// toolkit end to end: the platform's events as messages to the window,
// distributed to views; the controls drawing through the look into their
// Canvas; the window putting the DisplayLists together, Skia rendering
// them, the presenter copying the changed area into the window; text
// shaped by the text subsystem with the bundled Noto Sans (and the
// system's fonts for characters it lacks), drawn by Skia with the same
// font file; the layout keeping both centered when the
// window is resized (BLayoutBuilder). Below them, in a BBox, a BSlider
// drives a BStatusBar while its thumb moves (its modification message);
// a BScrollView shows a list of check boxes that is longer than its room;
// a BSplitView puts both side by side with a splitter to drag, on the
// first page of a BTabView whose second page says what this is. A BMenuBar
// on top resets the counter (after asking in a BAlert), quits, or shows
// that page and an about alert (its menus open in popups of the window;
// Alt+Escape opens them from the keyboard, Alt+R resets). A BMenuField
// chooses where the slider's hash marks are; a right click on the label
// opens a context menu (a BPopUpMenu) with the same commands as the
// menus. A BTextView on a page of its own takes notes,
// styled by the Text menu (Alt+B bold, Alt+L large, Alt+P plain); a
// BTextControl asks for a name, and the label greets it when Enter is
// pressed or the field is left. A page with lists shows a BListView and a
// BOutlineListView. Text dragged from another application drops into the
// notes; files dropped on the window are listed in an alert. File > Open
// shows the system's file dialog (BFilePanel). The texts are translated
// (B_TRANSLATE(), a German catalog comes with it; NGUI_LANGUAGE=x-pseudo
// pseudo-localizes them).


#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

#include <ngui/Alert.h>
#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/Catalog.h>
#include <ngui/CheckBox.h>
#include <ngui/DateFormat.h>
#include <ngui/FilePanel.h>
#include <ngui/Font.h>
#include <ngui/GroupView.h>
#include <ngui/IconUtils.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Invoker.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/ListView.h>
#include <ngui/LocaleRoster.h>
#include <ngui/LookRegistry.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/NumberFormat.h>
#include <ngui/OutlineListView.h>
#include <ngui/Palette.h>
#include <ngui/PopUpMenu.h>
#include <ngui/ScrollView.h>
#include <ngui/Slider.h>
#include <ngui/StatusBar.h>
#include <ngui/StringItem.h>
#include <ngui/StringView.h>
#include <ngui/TabView.h>
#include <ngui/TextControl.h>
#include <ngui/TextView.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#ifdef NGUI_DEMO_WITH_BLEND2D
#include <ngui/gfx/Blend2DBackend.h>
#endif
#include <ngui/platform/SdlPlatform.h>
#include <ngui/svg/LunaSvgRenderer.h>
#include <ngui/text/FontSource.h>
#include <ngui/text/FontTextSystem.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Demo"


namespace {


using namespace ngui;


constexpr std::uint32_t kClicked = FourCharCode("CLCK");
constexpr std::uint32_t kProgress = FourCharCode("PRGS");
constexpr std::uint32_t kReset = FourCharCode("RSET");
constexpr std::uint32_t kResetAnswered = FourCharCode("RSTA");
constexpr std::uint32_t kAbout = FourCharCode("ABOT");
constexpr std::uint32_t kStylePlain = FourCharCode("SPLN");
constexpr std::uint32_t kStyleBold = FourCharCode("SBLD");
constexpr std::uint32_t kStyleLarge = FourCharCode("SLRG");
constexpr std::uint32_t kStyleRed = FourCharCode("SRED");
constexpr std::uint32_t kHashMarks = FourCharCode("HASH");
constexpr std::uint32_t kName = FourCharCode("NAME");
constexpr std::uint32_t kChosen = FourCharCode("CHSN");
constexpr std::uint32_t kOpen = FourCharCode("OPEN");
constexpr std::uint32_t kLook = FourCharCode("LOOK");


// The counter's label: a right click opens a context menu with the
// commands of the menus, which sends them to the window.
class CounterLabel : public BStringView {
public:
	CounterLabel()
		:
		BStringView("label", B_TRANSLATE("Not clicked yet"))
	{
	}

	void MouseDown(gfx::Point where) override
	{
		const BMessage* current = Window()->CurrentMessage();
		if (current == nullptr || (current->GetInt32("buttons", 0) & 2) == 0)
			return;
		auto menu = std::make_unique<BPopUpMenu>("context", false, false);
		menu->AddItem(std::make_unique<BMenuItem>(B_TRANSLATE("Reset"),
			std::make_unique<BMessage>(kReset)));
		menu->AddItem(std::make_unique<BMenuItem>(B_TRANSLATE("About"),
			std::make_unique<BMessage>(kAbout)));
		(void)BPopUpMenu::Go(std::move(menu), ConvertToScreen(where), {.autoInvoke = true});
	}
};


// The window: counts the clicks of its button in its label; shows the
// value of its slider in its status bar.
class CounterWindow : public BWindow {
public:
	CounterWindow()
		:
		BWindow(gfx::Rect{100, 100, 620, 440}, "NewGUI", BWindowLook::Titled,
			BWindowFlag::QuitOnWindowClose | BWindowFlag::AutoUpdateSizeLimits)
	{
		auto label = std::make_unique<CounterLabel>();
		fLabel = label.get();
		auto button = std::make_unique<BButton>(B_TRANSLATE("Click me"),
			std::make_unique<BMessage>(kClicked));
		BButton* clickMe = button.get();
		// Tooltips show when the mouse rests over a view (W6b).
		button->SetToolTip(B_TRANSLATE("Counts the clicks.\nEnter clicks it too."));
		auto slider = std::make_unique<BSlider>("slider", B_TRANSLATE("Value"),
			std::make_unique<BMessage>(kProgress), 0, 100, BOrientation::Horizontal);
		slider->SetModificationMessage(std::make_unique<BMessage>(kProgress));
		slider->SetLimitLabels("0 %", "100 %");
		slider->SetHashMarks(BHashMarkLocation::BottomOrRight);
		slider->SetHashMarkCount(11);
		slider->SetToolTip(B_TRANSLATE("Drag the thumb or use the arrow keys"));
		fSlider = slider.get();
		// Where the slider's hash marks are, chosen in a menu field.
		auto marks = std::make_unique<BPopUpMenu>("marks");
		const std::pair<const char*, BHashMarkLocation> locations[] = {
			{B_TRANSLATE("None"), BHashMarkLocation::None},
			{B_TRANSLATE("Top"), BHashMarkLocation::TopOrLeft},
			{B_TRANSLATE("Bottom"), BHashMarkLocation::BottomOrRight},
			{B_TRANSLATE("Both"), BHashMarkLocation::Both}};
		for (const auto& [name, location] : locations) {
			auto message = std::make_unique<BMessage>(kHashMarks);
			(void)message->AddInt32("where", static_cast<std::int32_t>(location));
			marks->AddItem(std::make_unique<BMenuItem>(name, std::move(message)));
		}
		marks->ItemAt(2)->SetMarked(true);
		auto marksField = std::make_unique<BMenuField>("marks", B_TRANSLATE("Hash marks:"),
			std::move(marks));
		marksField->SetToolTip(B_TRANSLATE("Where the slider draws its marks"));
		auto name = std::make_unique<BTextControl>("name", B_TRANSLATE("Name:"), "",
			std::make_unique<BMessage>(kName));
		name->SetToolTip(B_TRANSLATE("Enter sends the name"));
		fName = name.get();
		// a password field: its text view hides typing (bullets)
		auto password = std::make_unique<BTextControl>("password", B_TRANSLATE("Password:"),
			"", nullptr);
		password->TextView()->HideTyping(true);
		password->SetToolTip(B_TRANSLATE("Shown as bullets; cannot be copied"));
		auto status = std::make_unique<BStatusBar>("status", B_TRANSLATE("Progress: "), " %");
		status->SetTo(0, {}, "0");
		fStatus = status.get();

		// The slider and the status bar in a labelled box.
		auto progress = std::make_unique<BGroupView>(BOrientation::Vertical);
		BLayoutBuilder::Group<>(progress.get())
			.SetInsets(kUseItemInsets)
			.Add(std::move(slider))
			.Add(std::move(marksField))
			.Add(std::move(name))
			.Add(std::move(password))
			.Add(std::move(status));
		auto box = std::make_unique<BBox>("progress box");
		box->SetLabel(B_TRANSLATE("Progress"));
		box->AddChild(std::move(progress));

		// A list of options, longer than the room for it: the scroll view
		// sets up its bar from the list's preferred size.
		auto options = std::make_unique<BGroupView>(BOrientation::Vertical);
		BLayoutBuilder::Group<> optionsBuilder(options.get());
		optionsBuilder.SetInsets(kUseHalfItemInsets);
		for (int i = 1; i <= 12; i++) {
			auto option = std::make_unique<BCheckBox>("Option " + std::to_string(i), nullptr);
			option->SetToolTip("Check box " + std::to_string(i) + " of 12");
			optionsBuilder.Add(std::move(option));
		}
		auto scrollView = std::make_unique<BScrollView>("options", std::move(options),
			BViewFlags{}, false, true);
		scrollView->SetExplicitMinSize({kSizeUnset, 60});
		scrollView->SetExplicitPreferredSize({kSizeUnset, 110});

		// The box and the list side by side, on the first of two pages.
		auto split = std::make_unique<BSplitView>(BOrientation::Horizontal);
		BLayoutBuilder::Split<>(split.get())
			.Add(std::move(box), 2.0f)
			.Add(std::move(scrollView), 1.0f);
		auto tabs = std::make_unique<BTabView>("tabs");
		tabs->AddTab(std::move(split));
		tabs->TabAt(0)->SetLabel(B_TRANSLATE("Panels"));
		// A page to type notes on: a text view that scrolls, styled by the
		// Text menu.
		auto notes = std::make_unique<BTextView>("notes");
		notes->SetStylable(true);
		notes->SetText("Type here. Shift and the arrows select, a double click selects "
			"a word, a triple click a line. The Text menu styles the selection.\n"
			"Characters the bundled Noto Sans lacks come from the system's fonts: "
			"\u2192 \u65e5\u672c\u8a9e \ud55c\uad6d\uc5b4 \u0e44\u0e17\u0e22 "
			"\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac\n"
			"Right to left: \u05e9\u05dc\u05d5\u05dd (123) "
			"\u0645\u0631\u062d\u0628\u0627\n"
			"\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd!\n");
		fNotes = notes.get();
		auto notesScroll = std::make_unique<BScrollView>("notes scroll", std::move(notes),
			BViewFlags{}, false, true);
		tabs->AddTab(std::move(notesScroll));
		tabs->TabAt(1)->SetLabel(B_TRANSLATE("Notes"));
		// A page with a list: a double click or Enter names the item in the
		// label.
		auto kits = std::make_unique<BListView>("kits");
		for (const char* kit : {"Application Kit", "Interface Kit", "Storage Kit",
			"Support Kit", "Media Kit", "Network Kit", "Game Kit", "Device Kit",
			"Translation Kit", "Mail Kit"}) {
			kits->AddItem(std::make_unique<BStringItem>(kit));
		}
		kits->ItemAt(6)->SetEnabled(false);
		kits->SetInvocationMessage(std::make_unique<BMessage>(kChosen));
		fKits = kits.get();
		auto kitsScroll = std::make_unique<BScrollView>("kits scroll", std::move(kits),
			BViewFlags{}, false, true);
		// Beside it an outline of the classes: the latches or Right and
		// Left expand and collapse.
		auto classes = std::make_unique<BOutlineListView>("classes");
		const std::pair<const char*, std::uint32_t> outline[] = {
			{"Interface Kit", 0}, {"Controls", 1}, {"BButton", 2}, {"BCheckBox", 2},
			{"BSlider", 2}, {"Lists", 1}, {"BListView", 2}, {"BOutlineListView", 2},
			{"Text", 1}, {"BTextView", 2}, {"BTextControl", 2},
			{"Application Kit", 0}, {"BLooper", 1}, {"BMessage", 1}};
		for (const auto& [className, level] : outline)
			classes->AddItem(std::make_unique<BStringItem>(className, level));
		classes->Collapse(classes->FullListItemAt(8));
		classes->SetInvocationMessage(std::make_unique<BMessage>(kChosen));
		fClasses = classes.get();
		auto classesScroll = std::make_unique<BScrollView>("classes scroll",
			std::move(classes), BViewFlags{}, false, true);
		auto lists = std::make_unique<BSplitView>(BOrientation::Horizontal);
		BLayoutBuilder::Split<>(lists.get())
			.Add(std::move(kitsScroll), 1.0f)
			.Add(std::move(classesScroll), 1.0f);
		tabs->AddTab(std::move(lists));
		tabs->TabAt(2)->SetLabel(B_TRANSLATE("Lists"));
		tabs->AddTab(std::make_unique<BStringView>("About",
			"NewGUI: Haiku's Interface Kit on SDL3 and Skia"));

		// The menus. The look menu has every look the backend can draw;
		// choosing one switches all windows at once.
		auto looks = std::make_unique<BMenu>(B_TRANSLATE("Look"));
		looks->SetRadioMode(true);
		const std::string current(LookRegistry::Default().Current()->Info().id);
		for (const LookInfo& look : LookRegistry::Default().Available()) {
			auto message = std::make_unique<BMessage>(kLook);
			(void)message->AddString("id", std::string(look.id));
			auto item = std::make_unique<BMenuItem>(look.displayName, std::move(message));
			item->SetMarked(look.id == current);
			looks->AddItem(std::move(item));
		}
		auto menuBar = std::make_unique<BMenuBar>("menu bar");
		BLayoutBuilder::Menu<>(menuBar.get())
			.AddMenu(B_TRANSLATE("File"))
				.AddItem(B_TRANSLATE("Open\u2026"), kOpen, 'O')
				.AddItem(B_TRANSLATE("Reset"), kReset, 'R')
				.AddSeparator()
				.AddItem(B_TRANSLATE("Quit"), kQuitRequested, 'Q')
			.End()
			.AddMenu(B_TRANSLATE("Text"))
				.AddItem(B_TRANSLATE("Plain"), kStylePlain, 'P')
				.AddItem(B_TRANSLATE("Bold"), kStyleBold, 'B')
				.AddItem(B_TRANSLATE("Large"), kStyleLarge, 'L')
				.AddItem(B_TRANSLATE("Red"), kStyleRed)
			.End()
			.AddMenu(std::move(looks)).End()
			.AddMenu(B_TRANSLATE("Help"))
				.AddItem(B_TRANSLATE("About"), kAbout)
			.End();
		fTabs = tabs.get();

		// The menu bar on top, without insets; below it the label and the
		// button stay centered and the tab view takes the width; the window
		// cannot become smaller than they and the window insets.
		BLayoutBuilder::Group<>(this, BOrientation::Vertical, 0)
			.Add(std::move(menuBar))
			.AddGroup(BOrientation::Vertical)
			.SetInsets(kUseWindowInsets)
			.AddGlue()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(std::move(label))
				.AddGlue()
			.End()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(std::move(button))
				.AddGlue()
			.End()
			.AddGlue()
			.Add(std::move(tabs))
			.End();

		clickMe->MakeDefault(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kProgress) {
			const std::int32_t value = fSlider->Value();
			fStatus->SetTo(static_cast<float>(value), {}, std::to_string(value));
			return;
		}
		if (message->what == kName) {
			const std::string name(fName->Text());
			fLabel->SetText(name.empty() ? B_TRANSLATE("Not clicked yet")
				: B_TRANSLATE("Hello, ") + name);
			return;
		}
		if (message->what == kChosen) {
			// the list or the outline
			const BListView* source = fKits;
			if (message->GetPointer("source", nullptr) == static_cast<const BListView*>(fClasses))
				source = fClasses;
			const auto* item = dynamic_cast<const BStringItem*>(
				source->ItemAt(message->GetInt32("index", -1)));
			if (item != nullptr)
				fLabel->SetText(B_TRANSLATE("Chosen: ") + item->Text());
			return;
		}
		if (message->what == kHashMarks) {
			fSlider->SetHashMarks(static_cast<BHashMarkLocation>(message->GetInt32("where", 0)));
			return;
		}
		if (message->what == kReset) {
			// asks first: "Cancel" (Escape) or "Reset" (Enter); the answer
			// comes back as kResetAnswered with "which"
			auto alert = std::make_unique<BAlert>(B_TRANSLATE("Reset"),
				B_TRANSLATE("Reset the click counter?"), B_TRANSLATE("Cancel"),
				B_TRANSLATE("Reset"), "", BButtonWidth::AsUsual, BAlertType::Warning);
			alert->SetShortcut(0, keys::kEscape);
			(void)BAlert::Go(std::move(alert), std::make_unique<BInvoker>(
				std::make_unique<BMessage>(kResetAnswered), BMessenger(this)));
			return;
		}
		if (message->what == kResetAnswered) {
			if (message->GetInt32("which", 0) == 1) {
				fClicks = 0;
				fLabel->SetText(B_TRANSLATE("Not clicked yet"));
			}
			return;
		}
		if (message->what == kStylePlain || message->what == kStyleBold
			|| message->what == kStyleLarge || message->what == kStyleRed) {
			_Style(message->what);
			return;
		}
		if (message->what == kOpen) {
			// the system's file dialog (BFilePanel); the choice comes as
			// kRefsReceived, then kCancel as it closes
			if (fOpenPanel == nullptr) {
				fOpenPanel = std::make_unique<BFilePanel>(BFilePanelMode::Open,
					BMessenger(this));
				fOpenPanel->SetTitle(B_TRANSLATE("Open files"));
				fOpenPanel->AddFilter(B_TRANSLATE("Text"), "txt;md");
				fOpenPanel->AddFilter(B_TRANSLATE("All files"), "*");
			}
			if (auto shown = fOpenPanel->Show(); !shown.has_value()) {
				(void)BAlert::Go(std::make_unique<BAlert>(B_TRANSLATE("Open"),
					shown.error().message, B_TRANSLATE("OK")), [](int32) {});
			}
			return;
		}
		if (message->what == kRefsReceived) {
			std::string text = B_TRANSLATE("Chosen:");
			for (std::int32_t i = 0; auto path = message->FindString("paths", i); i++)
				text += "\n" + std::string(*path);
			(void)BAlert::Go(std::make_unique<BAlert>(B_TRANSLATE("Open"), text,
				B_TRANSLATE("OK")), [](int32) {});
			return;
		}
		if (message->what == kCancel)
			return;
		if (message->WasDropped() && message->what == kSimpleData) {
			// files dragged from the file manager onto a view that does not
			// take them (text goes into the notes)
			std::string text = B_TRANSLATE("Dropped:");
			for (std::int32_t i = 0; auto path = message->FindString("paths", i); i++)
				text += "\n" + std::string(*path);
			(void)BAlert::Go(std::make_unique<BAlert>(B_TRANSLATE("Drop"), text,
				B_TRANSLATE("OK")), [](int32) {});
			return;
		}
		if (message->what == kLook) {
			// the look of every window, at run time (specification 18.5)
			const std::string id(message->GetString("id", ""));
			if (const auto result = LookRegistry::Default().SetCurrent(id); !result)
				std::fprintf(stderr, "%s\n", result.error().message.c_str());
			return;
		}
		if (message->what == kAbout) {
			// an info alert, which only needs to be closed
			fTabs->Select(3);
			// with today's date and a number as the user's locale writes them
			const std::time_t now = std::time(nullptr);
			const std::string text = std::string(
				B_TRANSLATE("NewGUI\n\nHaiku's Interface Kit on SDL3 and Skia.")) + "\n\n"
				+ BDateFormat().Format(now, BDateFormatStyle::Long) + ", "
				+ BTimeFormat().Format(now, BTimeFormatStyle::Short) + "\n"
				+ BNumberFormat().Format(1234567.89, 2);
			(void)BAlert::Go(std::make_unique<BAlert>(B_TRANSLATE("About"), text,
				B_TRANSLATE("OK")), [](int32) {});
			return;
		}
		if (message->what != kClicked) {
			BWindow::MessageReceived(message);
			return;
		}
		fClicks++;
		// "Clicked %d times" in the forms of the language
		std::string text = B_TRANSLATE_PLURAL("Clicked %d time", "Clicked %d times", fClicks);
		if (const std::size_t at = text.find("%d"); at != std::string::npos)
			text.replace(at, 2, std::to_string(fClicks));
		fLabel->SetText(text);
	}

private:
	BStringView* fLabel = nullptr;
	BSlider* fSlider = nullptr;
	BListView* fKits = nullptr;
	BOutlineListView* fClasses = nullptr;
	BStatusBar* fStatus = nullptr;
	BTabView* fTabs = nullptr;
	std::unique_ptr<BFilePanel> fOpenPanel;
	BTextView* fNotes = nullptr;

	// The Text menu: the selected notes (or what is typed next) plain, bold,
	// large or red.
	void _Style(std::uint32_t what)
	{
		if (what == kStylePlain) {
			const BFont plain = BFont::Plain();
			const gfx::Color text = Palette::Default().Get(BColorWhich::DocumentText);
			fNotes->SetFontAndColor(&plain, kFontAll, &text);
		} else if (what == kStyleBold) {
			BFont font = BFont::Bold();
			fNotes->SetFontAndColor(&font, BFontMode::FamilyAndStyle);
		} else if (what == kStyleLarge) {
			BFont font;
			font.SetSize(20);
			fNotes->SetFontAndColor(&font, BFontMode::Size);
		} else {
			const gfx::Color red = gfx::Color::FromRgba8(200, 0, 0);
			fNotes->SetFontAndColor(nullptr, BFontModeFlags{}, &red);
		}
	}
	BTextControl* fName = nullptr;
	int fClicks = 0;
};


class ButtonApp : public BApplication {
public:
	ButtonApp(int argc, char** argv)
		:
		BApplication("application/x-vnd.ngui-example-button", argc, argv)
	{
	}

	void ReadyToRun() override
	{
		auto window = std::make_unique<CounterWindow>();
		window->Show();
		(void)BLooper::Run(std::move(window));
	}
};


} // namespace


int
main(int argc, char** argv)
{
	gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
#ifdef NGUI_DEMO_WITH_BLEND2D
	// the second backend: NGUI_BACKEND=blend2d draws with it
	gfx::Blend2DBackend::Register(gfx::BackendRegistry::Default());
#endif
	// The text system of all windows; it lives until the end of main(). The
	// bundled fonts, with the system's (fontconfig, DirectWrite) for what
	// they lack.
	auto fonts = text::FontTextSystem::Create(text::SystemFontSource());
	if (!fonts.has_value()) {
		std::fprintf(stderr, "%s\n", fonts.error().message.c_str());
		return 1;
	}
	TextSystem::SetDefault(fonts->get());
	// The texts in the user's language: locale/<language>/LC_MESSAGES/
	// newgui-demo.mo beside the executable (German comes with it);
	// NGUI_LANGUAGE=x-pseudo shows them pseudo-localized.
	(void)BLocaleRoster::Default()->LoadCatalog("newgui-demo");
	// Vector icons (the alerts' type icons) drawn by lunasvg.
	const svg::LunaSvgRenderer icons;
	VectorIconRenderer::SetDefault(&icons);
	// SDL3, or the platform NGUI_PLATFORM names (headless for tests)
	(void)platform::SdlPlatform::Register(platform::PlatformRegistry::Default());
	ButtonApp app(argc, argv);
	if (auto used = app.UsePlatform(std::string_view()); !used.has_value()) {
		std::fprintf(stderr, "%s\n", used.error().message.c_str());
		return 1;
	}
	const int result = app.Run();
	VectorIconRenderer::SetDefault(nullptr);
	TextSystem::SetDefault(nullptr);
	return result;
}
