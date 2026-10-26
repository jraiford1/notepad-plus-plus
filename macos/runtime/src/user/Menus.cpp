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

// user32 menus and accelerators. Menus are kept as Win32 data; the menu bar and popup menus are
// NSMenus (MenuBridge.mm) filled from that data each time they open.

#include "MenuBridge.h"
#include "Resources.h"
#include "Window.h"
#include "nppmac/Startup.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace nppmac::user
{
	namespace
	{
		struct MenuItem
		{
			UINT _type = MFT_STRING;
			UINT _state = 0;
			UINT _id = 0;
			std::u16string _text;
			HMENU _submenu = nullptr;
			ULONG_PTR _data = 0;
			HBITMAP _bitmap = nullptr;
		};

		struct Menu
		{
			std::vector<MenuItem> _items;
			bool _popup = false;
			HWND _owner = nullptr; // the window whose menu bar it is
			MENUINFO _info{};
		};

		std::unordered_set<Menu*>& menus()
		{
			static auto* set = new std::unordered_set<Menu*>;
			return *set;
		}

		Menu* toMenu(HMENU handle)
		{
			auto* menu = reinterpret_cast<Menu*>(handle);
			return menus().count(menu) ? menu : nullptr;
		}

		HMENU newMenu(bool popup)
		{
			auto* menu = new Menu;
			menu->_popup = popup;
			menus().insert(menu);
			return reinterpret_cast<HMENU>(menu);
		}

		// Finds an item by command id (searching submenus) or by position.
		MenuItem* findItem(Menu* menu, UINT item, UINT flags, Menu** owner = nullptr, size_t* index = nullptr)
		{
			if (!menu)
				return nullptr;
			if (flags & MF_BYPOSITION)
			{
				if (item >= menu->_items.size())
					return nullptr;
				if (owner)
					*owner = menu;
				if (index)
					*index = item;
				return &menu->_items[item];
			}
			for (size_t i = 0; i < menu->_items.size(); ++i)
			{
				MenuItem& candidate = menu->_items[i];
				if (!candidate._submenu && candidate._id == item && !(candidate._type & MFT_SEPARATOR))
				{
					if (owner)
						*owner = menu;
					if (index)
						*index = i;
					return &candidate;
				}
				if (candidate._submenu)
				{
					if (MenuItem* found = findItem(toMenu(candidate._submenu), item, flags, owner, index))
						return found;
					// A popup item can be addressed by its submenu handle as id.
					if (reinterpret_cast<UINT_PTR>(candidate._submenu) == item)
					{
						if (owner)
							*owner = menu;
						if (index)
							*index = i;
						return &candidate;
					}
				}
			}
			return nullptr;
		}

		MenuItem itemFromFlags(UINT flags, UINT_PTR idOrSubmenu, LPCWSTR text)
		{
			MenuItem item;
			if (flags & MF_SEPARATOR)
				item._type = MFT_SEPARATOR;
			else if (flags & MF_OWNERDRAW)
				item._type = MFT_OWNERDRAW;
			else if (flags & MF_BITMAP)
				item._type = MFT_BITMAP;
			if (flags & MF_POPUP)
				item._submenu = reinterpret_cast<HMENU>(idOrSubmenu);
			else
				item._id = static_cast<UINT>(idOrSubmenu);
			if (flags & MF_CHECKED)
				item._state |= MFS_CHECKED;
			if (flags & (MF_GRAYED | MF_DISABLED))
				item._state |= MFS_DISABLED;
			if (flags & MF_DEFAULT)
				item._state |= MFS_DEFAULT;
			if (flags & MF_MENUBARBREAK)
				item._type |= MFT_MENUBARBREAK;
			if (flags & MF_RIGHTJUSTIFY)
				item._type |= MFT_RIGHTJUSTIFY;
			if (!(flags & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW)) && text)
				item._text = text;
			if (flags & MF_OWNERDRAW)
				item._data = reinterpret_cast<ULONG_PTR>(text);
			return item;
		}

		// --- Native menus -------------------------------------------------------------------------

		struct AppMenuCommands
		{
			UINT _about = 0;
			UINT _preferences = 0;
		};

		AppMenuCommands g_appMenuCommands;

		void menuWillOpen(uintptr_t menuHandle, void* context)
		{
			HWND owner = static_cast<HWND>(context);
			if (!IsWindow(owner))
				return;
			// The position of a popup in its parent is not known here; Notepad++ only uses the handle.
			SendMessageW(owner, WM_INITMENUPOPUP, static_cast<WPARAM>(menuHandle), 0);
		}

		std::vector<ui::MenuItemDescription> menuItems(uintptr_t menuHandle)
		{
			std::vector<ui::MenuItemDescription> result;
			Menu* menu = toMenu(reinterpret_cast<HMENU>(menuHandle));
			if (!menu)
				return result;
			for (const MenuItem& item : menu->_items)
			{
				ui::MenuItemDescription description;
				description._separator = (item._type & MFT_SEPARATOR) != 0;
				std::u16string text = item._text;
				const size_t tab = text.find(u'\t');
				if (tab != std::u16string::npos)
				{
					description._shortcut = text.substr(tab + 1);
					text = text.substr(0, tab);
				}
				// "&File" -> "File", "&&" -> "&".
				std::u16string title;
				for (size_t i = 0; i < text.size(); ++i)
				{
					if (text[i] == u'&' && i + 1 < text.size())
						++i;
					title.push_back(text[i]);
				}
				description._title = title;
				description._id = item._id;
				description._submenu = reinterpret_cast<uintptr_t>(item._submenu);
				description._checked = (item._state & MFS_CHECKED) != 0;
				description._radio = (item._type & MFT_RADIOCHECK) != 0;
				description._enabled = !(item._state & MFS_DISABLED);
				description._default = (item._state & MFS_DEFAULT) != 0;
				result.push_back(description);
			}
			return result;
		}

		void menuChosen(unsigned id, void* context)
		{
			HWND owner = static_cast<HWND>(context);
			if (IsWindow(owner))
				PostMessageW(owner, WM_COMMAND, MAKEWPARAM(id, 0), 0);
		}

		void aboutChosen(void* context)
		{
			if (g_appMenuCommands._about)
				menuChosen(g_appMenuCommands._about, context);
		}

		void preferencesChosen(void* context)
		{
			if (g_appMenuCommands._preferences)
				menuChosen(g_appMenuCommands._preferences, context);
		}

		ui::MenuCallbacks callbacks()
		{
			ui::MenuCallbacks result;
			result._willOpen = menuWillOpen;
			result._items = menuItems;
			result._chosen = menuChosen;
			result._about = aboutChosen;
			result._preferences = preferencesChosen;
			return result;
		}

		void refreshMenuBar(Menu* menu)
		{
			if (!menu || !menu->_owner || !IsWindow(menu->_owner))
				return;
			Window* window = fromHandle(menu->_owner);
			// The application name is the main window's class-independent product name.
			ui::setMenuBar(reinterpret_cast<uintptr_t>(menu), callbacks(), menu->_owner, nppmac::applicationName());
			(void)window;
		}

		// --- Menu templates -----------------------------------------------------------------------

		const BYTE* readString(const BYTE* p, std::u16string& text)
		{
			for (;;)
			{
				char16_t ch = 0;
				std::memcpy(&ch, p, 2);
				p += 2;
				if (ch == 0)
					break;
				text.push_back(ch);
			}
			return p;
		}

		// MENU templates: items of flags, id (not for popups), text; MF_END ends a level.
		const BYTE* parseMenu(const BYTE* p, const BYTE* end, HMENU target)
		{
			while (p + 2 <= end)
			{
				WORD flags = 0;
				std::memcpy(&flags, p, 2);
				p += 2;
				WORD id = 0;
				if (!(flags & MF_POPUP))
				{
					std::memcpy(&id, p, 2);
					p += 2;
				}
				std::u16string text;
				p = readString(p, text);
				if (flags & MF_POPUP)
				{
					HMENU submenu = CreatePopupMenu();
					p = parseMenu(p, end, submenu);
					AppendMenuW(target, (flags & ~MF_END) | MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(submenu), text.c_str());
				}
				else if (flags == 0 && id == 0 && text.empty())
				{
					AppendMenuW(target, MF_SEPARATOR, 0, nullptr);
				}
				else
				{
					AppendMenuW(target, flags & ~MF_END, id, text.c_str());
				}
				if (flags & MF_END)
					break;
			}
			return p;
		}

		// MENUEX templates: dwType, dwState, dwId, wFlags (0x01 popup, 0x80 last), text,
		// DWORD alignment, and for popups a help id before the children.
		const BYTE* parseMenuEx(const BYTE* base, const BYTE* p, const BYTE* end, HMENU target)
		{
			while (p + 14 <= end)
			{
				DWORD type = 0;
				DWORD state = 0;
				DWORD id = 0;
				WORD flags = 0;
				std::memcpy(&type, p, 4);
				std::memcpy(&state, p + 4, 4);
				std::memcpy(&id, p + 8, 4);
				std::memcpy(&flags, p + 12, 2);
				p += 14;
				std::u16string text;
				p = readString(p, text);
				p = base + ((static_cast<size_t>(p - base) + 3) & ~static_cast<size_t>(3));
				MENUITEMINFOW info{};
				info.cbSize = sizeof(info);
				info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_STRING;
				info.fType = type;
				info.fState = state;
				info.wID = id;
				info.dwTypeData = text.data();
				if (flags & 0x01)
				{
					p += 4; // help id
					HMENU submenu = CreatePopupMenu();
					p = parseMenuEx(base, p, end, submenu);
					info.fMask |= MIIM_SUBMENU;
					info.hSubMenu = submenu;
				}
				InsertMenuItemW(target, GetMenuItemCount(target), TRUE, &info);
				if (flags & 0x80)
					break;
			}
			return p;
		}
	}

	void detachMenu(HWND hwnd)
	{
		for (Menu* menu : menus())
		{
			if (menu->_owner == hwnd)
			{
				menu->_owner = nullptr;
				ui::clearMenuBar(hwnd);
			}
		}
	}
}

