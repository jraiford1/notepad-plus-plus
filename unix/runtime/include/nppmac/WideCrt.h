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

// The Microsoft CRT's wide functions that C code calls too (in plugins, where wchar_t is char16_t as
// well): with C linkage, so C and C++ bind to the same ones. Paths and commands are UTF-16, '\' read
// as '/' in paths.

#pragma once

#include <stdio.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C"
{
#endif

FILE* _wfopen(const char16_t* fileName, const char16_t* mode);
FILE* _wfreopen(const char16_t* fileName, const char16_t* mode, FILE* file);
// A pipe to or from a command run by the shell; the CRT's "t" and "b" in the mode are dropped.
FILE* _popen(const char* command, const char* mode);
FILE* _wpopen(const char16_t* command, const char16_t* mode);
int _pclose(FILE* file);
int _wsystem(const char16_t* command);
// The variable's value, valid until the variable changes; null when it is not set.
char16_t* _wgetenv(const char16_t* name);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
// The C library's wcslen and its family take its 4-byte wchar_t: C code gets 2-byte forms under the
// same names, as C++ code gets char16_t overloads (nppmac/WideString.h).

static inline size_t nppmac_wcsnlen(const char16_t* str, size_t maxCount)
{
	size_t length = 0;
	while (length < maxCount && str[length])
		++length;
	return length;
}

static inline size_t nppmac_wcslen(const char16_t* str)
{
	return nppmac_wcsnlen(str, (size_t)-1);
}

static inline int nppmac_wcsncmp(const char16_t* lhs, const char16_t* rhs, size_t count)
{
	for (; count > 0; ++lhs, ++rhs, --count)
	{
		if (*lhs != *rhs)
			return *lhs < *rhs ? -1 : 1;
		if (!*lhs)
			return 0;
	}
	return 0;
}

static inline int nppmac_wcscmp(const char16_t* lhs, const char16_t* rhs)
{
	return nppmac_wcsncmp(lhs, rhs, (size_t)-1);
}

static inline char16_t* nppmac_wcsncpy(char16_t* dest, const char16_t* src, size_t count)
{
	size_t i = 0;
	for (; i < count && src[i]; ++i)
		dest[i] = src[i];
	for (; i < count; ++i)
		dest[i] = 0;
	return dest;
}

static inline char16_t* nppmac_wcscpy(char16_t* dest, const char16_t* src)
{
	return nppmac_wcsncpy(dest, src, nppmac_wcslen(src) + 1);
}

static inline char16_t* nppmac_wcscat(char16_t* dest, const char16_t* src)
{
	nppmac_wcscpy(dest + nppmac_wcslen(dest), src);
	return dest;
}

static inline char16_t* nppmac_wcschr(const char16_t* str, char16_t ch)
{
	for (;; ++str)
	{
		if (*str == ch)
			return (char16_t*)str;
		if (!*str)
			return NULL;
	}
}

static inline char16_t* nppmac_wcsrchr(const char16_t* str, char16_t ch)
{
	const char16_t* found = NULL;
	for (;; ++str)
	{
		if (*str == ch)
			found = str;
		if (!*str)
			return (char16_t*)found;
	}
}

static inline char16_t* nppmac_wcsstr(const char16_t* str, const char16_t* subStr)
{
	const size_t length = nppmac_wcslen(subStr);
	for (; *str; ++str)
	{
		if (nppmac_wcsncmp(str, subStr, length) == 0)
			return (char16_t*)str;
	}
	return length == 0 ? (char16_t*)str : NULL;
}

#define wcsnlen nppmac_wcsnlen
#define wcslen nppmac_wcslen
#define wcsncmp nppmac_wcsncmp
#define wcscmp nppmac_wcscmp
#define wcsncpy nppmac_wcsncpy
#define wcscpy nppmac_wcscpy
#define wcscat nppmac_wcscat
#define wcschr nppmac_wcschr
#define wcsrchr nppmac_wcsrchr
#define wcsstr nppmac_wcsstr
#endif
