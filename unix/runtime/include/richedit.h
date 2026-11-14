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

// Wraps Wine's richedit.h.

#pragma once

#ifdef RC_INVOKED

// Resource scripts see the rich edit class names as strings and its styles (Wine's header has C
// declarations a resource script cannot hold).
#define RICHEDIT_CLASS "RichEdit20W"
#define RICHEDIT_CLASS20W "RichEdit20W"
#define MSFTEDIT_CLASS "RichEdit50W"
#define ES_SAVESEL 0x00008000
#define ES_SUNKEN 0x00004000
#define ES_DISABLENOSCROLL 0x00002000
#define ES_SELECTIONBAR 0x01000000
#define ES_NOOLEDRAGDROP 0x00000008
#define ES_VERTICAL 0x00400000

#else

#include_next <richedit.h>

#endif // RC_INVOKED
