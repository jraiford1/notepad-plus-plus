// This file is part of Notepad++ project (macOS build)
// Copyright (C)2026 Notepad++ macOS contributors

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// The AppKit side of windows (Backend.h): NSWindows and flipped NSViews with display-scaled
// surfaces, native input turned into window-manager calls, the event loop, timers and cursors.
// No <windows.h> here.

#import <Cocoa/Cocoa.h>

#include "Backend.h"

#include <cstdio>
#include <string>

using namespace nppmac::ui;

// MacTypes (Carbon) also defines Point and Rect.
using UiRect = nppmac::ui::Rect;
using UiPoint = nppmac::ui::Point;

namespace nppmac::ui
{
	class NativeView
	{
	public:
		ViewOwner* _owner = nullptr;
		ViewKind _kind = ViewKind::child;
		NSView* _view = nil;           // the client area
		NSWindow* _window = nil;       // top levels
		bool _adopted = false;         // a native control's view
		CGContextRef _surface = nullptr;
		int _surfaceWidth = 0;         // in points
		int _surfaceHeight = 0;
		CGFloat _surfaceScale = 1;
		bool _acceptsFiles = false;
		bool _seeThrough = false;
	};

	double primaryScreenHeight()
	{
		NSArray<NSScreen*>* screens = NSScreen.screens;
		return screens.count > 0 ? screens[0].frame.size.height : 1080;
	}

	// Windows screen rectangles have a top-left origin on the primary screen; AppKit's a bottom-left one.
	NSRect toCocoaScreenRect(const UiRect& rect)
	{
		return NSMakeRect(rect._left, primaryScreenHeight() - rect._bottom, rect.width(), rect.height());
	}

	UiRect fromCocoaScreenRect(NSRect rect)
	{
		const double height = primaryScreenHeight();
		UiRect result;
		result._left = static_cast<int>(std::lround(rect.origin.x));
		result._top = static_cast<int>(std::lround(height - rect.origin.y - rect.size.height));
		result._right = result._left + static_cast<int>(std::lround(rect.size.width));
		result._bottom = result._top + static_cast<int>(std::lround(rect.size.height));
		return result;
	}

	unsigned modifiersFrom(NSEventModifierFlags flags)
	{
		unsigned modifiers = 0;
		if (flags & NSEventModifierFlagShift)
			modifiers |= modifierShift;
		if (flags & NSEventModifierFlagControl)
			modifiers |= modifierControl;
		if (flags & NSEventModifierFlagCommand)
			modifiers |= modifierControl | modifierCommand;
		if (flags & NSEventModifierFlagOption)
			modifiers |= modifierAlt;
		return modifiers;
	}

	std::u16string toU16(NSString* string)
	{
		std::u16string result(string.length, u'\0');
		[string getCharacters:reinterpret_cast<unichar*>(result.data()) range:NSMakeRange(0, string.length)];
		return result;
	}

	NativeView* g_captureView = nullptr;
	bool g_cursorHidden = false;
}

// --- Views ------------------------------------------------------------------------------------------

@interface NppMacView : NSView
{
@public
	NativeView* _native;
	NSTrackingArea* _trackingArea;
}
@end

@implementation NppMacView

- (BOOL)isFlipped
{
	return YES;
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
	return YES;
}

- (BOOL)isOpaque
{
	return !(_native && _native->_seeThrough);
}

- (NSView*)hitTest:(NSPoint)point
{
	NSView* hit = [super hitTest:point];
	return hit == self && _native && _native->_seeThrough ? nil : hit;
}

- (void)updateTrackingAreas
{
	if (_trackingArea)
		[self removeTrackingArea:_trackingArea];
	_trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
		options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
		owner:self userInfo:nil];
	[self addTrackingArea:_trackingArea];
	[super updateTrackingAreas];
}

- (void)drawRect:(NSRect)dirtyRect
{
	if (!_native)
		return;
	const UiRect dirty{static_cast<int>(std::floor(NSMinX(dirtyRect))), static_cast<int>(std::floor(NSMinY(dirtyRect))),
		static_cast<int>(std::ceil(NSMaxX(dirtyRect))), static_cast<int>(std::ceil(NSMaxY(dirtyRect)))};
	_native->_owner->nativePaint(dirty);
	if (!_native)
		return;

	CGContextRef context = NSGraphicsContext.currentContext.CGContext;
	if (!_native->_surface)
	{
		CGContextSetRGBFillColor(context, 0.94, 0.94, 0.94, 1);
		CGContextFillRect(context, NSRectToCGRect(dirtyRect));
		return;
	}
	CGImageRef image = CGBitmapContextCreateImage(_native->_surface);
	if (!image)
		return;
	// The view is flipped: draw the image upright.
	CGContextSaveGState(context);
	CGContextClipToRect(context, NSRectToCGRect(dirtyRect));
	CGContextTranslateCTM(context, 0, _native->_surfaceHeight);
	CGContextScaleCTM(context, 1, -1);
	CGContextSetInterpolationQuality(context, kCGInterpolationNone);
	CGContextDrawImage(context, CGRectMake(0, 0, _native->_surfaceWidth, _native->_surfaceHeight), image);
	CGContextRestoreGState(context);
	CGImageRelease(image);
}

