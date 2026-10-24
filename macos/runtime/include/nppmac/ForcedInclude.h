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

// Included before every translation unit of the macOS build, the way upstream's MinGW build
// includes PowerEditor/gcc/gcc-fixes.h. Compiler-level differences only: API differences belong
// to the runtime headers and libraries.

#pragma once

// Upstream's own fixes for non-MSVC compilers (__try/__except and friends).
#include "../../../../PowerEditor/gcc/gcc-fixes.h"

// Notepad++ does not use sockets, and Wine's winsock.h clashes with the htonl() family of macros
// and the select() declarations of the macOS SDK: both parts of it are skipped.
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef __WINE_WINSOCKAPI_STDLIB_H
#define __WINE_WINSOCKAPI_STDLIB_H
#endif

// The Win32 functions are defined in the app, not imported from DLLs: Wine's headers would declare
// them hidden, and plugins could not bind to them.
#ifndef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT __attribute__((visibility("default")))
#endif

#include "nppmac/PlatformDecls.h"

#ifdef __cplusplus
#include "nppmac/MsvcCrt.h"
#include "nppmac/WideString.h"
#include "nppmac/Filesystem.h"
#endif
