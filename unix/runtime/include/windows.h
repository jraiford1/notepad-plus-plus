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

// Wraps Wine's windows.h and adds what the Windows SDK has but Wine's headers do not.

#pragma once

#include_next <windows.h>

#ifdef RC_INVOKED
// The Windows SDK's resource headers define the newer button styles; Wine's only in commctrl.h.
#ifndef BS_SPLITBUTTON
#define BS_SPLITBUTTON 0x0000000CL
#define BS_DEFSPLITBUTTON 0x0000000DL
#define BS_COMMANDLINK 0x0000000EL
#define BS_DEFCOMMANDLINK 0x0000000FL
#endif
#else
#include "nppmac/Win32Overloads.h"
#endif
