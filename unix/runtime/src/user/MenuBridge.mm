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

#include "MenuBridge.h"

using nppmac::ui::MenuCallbacks;
using nppmac::ui::MenuItemDescription;

namespace
{
	NSString* toNSString(const std::u16string& text)
	{
		return [NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
	}

	// "Ctrl+Shift+O" -> key equivalent "o" with Command and Shift, shown only: keys reach the program
	// through its own accelerators, never through NSMenu.
	void applyShortcut(NSMenuItem* item, const std::u16string& shortcut)
	{
		if (shortcut.empty())
			return;
		NSString* text = toNSString(shortcut);
		NSArray<NSString*>* parts = [text componentsSeparatedByString:@"+"];
		if (parts.count == 0)
			return;
		NSEventModifierFlags flags = 0;
		for (NSUInteger i = 0; i + 1 < parts.count; ++i)
		{
			NSString* modifier = parts[i].lowercaseString;
			if ([modifier isEqualToString:@"ctrl"])
				flags |= NSEventModifierFlagCommand;
			else if ([modifier isEqualToString:@"shift"])
				flags |= NSEventModifierFlagShift;
			else if ([modifier isEqualToString:@"alt"])
				flags |= NSEventModifierFlagOption;
		}
		NSString* key = parts.lastObject;
		if (key.length == 0 && parts.count >= 2)
			key = @"+"; // "Ctrl++"
		NSString* equivalent = nil;
		if (key.length == 1)
		{
			equivalent = key.lowercaseString;
		}
		else
		{
			static NSDictionary<NSString*, NSString*>* named = @{
				@"f1": [NSString stringWithFormat:@"%C", (unichar)NSF1FunctionKey], @"f2": [NSString stringWithFormat:@"%C", (unichar)NSF2FunctionKey],
				@"f3": [NSString stringWithFormat:@"%C", (unichar)NSF3FunctionKey], @"f4": [NSString stringWithFormat:@"%C", (unichar)NSF4FunctionKey],
				@"f5": [NSString stringWithFormat:@"%C", (unichar)NSF5FunctionKey], @"f6": [NSString stringWithFormat:@"%C", (unichar)NSF6FunctionKey],
				@"f7": [NSString stringWithFormat:@"%C", (unichar)NSF7FunctionKey], @"f8": [NSString stringWithFormat:@"%C", (unichar)NSF8FunctionKey],
				@"f9": [NSString stringWithFormat:@"%C", (unichar)NSF9FunctionKey], @"f10": [NSString stringWithFormat:@"%C", (unichar)NSF10FunctionKey],
				@"f11": [NSString stringWithFormat:@"%C", (unichar)NSF11FunctionKey], @"f12": [NSString stringWithFormat:@"%C", (unichar)NSF12FunctionKey],
				@"tab": @"\t", @"enter": @"\r", @"esc": @"\x1b", @"space": @" ",
				@"del": [NSString stringWithFormat:@"%C", (unichar)NSDeleteFunctionKey], @"delete": [NSString stringWithFormat:@"%C", (unichar)NSDeleteFunctionKey],
				@"backspace": @"\b", @"home": [NSString stringWithFormat:@"%C", (unichar)NSHomeFunctionKey],
				@"end": [NSString stringWithFormat:@"%C", (unichar)NSEndFunctionKey], @"up": [NSString stringWithFormat:@"%C", (unichar)NSUpArrowFunctionKey],
				@"down": [NSString stringWithFormat:@"%C", (unichar)NSDownArrowFunctionKey], @"left": [NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey],
				@"right": [NSString stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey],
				@"page up": [NSString stringWithFormat:@"%C", (unichar)NSPageUpFunctionKey], @"page down": [NSString stringWithFormat:@"%C", (unichar)NSPageDownFunctionKey],
			};
			equivalent = named[key.lowercaseString];
		}
		if (!equivalent)
			return;
		item.keyEquivalent = equivalent;
		item.keyEquivalentModifierMask = flags;
	}
}

@interface NppMacMenuDelegate : NSObject <NSMenuDelegate>
{
@public
	uintptr_t _menu;
	MenuCallbacks _callbacks;
	void* _context;
	BOOL _popup;
	unsigned _chosen;
	__unsafe_unretained NppMacMenuDelegate* _root; // popups: the delegate that reports the choice
}
@end

@implementation NppMacMenuDelegate

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	if (_callbacks._willOpen)
		_callbacks._willOpen(_menu, _context);
	[self fill:menu with:_callbacks._items(_menu)];
}

- (void)fill:(NSMenu*)menu with:(const std::vector<MenuItemDescription>&)items
{
	[menu removeAllItems];
	for (const MenuItemDescription& description : items)
	{
		if (description._separator)
		{
			[menu addItem:NSMenuItem.separatorItem];
			continue;
		}
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:toNSString(description._title) action:nil keyEquivalent:@""];
		item.tag = description._id;
		item.state = description._checked ? NSControlStateValueOn : NSControlStateValueOff;
		item.enabled = description._enabled;
		applyShortcut(item, description._shortcut);
		if (description._submenu)
		{
			NSMenu* submenu = [[NSMenu alloc] initWithTitle:item.title];
			submenu.autoenablesItems = NO;
			NppMacMenuDelegate* delegate = [[NppMacMenuDelegate alloc] init];
			delegate->_menu = description._submenu;
			delegate->_callbacks = _callbacks;
			delegate->_context = _context;
			delegate->_popup = _popup;
			delegate->_root = _root ?: self;
			submenu.delegate = delegate;
			// The menu keeps its delegate alive through the item.
			item.representedObject = delegate;
			item.submenu = submenu;
		}
		else
		{
			item.target = self;
			item.action = @selector(choose:);
		}
		[menu addItem:item];
	}
}

