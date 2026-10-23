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

// Wraps Wine's minwindef.h. Wine gives x86-64 Win32 functions the Microsoft calling convention
// (ms_abi) so that Wine can call into Windows binaries. Here everything is built from source for
// macOS, so the native calling convention is used on every architecture: arm64 and x86-64 alike.

#pragma once

#include_next <minwindef.h>

#undef __stdcall
#undef __cdecl
#undef __fastcall
#undef __thiscall
#undef __ms_va_list
#define __stdcall
#define __cdecl
#define __fastcall
#define __thiscall
#define __ms_va_list va_list