void nppmac::setApplicationMenuCommands(unsigned about, unsigned preferences)
{
	nppmac::user::g_appMenuCommands = {about, preferences};
}

using namespace nppmac::user;
namespace ui = nppmac::ui;

// --- Creation -------------------------------------------------------------------------------------

HMENU WINAPI CreateMenu()
{
	return newMenu(false);
}

HMENU WINAPI CreatePopupMenu()
{
	return newMenu(true);
}

BOOL WINAPI DestroyMenu(HMENU handle)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	for (const MenuItem& item : menu->_items)
	{
		if (item._submenu)
			DestroyMenu(item._submenu);
	}
	if (menu->_owner)
		ui::clearMenuBar(menu->_owner);
	menus().erase(menu);
	delete menu;
	return TRUE;
}

BOOL WINAPI IsMenu(HMENU handle)
{
	return toMenu(handle) ? TRUE : FALSE;
}

HMENU WINAPI LoadMenuIndirectW(const MENUTEMPLATE* menuTemplate)
{
	const auto* base = reinterpret_cast<const BYTE*>(menuTemplate);
	WORD version = 0;
	WORD headerSize = 0;
	std::memcpy(&version, base, 2);
	std::memcpy(&headerSize, base + 2, 2);
	HMENU menu = CreateMenu();
	// No size is given: the template ends where its last item says so.
	const BYTE* end = base + 0x100000;
	if (version == 1)
		parseMenuEx(base, base + 4 + headerSize, end, menu);
	else
		parseMenu(base + 4 + headerSize, end, menu);
	return menu;
}

