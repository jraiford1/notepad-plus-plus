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

// Microsoft CRT functions that are not about wide strings (nppmac/MsvcCrt.h, process.h, strsafe.h).

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc/malloc.h>
#include <pthread.h>
#include <unistd.h>
#include <windows.h>
#include <process.h>
#include <strsafe.h>
#include <comdef.h>

#include "MsvcFormat.h"

int __argc = 0;
char** __argv = nullptr;
char16_t** __wargv = nullptr;

errno_t memcpy_s(void* dest, size_t destSize, const void* src, size_t count)
{
	if (count == 0)
		return 0;
	if (!dest)
		return EINVAL;
	if (!src || count > destSize)
	{
		std::memset(dest, 0, destSize);
		return !src ? EINVAL : ERANGE;
	}
	std::memcpy(dest, src, count);
	return 0;
}

errno_t memmove_s(void* dest, size_t destSize, const void* src, size_t count)
{
	if (count == 0)
		return 0;
	if (!dest || !src)
		return EINVAL;
	if (count > destSize)
		return ERANGE;
	std::memmove(dest, src, count);
	return 0;
}

char* _itoa(int value, char* buffer, int radix)
{
	char* end = nullptr;
	if (radix != 10 && value < 0)
		end = std::to_chars(buffer, buffer + 33, static_cast<unsigned int>(value), radix).ptr;
	else
		end = std::to_chars(buffer, buffer + 33, value, radix).ptr;
	*end = '\0';
	return buffer;
}

char* itoa(int value, char* buffer, int radix)
{
	return _itoa(value, buffer, radix);
}

errno_t _itoa_s(int value, char* buffer, size_t size, int radix)
{
	char temp[34]{};
	_itoa(value, temp, radix);
	return strcpy_s(buffer, size, temp);
}

_se_translator_function _set_se_translator(_se_translator_function translator)
{
	static _se_translator_function current = nullptr;
	_se_translator_function previous = current;
	current = translator;
	return previous;
}

_locale_t _create_locale(int, const char* localeName)
{
	// Notepad++ asks for the user's locale ("") or the classic one ("C"); both exist on macOS.
	const char* name = (localeName && *localeName) ? localeName : nullptr;
	locale_t locale = newlocale(LC_ALL_MASK, name ? name : "", nullptr);
	return locale ? locale : newlocale(LC_ALL_MASK, "C", nullptr);
}

void _free_locale(_locale_t locale)
{
	if (locale)
		freelocale(locale);
}

double _wcstod_l(const char16_t* str, char16_t** end, _locale_t locale)
{
	const std::string narrow = nppmac::toUtf8(str);
	char* narrowEnd = nullptr;
	const double value = strtod_l(narrow.c_str(), &narrowEnd, locale);
	if (end)
	{
		// Map the narrow end offset back to UTF-16 units.
		const std::u16string consumed = nppmac::toUtf16(std::string_view{narrow.c_str(), static_cast<size_t>(narrowEnd - narrow.c_str())});
		*end = const_cast<char16_t*>(str + consumed.size());
	}
	return value;
}

double _wtof_l(const char16_t* str, _locale_t locale)
{
	return _wcstod_l(str, nullptr, locale);
}

// --- process.h ------------------------------------------------------------------------------------

namespace
{
	struct ThreadStart
	{
		unsigned (*_function)(void*) = nullptr;
		void* _argument = nullptr;
	};

	void* runThread(void* parameter)
	{
		const ThreadStart start = *static_cast<ThreadStart*>(parameter);
		delete static_cast<ThreadStart*>(parameter);
		const unsigned result = start._function(start._argument);
		return reinterpret_cast<void*>(static_cast<uintptr_t>(result));
	}
}

uintptr_t _beginthreadex(void*, unsigned stackSize, unsigned (*startAddress)(void*), void* argList, unsigned initFlag, unsigned* threadId)
{
	// Threads that must start suspended, or that must be waited on through their HANDLE, go
	// through CreateThread, which the kernel part of the runtime implements with a handle object.
	DWORD id = 0;
	HANDLE thread = CreateThread(nullptr, stackSize, reinterpret_cast<LPTHREAD_START_ROUTINE>(startAddress), argList, initFlag, &id);
	if (threadId)
		*threadId = id;
	return reinterpret_cast<uintptr_t>(thread);
}

void _endthreadex(unsigned retVal)
{
	ExitThread(retVal);
}

int _getpid(void)
{
	return getpid();
}

// --- strsafe.h ------------------------------------------------------------------------------------

HRESULT StringCchCopyW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR src)
{
	return StringCchCopyNW(dest, cchDest, src, STRSAFE_MAX_CCH);
}

HRESULT StringCchCopyNW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR src, size_t cchToCopy)
{
	if (!dest || cchDest == 0 || cchDest > STRSAFE_MAX_CCH)
		return STRSAFE_E_INVALID_PARAMETER;
	const size_t length = wcsnlen(src ? src : u"", cchToCopy);
	const size_t n = std::min(length, cchDest - 1);
	std::copy_n(src ? src : u"", n, dest);
	dest[n] = u'\0';
	return n < length ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}

