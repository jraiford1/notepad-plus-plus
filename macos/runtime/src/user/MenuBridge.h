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

// The AppKit side of menus: the menu bar and popup menus as NSMenus whose contents are asked for
// each time a menu opens, so they always show the program's current state. Plain C++ types only.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nppmac::ui
{
	struct MenuItemDescription
	{
		std::u16string _title;       // without '&' markers
		std::u16string _shortcut;    // the text after a tab ("Ctrl+O"), shown on the right
		unsigned _id = 0;
		uintptr_t _submenu = 0;      // an HMENU, or 0
		bool _separator = false;
		bool _checked = false;
		bool _radio = false;
		bool _enabled = true;
		bool _default = false;
	};

	struct MenuCallbacks
	{
		// Called before a menu opens (WM_INITMENUPOPUP), then for its items.
		void (*_willOpen)(uintptr_t menu, void* context) = nullptr;
		std::vector<MenuItemDescription> (*_items)(uintptr_t menu) = nullptr;
		// A menu bar item was chosen.
		void (*_chosen)(unsigned id, void* context) = nullptr;
		// The application menu's commands.
		void (*_about)(void* context) = nullptr;
		void (*_preferences)(void* context) = nullptr;
	};

	// Replaces the application's menu bar with the given menu's popups, after the standard
	// application menu. context identifies the owner window in the callbacks.
	void setMenuBar(uintptr_t menu, const MenuCallbacks& callbacks, void* context, const std::u16string& applicationName);
	void clearMenuBar(void* context);

	// Shows a popup menu at a screen point (Windows coordinates) and waits; returns the chosen id, or 0.
	unsigned trackPopupMenu(uintptr_t menu, int screenX, int screenY, const MenuCallbacks& callbacks, void* context);
}
