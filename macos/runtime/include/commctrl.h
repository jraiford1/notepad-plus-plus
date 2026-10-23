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

#include_next <commctrl.h>

#ifndef ListView_SetItemCountEx
#define ListView_SetItemCountEx(hwndLV, cItems, dwFlags) \
	SNDMSG((hwndLV), LVM_SETITEMCOUNT, (WPARAM)(cItems), (LPARAM)(dwFlags))
#endif

#ifndef LVCDI_ITEM
#define LVCDI_ITEM 0x00000000
#define LVCDI_GROUP 0x00000001
#define LVCDI_ITEMSLIST 0x00000002
#endif
