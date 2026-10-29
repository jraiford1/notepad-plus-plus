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
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include "Backend.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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
		CGContextRef _surface = nullptr;  // draws into _ioSurface
		IOSurfaceRef _ioSurface = nullptr; // the layer's contents: the window's pixels, once (owned by _surface)
		int _surfaceWidth = 0;         // in points
		int _surfaceHeight = 0;
		CGFloat _surfaceScale = 1;
		bool _acceptsFiles = false;
		bool _seeThrough = false;
		NSScroller* _scrollers[2] = {};    // horizontal, vertical
		ScrollBarState _scrollStates[2];
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

	void layoutScrollers(NativeView* native);
}

// --- Views ------------------------------------------------------------------------------------------

// A standard scroll bar. It reports clicks and drags to the window manager; a knob drag ends with
// a thumb position and an end, as Windows' scroll bars do.
@interface NppMacScroller : NSScroller
{
@public
	NativeView* _native;
	bool _vertical;
	bool _draggedKnob;
}
@end

@implementation NppMacScroller

- (void)scrolled:(id)sender
{
	if (!_native || !_native->_owner)
		return;
	switch (self.hitPart)
	{
		case NSScrollerKnob:
			_draggedKnob = true;
			_native->_owner->nativeScroll(_vertical, ScrollPart::thumbTrack, self.doubleValue);
			break;
		case NSScrollerDecrementPage:
			_native->_owner->nativeScroll(_vertical, ScrollPart::pageUp, self.doubleValue);
			break;
		case NSScrollerIncrementPage:
			_native->_owner->nativeScroll(_vertical, ScrollPart::pageDown, self.doubleValue);
			break;
		default:
			break;
	}
}

- (void)mouseDown:(NSEvent*)event
{
	_draggedKnob = false;
	[super mouseDown:event]; // tracks the mouse until it comes up
	if (!_native || !_native->_owner)
		return;
	if (_draggedKnob)
		_native->_owner->nativeScroll(_vertical, ScrollPart::thumbPosition, self.doubleValue);
	_native->_owner->nativeScroll(_vertical, ScrollPart::end, self.doubleValue);
}

@end

// What the text input system did with the key interpretKey is running through it.
struct TextInputCollector
{
	bool _active = false;
	std::u16string _committed;
};
static TextInputCollector g_textInput;

// A test run (NPPMAC_TEST_RUN=1) is invisible and deaf to the Mac's own keyboard: it takes only the
// key events its test aids make, sent while g_sendingTestEvent is set.
static const bool g_testRun = std::getenv("NPPMAC_TEST_RUN") != nullptr;
static bool g_sendingTestEvent = false;

@interface NppMacView : NSView <NSTextInputClient>
{
@public
	NativeView* _native;
	NSTrackingArea* _trackingArea;
	NSString* _markedText; // the text an input method is composing, nil when none
}
@end

@interface NppMacWindow : NSWindow
{
@public
	NativeView* _native;
	BOOL _borderless;
	BOOL _ordered; // ordered in: shown, as Windows sees it (also while the application is hidden or minimized)
}
@end

namespace
{
	void releaseSurface(NativeView* native);

	// Shown as Windows sees it: in a window that is ordered in, and not hidden. (NSWindow.isVisible is also
	// false while the application is hidden or the window minimized, where Windows' windows are shown.)
	bool isShown(NSView* view)
	{
		NSWindow* window = view.window;
		if (!window || view.isHiddenOrHasHiddenAncestor)
			return false;
		return [window isKindOfClass:NppMacWindow.class] ? static_cast<NppMacWindow*>(window)->_ordered : window.isVisible;
	}

	// A view that is not shown gives its surface up; it is painted again, whole, when it is shown.
	void givePixelsUp(NppMacView* view)
	{
		NativeView* native = view->_native;
		if (!native || !native->_surface)
			return;
		releaseSurface(native);
		view.layer.contents = nil;
		native->_owner->nativePixelsLost();
	}

