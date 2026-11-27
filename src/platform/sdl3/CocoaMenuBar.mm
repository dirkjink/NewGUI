/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/CocoaMenuBar.h"

#import <AppKit/AppKit.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>


using ngui::BMessage;
using ngui::BMessenger;
using ngui::BModifier;
using ngui::platform::SystemMenuItem;


// What the items and menus report to: the window whose menus they are,
// and the application for "About".
@interface NGUIMenuTarget : NSObject <NSMenuDelegate> {
	BMessenger _window;
}
- (instancetype)initWithWindow:(BMessenger)window;
- (void)choose:(NSMenuItem*)item;
- (void)about:(id)sender;
@end


@implementation NGUIMenuTarget

- (instancetype)initWithWindow:(BMessenger)window
{
	self = [super init];
	if (self != nil)
		_window = window;
	return self;
}

- (void)choose:(NSMenuItem*)item
{
	BMessage message(ngui::platform::kSystemMenuInvoked);
	(void)message.AddInt64("id",
		static_cast<std::int64_t>([static_cast<NSNumber*>(item.representedObject)
			unsignedLongLongValue]));
	(void)_window.SendMessage(message);
}

- (void)about:(id)sender
{
	(void)sender;
	(void)ngui::AppMessenger().SendMessage(ngui::kAboutRequested);
}

- (void)menuWillOpen:(NSMenu*)menu
{
	(void)menu;
	(void)_window.SendMessage(ngui::platform::kSystemMenusBeginning);
}

- (void)menuDidClose:(NSMenu*)menu
{
	(void)menu;
	(void)_window.SendMessage(ngui::platform::kSystemMenusEnded);
}

@end


namespace {


// NSMenu keeps neither its items' targets nor its delegate: the target of
// the menus shown stays here, with the window they are of.
NGUIMenuTarget* sTarget = nil;
BMessenger sWindow;


NSString*
String(const std::string& text)
{
	NSString* string = [NSString stringWithUTF8String:text.c_str()];
	return string != nil ? string : @"";
}


// The key equivalent of a shortcut: Cocoa reads an upper-case letter as one
// with Shift, Haiku's shortcuts are upper-case without it.
void
SetShortcut(NSMenuItem* item, const SystemMenuItem& entry)
{
	if (entry.shortcut < 0x21 || entry.shortcut > 0x7e)
		return;
	const unichar character = static_cast<unichar>(
		std::tolower(static_cast<int>(entry.shortcut)));
	item.keyEquivalent = [NSString stringWithCharacters:&character length:1];
	NSEventModifierFlags mask = 0;
	if (!entry.modifiers.Has(BModifier::NoCommand))
		mask |= NSEventModifierFlagCommand;
	if (entry.modifiers.Has(BModifier::Shift))
		mask |= NSEventModifierFlagShift;
	if (entry.modifiers.Has(BModifier::Control))
		mask |= NSEventModifierFlagControl;
	if (entry.modifiers.Has(BModifier::Option))
		mask |= NSEventModifierFlagOption;
	item.keyEquivalentModifierMask = mask;
}


// Title, state, enabling and key of \a item as \a entry has them.
void
SetAttributes(NSMenuItem* item, const SystemMenuItem& entry)
{
	item.title = String(entry.label);
	if (item.submenu != nil)
		item.submenu.title = item.title;
	else {
		item.representedObject = @(entry.id);
		item.keyEquivalent = @"";
		item.keyEquivalentModifierMask = 0;
		SetShortcut(item, entry);
	}
	item.enabled = entry.enabled;
	item.state = entry.marked ? NSControlStateValueOn : NSControlStateValueOff;
}


// Whether \a menu's items from \a first on are \a entries in shape: as many,
// separators and submenus at the same places.
bool
SameShape(NSMenu* menu, NSInteger first, const std::vector<SystemMenuItem>& entries)
{
	if (menu.numberOfItems - first != static_cast<NSInteger>(entries.size()))
		return false;
	for (std::size_t index = 0; index < entries.size(); index++) {
		NSMenuItem* item = [menu itemAtIndex:first + static_cast<NSInteger>(index)];
		const SystemMenuItem& entry = entries[index];
		if (item.isSeparatorItem != entry.separator
			|| (item.submenu != nil) != !entry.items.empty()) {
			return false;
		}
		if (item.submenu != nil && !SameShape(item.submenu, 0, entry.items))
			return false;
	}
	return true;
}


// \a menu's items from \a first on as \a entries say, in place: a menu that
// is open stays open (the window passes its menus on again when one opens,
// after MenusBeginning()).
void
Update(NSMenu* menu, NSInteger first, const std::vector<SystemMenuItem>& entries)
{
	for (std::size_t index = 0; index < entries.size(); index++) {
		NSMenuItem* item = [menu itemAtIndex:first + static_cast<NSInteger>(index)];
		const SystemMenuItem& entry = entries[index];
		if (entry.separator)
			continue;
		SetAttributes(item, entry);
		if (item.submenu != nil)
			Update(item.submenu, 0, entry.items);
	}
}


NSMenuItem*
MakeItem(const SystemMenuItem& entry)
{
	if (entry.separator)
		return [NSMenuItem separatorItem];
	NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:String(entry.label) action:nil
		keyEquivalent:@""];
	if (!entry.items.empty()) {
		NSMenu* submenu = [[NSMenu alloc] initWithTitle:String(entry.label)];
		submenu.autoenablesItems = NO;
		for (const SystemMenuItem& child : entry.items)
			[submenu addItem:MakeItem(child)];
		item.submenu = submenu;
	} else {
		item.action = @selector(choose:);
		item.target = sTarget;
		item.representedObject = @(entry.id);
		SetShortcut(item, entry);
	}
	item.enabled = entry.enabled;
	item.state = entry.marked ? NSControlStateValueOn : NSControlStateValueOff;
	return item;
}


