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

// Wraps <sys/stat.h> with the Microsoft CRT's names: its mode bits, and its stat with 64-bit sizes,
// which macOS's own stat is, with the wide form taking a UTF-16 path ('\' read as '/').

#pragma once

#include_next <sys/stat.h>

#ifndef _S_IFMT
#define _S_IFMT S_IFMT
#define _S_IFDIR S_IFDIR
#define _S_IFCHR S_IFCHR
#define _S_IFIFO S_IFIFO
#define _S_IFREG S_IFREG
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR
#define _S_IEXEC S_IXUSR
#endif

#ifndef _stati64
#define _stati64 stat
#endif

#ifdef __cplusplus
extern "C"
#endif
int _wstati64(const char16_t* path, struct stat* buffer);