- (UiPoint)clientPoint:(NSEvent*)event
{
	const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
	return UiPoint{static_cast<int>(std::floor(point.x)), static_cast<int>(std::floor(point.y))};
}

- (void)sendMouse:(MouseAction)action button:(MouseButton)button event:(NSEvent*)event delta:(int)delta
{
	if (_native)
		_native->_owner->nativeMouse(action, button, [self clientPoint:event], modifiersFrom(event.modifierFlags), delta);
}

- (void)mouseDown:(NSEvent*)event
{
	// macOS Control-click is a right click.
	const bool right = (event.modifierFlags & NSEventModifierFlagControl) != 0;
	const MouseAction action = event.clickCount == 2 ? MouseAction::doubleClick : MouseAction::down;
	[self sendMouse:action button:right ? MouseButton::right : MouseButton::left event:event delta:0];
}

- (void)mouseUp:(NSEvent*)event
{
	const bool right = (event.modifierFlags & NSEventModifierFlagControl) != 0;
	[self sendMouse:MouseAction::up button:right ? MouseButton::right : MouseButton::left event:event delta:0];
}

- (void)rightMouseDown:(NSEvent*)event
{
	[self sendMouse:event.clickCount == 2 ? MouseAction::doubleClick : MouseAction::down button:MouseButton::right event:event delta:0];
}

- (void)rightMouseUp:(NSEvent*)event
{
	[self sendMouse:MouseAction::up button:MouseButton::right event:event delta:0];
}

- (void)otherMouseDown:(NSEvent*)event
{
	[self sendMouse:event.clickCount == 2 ? MouseAction::doubleClick : MouseAction::down button:MouseButton::middle event:event delta:0];
}

- (void)otherMouseUp:(NSEvent*)event
{
	[self sendMouse:MouseAction::up button:MouseButton::middle event:event delta:0];
}

- (void)mouseMoved:(NSEvent*)event
{
	[self sendMouse:MouseAction::move button:MouseButton::none event:event delta:0];
}

- (void)mouseDragged:(NSEvent*)event
{
	[self sendMouse:MouseAction::move button:MouseButton::none event:event delta:0];
}

- (void)rightMouseDragged:(NSEvent*)event
{
	[self sendMouse:MouseAction::move button:MouseButton::none event:event delta:0];
}

- (void)otherMouseDragged:(NSEvent*)event
{
	[self sendMouse:MouseAction::move button:MouseButton::none event:event delta:0];
}

- (void)mouseExited:(NSEvent*)event
{
	[self sendMouse:MouseAction::leave button:MouseButton::none event:event delta:0];
}

- (void)scrollWheel:(NSEvent*)event
{
	// Windows' notch is 120; a trackpad's precise deltas are scaled to the same feel.
	const CGFloat scale = event.hasPreciseScrollingDeltas ? 4 : 40;
	const int vertical = static_cast<int>(std::lround(event.scrollingDeltaY * scale));
	const int horizontal = static_cast<int>(std::lround(-event.scrollingDeltaX * scale));
	if (vertical != 0)
		[self sendMouse:MouseAction::wheel button:MouseButton::none event:event delta:vertical];
	if (horizontal != 0)
		[self sendMouse:MouseAction::horizontalWheel button:MouseButton::none event:event delta:horizontal];
}

- (void)keyDown:(NSEvent*)event
{
	// Keys are handled at the application level (NppMacApplication), never here.
}

- (void)keyUp:(NSEvent*)event
{
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
	return _native && _native->_owner->nativeAcceptsFiles() ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
	if (!_native)
		return NO;
	NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
	std::vector<std::string> paths;
	for (NSURL* url in urls)
		paths.push_back(url.path.UTF8String);
	const NSPoint point = [self convertPoint:sender.draggingLocation fromView:nil];
	_native->_owner->nativeFilesDropped(paths, UiPoint{static_cast<int>(point.x), static_cast<int>(point.y)});
	return YES;
}

@end

// --- Windows ----------------------------------------------------------------------------------------

@interface NppMacWindow : NSWindow
{
@public
	NativeView* _native;
	BOOL _borderless;
}
@end

@implementation NppMacWindow