// The application menu: About, Services, Hide, Hide Others, Show All, Quit.
NSMenuItem*
ApplicationMenu()
{
	NSString* name = [[NSProcessInfo processInfo] processName];
	NSMenu* menu = [[NSMenu alloc] initWithTitle:name];
	NSMenuItem* about = [menu addItemWithTitle:[@"About " stringByAppendingString:name]
		action:@selector(about:) keyEquivalent:@""];
	about.target = sTarget;
	[menu addItem:[NSMenuItem separatorItem]];
	NSMenuItem* services = [menu addItemWithTitle:@"Services" action:nil keyEquivalent:@""];
	NSMenu* servicesMenu = [[NSMenu alloc] initWithTitle:@"Services"];
	services.submenu = servicesMenu;
	NSApp.servicesMenu = servicesMenu;
	[menu addItem:[NSMenuItem separatorItem]];
	[menu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:)
		keyEquivalent:@"h"];
	NSMenuItem* others = [menu addItemWithTitle:@"Hide Others"
		action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
	others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
	[menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:)
		keyEquivalent:@""];
	[menu addItem:[NSMenuItem separatorItem]];
	// terminate: asks SDL, which asks the application (kQuitRequested)
	[menu addItemWithTitle:[@"Quit " stringByAppendingString:name]
		action:@selector(terminate:) keyEquivalent:@"q"];
	NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
	item.submenu = menu;
	return item;
}


} // namespace


void
ngui::platform::sdl3::InstallMenuBar(const std::vector<SystemMenuItem>& menus,
	BMessenger window)
{
	// the same window's menus in the same shape: in place (index 0 is the
	// application menu)
	NSMenu* shown = NSApp.mainMenu;
	if (sTarget != nil && window == sWindow && shown != nil && SameShape(shown, 1, menus)) {
		Update(shown, 1, menus);
		return;
	}
	sWindow = window;
	sTarget = [[NGUIMenuTarget alloc] initWithWindow:window];
	NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
	[bar addItem:ApplicationMenu()];
	for (const SystemMenuItem& entry : menus) {
		NSMenuItem* item = MakeItem(entry);
		if (item.submenu != nil)
			item.submenu.delegate = sTarget;
		[bar addItem:item];
	}
	NSApp.mainMenu = bar;
}