HMENU WINAPI LoadMenuW(HINSTANCE instance, LPCWSTR name)
{
	std::span<const BYTE> data = resourceBytes(name, RT_MENU, instance);
	if (data.empty())
	{
		SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
		return nullptr;
	}
	return LoadMenuIndirectW(reinterpret_cast<const MENUTEMPLATE*>(data.data()));
}

// --- Items ----------------------------------------------------------------------------------------

BOOL WINAPI InsertMenuW(HMENU handle, UINT position, UINT flags, UINT_PTR idOrSubmenu, LPCWSTR text)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	MenuItem item = itemFromFlags(flags, idOrSubmenu, text);
	size_t index = menu->_items.size();
	if (flags & MF_BYPOSITION)
	{
		if (position < menu->_items.size())
			index = position;
	}
	else
	{
		Menu* owner = nullptr;
		size_t found = 0;
		if (findItem(menu, position, flags, &owner, &found))
		{
			menu = owner;
			index = found;
		}
	}
	menu->_items.insert(menu->_items.begin() + static_cast<std::ptrdiff_t>(index), item);
	return TRUE;
}

BOOL WINAPI AppendMenuW(HMENU handle, UINT flags, UINT_PTR idOrSubmenu, LPCWSTR text)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	menu->_items.push_back(itemFromFlags(flags, idOrSubmenu, text));
	return TRUE;
}