- (BOOL)canBecomeKeyWindow
{
	return YES;
}

- (BOOL)canBecomeMainWindow
{
	return !_borderless;
}

@end

@interface NppMacWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation NppMacWindowDelegate

- (NativeView*)nativeFor:(NSNotification*)notification
{
	NSWindow* window = notification.object;
	return [window isKindOfClass:NppMacWindow.class] ? static_cast<NppMacWindow*>(window)->_native : nullptr;
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	if ([sender isKindOfClass:NppMacWindow.class])
	{
		NativeView* native = static_cast<NppMacWindow*>(sender)->_native;
		if (native)
			native->_owner->nativeCloseRequested();
	}
	// The window manager destroys the window itself if the program agrees.
	return NO;
}

- (void)reportFrame:(NSNotification*)notification
{
	NativeView* native = [self nativeFor:notification];
	if (native)
		native->_owner->nativeFrameChanged(windowRectOnScreen(native), clientRectOnScreen(native));
}

- (void)windowDidResize:(NSNotification*)notification
{
	[self reportFrame:notification];
}

- (void)windowDidMove:(NSNotification*)notification
{
	[self reportFrame:notification];
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
	NativeView* native = [self nativeFor:notification];
	if (native)
		native->_owner->nativeActivated(true);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
	NativeView* native = [self nativeFor:notification];
	if (native)
		native->_owner->nativeActivated(false);
}

- (void)windowDidMiniaturize:(NSNotification*)notification
{
	NativeView* native = [self nativeFor:notification];
	if (native)
		native->_owner->nativeMinimized(true);
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
	NativeView* native = [self nativeFor:notification];
	if (native)
		native->_owner->nativeMinimized(false);
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
	NativeView* native = [self nativeFor:notification];
	if (native)
		invalidate(native, nullptr);
}

@end

// --- Application ------------------------------------------------------------------------------------

@interface NppMacApplication : NSApplication
@end

@implementation NppMacApplication

- (void)sendEvent:(NSEvent*)event
{
	switch (event.type)
	{
		case NSEventTypeKeyDown:
		case NSEventTypeKeyUp:
		{
			// The application menu's own keys (Command-Q, Command-H, Command-, for Settings) stay the
			// menu's; every other key goes to the window manager, so that TranslateAccelerator sees it
			// first, as on Windows.
			const NSEventModifierFlags flags = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
			NSString* plain = event.charactersIgnoringModifiers.lowercaseString;
			if (event.type == NSEventTypeKeyDown && flags == NSEventModifierFlagCommand
				&& ([plain isEqualToString:@"q"] || [plain isEqualToString:@"h"] || [plain isEqualToString:@","]))
			{
				[super sendEvent:event];
				return;
			}
			// Panels (open/save, alerts) and other non-runtime windows handle their own keys.
			NSWindow* keyWindow = self.keyWindow;
			if (keyWindow && ![keyWindow isKindOfClass:NppMacWindow.class])
			{
				[super sendEvent:event];
				return;
			}
			KeyEvent key;
			key._down = event.type == NSEventTypeKeyDown;
			key._repeat = event.isARepeat;
			key._keyCode = event.keyCode;
			key._modifiers = modifiersFrom(event.modifierFlags);
			key._characters = toU16(event.characters ?: @"");
			key._charactersIgnoringModifiers = toU16(event.charactersIgnoringModifiers ?: @"");
			key._nsEvent = (__bridge_retained void*)event;
			windowManagerKeyEvent(key);
			return;
		}
		case NSEventTypeFlagsChanged:
		{
			const unsigned short code = event.keyCode;
			const NSEventModifierFlags flags = event.modifierFlags;
			bool down = false;
			switch (code)
			{
				case 0x38:
				case 0x3C:
					down = (flags & NSEventModifierFlagShift) != 0;
					break;
				case 0x3B:
				case 0x3E:
					down = (flags & NSEventModifierFlagControl) != 0;
					break;
				case 0x37:
				case 0x36:
					down = (flags & NSEventModifierFlagCommand) != 0;
					break;
				case 0x3A:
				case 0x3D:
					down = (flags & NSEventModifierFlagOption) != 0;
					break;
				default:
					break;
			}
			windowManagerModifiersChanged(modifiersFrom(flags), code, down);
			[super sendEvent:event];
			return;
		}
		case NSEventTypeLeftMouseDragged:
		case NSEventTypeLeftMouseUp:
		case NSEventTypeMouseMoved:
		{
			// While a window has the capture, mouse input goes to its view wherever the mouse is.
			if (g_captureView && g_captureView->_view && event.window && event.window != g_captureView->_view.window)
			{
				NppMacView* view = static_cast<NppMacView*>(g_captureView->_view);
				if ([view isKindOfClass:NppMacView.class])
				{
					if (event.type == NSEventTypeLeftMouseUp)
						[view mouseUp:event];
					else
						[view mouseDragged:event];
					return;
				}
			}
			[super sendEvent:event];
			return;
		}
		default:
			[super sendEvent:event];
	}
}