- (void)choose:(NSMenuItem*)item
{
	_chosen = static_cast<unsigned>(item.tag);
	if (_popup)
	{
		// The popup's root delegate reports the choice after the menu closes.
		NppMacMenuDelegate* root = _root ?: self;
		root->_chosen = _chosen;
		return;
	}
	if (_callbacks._chosen)
		_callbacks._chosen(_chosen, _context);
}

@end

@interface NppMacApplicationMenuTarget : NSObject
{
@public
	MenuCallbacks _callbacks;
	void* _context;
}
@end

@implementation NppMacApplicationMenuTarget

- (void)about:(id)sender
{
	if (_callbacks._about)
		_callbacks._about(_context);
}

- (void)preferences:(id)sender
{
	if (_callbacks._preferences)
		_callbacks._preferences(_context);
}

@end

namespace
{
	NSMutableArray* g_retained = nil;
	void* g_menuBarOwner = nullptr;
}

namespace nppmac::ui
{
	void setMenuBar(uintptr_t menu, const MenuCallbacks& callbacks, void* context, const std::u16string& applicationName)
	{
		if (!g_retained)
			g_retained = [NSMutableArray array];
		[g_retained removeAllObjects];
		g_menuBarOwner = context;

		NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
		NSString* name = toNSString(applicationName);

		// The application menu: what every Mac app has.
		NppMacApplicationMenuTarget* target = [[NppMacApplicationMenuTarget alloc] init];
		target->_callbacks = callbacks;
		target->_context = context;
		[g_retained addObject:target];
		NSMenu* applicationMenu = [[NSMenu alloc] initWithTitle:name];
		NSMenuItem* about = [applicationMenu addItemWithTitle:[NSString stringWithFormat:@"About %@", name] action:@selector(about:) keyEquivalent:@""];
		about.target = target;
		[applicationMenu addItem:NSMenuItem.separatorItem];
		NSMenuItem* preferences = [applicationMenu addItemWithTitle:@"Settings…" action:@selector(preferences:) keyEquivalent:@","];
		preferences.target = target;
		[applicationMenu addItem:NSMenuItem.separatorItem];
		NSMenu* services = [[NSMenu alloc] initWithTitle:@"Services"];
		NSMenuItem* servicesItem = [applicationMenu addItemWithTitle:@"Services" action:nil keyEquivalent:@""];
		servicesItem.submenu = services;
		NSApp.servicesMenu = services;
		[applicationMenu addItem:NSMenuItem.separatorItem];
		[applicationMenu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", name] action:@selector(hide:) keyEquivalent:@"h"];
		NSMenuItem* hideOthers = [applicationMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
		hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
		[applicationMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
		[applicationMenu addItem:NSMenuItem.separatorItem];
		[applicationMenu addItemWithTitle:[NSString stringWithFormat:@"Quit %@", name] action:@selector(terminate:) keyEquivalent:@"q"];
		NSMenuItem* applicationItem = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
		applicationItem.submenu = applicationMenu;
		[bar addItem:applicationItem];

		// The program's menus, each filled when it opens.
		NppMacMenuDelegate* top = [[NppMacMenuDelegate alloc] init];
		top->_menu = menu;
		top->_callbacks = callbacks;
		top->_context = context;
		top->_popup = NO;
		[g_retained addObject:top];
		for (const MenuItemDescription& description : callbacks._items(menu))
		{
			if (description._separator || !description._submenu)
				continue;
			NSMenu* submenu = [[NSMenu alloc] initWithTitle:toNSString(description._title)];
			submenu.autoenablesItems = NO;
			NppMacMenuDelegate* delegate = [[NppMacMenuDelegate alloc] init];
			delegate->_menu = description._submenu;
			delegate->_callbacks = callbacks;
			delegate->_context = context;
			delegate->_popup = NO;
			submenu.delegate = delegate;
			[g_retained addObject:delegate];
			NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:submenu.title action:nil keyEquivalent:@""];
			item.submenu = submenu;
			[bar addItem:item];
			// Fill it once now, so that the Help menu's search and VoiceOver see its items.
			[delegate fill:submenu with:callbacks._items(description._submenu)];
		}
		NSApp.mainMenu = bar;
	}

	void clearMenuBar(void* context)
	{
		if (context != g_menuBarOwner)
			return;
		g_menuBarOwner = nullptr;
		NSApp.mainMenu = [[NSMenu alloc] initWithTitle:@""];
		[g_retained removeAllObjects];
	}

	unsigned trackPopupMenu(uintptr_t menu, int screenX, int screenY, const MenuCallbacks& callbacks, void* context)
	{
		NppMacMenuDelegate* root = [[NppMacMenuDelegate alloc] init];
		root->_menu = menu;
		root->_callbacks = callbacks;
		root->_context = context;
		root->_popup = YES;
		root->_chosen = 0;
		NSMenu* popup = [[NSMenu alloc] initWithTitle:@""];
		popup.autoenablesItems = NO;
		if (callbacks._willOpen)
			callbacks._willOpen(menu, context);
		root->_root = nil;
		[root fill:popup with:callbacks._items(menu)];
		const double primaryHeight = NSScreen.screens.count > 0 ? NSScreen.screens[0].frame.size.height : 0;
		[popup popUpMenuPositioningItem:nil atLocation:NSMakePoint(screenX, primaryHeight - screenY) inView:nil];
		return root->_chosen;
	}
}
