/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/CocoaDragDrop.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <map>
#include <string>
#include <string_view>

#include <objc/runtime.h>

#include <ngui/AppDefs.h>
#include <ngui/Message.h>

#include "DragData.h"
#include "PlatformEvent.h"
#include "platform/sdl3/DropPayload.h"


using ngui::BMessage;
using ngui::BMessenger;
using ngui::platform::DragItem;


namespace {


// The NSWindows that take drops, with their SDL ids; main thread.
std::map<void*, SDL_WindowID> sDropWindows;
std::uint32_t sDropEvent = 0;
// SDL's performDragOperation: of its window class, for windows not ours.
IMP sSdlPerform = nullptr;


std::vector<std::byte>
Bytes(NSData* data)
{
	const auto* bytes = static_cast<const std::byte*>(data.bytes);
	return std::vector<std::byte>(bytes, bytes + data.length);
}


std::vector<std::byte>
Bytes(std::string_view text)
{
	const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
	return std::vector<std::byte>(bytes, bytes + text.size());
}


NSData*
Data(const std::vector<std::byte>& bytes)
{
	return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}


NSString*
String(const std::vector<std::byte>& bytes)
{
	NSString* string = [[NSString alloc] initWithBytes:bytes.data() length:bytes.size()
		encoding:NSUTF8StringEncoding];
	return string != nil ? string : @"";
}


// The pasteboard type of a MIME type other than text and files.
NSPasteboardType
TypeOf(const std::string& mime)
{
	if (mime == "image/png")
		return NSPasteboardTypePNG;
	if (mime == "image/tiff")
		return NSPasteboardTypeTIFF;
	if (mime == "text/html")
		return NSPasteboardTypeHTML;
	if (mime == "text/rtf")
		return NSPasteboardTypeRTF;
	if (mime == "application/pdf")
		return NSPasteboardTypePDF;
	UTType* type = [UTType typeWithMIMEType:[NSString stringWithUTF8String:mime.c_str()]];
	return type != nil ? type.identifier : nil;
}


// What the items are for NSPasteboard: each file of a text/uri-list as its
// NSURL, everything else in one NSPasteboardItem.
NSArray<id<NSPasteboardWriting>>*
WritersOf(const std::vector<DragItem>& items)
{
	NSMutableArray<id<NSPasteboardWriting>>* writers = [NSMutableArray array];
	NSPasteboardItem* rest = [[NSPasteboardItem alloc] init];
	bool restUsed = false;
	for (const DragItem& item : items) {
		if (item.type == "text/uri-list") {
			const std::string_view list(reinterpret_cast<const char*>(item.data.data()),
				item.data.size());
			for (const std::string& path : ngui::detail::PathsOf(list)) {
				[writers addObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:
					path.c_str()]]];
			}
			continue;
		}
		if (item.type == "text/plain") {
			restUsed = [rest setString:String(item.data) forType:NSPasteboardTypeString]
				|| restUsed;
			continue;
		}
		if (NSPasteboardType type = TypeOf(item.type))
			restUsed = [rest setData:Data(item.data) forType:type] || restUsed;
	}
	if (restUsed)
		[writers addObject:rest];
	return writers;
}


// The window's content, top left origin, from a point of its base
// coordinates (bottom left origin).
ngui::gfx::Point
ContentPoint(NSWindow* window, NSPoint point)
{
	const CGFloat height = window.contentView.frame.size.height;
	return {static_cast<float>(point.x), static_cast<float>(height - point.y)};
}


// In place of SDL's performDragOperation: on its window class: for the
// windows that take drops, all the data in a DropPayload in SDL's queue.
BOOL
PerformDragOperation(id self, SEL selector, id<NSDraggingInfo> sender)
{
	const auto found = sDropWindows.find((__bridge void*)self);
	if (found == sDropWindows.end()) {
		using Perform = BOOL (*)(id, SEL, id<NSDraggingInfo>);
		return sSdlPerform != nullptr
			? reinterpret_cast<Perform>(sSdlPerform)(self, selector, sender) : NO;
	}
	std::vector<DragItem> items = ngui::platform::sdl3::ReadDragItems(
		(__bridge void*)[sender draggingPasteboard]);
	const bool any = !items.empty();
	SDL_Event event{};
	event.type = sDropEvent;
	event.user.windowID = found->second;
	event.user.data1 = new ngui::platform::sdl3::DropPayload{std::move(items),
		ContentPoint((NSWindow*)self, [sender draggingLocation])};
	if (!SDL_PushEvent(&event))
		delete static_cast<ngui::platform::sdl3::DropPayload*>(event.user.data1);
	return any ? YES : NO;
}


} // namespace


// The source of a drag that left the application: copies, and gives the
// window the release AppKit kept.
@interface NGUIDragSource : NSObject <NSDraggingSource> {
	BMessenger _target;
}
- (instancetype)initWithTarget:(BMessenger)target;
@end


namespace {


// The drag running, kept while AppKit runs it.
NGUIDragSource* sSource = nil;


} // namespace


@implementation NGUIDragSource

- (instancetype)initWithTarget:(BMessenger)target
{
	self = [super init];
	if (self != nil)
		_target = target;
	return self;
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
	sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
	(void)session;
	(void)context;
	return NSDragOperationCopy | NSDragOperationGeneric;
}

- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)point
	operation:(NSDragOperation)operation
{
	(void)session;
	(void)point;
	(void)operation;
	BMessage up(ngui::kMouseUp);
	(void)up.AddInt32("buttons", 0);
	ngui::PlatformEventMark::Set(up);
	(void)_target.SendMessage(up);
	if (sSource == self)
		sSource = nil;
}

@end


namespace ngui::platform::sdl3 {


void
AcceptCocoaDrops(SDL_Window* window, std::uint32_t eventType)
{
	auto* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	if (nsWindow == nil)
		return;
	sDropEvent = eventType;
	sDropWindows[(__bridge void*)nsWindow] = SDL_GetWindowID(window);
	if (sSdlPerform == nullptr) {
		sSdlPerform = class_replaceMethod(object_getClass(nsWindow),
			@selector(performDragOperation:), reinterpret_cast<IMP>(&PerformDragOperation),
			"c@:@");
	}
	[nsWindow registerForDraggedTypes:@[NSPasteboardTypeFileURL, NSPasteboardTypeString,
		NSPasteboardTypePNG, NSPasteboardTypeTIFF, NSPasteboardTypeHTML, NSPasteboardTypeRTF,
		NSPasteboardTypePDF, NSPasteboardTypeURL]];
}


void
ForgetCocoaDrops(SDL_Window* window)
{
	void* nsWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
		SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	sDropWindows.erase(nsWindow);
}


bool
StartCocoaDrag(SDL_Window* window, const std::vector<DragItem>& items, BMessenger target)
{
	auto* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	if (nsWindow == nil)
		return false;
	NSArray<id<NSPasteboardWriting>>* writers = WritersOf(items);
	if (writers.count == 0)
		return false;
	NSView* view = nsWindow.contentView;
	const NSPoint mouse = [nsWindow mouseLocationOutsideOfEventStream];
	const NSPoint inView = [view convertPoint:mouse fromView:nil];
	NSImage* image = [NSImage imageNamed:NSImageNameMultipleDocuments];
	NSMutableArray<NSDraggingItem*>* dragged = [NSMutableArray array];
	for (id<NSPasteboardWriting> writer in writers) {
		NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:writer];
		[item setDraggingFrame:NSMakeRect(inView.x - 16, inView.y - 16, 32, 32) contents:image];
		[dragged addObject:item];
	}
	// AppKit wants the mouse event the drag starts with
	NSEvent* event = NSApp.currentEvent;
	if (event == nil || (event.type != NSEventTypeLeftMouseDragged
			&& event.type != NSEventTypeLeftMouseDown)) {
		event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged location:mouse
			modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime
			windowNumber:nsWindow.windowNumber context:nil eventNumber:0 clickCount:1
			pressure:1];
	}
	NGUIDragSource* source = [[NGUIDragSource alloc] initWithTarget:target];
	NSDraggingSession* session = [view beginDraggingSessionWithItems:dragged event:event
		source:source];
	if (session == nil)
		return false;
	sSource = source;
	return true;
}


void
WriteDragItems(const std::vector<DragItem>& items, void* pasteboard)
{
	auto* board = (__bridge NSPasteboard*)pasteboard;
	[board clearContents];
	[board writeObjects:WritersOf(items)];
}


std::vector<DragItem>
ReadDragItems(void* pasteboard)
{
	auto* board = (__bridge NSPasteboard*)pasteboard;
	std::vector<DragItem> items;
	const auto add = [&items](std::string type, std::vector<std::byte> bytes) {
		for (const DragItem& item : items) {
			if (item.type == type)
				return;
		}
		items.push_back({std::move(type), std::move(bytes)});
	};
	// files as a text/uri-list
	NSArray<NSURL*>* files = [board readObjectsForClasses:@[NSURL.class]
		options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
	std::string uris;
	for (NSURL* file in files) {
		if (const char* path = file.path.UTF8String)
			uris += ngui::detail::FileUri(path) + "\r\n";
	}
	if (!uris.empty())
		add("text/uri-list", Bytes(uris));
	if (NSString* text = [board stringForType:NSPasteboardTypeString]) {
		if (const char* utf8 = text.UTF8String)
			add("text/plain", Bytes(std::string_view(utf8)));
	}
	// the other types by their MIME types
	for (NSPasteboardType type in board.types) {
		if ([type isEqualToString:NSPasteboardTypeString]
			|| [type isEqualToString:NSPasteboardTypeFileURL]) {
			continue;
		}
		std::string mime;
		if ([type isEqualToString:NSPasteboardTypePNG])
			mime = "image/png";
		else if ([type isEqualToString:NSPasteboardTypeTIFF])
			mime = "image/tiff";
		else if ([type isEqualToString:NSPasteboardTypeHTML])
			mime = "text/html";
		else if ([type isEqualToString:NSPasteboardTypeRTF])
			mime = "text/rtf";
		else if ([type isEqualToString:NSPasteboardTypePDF])
			mime = "application/pdf";
		else if (UTType* uti = [UTType typeWithIdentifier:type];
				uti != nil && uti.preferredMIMEType != nil) {
			mime = uti.preferredMIMEType.UTF8String;
		}
		if (mime.empty())
			continue;
		if (NSData* data = [board dataForType:type])
			add(std::move(mime), Bytes(data));
	}
	return items;
}


} // namespace ngui::platform::sdl3