@end

@interface NppMacApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation NppMacApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	// Quitting closes the program's main window the Windows way; the program exits when it is done.
	for (NSWindow* window in NSApp.orderedWindows)
	{
		if ([window isKindOfClass:NppMacWindow.class] && !static_cast<NppMacWindow*>(window)->_borderless && window.isVisible)
		{
			NativeView* native = static_cast<NppMacWindow*>(window)->_native;
			if (native)
			{
				native->_owner->nativeCloseRequested();
				return NSTerminateCancel;
			}
		}
	}
	return NSTerminateNow;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag
{
	return YES;
}

@end

namespace
{
	NppMacWindowDelegate* g_windowDelegate = nil;
	NppMacApplicationDelegate* g_applicationDelegate = nil;

	NppMacView* createSurfaceView(NativeView* native, NSRect frame)
	{
		NppMacView* view = [[NppMacView alloc] initWithFrame:frame];
		view->_native = native;
		view.wantsLayer = NO;
		view.autoresizingMask = NSViewNotSizable;
		return view;
	}

	void ensureSurface(NativeView* native)
	{
		if (native->_adopted || !native->_view)
			return;
		const NSRect bounds = native->_view.bounds;
		const int width = std::max(1, static_cast<int>(std::ceil(bounds.size.width)));
		const int height = std::max(1, static_cast<int>(std::ceil(bounds.size.height)));
		const CGFloat scale = native->_view.window ? native->_view.window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
		if (native->_surface && native->_surfaceWidth == width && native->_surfaceHeight == height && native->_surfaceScale == scale)
			return;

		CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGContextRef surface = CGBitmapContextCreate(nullptr, static_cast<size_t>(width * scale), static_cast<size_t>(height * scale), 8, 0,
			colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
		CGColorSpaceRelease(colorSpace);
		// Keep what the old surface showed, so that a resize does not flash.
		if (native->_surface)
		{
			CGImageRef old = CGBitmapContextCreateImage(native->_surface);
			if (old)
			{
				CGContextDrawImage(surface, CGRectMake(0, (height - native->_surfaceHeight) * scale, native->_surfaceWidth * scale, native->_surfaceHeight * scale), old);
				CGImageRelease(old);
			}
			CGContextRelease(native->_surface);
		}
		else if (!native->_seeThrough)
		{
			CGContextSetRGBFillColor(surface, 0.94, 0.94, 0.94, 1);
			CGContextFillRect(surface, CGRectMake(0, 0, width * scale, height * scale));
		}
		// Top-left origin in points.
		CGContextTranslateCTM(surface, 0, height * scale);
		CGContextScaleCTM(surface, scale, -scale);
		native->_surface = surface;
		native->_surfaceWidth = width;
		native->_surfaceHeight = height;
		native->_surfaceScale = scale;
	}

	NSUInteger styleMask(ViewKind kind, const TopLevelStyle& style)
	{
		if (kind == ViewKind::popup)
			return NSWindowStyleMaskBorderless;
		NSUInteger mask = 0;
		if (style._titled)
			mask |= NSWindowStyleMaskTitled;
		if (style._closable)
			mask |= NSWindowStyleMaskClosable;
		if (style._resizable)
			mask |= NSWindowStyleMaskResizable;
		if (style._minimizable)
			mask |= NSWindowStyleMaskMiniaturizable;
		if (mask == 0)
			mask = NSWindowStyleMaskBorderless;
		return mask;
	}
}

namespace nppmac::ui
{
	void dumpMenuBar(NSMenu* menu, int depth);