	// A top-level window's views as it is hidden (they give their pixels up) or shown (they are painted).
	void hideTree(NSView* view)
	{
		if ([view isKindOfClass:NppMacView.class])
			givePixelsUp(static_cast<NppMacView*>(view));
		for (NSView* subview in view.subviews)
			hideTree(subview);
	}

	void showTree(NSView* view)
	{
		if ([view isKindOfClass:NppMacView.class] && !view.isHidden)
			view.needsDisplay = YES;
		for (NSView* subview in view.subviews)
			showTree(subview);
	}

	// A copy of the part of a window's surface that covers area (in points), as an image of its own,
	// for drawRect (an image of the view): an image of the surface itself would be copied whole by
	// the surface's next drawing.
	CGImageRef copySurfaceArea(NativeView* native, NSRect area)
	{
		CGContextRef surface = native->_surface;
		const CGFloat scale = native->_surfaceScale;
		const size_t surfaceWidth = CGBitmapContextGetWidth(surface);
		const size_t surfaceHeight = CGBitmapContextGetHeight(surface);
		const auto* pixels = static_cast<const uint8_t*>(CGBitmapContextGetData(surface));
		if (!pixels)
			return nullptr;
		const auto clamp = [](CGFloat value, size_t limit)
		{
			return static_cast<size_t>(std::clamp<CGFloat>(value, 0, static_cast<CGFloat>(limit)));
		};
		const size_t left = clamp(std::floor(NSMinX(area) * scale), surfaceWidth);
		const size_t top = clamp(std::floor(NSMinY(area) * scale), surfaceHeight);
		const size_t right = clamp(std::ceil(NSMaxX(area) * scale), surfaceWidth);
		const size_t bottom = clamp(std::ceil(NSMaxY(area) * scale), surfaceHeight);
		if (right <= left || bottom <= top)
			return nullptr;

		// The surface's rows run from the top (its context is flipped to a top-left origin).
		const size_t width = right - left;
		const size_t height = bottom - top;
		const size_t bytesPerPixel = CGBitmapContextGetBitsPerPixel(surface) / 8;
		const size_t sourceStride = CGBitmapContextGetBytesPerRow(surface);
		const size_t stride = width * bytesPerPixel;
		CFMutableDataRef copy = CFDataCreateMutable(nullptr, static_cast<CFIndex>(stride * height));
		CFDataSetLength(copy, static_cast<CFIndex>(stride * height));
		uint8_t* target = CFDataGetMutableBytePtr(copy);
		for (size_t row = 0; row < height; ++row)
			std::memcpy(target + row * stride, pixels + (top + row) * sourceStride + left * bytesPerPixel, stride);

		CGDataProviderRef provider = CGDataProviderCreateWithCFData(copy);
		CFRelease(copy);
		CGImageRef image = CGImageCreate(width, height, CGBitmapContextGetBitsPerComponent(surface), CGBitmapContextGetBitsPerPixel(surface),
			stride, CGBitmapContextGetColorSpace(surface), CGBitmapContextGetBitmapInfo(surface), provider, nullptr, false,
			kCGRenderingIntentDefault);
		CGDataProviderRelease(provider);
		return image;
	}
}

@implementation NppMacView

- (BOOL)isFlipped
{
	return YES;
}

- (void)setFrameSize:(NSSize)size
{
	[super setFrameSize:size];
	if (_native && (_native->_scrollers[0] || _native->_scrollers[1]))
		layoutScrollers(_native);
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
	return YES;
}

- (BOOL)resignFirstResponder
{
	// Focus moving away ends a composition, as on Windows: what was composed is dropped.
	if (_markedText)
	{
		[self.inputContext discardMarkedText];
		[self clearMarkedText];
	}
	return [super resignFirstResponder];
}

// --- NSTextInputClient: the window's text is the owner's; only the composition is kept here.

