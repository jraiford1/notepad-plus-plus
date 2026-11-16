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

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <cmath>
#include <vector>

#include "ScintillaView.h"
#include "../ScintillaBridge.h"

namespace
{
	struct Registered
	{
		__weak ScintillaView* _view = nil;
		intptr_t _window = 0;
		nppmac::scintilla::ContextMenuFunc _contextMenu = nullptr;
	};

	std::vector<Registered>& registered()
	{
		static auto* list = new std::vector<Registered>;
		return *list;
	}

	id g_rightMouseMonitor = nil;

	// Notepad++ shows its own editor context menu on WM_CONTEXTMENU; Scintilla's popup is off.
	void installContextMenuMonitor()
	{
		if (g_rightMouseMonitor)
			return;
		g_rightMouseMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseDown | NSEventMaskLeftMouseDown
			handler:^NSEvent*(NSEvent* event) {
				const bool rightClick = event.type == NSEventTypeRightMouseDown
					|| (event.type == NSEventTypeLeftMouseDown && (event.modifierFlags & NSEventModifierFlagControl));
				if (!rightClick || !event.window)
					return event;
				NSView* hit = [event.window.contentView hitTest:[event.window.contentView.superview convertPoint:event.locationInWindow fromView:nil]];
				for (const Registered& entry : registered())
				{
					ScintillaView* view = entry._view;
					if (view && hit && [hit isDescendantOf:view] && entry._contextMenu)
					{
						const NSPoint screen = [event.window convertPointToScreen:event.locationInWindow];
						const double primaryHeight = NSScreen.screens.count > 0 ? NSScreen.screens[0].frame.size.height : 0;
						entry._contextMenu(entry._window, static_cast<int>(screen.x), static_cast<int>(primaryHeight - screen.y));
						return nil;
					}
				}
				return event;
			}];
	}

	// AppKit scrolls a view at once with the wheel ("responsive scrolling") unless the view handles
	// scrollWheel itself, as Scintilla's does (only to pass it on, or to zoom with Command): then it eases
	// each wheel event in over a quarter of a second instead, so the editor trailed the wheel, and a short
	// document's little scrolling crawled. Scintilla's view draws any part of itself it is asked to, which
	// is what responsive scrolling needs, so it says it can.
	void allowResponsiveScrolling()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		if (Class content = NSClassFromString(@"SCIContentView"))
			class_addMethod(object_getClass(content), @selector(isCompatibleWithResponsiveScrolling),
				imp_implementationWithBlock(^BOOL(id) { return YES; }), "c@:");
	}

	// Scintilla's view rounds each vertical scroll position to the nearest whole line (adjustScroll:). A
	// slow scroll, a few points an event, was rounded back each time, so the text never moved, and a
	// faster one moved a line at a time. Scintilla draws in the document's coordinates, so it can stop
	// part-way through a line, as macOS's text views do (its overscroll at the ends already did). The
	// position is rounded only to whole points, as Scintilla does across (parts of points left debris on
	// Retina screens), and what the rounding leaves is carried to the next event, so that a scroll of
	// less than a point an event moves the text too, as fast as the fingers.
	__weak NSView* g_scrolledView = nil; // the editor's view the rest is about
	CGFloat g_leftoverX = 0;
	CGFloat g_leftoverY = 0;
	NSPoint g_allowed{NAN, NAN}; // where adjustScroll: last let it go

	CGFloat toPoint(CGFloat value, CGFloat& leftover)
	{
		const CGFloat wanted = value + leftover;
		const CGFloat point = std::nearbyint(wanted);
		leftover = wanted - point;
		return point;
	}

	NSRect adjustScroll(id self, SEL, NSRect proposed)
	{
		NSView* view = self;
		if (g_scrolledView != view)
		{
			g_scrolledView = view;
			g_leftoverX = g_leftoverY = 0;
		}
		const NSSize size = view.bounds.size;
		NSRect rect = proposed;
		// Only inside the document: outside is the overscroll's.
		if (rect.origin.y > 0 && NSMaxY(rect) < size.height)
			rect.origin.y = toPoint(rect.origin.y, g_leftoverY);
		else
			g_leftoverY = 0;
		if (rect.origin.x > 0 && NSMaxX(rect) < size.width)
			rect.origin.x = toPoint(rect.origin.x, g_leftoverX);
		else
			g_leftoverX = 0;
		g_allowed = rect.origin;
		return rect;
	}

	// AppKit moves the editor's view where adjustScroll: let it go. Scintilla moves it itself
	// (scrollToPoint:) when it gives its document a new size, as when a line longer than any shown so far
	// comes into view: back to the start of the line at its top, which while scrolling was a jump back.
	// The view stays where it is in that line; a move to another line is made.
	using ScrollToPoint = void (*)(id, SEL, NSPoint);
	ScrollToPoint g_scrollToPoint = nullptr;
	Class g_contentClass = nil; // Scintilla's view of the text

	void scrollToPoint(NSClipView* self, SEL selector, NSPoint point)
	{
		if (!NSEqualPoints(point, g_allowed) && [self.documentView isKindOfClass:g_contentClass])
		{
			NSView* editor = self.superview.superview;
			const CGFloat y = self.bounds.origin.y;
			static Class editorClass = NSClassFromString(@"ScintillaView");
			const CGFloat lineHeight = editorClass && [editor isKindOfClass:editorClass]
				? static_cast<CGFloat>([static_cast<ScintillaView*>(editor) message:SCI_TEXTHEIGHT wParam:0 lParam:0]) : 0;
			if (lineHeight > 0 && point.y != y && point.y == std::floor(y / lineHeight) * lineHeight)
				point.y = y;
		}
		g_allowed = NSMakePoint(NAN, NAN);
		g_scrollToPoint(self, selector, point);
	}

	void scrollByPoints()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		Class content = NSClassFromString(@"SCIContentView");
		Method method = content ? class_getInstanceMethod(content, @selector(adjustScroll:)) : nullptr;
		g_contentClass = content;
		if (!method)
			return;
		class_replaceMethod(content, @selector(adjustScroll:), reinterpret_cast<IMP>(adjustScroll), method_getTypeEncoding(method));
		Method clipMethod = class_getInstanceMethod(NSClipView.class, @selector(scrollToPoint:));
		g_scrollToPoint = reinterpret_cast<ScrollToPoint>(class_replaceMethod(NSClipView.class, @selector(scrollToPoint:),
			reinterpret_cast<IMP>(scrollToPoint), method_getTypeEncoding(clipMethod)));
		if (!g_scrollToPoint)
			g_scrollToPoint = reinterpret_cast<ScrollToPoint>(method_getImplementation(clipMethod));
	}
}

