/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/CocoaNotifications.h"

#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <utility>


using ngui::BMessage;
using ngui::BMessenger;
using ngui::platform::NotificationSpec;


namespace {


// What a click on a notification sends where, by its identifier.
std::mutex sLock;
std::map<std::string, std::pair<BMessenger, BMessage>> sClicks;


NSString*
StringOf(const std::string& text)
{
	NSString* string = [[NSString alloc] initWithBytes:text.data() length:text.size()
		encoding:NSUTF8StringEncoding];
	return string != nil ? string : @"";
}


// The icon as a PNG file in the temporary folder, for an attachment (the
// center moves it into its own store); nil without one.
NSURL*
IconFile(const NotificationSpec& spec)
{
	const auto pixels = static_cast<std::size_t>(spec.iconWidth)
		* static_cast<std::size_t>(spec.iconHeight);
	if (spec.iconWidth <= 0 || spec.iconHeight <= 0 || spec.icon.size() != pixels * 4)
		return nil;
	NSBitmapImageRep* image = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
		pixelsWide:spec.iconWidth pixelsHigh:spec.iconHeight bitsPerSample:8
		samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
		bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:spec.iconWidth * 4
		bitsPerPixel:32];
	if (image == nil)
		return nil;
	std::copy(spec.icon.begin(), spec.icon.end(), image.bitmapData);
	NSData* png = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	NSString* name = [NSUUID.UUID.UUIDString stringByAppendingPathExtension:@"png"];
	NSURL* file = [NSURL fileURLWithPath:[NSTemporaryDirectory()
		stringByAppendingPathComponent:name]];
	return [png writeToURL:file atomically:YES] ? file : nil;
}


} // namespace


// The center's delegate: shows notifications while the application is in
// front, and takes their clicks.
@interface NGUINotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end


@implementation NGUINotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
	willPresentNotification:(UNNotification*)notification
	withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
	(void)center;
	(void)notification;
	completionHandler(UNNotificationPresentationOptionBanner
		| UNNotificationPresentationOptionList);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
	didReceiveNotificationResponse:(UNNotificationResponse*)response
	withCompletionHandler:(void (^)(void))completionHandler
{
	(void)center;
	if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier]) {
		if (const char* identifier = response.notification.request.identifier.UTF8String)
			ngui::platform::sdl3::CocoaNotifications::Clicked(identifier);
	}
	completionHandler();
}

@end


namespace {


NGUINotificationDelegate* sDelegate = nil;


} // namespace


namespace ngui::platform::sdl3 {


CocoaNotificationContent
ContentOf(const NotificationSpec& spec)
{
	CocoaNotificationContent content;
	content.identifier = spec.id.empty()
		? std::string(NSUUID.UUID.UUIDString.UTF8String) : spec.id;
	content.title = spec.title;
	content.body = spec.content;
	if (spec.kind == NotificationSpec::Kind::Progress) {
		const long percent = std::lround(std::clamp(spec.progress, 0.0f, 1.0f) * 100);
		content.body += (content.body.empty() ? "" : "\n") + std::to_string(percent) + " %";
	}
	content.thread = spec.group.empty() ? spec.application : spec.group;
	content.timeSensitive = spec.kind == NotificationSpec::Kind::Important
		|| spec.kind == NotificationSpec::Kind::Error;
	return content;
}


std::expected<std::unique_ptr<CocoaNotifications>, Error>
CocoaNotifications::Create()
{
	// The center throws for a program without a bundle.
	if (NSBundle.mainBundle.bundleIdentifier == nil)
		return Failure(ErrorCode::NotSupported, "notifications need an application bundle");
	UNUserNotificationCenter* center = UNUserNotificationCenter.currentNotificationCenter;
	if (sDelegate == nil)
		sDelegate = [[NGUINotificationDelegate alloc] init];
	center.delegate = sDelegate;
	return std::unique_ptr<CocoaNotifications>(new CocoaNotifications());
}


CocoaNotifications::~CocoaNotifications()
{
	std::scoped_lock guard(sLock);
	sClicks.clear();
}


void
CocoaNotifications::Show(const NotificationSpec& spec)
{
	const CocoaNotificationContent content = ContentOf(spec);
	Remember(content.identifier, spec);
	UNMutableNotificationContent* request = [[UNMutableNotificationContent alloc] init];
	request.title = StringOf(content.title);
	request.body = StringOf(content.body);
	request.threadIdentifier = StringOf(content.thread);
	request.interruptionLevel = content.timeSensitive
		? UNNotificationInterruptionLevelTimeSensitive : UNNotificationInterruptionLevelActive;
	if (NSURL* file = IconFile(spec)) {
		UNNotificationAttachment* icon = [UNNotificationAttachment
			attachmentWithIdentifier:@"icon" URL:file options:nil error:nil];
		if (icon != nil)
			request.attachments = @[icon];
	}
	NSString* identifier = StringOf(content.identifier);
	UNUserNotificationCenter* center = UNUserNotificationCenter.currentNotificationCenter;
	// asked once; macOS remembers the answer
	[center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
		| UNAuthorizationOptionSound completionHandler:^(BOOL granted, NSError* error) {
			(void)error;
			if (!granted)
				return;
			[center addNotificationRequest:[UNNotificationRequest requestWithIdentifier:identifier
				content:request trigger:nil] withCompletionHandler:nil];
		}];
}


void
CocoaNotifications::Clicked(const std::string& identifier)
{
	std::pair<BMessenger, BMessage> click;
	{
		std::scoped_lock guard(sLock);
		const auto found = sClicks.find(identifier);
		if (found == sClicks.end())
			return;
		click = found->second;
	}
	(void)click.first.SendMessage(click.second);
}


void
CocoaNotifications::Remember(const std::string& identifier, const NotificationSpec& spec)
{
	std::scoped_lock guard(sLock);
	if (spec.onClick.has_value())
		sClicks.insert_or_assign(identifier, std::make_pair(spec.target, *spec.onClick));
	else
		sClicks.erase(identifier);
}


} // namespace ngui::platform::sdl3
