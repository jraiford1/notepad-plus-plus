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

// Microsoft CRT extensions that are not part of the Windows API (so not in Wine's headers)
// and not in the macOS C library.

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <xlocale.h>

#ifdef __cplusplus
#include <iterator>
#define _countof(array) std::size(array)
#endif

inline unsigned short _byteswap_ushort(unsigned short value)
{
	return __builtin_bswap16(value);
}

inline unsigned long _byteswap_ulong(unsigned long value)
{
	return __builtin_bswap32(static_cast<uint32_t>(value));
}

inline unsigned long long _byteswap_uint64(unsigned long long value)
{
	return __builtin_bswap64(value);
}

typedef int errno_t;

// _locale_t is an opaque handle in the CRT; macOS's locale_t plays the same role.
typedef locale_t _locale_t;

enum
{
	_LC_ALL_CRT = 0, // LC_ALL, as the CRT numbers its categories
};

_locale_t _create_locale(int category, const char* localeName);
void _free_locale(_locale_t locale);
double _wcstod_l(const char16_t* str, char16_t** end, _locale_t locale);
double _wtof_l(const char16_t* str, _locale_t locale);

// Narrow case-insensitive comparisons and other CRT names for POSIX functions.
#include <cstring>
#include <strings.h>

inline int _stricmp(const char* lhs, const char* rhs)
{
	return strcasecmp(lhs, rhs);
}

inline int _strnicmp(const char* lhs, const char* rhs, size_t count)
{
	return strncasecmp(lhs, rhs, count);
}

inline int stricmp(const char* lhs, const char* rhs)
{
	return strcasecmp(lhs, rhs);
}

inline int strnicmp(const char* lhs, const char* rhs, size_t count)
{
	return strncasecmp(lhs, rhs, count);
}

inline char* _strdup(const char* str)
{
	return strdup(str);
}

// Compares bytes ignoring ASCII case.
inline int _memicmp(const void* lhs, const void* rhs, size_t count)
{
	const auto* a = static_cast<const unsigned char*>(lhs);
	const auto* b = static_cast<const unsigned char*>(rhs);
	for (size_t i = 0; i < count; ++i)
	{
		const int difference = std::tolower(a[i]) - std::tolower(b[i]);
		if (difference != 0)
			return difference;
	}
	return 0;
}

inline int memicmp(const void* lhs, const void* rhs, size_t count)
{
	return _memicmp(lhs, rhs, count);
}

// Visual C++'s keyword and macros of <stdlib.h>.
#ifndef _inline
#define _inline inline
#endif
#ifndef __max
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#endif

// The Microsoft strtok_s takes the same arguments as POSIX strtok_r.
inline char* strtok_s(char* str, const char* delimiters, char** context)
{
	return strtok_r(str, delimiters, context);
}

// An environment variable's value in a buffer of its own (malloc), or null when it is not set.
errno_t _dupenv_s(char** buffer, size_t* size, const char* name);

// Truncation marker for the _s functions' count argument, and their "truncated" result.
#ifndef _TRUNCATE
#define _TRUNCATE (static_cast<size_t>(-1))
#endif
#ifndef STRUNCATE
#define STRUNCATE 80
#endif

// Path limits: a whole path (stdlib.h's _MAX_PATH, MAX_PATH's twin), and the components of _splitpath.
#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif
#ifndef _MAX_DRIVE
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256
#endif

errno_t memcpy_s(void* dest, size_t destSize, const void* src, size_t count);
errno_t memmove_s(void* dest, size_t destSize, const void* src, size_t count);
char* _itoa(int value, char* buffer, int radix);
char* itoa(int value, char* buffer, int radix);
errno_t _itoa_s(int value, char* buffer, size_t size, int radix);
// The Microsoft forms of the thread-safe time conversions (the result first).
errno_t localtime_s(struct tm* result, const time_t* time);
errno_t fopen_s(FILE** file, const char* fileName, const char* mode);
// The size of a block from malloc.
size_t _msize(void* memory);
errno_t gmtime_s(struct tm* result, const time_t* time);

// Structured exception translation does not exist outside the Microsoft CRT: the runtime accepts
// the translator and never calls it (there are no SEH exceptions to translate).
struct _EXCEPTION_POINTERS;
typedef void (*_se_translator_function)(unsigned int, struct _EXCEPTION_POINTERS*);
_se_translator_function _set_se_translator(_se_translator_function translator);

// Command line, as the CRT exposes it to WinMain programs. Set by the runtime's startup code.
extern int __argc;
extern char** __argv;
extern char16_t** __wargv;