HRESULT StringCchCatW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR src)
{
	if (!dest || cchDest == 0 || cchDest > STRSAFE_MAX_CCH)
		return STRSAFE_E_INVALID_PARAMETER;
	const size_t existing = wcsnlen(dest, cchDest);
	if (existing == cchDest)
		return STRSAFE_E_INVALID_PARAMETER;
	return StringCchCopyW(dest + existing, cchDest - existing, src);
}

HRESULT StringCchLengthW(STRSAFE_LPCWSTR str, size_t cchMax, size_t* length)
{
	if (!str || cchMax > STRSAFE_MAX_CCH)
		return STRSAFE_E_INVALID_PARAMETER;
	const size_t n = wcsnlen(str, cchMax);
	if (length)
		*length = n == cchMax ? 0 : n;
	return n == cchMax ? STRSAFE_E_INVALID_PARAMETER : S_OK;
}

HRESULT StringCchVPrintfW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR format, va_list args)
{
	if (!dest || cchDest == 0 || cchDest > STRSAFE_MAX_CCH)
		return STRSAFE_E_INVALID_PARAMETER;
	const std::u16string text = nppmac::crt::formatWide(format, args);
	const size_t n = std::min(text.size(), cchDest - 1);
	std::copy_n(text.data(), n, dest);
	dest[n] = u'\0';
	return n < text.size() ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}

HRESULT StringCchPrintfW(STRSAFE_LPWSTR dest, size_t cchDest, STRSAFE_LPCWSTR format, ...)
{
	va_list args;
	va_start(args, format);
	const HRESULT result = StringCchVPrintfW(dest, cchDest, format, args);
	va_end(args);
	return result;
}

HRESULT StringCbPrintfW(STRSAFE_LPWSTR dest, size_t cbDest, STRSAFE_LPCWSTR format, ...)
{
	va_list args;
	va_start(args, format);
	const HRESULT result = StringCchVPrintfW(dest, cbDest / sizeof(char16_t), format, args);
	va_end(args);
	return result;
}

HRESULT StringCbCopyW(STRSAFE_LPWSTR dest, size_t cbDest, STRSAFE_LPCWSTR src)
{
	return StringCchCopyW(dest, cbDest / sizeof(char16_t), src);
}

HRESULT StringCbCatW(STRSAFE_LPWSTR dest, size_t cbDest, STRSAFE_LPCWSTR src)
{
	return StringCchCatW(dest, cbDest / sizeof(char16_t), src);
}

HRESULT StringCchCopyA(STRSAFE_LPSTR dest, size_t cchDest, STRSAFE_LPCSTR src)
{
	if (!dest || cchDest == 0 || cchDest > STRSAFE_MAX_CCH)
		return STRSAFE_E_INVALID_PARAMETER;
	const size_t length = std::strlen(src ? src : "");
	const size_t n = std::min(length, cchDest - 1);
	std::memcpy(dest, src ? src : "", n);
	dest[n] = '\0';
	return n < length ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}

HRESULT StringCchCatA(STRSAFE_LPSTR dest, size_t cchDest, STRSAFE_LPCSTR src)
{
	if (!dest || cchDest == 0 || cchDest > STRSAFE_MAX_CCH)
		return STRSAFE_E_INVALID_PARAMETER;
	const size_t existing = strnlen(dest, cchDest);
	if (existing == cchDest)
		return STRSAFE_E_INVALID_PARAMETER;
	return StringCchCopyA(dest + existing, cchDest - existing, src);
}

// --- comdef.h -------------------------------------------------------------------------------------

const char16_t* _com_error::ErrorMessage() const noexcept
{
	thread_local char16_t message[64]{};
	swprintf(message, 64, u"Unknown error 0x%08lX", static_cast<unsigned int>(_result));
	return message;
}

errno_t fopen_s(FILE** file, const char* fileName, const char* mode)
{
	if (!file || !fileName || !mode)
		return EINVAL;
	*file = std::fopen(fileName, mode);
	return *file ? 0 : errno;
}

errno_t _dupenv_s(char** buffer, size_t* size, const char* name)
{
	if (!buffer || !name)
		return EINVAL;
	*buffer = nullptr;
	if (size)
		*size = 0;
	const char* value = std::getenv(name);
	if (!value)
		return 0;
	*buffer = strdup(value);
	if (!*buffer)
		return ENOMEM;
	if (size)
		*size = std::strlen(value) + 1;
	return 0;
}

size_t _msize(void* memory)
{
	return memory ? malloc_size(memory) : 0;
}

errno_t localtime_s(struct tm* result, const time_t* time)
{
	if (!result || !time)
		return EINVAL;
	return ::localtime_r(time, result) ? 0 : EINVAL;
}

errno_t gmtime_s(struct tm* result, const time_t* time)
{
	if (!result || !time)
		return EINVAL;
	return ::gmtime_r(time, result) ? 0 : EINVAL;
}
