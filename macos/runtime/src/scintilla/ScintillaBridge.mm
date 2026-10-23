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

#include <vector>

#include "ScintillaView.h"
#include "ScintillaBridge.h"

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
}

namespace nppmac::scintilla
{
	void* createView(intptr_t window, NotifyFunc notify, ContextMenuFunc contextMenu)
	{
		ScintillaView* view = [[ScintillaView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
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

	void insertText(void* view, const std::u16string& text)
	{
		NSString* string = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
		[(__bridge ScintillaView*)view insertText:string];
	}
}
