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

// Replaces Wine's strsafe.h, whose wide functions take wchar_t: on macOS the Windows
// wide-character type is char16_t. Declares the StrSafe functions Notepad++ uses.

#pragma once

#include <windows.h>

typedef char16_t* STRSAFE_LPWSTR;
typedef const char16_t* STRSAFE_LPCWSTR;
typedef char* STRSAFE_LPSTR;
typedef const char* STRSAFE_LPCSTR;

#ifndef STRSAFE_MAX_CCH
#define STRSAFE_MAX_CCH 2147483647
#endif

#ifndef STRSAFE_E_INSUFFICIENT_BUFFER
#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#define STRSAFE_E_INVALID_PARAMETER ((HRESULT)0x80070057L)
#define STRSAFE_E_END_OF_FILE ((HRESULT)0x80070026L)
#endif

HRESULT StringCchCopyW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR src);
HRESULT StringCchCopyNW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR src, size_t cchToCopy);
HRESULT StringCchCatW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR src);
HRESULT StringCchLengthW(STRSAFE_LPCWSTR str, size_t cchMax, size_t* length);
HRESULT StringCchPrintfW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR format, ...);
HRESULT StringCbCopyW(STRSAFE_LPWSTR dest, size_t cbDest, STRSAFE_LPCWSTR src);
HRESULT StringCbCatW(STRSAFE_LPWSTR dest, size_t cbDest, STRSAFE_LPCWSTR src);
HRESULT StringCchCopyA(STRSAFE_LPSTR dest, size_t cchDest, STRSAFE_LPCSTR src);
HRESULT StringCchCatA(STRSAFE_LPSTR dest, size_t cchDest, STRSAFE_LPCSTR src);

#define StringCchCopy StringCchCopyW
#define StringCchCopyN StringCchCopyNW
#define StringCchCat StringCchCatW
#define StringCchLength StringCchLengthW
#define StringCchPrintf StringCchPrintfW
#define StringCbCopy StringCbCopyW
#define StringCbCat StringCbCatW