	// Debugging aid: NPPMAC_SNAPSHOT=<file.png> renders the frontmost program window into a PNG after
	// NPPMAC_SNAPSHOT_DELAY seconds (default 5), without needing screen-recording permission;
	// NPPMAC_SNAPSHOT_EXIT=1 then quits.
	void scheduleSnapshot()
	{
		const char* path = std::getenv("NPPMAC_SNAPSHOT");
		if (!path || !*path)
			return;
		const char* delayText = std::getenv("NPPMAC_SNAPSHOT_DELAY");
		const double delay = delayText ? std::atof(delayText) : 5.0;
		NSString* file = [NSString stringWithUTF8String:path];
		NSTimer* timer = [NSTimer timerWithTimeInterval:delay repeats:NO block:^(NSTimer*) {
			if (std::getenv("NPPMAC_DUMP_TREE"))
			{
				windowManagerDumpTree();
				dumpMenuBar(NSApp.mainMenu, 0);
			}
			for (NSWindow* window in NSApp.orderedWindows)
			{
				if (![window isKindOfClass:NppMacWindow.class] || !window.isVisible)
					continue;
				NSView* content = window.contentView;
				NSBitmapImageRep* bitmap = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
				[content cacheDisplayInRect:content.bounds toBitmapImageRep:bitmap];
				NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
				[png writeToFile:file atomically:YES];
				break;
			}
			if (std::getenv("NPPMAC_SNAPSHOT_EXIT"))
				_exit(0); // no static destructors: the program is mid-run
		}];
		[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
	}

	// Debugging aid: NPPMAC_TEST_TYPE=<text> types the text into the key window after NPPMAC_TEST_TYPE_DELAY
	// seconds (default 2), as key events going through the application like the user's; '\n' is Return
	// and ESC is Escape, and '^' makes the next character a Command shortcut ("^s" is ⌘S).
	void scheduleTyping()
	{
		const char* text = std::getenv("NPPMAC_TEST_TYPE");
		if (!text || !*text)
			return;
		const char* delayText = std::getenv("NPPMAC_TEST_TYPE_DELAY");
		const double delay = delayText ? std::atof(delayText) : 2.0;
		NSString* characters = [NSString stringWithUTF8String:text];
		// One key every 50 ms, each to the key window of its moment, as a person types: a key that
		// opens a dialog is followed by keys for the dialog.
		constexpr double keyInterval = 0.05;
		int keyIndex = 0;
		for (NSUInteger i = 0; i < characters.length; ++i)
		{
			NSEventModifierFlags modifiers = 0;
			if ([characters characterAtIndex:i] == '^' && i + 1 < characters.length)
			{
				modifiers = NSEventModifierFlagCommand;
				++i;
			}
			NSString* character = [characters substringWithRange:NSMakeRange(i, 1)];
			const bool isReturn = [character isEqualToString:@"\n"];
			const bool isEscape = [character isEqualToString:@"\x1b"];
			if (isReturn)
				character = @"\r";
			const unsigned short keyCode = isReturn ? 36 : (isEscape ? 53 : 0);
			NSTimer* timer = [NSTimer timerWithTimeInterval:delay + keyIndex++ * keyInterval repeats:NO block:^(NSTimer*) {
				NSWindow* window = NSApp.keyWindow ? NSApp.keyWindow : NSApp.orderedWindows.firstObject;
				for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp})
				{
					NSEvent* event = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:modifiers timestamp:NSProcessInfo.processInfo.systemUptime
						windowNumber:window.windowNumber context:nil characters:character charactersIgnoringModifiers:character isARepeat:NO keyCode:keyCode];
					[NSApp sendEvent:event];
				}
			}];
			[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
		}
	}