BOOL WINAPI ModifyMenuW(HMENU handle, UINT position, UINT flags, UINT_PTR idOrSubmenu, LPCWSTR text)
{
	MenuItem* item = findItem(toMenu(handle), position, flags);
	if (!item)
		return FALSE;
	*item = itemFromFlags(flags, idOrSubmenu, text);
	return TRUE;
}

BOOL WINAPI RemoveMenu(HMENU handle, UINT position, UINT flags)
{
	Menu* owner = nullptr;
	size_t index = 0;
	if (!findItem(toMenu(handle), position, flags, &owner, &index))
		return FALSE;
	owner->_items.erase(owner->_items.begin() + static_cast<std::ptrdiff_t>(index));
	return TRUE;
}

BOOL WINAPI DeleteMenu(HMENU handle, UINT position, UINT flags)
{
	Menu* owner = nullptr;
	size_t index = 0;
	MenuItem* item = findItem(toMenu(handle), position, flags, &owner, &index);
	if (!item)
		return FALSE;
	HMENU submenu = item->_submenu;
	owner->_items.erase(owner->_items.begin() + static_cast<std::ptrdiff_t>(index));
	if (submenu)
		DestroyMenu(submenu);
	return TRUE;
}

int WINAPI GetMenuItemCount(HMENU handle)
{
	Menu* menu = toMenu(handle);
	return menu ? static_cast<int>(menu->_items.size()) : -1;
}

UINT WINAPI GetMenuItemID(HMENU handle, int position)
{
	Menu* menu = toMenu(handle);
	if (!menu || position < 0 || static_cast<size_t>(position) >= menu->_items.size())
		return static_cast<UINT>(-1);
	const MenuItem& item = menu->_items[static_cast<size_t>(position)];
	return item._submenu ? static_cast<UINT>(-1) : item._id;
}

HMENU WINAPI GetSubMenu(HMENU handle, int position)
{
	Menu* menu = toMenu(handle);
	if (!menu || position < 0 || static_cast<size_t>(position) >= menu->_items.size())
		return nullptr;
	return menu->_items[static_cast<size_t>(position)]._submenu;
}

UINT WINAPI GetMenuState(HMENU handle, UINT position, UINT flags)
{
	MenuItem* item = findItem(toMenu(handle), position, flags);
	if (!item)
		return static_cast<UINT>(-1);
	UINT state = item->_state | (item->_type & (MFT_SEPARATOR | MFT_OWNERDRAW | MFT_BITMAP | MFT_MENUBARBREAK | MFT_RIGHTJUSTIFY));
	if (item->_submenu)
		state |= MF_POPUP | (static_cast<UINT>(GetMenuItemCount(item->_submenu)) << 8);
	return state;
}

int WINAPI GetMenuStringW(HMENU handle, UINT position, LPWSTR text, int size, UINT flags)
{
	MenuItem* item = findItem(toMenu(handle), position, flags);
	if (!item)
		return 0;
	if (!text || size <= 0)
		return static_cast<int>(item->_text.size());
	lstrcpynW(text, item->_text.c_str(), size);
	return lstrlenW(text);
}

