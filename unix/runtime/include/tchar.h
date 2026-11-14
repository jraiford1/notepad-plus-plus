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

// Wraps Wine's tchar.h, which plugins include for the _T() and _tcs* names.

#pragma once

#ifdef RC_INVOKED
// A resource script gets the macros only: the C library is not there.
#ifndef _T
#define _T(string) L##string
#define _TEXT(string) L##string
#endif
#else

// macOS has its own wctype_t (and wint_t, which the compiler knows): Wine's are not declared again.
#ifndef _WCTYPE_T_DEFINED
#define _WCTYPE_T_DEFINED
#endif

#include_next <tchar.h>

// _T() is __TEXT(), which <windows.h> defines (winnt.rh): code that only includes <tchar.h> has it too.
#ifndef __TEXT
#define __TEXT(string) u##string
#endif
#endif // RC_INVOKED