- (void)clearMarkedText
{
	_markedText = nil;
	if (_native)
		_native->_owner->nativeComposition({}, 0);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
	NSString* text = [string isKindOfClass:NSAttributedString.class] ? [static_cast<NSAttributedString*>(string) string] : static_cast<NSString*>(string);
	if (_markedText)
		[self clearMarkedText];
	if (g_textInput._active)
		g_textInput._committed += toU16(text);
	else if (_native)
		_native->_owner->nativeInsertText(toU16(text));
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
	NSString* text = [string isKindOfClass:NSAttributedString.class] ? [static_cast<NSAttributedString*>(string) string] : static_cast<NSString*>(string);
	if (text.length == 0)
	{
		[self clearMarkedText];
		return;
	}
	_markedText = [text copy];
	if (_native)
		_native->_owner->nativeComposition(toU16(text), std::min<NSUInteger>(selectedRange.location, text.length));
}

- (void)unmarkText
{
	// The input method asks for the composition to be kept as it is.
	NSString* text = _markedText;
	if (!text)
		return;
	[self clearMarkedText];
	[self insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (BOOL)hasMarkedText
{
	return _markedText.length > 0;
}

- (NSRange)markedRange
{
	return _markedText ? NSMakeRange(0, _markedText.length) : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
	return NSMakeRange(_markedText ? _markedText.length : 0, 0);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
	return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
	return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
	return NSNotFound;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
	if (!_native || !self.window)
		return NSZeroRect;
	const UiRect caret = _native->_owner->nativeCaretRect();
	const NSRect local = NSMakeRect(caret._left, caret._top, std::max(1, caret.width()), std::max(1, caret.height()));
	return [self.window convertRectToScreen:[self convertRect:local toView:nil]];
}

- (void)doCommandBySelector:(SEL)selector
{
	// Return, Tab, arrows and the like: the window gets them as the usual WM_KEYDOWN.
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

// The window's pixels are its layer's contents: the surface it draws into, which Core Animation shows
// as it is. (Drawn through drawRect, AppKit would keep a copy of them, and the layer one more.)
- (BOOL)wantsUpdateLayer
{
	return YES;
}

- (void)updateLayer
{
	// A view that is not shown is not painted: it is when it is shown (viewDidUnhide, showTree).
	if (!_native || !isShown(self))
		return;
	const NSRect bounds = self.bounds;
	_native->_owner->nativePaint(UiRect{0, 0, static_cast<int>(std::ceil(NSWidth(bounds))), static_cast<int>(std::ceil(NSHeight(bounds)))});
	if (!_native)
		return;
	CALayer* layer = self.layer;
	if (!_native->_ioSurface)
	{
		layer.contents = nil;
		layer.backgroundColor = [NSColor colorWithSRGBRed:0.94 green:0.94 blue:0.94 alpha:1].CGColor;
		return;
	}
	// Set again (through nil) so Core Animation reads the surface's new pixels, without the fade a
	// change of contents otherwise has.
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	layer.backgroundColor = nil;
	layer.contents = nil;
	layer.contents = (__bridge id)_native->_ioSurface;
	[CATransaction commit];
}

// Hidden, itself or with an ancestor: it gives its pixels up, as a window that is not shown has none.
- (void)viewDidHide
{
	[super viewDidHide];
	givePixelsUp(self);
}

- (void)viewDidUnhide
{
	[super viewDidUnhide];
	// What is inside is shown again too: its views were not painted while hidden (made then, or given
	// their pixels up), and AppKit does not ask them again.
	if (isShown(self))
		showTree(self);
}

// Only for an image of the view (cacheDisplayInRect: the test runs' snapshots); the screen shows the layer.
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
		// The rectangle AppKit asks for can reach past the view; only the view's own area is filled.
		CGContextSetRGBFillColor(context, 0.94, 0.94, 0.94, 1);
		CGContextFillRect(context, NSRectToCGRect(NSIntersectionRect(dirtyRect, self.bounds)));
		return;
	}
	CGImageRef image = copySurfaceArea(_native, dirtyRect);
	if (!image)
		return;
	// The view is flipped: draw the image upright, where its area is in the surface.
	const CGFloat scale = _native->_surfaceScale;
	const CGFloat left = std::floor(NSMinX(dirtyRect) * scale) / scale;
	const CGFloat top = std::floor(NSMinY(dirtyRect) * scale) / scale;
	const CGFloat width = static_cast<CGFloat>(CGImageGetWidth(image)) / scale;
	const CGFloat height = static_cast<CGFloat>(CGImageGetHeight(image)) / scale;
	CGContextSaveGState(context);
	CGContextClipToRect(context, NSRectToCGRect(dirtyRect));
	CGContextTranslateCTM(context, 0, top + height);
	CGContextScaleCTM(context, 1, -1);
	CGContextSetInterpolationQuality(context, kCGInterpolationNone);
	CGContextDrawImage(context, CGRectMake(left, 0, width, height), image);
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

@implementation NppMacWindow

// Every way of ordering a window in or out comes here.
- (void)orderWindow:(NSWindowOrderingMode)place relativeTo:(NSInteger)other
{
	_ordered = place != NSWindowOut;
	[super orderWindow:place relativeTo:other];
}

- (BOOL)canBecomeKeyWindow
{
	// A test run's windows never take the keyboard: it stays with whoever is using the Mac.
	return !g_testRun;
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
	if (g_testRun && !g_sendingTestEvent
		&& (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp || event.type == NSEventTypeFlagsChanged))
		return;
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

// Files macOS hands the program while it launches, kept for its command line (see takeLaunchFiles).
static bool g_launched = false;
static std::vector<std::string> g_launchFiles;

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

- (void)applicationDidBecomeActive:(NSNotification*)notification
{
	windowManagerApplicationActivated(true);
}

- (void)applicationDidResignActive:(NSNotification*)notification
{
	windowManagerApplicationActivated(false);
}

- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls
{
	std::vector<std::string> paths;
	for (NSURL* url in urls)
	{
		if (url.isFileURL)
			paths.push_back(url.path.UTF8String);
	}
	if (paths.empty())
		return;
	// While launching, the files join the command line, as files given to notepad++.exe do.
	if (!g_launched)
	{
		g_launchFiles.insert(g_launchFiles.end(), paths.begin(), paths.end());
		return;
	}
	[self openFiles:paths attempt:0];
}

- (void)openFiles:(std::vector<std::string>)paths attempt:(int)attempt
{
	// Between launching and the main window's creation there is nothing to drop on yet.
	if (windowManagerOpenFiles(paths) || attempt >= 50)
		return;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
		[self openFiles:paths attempt:attempt + 1];
	});
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
		view.wantsLayer = YES;
		view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
		// The surface's first row is the window's top; it is not stretched while the window resizes.
		view.layer.contentsGravity = kCAGravityTopLeft;
		view.autoresizingMask = NSViewNotSizable;
		// A window's pixels stay inside it, as on Windows (views do not clip by default since macOS 14).
		view.clipsToBounds = YES;
		return view;
	}

	void releaseIOSurface(void* ioSurface, void*)
	{
		IOSurfaceUnlock(static_cast<IOSurfaceRef>(ioSurface), 0, nullptr);
		CFRelease(static_cast<IOSurfaceRef>(ioSurface));
	}

	void releaseSurface(NativeView* native)
	{
		if (native->_surface)
			CGContextRelease(native->_surface);
		native->_surface = nullptr;
		native->_ioSurface = nullptr;
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

		// An IOSurface, which Core Animation shows without copying it, locked for drawing for its lifetime:
		// the layer is told when it changed (updateLayer).
		const size_t pixelWidth = static_cast<size_t>(width * scale);
		const size_t pixelHeight = static_cast<size_t>(height * scale);
		NSDictionary* properties = @{
			(id)kIOSurfaceWidth: @(pixelWidth),
			(id)kIOSurfaceHeight: @(pixelHeight),
			(id)kIOSurfaceBytesPerElement: @4,
			(id)kIOSurfacePixelFormat: @(static_cast<unsigned>('BGRA')),
		};
		IOSurfaceRef ioSurface = IOSurfaceCreate((__bridge CFDictionaryRef)properties);
		if (!ioSurface)
			return;
		IOSurfaceLock(ioSurface, 0, nullptr);
		// The context owns the IOSurface: it is unlocked and released with the context's last
		// reference (a window DC keeps one while it is used).
		CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGContextRef surface = CGBitmapContextCreateWithData(IOSurfaceGetBaseAddress(ioSurface), pixelWidth, pixelHeight, 8,
			IOSurfaceGetBytesPerRow(ioSurface), colorSpace,
			static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little), releaseIOSurface, ioSurface);
		CGColorSpaceRelease(colorSpace);
		if (!surface)
		{
			releaseIOSurface(ioSurface, nullptr);
			return;
		}
		if (pixelWidth * pixelHeight > 0)
			std::memset(IOSurfaceGetBaseAddress(ioSurface), 0, IOSurfaceGetBytesPerRow(ioSurface) * pixelHeight);
		// Keep what the old surface showed, so that a resize does not flash.
		if (native->_surface)
		{
			CGImageRef old = CGBitmapContextCreateImage(native->_surface);
			if (old)
			{
				CGContextDrawImage(surface, CGRectMake(0, (height - native->_surfaceHeight) * scale, native->_surfaceWidth * scale, native->_surfaceHeight * scale), old);
				CGImageRelease(old);
			}
			releaseSurface(native);
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
		native->_ioSurface = ioSurface;
		native->_view.layer.contentsScale = scale;
		native->_view.layer.contents = (__bridge id)ioSurface;
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
				std::fprintf(stderr, "NSApp active=%d hidden=%d keyWindow=%d\n", NSApp.isActive ? 1 : 0, NSApp.isHidden ? 1 : 0, NSApp.keyWindow ? 1 : 0);
				for (NSWindow* window in NSApp.orderedWindows)
				{
					std::fprintf(stderr, "NSWindow %s visible=%d occlusion=%lu frame=%s\n", NSStringFromClass(window.class).UTF8String, window.isVisible ? 1 : 0,
						static_cast<unsigned long>(window.occlusionState), NSStringFromRect(window.frame).UTF8String);
				}
				dumpMenuBar(NSApp.mainMenu, 0);
			}
			if (NativeView* native = windowManagerSnapshotView(); native && native->_view)
			{
				NSView* content = native->_view;
				NSBitmapImageRep* bitmap = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
				[content cacheDisplayInRect:content.bounds toBitmapImageRep:bitmap];
				NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
				[png writeToFile:file atomically:YES];
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
					g_sendingTestEvent = true;
					[NSApp sendEvent:event];
					g_sendingTestEvent = false;
				}
			}];
			[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
		}

		// NPPMAC_TEST_IME=<composing>[|<committed>] then plays an input method on the focused view (the
		// text input system serves only the active program, which a test cannot make itself): it
		// composes the first text and, a second later, commits the second.
		const char* ime = std::getenv("NPPMAC_TEST_IME");
		if (!ime || !*ime)
			return;
		const std::u16string imeText = toU16([NSString stringWithUTF8String:ime]);
		const size_t bar = imeText.find(u'|');
		const std::u16string composing = imeText.substr(0, bar);
		const std::u16string committed = bar == std::u16string::npos ? std::u16string{} : imeText.substr(bar + 1);
		for (int step = 0; step < (committed.empty() ? 1 : 2); ++step)
		{
			NSTimer* timer = [NSTimer timerWithTimeInterval:delay + keyIndex * keyInterval + 0.3 + step repeats:NO block:^(NSTimer*) {
				testComposition(windowManagerFocusView(), composing, step == 0 ? std::u16string{} : committed);
			}];
			[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
		}
	}

	// Debugging aid: NPPMAC_TEST_SWITCH_BACK=<seconds> acts as if the user switched to another program
	// and back then (the program cannot become active in a test run).
	void scheduleSwitchBack()
	{
		const char* delayText = std::getenv("NPPMAC_TEST_SWITCH_BACK");
		if (!delayText || !*delayText)
			return;
		NSTimer* timer = [NSTimer timerWithTimeInterval:std::atof(delayText) repeats:NO block:^(NSTimer*) {
			windowManagerApplicationActivated(false);
			windowManagerApplicationActivated(true);
		}];
		[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
	}

	// Debugging aid: NPPMAC_TEST_COMMAND=<id>[,<id>...] sends those menu commands, half a second apart,
	// after NPPMAC_TEST_COMMAND_DELAY seconds (default 2), for dialogs without a shortcut; click:<id>
	// clicks the button with that control id instead (a dialog's check box or radio button).
	void scheduleCommand()
	{
		const char* command = std::getenv("NPPMAC_TEST_COMMAND");
		if (!command || !*command)
			return;
		const char* delayText = std::getenv("NPPMAC_TEST_COMMAND_DELAY");
		double delay = delayText ? std::atof(delayText) : 2.0;
		for (const char* next = command; *next;)
		{
			const bool click = std::strncmp(next, "click:", 6) == 0;
			if (click)
				next += 6;
			char* end = nullptr;
			const int id = static_cast<int>(std::strtol(next, &end, 10));
			if (end == next)
				break;
			NSTimer* timer = [NSTimer timerWithTimeInterval:delay repeats:NO block:^(NSTimer*) {
				if (click)
					windowManagerTestClick(id);
				else
					windowManagerTestCommand(id);
			}];
			[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
			delay += 0.5;
			next = *end == ',' ? end + 1 : end;
		}
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

	std::vector<std::string> takeLaunchFiles()
	{
		return std::exchange(g_launchFiles, {});
	}

	void setLaunched()
	{
		g_launched = true;
	}

	void createApplication()
	{
		// AppKit adds Dictation and Emoji items to any menu titled "Edit", again each time the program
		// refills it; the editor takes its input as key events, which those do not send.
		[NSUserDefaults.standardUserDefaults registerDefaults:@{@"NSDisabledDictationMenuItem": @YES, @"NSDisabledCharacterPaletteMenuItem": @YES}];
		[NppMacApplication sharedApplication];
		// Debugging aid: NPPMAC_TEST_APPEARANCE=dark (or light) runs the program as if the system were so.
		if (const char* appearance = std::getenv("NPPMAC_TEST_APPEARANCE"))
			NSApp.appearance = [NSAppearance appearanceNamed:std::strcmp(appearance, "dark") == 0 ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
		g_applicationDelegate = [[NppMacApplicationDelegate alloc] init];
		NSApp.delegate = g_applicationDelegate;
		g_windowDelegate = [[NppMacWindowDelegate alloc] init];
		scheduleSnapshot();
		scheduleTyping();
		scheduleCommand();
		scheduleSwitchBack();
		[NSDistributedNotificationCenter.defaultCenter addObserverForName:@"AppleInterfaceThemeChangedNotification" object:nil queue:NSOperationQueue.mainQueue
			usingBlock:^(NSNotification*) {
				windowManagerAppearanceChanged();
			}];
	}

	// The NSWindow of a top-level or popup view; its content is a new surface view, or the given one.
	void createWindow(NativeView* native, ViewKind kind, const UiRect& frame, const TopLevelStyle& style, NppMacView* content)
	{
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
		if (!content)
			content = createSurfaceView(native, NSMakeRect(0, 0, contentRect.size.width, contentRect.size.height));
		window.contentView = content;
		content.hidden = NO;
		// A test run's windows are drawn (snapshots render the views) but never seen or clicked:
		// they must not get in the way of whoever is using the Mac.
		if (std::getenv("NPPMAC_TEST_RUN"))
		{
			window.alphaValue = 0;
			window.ignoresMouseEvents = YES;
			window.hasShadow = NO;
		}
		native->_kind = kind;
		native->_window = window;
		native->_view = content;
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

		createWindow(native, kind, frame, style, nil);
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
		for (NSScroller* scroller : native->_scrollers)
		{
			if (scroller)
				static_cast<NppMacScroller*>(scroller)->_native = nullptr;
		}
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
		releaseSurface(native);
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
				hideTree(native->_window.contentView);
				return;
			}
			else if (activate)
			{
				nppmac::platform::activateApplication();
				if (g_testRun)
					[native->_window orderFront:nil];
				else
					[native->_window makeKeyAndOrderFront:nil];
			}
			else
			{
				[native->_window orderFront:nil];
			}
			showTree(native->_window.contentView);
			return;
		}
		native->_view.hidden = !visible;
		if (visible && isShown(native->_view))
			showTree(native->_view);
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

	void setParent(NativeView* native, NativeView* newParent, const UiRect& frame, const TopLevelStyle& style, bool visible)
	{
		if (!native)
			return;
		NSView* view = native->_view;
		if (native->_window)
		{
			if (!newParent)
				return;
			// A top-level window becomes a child: its content view moves, its NSWindow goes.
			NppMacWindow* window = static_cast<NppMacWindow*>(native->_window);
			window->_native = nullptr;
			window.delegate = nil;
			window.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
			[window orderOut:nil];
			[window close];
			native->_window = nil;
			native->_kind = ViewKind::child;
		}
		[view removeFromSuperview];
		if (newParent && newParent->_view)
		{
			// A content view resizes with its window; a child is placed by the window manager.
			view.autoresizingMask = NSViewNotSizable;
			view.hidden = !visible;
			[newParent->_view addSubview:view positioned:NSWindowAbove relativeTo:nil];
			return;
		}
		// A child becomes a top-level window.
		if (!native->_adopted && [view isKindOfClass:NppMacView.class])
		{
			createWindow(native, ViewKind::topLevel, frame, style, static_cast<NppMacView*>(view));
			if (visible)
				[native->_window orderFront:nil];
		}
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
		if (window && !window.isKeyWindow && window.isVisible && !g_testRun)
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

	TextInputResult interpretKey(NativeView* native, void* nsEvent)
	{
		TextInputResult result;
		if (!native || !nsEvent || ![native->_view isKindOfClass:NppMacView.class])
			return result;
		NppMacView* view = static_cast<NppMacView*>(native->_view);
		NSEvent* event = (__bridge NSEvent*)nsEvent;
		// Input methods serve the active program's current input context only; otherwise the key is
		// an ordinary one.
		NSTextInputContext* context = view.inputContext;
		if (!context || NSTextInputContext.currentInputContext != context)
			return result;
		// Return, Tab, Escape, Backspace, arrows and function keys stay ordinary keys, unless something
		// is being composed: then the input method has them (Return commits, Escape cancels).
		NSString* characters = event.characters ?: @"";
		const unichar first = characters.length > 0 ? [characters characterAtIndex:0] : 0;
		const bool commandKey = characters.length > 0 && (first < 0x20 || first == 0x7F || (first >= 0xF700 && first <= 0xF8FF));
		if (commandKey && !view.hasMarkedText)
			return result;
		// Text it enters while handling the key is collected here; text it enters later (input methods
		// may answer asynchronously) arrives through nativeInsertText.
		g_textInput = TextInputCollector{true};
		[context handleEvent:event];
		result._consumed = true;
		result._committed = std::move(g_textInput._committed);
		g_textInput = TextInputCollector{};
		return result;
	}

	void testComposition(NativeView* native, const std::u16string& composing, const std::u16string& committed)
	{
		if (!native || ![native->_view isKindOfClass:NppMacView.class])
			return;
		NppMacView* view = static_cast<NppMacView*>(native->_view);
		NSString* marked = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(composing.data()) length:composing.size()];
		[view setMarkedText:marked selectedRange:NSMakeRange(marked.length, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
		if (!committed.empty())
		{
			NSString* text = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(committed.data()) length:committed.size()];
			[view insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
		}
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

	NSAppearance* toNSAppearance(Appearance appearance)
	{
		switch (appearance)
		{
			case Appearance::light:
				return [NSAppearance appearanceNamed:NSAppearanceNameAqua];
			case Appearance::dark:
				return [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
			default:
			{
				// The system's, unless a test run chose one (NPPMAC_TEST_APPEARANCE).
				const char* test = std::getenv("NPPMAC_TEST_APPEARANCE");
				if (!test)
					return nil;
				return [NSAppearance appearanceNamed:std::strcmp(test, "dark") == 0 ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
			}
		}
	}

	void setApplicationAppearance(Appearance appearance)
	{
		NSApp.appearance = toNSAppearance(appearance);
	}

	void setWindowAppearance(NativeView* native, Appearance appearance)
	{
		if (native && native->_window)
			native->_window.appearance = appearance == Appearance::system ? nil : toNSAppearance(appearance);
	}

	int scrollBarThickness()
	{
		return static_cast<int>(std::lround([NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy]));
	}

	// The scroll bars sit at the right and bottom edges, above the view's children.
	void layoutScrollers(NativeView* native)
	{
		if (!native || !native->_view)
			return;
		const NSRect bounds = native->_view.bounds;
		const CGFloat thickness = scrollBarThickness();
		const bool horizontal = native->_scrollStates[0]._visible;
		const bool vertical = native->_scrollStates[1]._visible;
		if (NSScroller* scroller = native->_scrollers[1])
		{
			scroller.frame = NSMakeRect(bounds.size.width - thickness, 0, thickness, std::max<CGFloat>(0, bounds.size.height - (horizontal ? thickness : 0)));
			scroller.hidden = !vertical;
			[native->_view addSubview:scroller positioned:NSWindowAbove relativeTo:nil];
		}
		if (NSScroller* scroller = native->_scrollers[0])
		{
			scroller.frame = NSMakeRect(0, bounds.size.height - thickness, std::max<CGFloat>(0, bounds.size.width - (vertical ? thickness : 0)), thickness);
			scroller.hidden = !horizontal;
			[native->_view addSubview:scroller positioned:NSWindowAbove relativeTo:nil];
		}
	}

	void setScrollBar(NativeView* native, bool vertical, const ScrollBarState& state)
	{
		if (!native || native->_adopted || !native->_view)
			return;
		const int index = vertical ? 1 : 0;
		native->_scrollStates[index] = state;
		NSScroller* scroller = native->_scrollers[index];
		if (!scroller && state._visible)
		{
			// The frame's shape tells NSScroller which way it scrolls.
			NppMacScroller* created = [[NppMacScroller alloc] initWithFrame:vertical ? NSMakeRect(0, 0, 15, 100) : NSMakeRect(0, 0, 100, 15)];
			created->_native = native;
			created->_vertical = vertical;
			created.scrollerStyle = NSScrollerStyleLegacy;
			created.target = created;
			created.action = @selector(scrolled:);
			native->_scrollers[index] = created;
			scroller = created;
		}
		if (!scroller)
			return;
		scroller.enabled = state._enabled && state._proportion < 1;
		scroller.doubleValue = std::clamp(state._value, 0.0, 1.0);
		scroller.knobProportion = std::clamp(state._proportion, 0.0, 1.0);
		layoutScrollers(native);
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