	// Debugging aid: NPPMAC_TEST_COMMAND=<id> sends that menu command after NPPMAC_TEST_COMMAND_DELAY
	// seconds (default 2), for dialogs without a shortcut.
	void scheduleCommand()
	{
		const char* command = std::getenv("NPPMAC_TEST_COMMAND");
		if (!command || !*command)
			return;
		const char* delayText = std::getenv("NPPMAC_TEST_COMMAND_DELAY");
		const double delay = delayText ? std::atof(delayText) : 2.0;
		const int id = std::atoi(command);
		NSTimer* timer = [NSTimer timerWithTimeInterval:delay repeats:NO block:^(NSTimer*) {
			windowManagerTestCommand(id);
		}];
		[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
	}

	void dumpMenuBar(NSMenu* menu, int depth)
	{
		for (NSMenuItem* item in menu.itemArray)
		{
			if (item.isSeparatorItem)
				continue;
			std::fprintf(stderr, "%*s%s%s\n", depth * 2, "", item.title.UTF8String, item.keyEquivalent.length ? (std::string{"  ["} + item.keyEquivalent.UTF8String + "]").c_str() : "");
			if (item.submenu && depth < 1)
				dumpMenuBar(item.submenu, depth + 1);
		}
	}

	void createApplication()
	{
		// AppKit adds Dictation and Emoji items to any menu titled "Edit", again each time the program
		// refills it; the editor takes its input as key events, which those do not send.
		[NSUserDefaults.standardUserDefaults registerDefaults:@{@"NSDisabledDictationMenuItem": @YES, @"NSDisabledCharacterPaletteMenuItem": @YES}];
		[NppMacApplication sharedApplication];
		g_applicationDelegate = [[NppMacApplicationDelegate alloc] init];
		NSApp.delegate = g_applicationDelegate;
		g_windowDelegate = [[NppMacWindowDelegate alloc] init];
		scheduleSnapshot();
		scheduleTyping();
		scheduleCommand();
	}

	NativeView* createView(ViewOwner* owner, ViewKind kind, NativeView* parent, const UiRect& frame, const TopLevelStyle& style)
	{
		auto* native = new NativeView;
		native->_owner = owner;
		native->_kind = kind;
		if (kind == ViewKind::child)
		{
			NppMacView* view = createSurfaceView(native, NSMakeRect(frame._left, frame._top, std::max(0, frame.width()), std::max(0, frame.height())));
			native->_view = view;
			view.hidden = YES;
			// New child windows start at the bottom of the z-order, as on Windows.
			if (parent && parent->_view)
				[parent->_view addSubview:view positioned:NSWindowBelow relativeTo:nil];
			return native;
		}

		const NSUInteger mask = styleMask(kind, style);
		const NSRect contentRect = [NSWindow contentRectForFrameRect:toCocoaScreenRect(frame) styleMask:mask];
		NppMacWindow* window = [[NppMacWindow alloc] initWithContentRect:contentRect styleMask:mask backing:NSBackingStoreBuffered defer:NO];
		window->_native = native;
		window->_borderless = (mask == NSWindowStyleMaskBorderless);
		window.releasedWhenClosed = NO;
		window.delegate = g_windowDelegate;
		window.acceptsMouseMovedEvents = YES;
		window.tabbingMode = NSWindowTabbingModeDisallowed;
		if (style._topMost || kind == ViewKind::popup)
			window.level = kind == ViewKind::popup ? NSPopUpMenuWindowLevel : NSFloatingWindowLevel;
		if (style._toolWindow)
			window.hidesOnDeactivate = NO;
		NppMacView* content = createSurfaceView(native, NSMakeRect(0, 0, contentRect.size.width, contentRect.size.height));
		window.contentView = content;
		native->_window = window;
		native->_view = content;
		return native;
	}

	NativeView* adoptView(ViewOwner* owner, NativeView* parent, void* nsView, const UiRect& frame)
	{
		auto* native = new NativeView;
		native->_owner = owner;
		native->_kind = ViewKind::child;
		native->_adopted = true;
		NSView* view = (__bridge_transfer NSView*)nsView; // the window manager now owns the view
		native->_view = view;
		view.frame = NSMakeRect(frame._left, frame._top, std::max(0, frame.width()), std::max(0, frame.height()));
		view.hidden = YES;
		if (parent && parent->_view)
			[parent->_view addSubview:view positioned:NSWindowBelow relativeTo:nil];
		return native;
	}

	void* nsView(NativeView* native)
	{
		return native ? (__bridge void*)native->_view : nullptr;
	}

	void destroyView(NativeView* native)
	{
		if (!native)
			return;
		if (g_captureView == native)
			g_captureView = nullptr;
		if ([native->_view isKindOfClass:NppMacView.class])
			static_cast<NppMacView*>(native->_view)->_native = nullptr;
		if (native->_window)
		{
			static_cast<NppMacWindow*>(native->_window)->_native = nullptr;
			native->_window.delegate = nil;
			[native->_window orderOut:nil];
			[native->_window close];
		}
		else
		{
			[native->_view removeFromSuperview];
		}
		if (native->_surface)
			CGContextRelease(native->_surface);
		native->_view = nil;
		native->_window = nil;
		delete native;
	}

	void setFrame(NativeView* native, const UiRect& frame)
	{
		if (!native)
			return;
		if (native->_window)
		{
			[native->_window setFrame:toCocoaScreenRect(frame) display:NO];
			return;
		}
		native->_view.frame = NSMakeRect(frame._left, frame._top, std::max(0, frame.width()), std::max(0, frame.height()));
	}

	void setVisible(NativeView* native, bool visible, bool activate)
	{
		if (!native)
			return;
		if (native->_window)
		{
			if (!visible)
			{
				[native->_window orderOut:nil];
			}
			else if (activate)
			{
				[NSApp activateIgnoringOtherApps:YES];
				[native->_window makeKeyAndOrderFront:nil];
			}
			else
			{
				[native->_window orderFront:nil];
			}
			return;
		}
		native->_view.hidden = !visible;
	}

	void setTitle(NativeView* native, const std::u16string& title)
	{
		if (native && native->_window)
			native->_window.title = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(title.data()) length:title.size()];
	}

	void setEnabled(NativeView* native, bool enabled)
	{
		if (native && [native->_view isKindOfClass:NSControl.class])
			static_cast<NSControl*>(native->_view).enabled = enabled;
	}

