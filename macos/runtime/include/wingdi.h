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

// Wraps Wine's wingdi.h. Resource scripts (a dialog's FONT statement) use the character sets, which
// Wine defines with a (BYTE) cast that resource compilers do not read: there they are plain numbers.

#pragma once

#include_next <wingdi.h>

#ifdef RC_INVOKED
#undef ANSI_CHARSET
#define ANSI_CHARSET 0
#undef DEFAULT_CHARSET
#define DEFAULT_CHARSET 1
#undef SYMBOL_CHARSET
#define SYMBOL_CHARSET 2
#undef SHIFTJIS_CHARSET
#define SHIFTJIS_CHARSET 128
#undef HANGEUL_CHARSET
#define HANGEUL_CHARSET 129
#undef HANGUL_CHARSET
#define HANGUL_CHARSET 129
#undef GB2312_CHARSET
#define GB2312_CHARSET 134
#undef CHINESEBIG5_CHARSET
#define CHINESEBIG5_CHARSET 136
#undef GREEK_CHARSET
#define GREEK_CHARSET 161
#undef TURKISH_CHARSET
#define TURKISH_CHARSET 162
#undef VIETNAMESE_CHARSET
#define VIETNAMESE_CHARSET 163
#undef HEBREW_CHARSET
#define HEBREW_CHARSET 177
#undef ARABIC_CHARSET
#define ARABIC_CHARSET 178
#undef BALTIC_CHARSET
#define BALTIC_CHARSET 186
#undef RUSSIAN_CHARSET
#define RUSSIAN_CHARSET 204
#undef THAI_CHARSET
#define THAI_CHARSET 222
#undef EASTEUROPE_CHARSET
#define EASTEUROPE_CHARSET 238
#undef EE_CHARSET
#define EE_CHARSET 238
#undef JOHAB_CHARSET
#define JOHAB_CHARSET 130
#undef MAC_CHARSET
#define MAC_CHARSET 77
#undef OEM_CHARSET
#define OEM_CHARSET 255
#endif