BOOL WINAPI GetMenuItemInfoW(HMENU handle, UINT position, BOOL byPosition, LPMENUITEMINFOW info)
{
	MenuItem* item = findItem(toMenu(handle), position, byPosition ? MF_BYPOSITION : MF_BYCOMMAND);
	if (!item)
		return FALSE;
	if (info->fMask & (MIIM_FTYPE | MIIM_TYPE))
		info->fType = item->_type;
	if (info->fMask & MIIM_STATE)
		info->fState = item->_state;
	if (info->fMask & MIIM_ID)
		info->wID = item->_id;
	if (info->fMask & MIIM_SUBMENU)
		info->hSubMenu = item->_submenu;
	if (info->fMask & MIIM_DATA)
		info->dwItemData = item->_data;
	if (info->fMask & MIIM_BITMAP)
		info->hbmpItem = item->_bitmap;
	if (info->fMask & (MIIM_STRING | MIIM_TYPE))
	{
		if (info->dwTypeData && info->cch > 0)
			lstrcpynW(info->dwTypeData, item->_text.c_str(), static_cast<int>(info->cch));
		info->cch = static_cast<UINT>(item->_text.size());
	}
	return TRUE;
}

// The A forms: MENUITEMINFOA is MENUITEMINFOW with the text in UTF-8 (the ANSI code page here).
static_assert(sizeof(MENUITEMINFOA) == sizeof(MENUITEMINFOW));

namespace
{
	// An item's information in the W form, its text (when it has one) converted into text.
	MENUITEMINFOW wideItemInfo(const MENUITEMINFOA& info, std::u16string& text)
	{
		MENUITEMINFOW wide{};
		std::memcpy(&wide, &info, sizeof(wide));
		const UINT type = (info.fMask & (MIIM_FTYPE | MIIM_TYPE)) ? info.fType : 0;
		if ((info.fMask & (MIIM_STRING | MIIM_TYPE)) && info.dwTypeData && !(type & (MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW)))
		{
			text = nppmac::toUtf16(info.dwTypeData);
			wide.dwTypeData = text.data();
		}
		return wide;
	}
}

BOOL WINAPI GetMenuItemInfoA(HMENU handle, UINT position, BOOL byPosition, LPMENUITEMINFOA info)
{
	LPSTR text = info->dwTypeData;
	const UINT size = info->cch;
	MENUITEMINFOW wide{};
	std::memcpy(&wide, info, sizeof(wide));
	wide.dwTypeData = nullptr;
	wide.cch = 0;
	if (!GetMenuItemInfoW(handle, position, byPosition, &wide))
		return FALSE;
	std::memcpy(info, &wide, sizeof(wide));
	info->dwTypeData = text;
	info->cch = size;
	if (info->fMask & (MIIM_STRING | MIIM_TYPE))
	{
		const std::string narrow = nppmac::toUtf8(findItem(toMenu(handle), position, byPosition ? MF_BYPOSITION : MF_BYCOMMAND)->_text);
		if (text && size > 0)
			lstrcpynA(text, narrow.c_str(), static_cast<int>(size));
		info->cch = static_cast<UINT>(narrow.size());
	}
	return TRUE;
}

BOOL WINAPI SetMenuItemInfoW(HMENU handle, UINT position, BOOL byPosition, LPCMENUITEMINFOW info)
{
	MenuItem* item = findItem(toMenu(handle), position, byPosition ? MF_BYPOSITION : MF_BYCOMMAND);
	if (!item)
		return FALSE;
	if (info->fMask & (MIIM_FTYPE | MIIM_TYPE))
		item->_type = info->fType;
	if (info->fMask & MIIM_STATE)
		item->_state = info->fState;
	if (info->fMask & MIIM_ID)
		item->_id = info->wID;
	if (info->fMask & MIIM_SUBMENU)
		item->_submenu = info->hSubMenu;
	if (info->fMask & MIIM_DATA)
		item->_data = info->dwItemData;
	if (info->fMask & MIIM_BITMAP)
		item->_bitmap = info->hbmpItem;
	if ((info->fMask & (MIIM_STRING | MIIM_TYPE)) && info->dwTypeData && !(item->_type & (MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW)))
		item->_text = info->dwTypeData;
	return TRUE;
}