	void setParent(NativeView* native, NativeView* newParent)
	{
		if (!native || native->_window)
			return;
		[native->_view removeFromSuperview];
		if (newParent && newParent->_view)
			[newParent->_view addSubview:native->_view positioned:NSWindowAbove relativeTo:nil];
	}

	void orderView(NativeView* native, bool front, NativeView* above)
	{
		if (!native)
			return;
		if (native->_window)
		{
			if (front)
				[native->_window orderFront:nil];
			else
				[native->_window orderBack:nil];
			return;
		}
		NSView* superview = native->_view.superview;
		if (!superview)
			return;
		NSView* view = native->_view;
		[view removeFromSuperview];
		// Windows' z-order "top" is AppKit's last subview.
		if (above && above->_view && above->_view.superview == superview)
			[superview addSubview:view positioned:NSWindowBelow relativeTo:above->_view];
		else
			[superview addSubview:view positioned:front ? NSWindowAbove : NSWindowBelow relativeTo:nil];
	}

	UiRect clientRectOnScreen(NativeView* native)
	{
		if (!native || !native->_window)
			return {};
		const NSRect content = [native->_window contentRectForFrameRect:native->_window.frame];
		return fromCocoaScreenRect(content);
	}

	UiRect windowRectOnScreen(NativeView* native)
	{
		if (!native || !native->_window)
			return {};
		return fromCocoaScreenRect(native->_window.frame);
	}

	UiRect windowRectForClient(const UiRect& clientOnScreen, const TopLevelStyle& style)
	{
		const NSRect frame = [NSWindow frameRectForContentRect:toCocoaScreenRect(clientOnScreen) styleMask:styleMask(ViewKind::topLevel, style)];
		return fromCocoaScreenRect(frame);
	}

	void invalidate(NativeView* native, const UiRect* rect)
	{
		if (!native || !native->_view)
			return;
		if (rect)
			[native->_view setNeedsDisplayInRect:NSMakeRect(rect->_left, rect->_top, rect->width(), rect->height())];
		else
			native->_view.needsDisplay = YES;
	}

	void displayNow(NativeView* native)
	{
		if (native && native->_view)
			[native->_view displayIfNeeded];
	}

	void makeKeyAndFocus(NativeView* native)
	{
		if (!native)
			return;
		NSWindow* window = native->_window ?: native->_view.window;
		if (window && !window.isKeyWindow && window.isVisible)
			[window makeKeyAndOrderFront:nil];
	}

	void focusNativeView(NativeView* native)
	{
		if (!native || !native->_view.window)
			return;
		NSView* target = native->_view;
		// ScintillaView keeps its text view (the one that takes keys) as its "content".
		if ([target respondsToSelector:@selector(content)])
			target = [target performSelector:@selector(content)];
		else if (!target.acceptsFirstResponder)
		{
			for (NSView* subview in target.subviews)
			{
				if (subview.acceptsFirstResponder)
				{
					target = subview;
					break;
				}
			}
		}
		[native->_view.window makeFirstResponder:target];
	}

	void forwardKey(NativeView* native, void* nsEvent)
	{
		if (!native || !nsEvent)
			return;
		NSEvent* event = (__bridge NSEvent*)nsEvent;
		NSResponder* responder = native->_view.window.firstResponder;
		NSView* target = native->_view;
		if ([responder isKindOfClass:NSView.class] && [static_cast<NSView*>(responder) isDescendantOf:native->_view])
			target = static_cast<NSView*>(responder);
		if (event.type == NSEventTypeKeyDown)
			[target keyDown:event];
		else if (event.type == NSEventTypeKeyUp)
			[target keyUp:event];
	}

	void retainNativeEvent(void* nsEvent)
	{
		if (nsEvent)
			CFRetain(nsEvent);
	}

	void releaseNativeEvent(void* nsEvent)
	{
		if (nsEvent)
			CFRelease(nsEvent);
	}

	bool isMinimized(NativeView* native)
	{
		return native && native->_window && native->_window.isMiniaturized;
	}

	bool isMaximized(NativeView* native)
	{
		return native && native->_window && native->_window.isZoomed && !native->_window.isMiniaturized;
	}

	void minimize(NativeView* native)
	{
		if (native && native->_window)
			[native->_window miniaturize:nil];
	}

	void maximize(NativeView* native)
	{
		if (native && native->_window && !native->_window.isZoomed)
			[native->_window zoom:nil];
	}

	void restore(NativeView* native)
	{
		if (!native || !native->_window)
			return;
		if (native->_window.isMiniaturized)
			[native->_window deminiaturize:nil];
		else if (native->_window.isZoomed)
			[native->_window zoom:nil];
	}

