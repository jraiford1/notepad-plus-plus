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

// Wraps Wine's shlwapi.h: with the QITAB macros, which Wine's header does not define, for the
// QueryInterface tables of COM objects (QISearch). Interfaces are named by their IID_ constants.

#pragma once

#include_next <shlwapi.h>

#ifndef OFFSETOFCLASS
#define OFFSETOFCLASS(base, derived) ((DWORD)(DWORD_PTR)(static_cast<base*>((derived*)8)) - 8)
#endif
#ifndef QITABENT
#define QITABENTMULTI(Cthis, Ifoo, Iimpl) { &IID_##Ifoo, OFFSETOFCLASS(Iimpl, Cthis) }
#define QITABENTMULTI2(Cthis, Ifoo, Iimpl) { &Ifoo, OFFSETOFCLASS(Iimpl, Cthis) }
#define QITABENT(Cthis, Ifoo) QITABENTMULTI(Cthis, Ifoo, Ifoo)
#endif