BOOL WINAPI SetMenuItemInfoA(HMENU handle, UINT position, BOOL byPosition, LPCMENUITEMINFOA info)
{
	std::u16string text;
	const MENUITEMINFOW wide = wideItemInfo(*info, text);
	return SetMenuItemInfoW(handle, position, byPosition, &wide);
}

BOOL WINAPI InsertMenuItemW(HMENU handle, UINT position, BOOL byPosition, LPCMENUITEMINFOW info)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	MenuItem item;
	if (info->fMask & (MIIM_FTYPE | MIIM_TYPE))
		item._type = info->fType;
	if (info->fMask & MIIM_STATE)
		item._state = info->fState;
	if (info->fMask & MIIM_ID)
		item._id = info->wID;
	if (info->fMask & MIIM_SUBMENU)
		item._submenu = info->hSubMenu;
	if (info->fMask & MIIM_DATA)
		item._data = info->dwItemData;
	if (info->fMask & MIIM_BITMAP)
		item._bitmap = info->hbmpItem;
	if ((info->fMask & (MIIM_STRING | MIIM_TYPE)) && info->dwTypeData && !(item._type & (MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW)))
		item._text = info->dwTypeData;

	size_t index = menu->_items.size();
	if (byPosition)
	{
		if (position < menu->_items.size())
			index = position;
	}
	else
	{
		Menu* owner = nullptr;
		size_t found = 0;
		if (findItem(menu, position, MF_BYCOMMAND, &owner, &found))
		{
			menu = owner;
			index = found;
		}
	}
	menu->_items.insert(menu->_items.begin() + static_cast<std::ptrdiff_t>(index), item);
	return TRUE;
}

BOOL WINAPI InsertMenuItemA(HMENU handle, UINT position, BOOL byPosition, LPCMENUITEMINFOA info)
{
	std::u16string text;
	const MENUITEMINFOW wide = wideItemInfo(*info, text);
	return InsertMenuItemW(handle, position, byPosition, &wide);
}

DWORD WINAPI CheckMenuItem(HMENU handle, UINT id, UINT flags)
{
	MenuItem* item = findItem(toMenu(handle), id, flags);
	if (!item)
		return static_cast<DWORD>(-1);
	const DWORD previous = (item->_state & MFS_CHECKED) ? MF_CHECKED : MF_UNCHECKED;
	if (flags & MF_CHECKED)
		item->_state |= MFS_CHECKED;
	else
		item->_state &= ~MFS_CHECKED;
	return previous;
}

BOOL WINAPI CheckMenuRadioItem(HMENU handle, UINT first, UINT last, UINT check, UINT flags)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	Menu* owner = nullptr;
	size_t firstIndex = 0;
	size_t lastIndex = 0;
	if (!findItem(menu, first, flags, &owner, &firstIndex))
		return FALSE;
	Menu* lastOwner = nullptr;
	if (!findItem(menu, last, flags, &lastOwner, &lastIndex) || lastOwner != owner)
		return FALSE;
	for (size_t i = firstIndex; i <= lastIndex && i < owner->_items.size(); ++i)
	{
		MenuItem& item = owner->_items[i];
		const bool selected = (flags & MF_BYPOSITION) ? i == check : item._id == check;
		item._type |= MFT_RADIOCHECK;
		if (selected)
			item._state |= MFS_CHECKED;
		else
			item._state &= ~MFS_CHECKED;
	}
	return TRUE;
}

BOOL WINAPI EnableMenuItem(HMENU handle, UINT id, UINT flags)
{
	MenuItem* item = findItem(toMenu(handle), id, flags);
	if (!item)
		return -1;
	const BOOL previous = (item->_state & MFS_DISABLED) ? MF_GRAYED : MF_ENABLED;
	if (flags & (MF_GRAYED | MF_DISABLED))
		item->_state |= MFS_DISABLED;
	else
		item->_state &= ~MFS_DISABLED;
	return previous;
}

