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

// Wraps Wine's commctrl.h and adds the common-control macros and constants Notepad++ uses
// that Wine's header does not define.

#pragma once

#ifdef RC_INVOKED

// Resource scripts see the common control styles and, as strings, the control class names (Wine's
// header defines those as C arrays outside Visual C++).
#include <commctrl.rh>
#define WC_HEADER "SysHeader32"
#define WC_LISTVIEW "SysListView32"
#define WC_TREEVIEW "SysTreeView32"
#define WC_TABCONTROL "SysTabControl32"
#define WC_COMBOBOXEX "ComboBoxEx32"
#define WC_IPADDRESS "SysIPAddress32"
#define WC_PAGESCROLLER "SysPager"
#define WC_NATIVEFONTCTL "NativeFontCtl"
#define WC_LINK "SysLink"
#define WC_BUTTON "Button"
#define WC_STATIC "Static"
#define WC_EDIT "Edit"
#define WC_LISTBOX "ListBox"
#define WC_COMBOBOX "ComboBox"
#define WC_SCROLLBAR "ScrollBar"
#define TOOLBARCLASSNAME "ToolbarWindow32"
#define REBARCLASSNAME "ReBarWindow32"
#define STATUSCLASSNAME "msctls_statusbar32"
#define TOOLTIPS_CLASS "tooltips_class32"
#define TRACKBAR_CLASS "msctls_trackbar32"
#define UPDOWN_CLASS "msctls_updown32"
#define PROGRESS_CLASS "msctls_progress32"
#define HOTKEY_CLASS "msctls_hotkey32"
#define ANIMATE_CLASS "SysAnimate32"
#define MONTHCAL_CLASS "SysMonthCal32"
#define DATETIMEPICK_CLASS "SysDateTimePick32"

#else

#include_next <commctrl.h>

#ifndef ListView_SetItemCountEx
#define ListView_SetItemCountEx(hwndLV, cItems, dwFlags) \
	SNDMSG((hwndLV), LVM_SETITEMCOUNT, (WPARAM)(cItems), (LPARAM)(dwFlags))
#endif

// The Windows SDK's older name, which Wine's header does not keep.
#ifndef LPTV_HITTESTINFO
#define LPTV_HITTESTINFO LPTVHITTESTINFO
#endif

#ifndef LVCDI_ITEM
#define LVCDI_ITEM 0x00000000
#define LVCDI_GROUP 0x00000001
#define LVCDI_ITEMSLIST 0x00000002
#endif

#endif // RC_INVOKED