namespace nppmac::scintilla
{
	void* createView(intptr_t window, NotifyFunc notify, ContextMenuFunc contextMenu)
	{
		allowResponsiveScrolling();
		scrollByPoints();
		ScintillaView* view = [[ScintillaView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
		// The line numbers' margin draws the whole of a line that is part-way out of the editor; it
		// stays inside the editor, as the text does (views do not clip by default since macOS 14).
		view.scrollView.verticalRulerView.clipsToBounds = YES;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		[view registerNotifyCallback:window value:notify];
#pragma clang diagnostic pop
		registered().push_back({view, window, contextMenu});
		installContextMenuMonitor();
		return (__bridge_retained void*)view;
	}

	void forgetView(void* view)
	{
		auto& list = registered();
		for (auto it = list.begin(); it != list.end();)
		{
			if (!it->_view || (__bridge void*)it->_view == view)
				it = list.erase(it);
			else
				++it;
		}
	}

	intptr_t message(void* view, unsigned message, uintptr_t wParam, intptr_t lParam)
	{
		return [(__bridge ScintillaView*)view message:message wParam:wParam lParam:lParam];
	}

	void* printSurface(void* canvasHandle)
	{
		return canvasHandle; // the CGContext, as Scintilla's Cocoa layer draws on it
	}

	void insertText(void* view, const std::u16string& text)
	{
		NSString* string = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
		[(__bridge ScintillaView*)view insertText:string];
	}
}