BOOL WINAPI SetMenuItemBitmaps(HMENU handle, UINT position, UINT flags, HBITMAP unchecked, HBITMAP)
{
	MenuItem* item = findItem(toMenu(handle), position, flags);
	if (!item)
		return FALSE;
	item->_bitmap = unchecked;
	return TRUE;
}

UINT WINAPI GetMenuDefaultItem(HMENU handle, UINT byPosition, UINT)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return static_cast<UINT>(-1);
	for (size_t i = 0; i < menu->_items.size(); ++i)
	{
		if (menu->_items[i]._state & MFS_DEFAULT)
			return byPosition ? static_cast<UINT>(i) : menu->_items[i]._id;
	}
	return static_cast<UINT>(-1);
}

BOOL WINAPI SetMenuDefaultItem(HMENU handle, UINT item, UINT byPosition)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	for (size_t i = 0; i < menu->_items.size(); ++i)
	{
		MenuItem& candidate = menu->_items[i];
		const bool match = byPosition ? i == item : candidate._id == item;
		if (match)
			candidate._state |= MFS_DEFAULT;
		else
			candidate._state &= ~MFS_DEFAULT;
	}
	return TRUE;
}

BOOL WINAPI GetMenuInfo(HMENU handle, LPMENUINFO info)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	const DWORD mask = info->fMask;
	*info = menu->_info;
	info->cbSize = sizeof(MENUINFO);
	info->fMask = mask;
	return TRUE;
}

BOOL WINAPI SetMenuInfo(HMENU handle, LPCMENUINFO info)
{
	Menu* menu = toMenu(handle);
	if (!menu)
		return FALSE;
	menu->_info = *info;
	return TRUE;
}

// --- Menu bars ------------------------------------------------------------------------------------

HMENU WINAPI GetMenu(HWND hwnd)
{
	// A child window's id and a top-level window's menu are the same field, as on Windows, where
	// GetMenu on a child returns its id (BabyGrid finds its grids this way).
	Window* window = fromHandle(hwnd);
	return window ? reinterpret_cast<HMENU>(window->_id) : nullptr;
}

// The window menu (system menu) of the title bar, which a macOS window does not have: none, as for a
// Windows window without WS_SYSMENU.
HMENU WINAPI GetSystemMenu(HWND, BOOL)
{
	return nullptr;
}

BOOL WINAPI SetMenu(HWND hwnd, HMENU handle)
{
	Window* window = fromHandle(hwnd);
	if (!window || !window->isTopLevel())
		return FALSE;
	if (Menu* previous = toMenu(reinterpret_cast<HMENU>(window->_id)))
		previous->_owner = nullptr;
	window->_id = reinterpret_cast<UINT_PTR>(handle);
	Menu* menu = toMenu(handle);
	if (menu)
	{
		menu->_owner = hwnd;
		refreshMenuBar(menu);
	}
	else
	{
		ui::clearMenuBar(hwnd);
	}
	return TRUE;
}

BOOL WINAPI DrawMenuBar(HWND hwnd)
{
	// Items are read again whenever a menu opens; only the menu bar's titles need rebuilding.
	refreshMenuBar(toMenu(GetMenu(hwnd)));
	return TRUE;
}

BOOL WINAPI GetMenuBarInfo(HWND, LONG, LONG, PMENUBARINFO info)
{
	// The menu bar is the macOS one, outside the window.
	info->rcBar = {0, 0, 0, 0};
	info->hMenu = nullptr;
	info->hwndMenu = nullptr;
	info->fBarFocused = FALSE;
	info->fFocused = FALSE;
	return FALSE;
}

// --- Popup menus ----------------------------------------------------------------------------------