	void setSeeThrough(NativeView* native, bool seeThrough)
	{
		if (!native || native->_adopted || native->_seeThrough == seeThrough)
			return;
		native->_seeThrough = seeThrough;
		native->_view.needsDisplay = YES;
	}

	void clearSurface(NativeView* native, const UiRect& rect)
	{
		if (!native)
			return;
		ensureSurface(native);
		if (!native->_surface)
			return;
		CGContextClearRect(native->_surface, CGRectMake(rect._left, rect._top, rect.width(), rect.height()));
	}

	void* surfaceContext(NativeView* native)
	{
		if (!native || native->_adopted)
			return nullptr;
		ensureSurface(native);
		return native->_surface;
	}

	UiRect mainScreenRect()
	{
		NSArray<NSScreen*>* screens = NSScreen.screens;
		return screens.count > 0 ? fromCocoaScreenRect(screens[0].frame) : UiRect{0, 0, 1920, 1080};
	}

	UiRect mainScreenWorkArea()
	{
		NSArray<NSScreen*>* screens = NSScreen.screens;
		return screens.count > 0 ? fromCocoaScreenRect(screens[0].visibleFrame) : UiRect{0, 25, 1920, 1080};
	}

	UiPoint cursorPosition()
	{
		const NSPoint location = NSEvent.mouseLocation;
		return UiPoint{static_cast<int>(std::floor(location.x)), static_cast<int>(std::floor(primaryScreenHeight() - location.y))};
	}

	double backingScale()
	{
		return NSScreen.mainScreen.backingScaleFactor;
	}

	void setAcceptsFiles(NativeView* native, bool accepts)
	{
		if (!native || !native->_view)
			return;
		native->_acceptsFiles = accepts;
		if (accepts)
			[native->_view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
		else
			[native->_view unregisterDraggedTypes];
	}

	void setCapture(NativeView* native)
	{
		g_captureView = native;
	}

	void releaseCapture()
	{
		g_captureView = nullptr;
	}

	void setCursor(CursorShape shape)
	{
		NSCursor* cursor = NSCursor.arrowCursor;
		switch (shape)
		{
			case CursorShape::iBeam:
				cursor = NSCursor.IBeamCursor;
				break;
			case CursorShape::cross:
				cursor = NSCursor.crosshairCursor;
				break;
			case CursorShape::sizeWE:
				cursor = NSCursor.resizeLeftRightCursor;
				break;
			case CursorShape::sizeNS:
				cursor = NSCursor.resizeUpDownCursor;
				break;
			case CursorShape::sizeAll:
				cursor = NSCursor.openHandCursor;
				break;
			case CursorShape::no:
				cursor = NSCursor.operationNotAllowedCursor;
				break;
			case CursorShape::hand:
				cursor = NSCursor.pointingHandCursor;
				break;
			case CursorShape::upArrow:
				cursor = NSCursor.resizeUpCursor;
				break;
			default:
				break;
		}
		if (g_cursorHidden)
		{
			[NSCursor unhide];
			g_cursorHidden = false;
		}
		[cursor set];
	}

	void hideCursor(bool hide)
	{
		if (hide && !g_cursorHidden)
			[NSCursor hide];
		else if (!hide && g_cursorHidden)
			[NSCursor unhide];
		g_cursorHidden = hide;
	}

	void runEventLoopOnce(int timeoutMilliseconds)
	{
		@autoreleasepool
		{
			NSDate* until = timeoutMilliseconds < 0 ? NSDate.distantFuture : [NSDate dateWithTimeIntervalSinceNow:timeoutMilliseconds / 1000.0];
			NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
			if (event && !(event.type == NSEventTypeApplicationDefined && event.subtype == 0x4E50))
				[NSApp sendEvent:event];
			[NSApp updateWindows];
		}
	}

	void wakeEventLoop()
	{
		dispatch_async(dispatch_get_main_queue(), ^{
			NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0
				windowNumber:0 context:nil subtype:0x4E50 data1:0 data2:0];
			[NSApp postEvent:event atStart:NO];
		});
	}

	void* startTimer(unsigned milliseconds, TimerCallback callback, void* context, uintptr_t id)
	{
		NSTimer* timer = [NSTimer timerWithTimeInterval:milliseconds / 1000.0 repeats:YES block:^(NSTimer*) {
			callback(context, id);
			// Let a GetMessage blocked in nextEventMatchingMask see the WM_TIMER.
			NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0
				windowNumber:0 context:nil subtype:0x4E50 data1:0 data2:0];
			[NSApp postEvent:event atStart:NO];
		}];
		[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
		return (__bridge_retained void*)timer;
	}

	void stopTimer(void* timer)
	{
		if (!timer)
			return;
		NSTimer* object = (__bridge_transfer NSTimer*)timer;
		[object invalidate];
	}
}