BOOL WINAPI TrackPopupMenuEx(HMENU handle, UINT flags, int x, int y, HWND owner, LPTPMPARAMS)
{
	if (!toMenu(handle))
		return FALSE;
	const unsigned chosen = ui::trackPopupMenu(reinterpret_cast<uintptr_t>(handle), x, y, callbacks(), owner);
	if (flags & TPM_RETURNCMD)
		return static_cast<BOOL>(chosen);
	if (chosen && !(flags & TPM_NONOTIFY))
		PostMessageW(owner, WM_COMMAND, MAKEWPARAM(chosen, 0), 0);
	return TRUE;
}

BOOL WINAPI TrackPopupMenu(HMENU handle, UINT flags, int x, int y, int, HWND owner, const RECT*)
{
	return TrackPopupMenuEx(handle, flags, x, y, owner, nullptr);
}

BOOL WINAPI EndMenu()
{
	return TRUE;
}

// --- Accelerators ---------------------------------------------------------------------------------

namespace
{
	struct AcceleratorTable
	{
		std::vector<ACCEL> _entries;
	};

	std::unordered_set<AcceleratorTable*>& tables()
	{
		static auto* set = new std::unordered_set<AcceleratorTable*>;
		return *set;
	}

	AcceleratorTable* toTable(HACCEL handle)
	{
		auto* table = reinterpret_cast<AcceleratorTable*>(handle);
		return tables().count(table) ? table : nullptr;
	}
}

HACCEL WINAPI CreateAcceleratorTableW(LPACCEL entries, int count)
{
	auto* table = new AcceleratorTable;
	table->_entries.assign(entries, entries + std::max(0, count));
	tables().insert(table);
	return reinterpret_cast<HACCEL>(table);
}

BOOL WINAPI DestroyAcceleratorTable(HACCEL handle)
{
	AcceleratorTable* table = toTable(handle);
	if (!table)
		return FALSE;
	tables().erase(table);
	delete table;
	return TRUE;
}

int WINAPI CopyAcceleratorTableW(HACCEL handle, LPACCEL entries, int count)
{
	AcceleratorTable* table = toTable(handle);
	if (!table)
		return 0;
	if (!entries)
		return static_cast<int>(table->_entries.size());
	const int n = std::min<int>(count, static_cast<int>(table->_entries.size()));
	std::copy_n(table->_entries.begin(), n, entries);
	return n;
}

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE instance, LPCWSTR name)
{
	// Resource entries: fFlags, wAnsi, wId, padding; 0x80 in fFlags marks the last one.
	std::span<const BYTE> data = resourceBytes(name, RT_ACCELERATOR, instance);
	std::vector<ACCEL> entries;
	for (size_t offset = 0; offset + 8 <= data.size(); offset += 8)
	{
		WORD flags = 0;
		WORD key = 0;
		WORD id = 0;
		std::memcpy(&flags, data.data() + offset, 2);
		std::memcpy(&key, data.data() + offset + 2, 2);
		std::memcpy(&id, data.data() + offset + 4, 2);
		entries.push_back({static_cast<BYTE>(flags & 0x7F), key, id});
		if (flags & 0x80)
			break;
	}
	return entries.empty() ? nullptr : CreateAcceleratorTableW(entries.data(), static_cast<int>(entries.size()));
}

int WINAPI TranslateAcceleratorW(HWND hwnd, HACCEL handle, LPMSG msg)
{
	AcceleratorTable* table = toTable(handle);
	if (!table || !hwnd)
		return 0;
	const bool keyDown = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
	const bool character = msg->message == WM_CHAR || msg->message == WM_SYSCHAR;
	if (!keyDown && !character)
		return 0;
	const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	for (const ACCEL& entry : table->_entries)
	{
		if (entry.fVirt & FVIRTKEY)
		{
			if (!keyDown || entry.key != msg->wParam)
				continue;
			if (((entry.fVirt & FCONTROL) != 0) != control || ((entry.fVirt & FSHIFT) != 0) != shift || ((entry.fVirt & FALT) != 0) != alt)
				continue;
		}
		else
		{
			if (!character || entry.key != msg->wParam)
				continue;
			if (((entry.fVirt & FALT) != 0) != alt)
				continue;
		}
		SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(entry.cmd, 1), 0);
		return 1;
	}
	return 0;
}
