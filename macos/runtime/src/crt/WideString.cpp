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

// char16_t implementations of the C and Microsoft CRT wide-string functions (nppmac/WideString.h).

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <xlocale.h>

#include "nppmac/WideString.h"
#include <mbstring.h>
#include "MsvcFormat.h"

namespace
{
	// Case mapping and classification for UTF-16 code units, with full Unicode tables (the "C" locale
	// of the macOS C library only knows ASCII).
	locale_t unicodeLocale()
	{
		static locale_t locale = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
		return locale;
	}

	char16_t toLower16(char16_t ch)
	{
		if (ch < 0x80)
			return (ch >= u'A' && ch <= u'Z') ? static_cast<char16_t>(ch + (u'a' - u'A')) : ch;
		if (ch >= 0xD800 && ch <= 0xDFFF)
			return ch;
		return static_cast<char16_t>(towlower_l(static_cast<wint_t>(ch), unicodeLocale()));
	}

	char16_t toUpper16(char16_t ch)
	{
		if (ch < 0x80)
			return (ch >= u'a' && ch <= u'z') ? static_cast<char16_t>(ch - (u'a' - u'A')) : ch;
		if (ch >= 0xD800 && ch <= 0xDFFF)
			return ch;
		return static_cast<char16_t>(towupper_l(static_cast<wint_t>(ch), unicodeLocale()));
	}

	// The numeric parsers work on a narrow copy with one byte per UTF-16 unit, so that offsets
	// (for the end pointer) are the same in both. Non-ASCII units become a byte no parser accepts.
	std::string asciiShadow(const char16_t* str)
	{
		std::string result;
		for (; *str != u'\0'; ++str)
			result.push_back(*str < 0x80 ? static_cast<char>(*str) : '\x7f');
		return result;
	}

	template <typename T, typename Parser>
	T parseNumber(const char16_t* str, char16_t** end, Parser parser)
	{
		const std::string shadow = asciiShadow(str);
		char* narrowEnd = nullptr;
		const T value = parser(shadow.c_str(), &narrowEnd);
		if (end)
			*end = const_cast<char16_t*>(str + (narrowEnd - shadow.c_str()));
		return value;
	}

	locale_t classicLocale()
	{
		static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
		return locale;
	}

	template <typename T>
	char16_t* integerToString(T value, char16_t* buffer, int radix)
	{
		char narrow[72]{};
		auto [ptr, ec] = std::to_chars(narrow, narrow + sizeof(narrow) - 1, value, radix);
		*ptr = '\0';
		char16_t* out = buffer;
		for (const char* p = narrow; *p != '\0'; ++p)
			*out++ = static_cast<char16_t>(*p);
		*out = u'\0';
		return buffer;
	}

	// Writes formatted text into a caller's buffer with C99 snprintf semantics:
	// always terminated when count > 0; returns the full length.
	template <typename CharT>
	int writeTruncated(CharT* buffer, size_t count, const std::basic_string<CharT>& text)
	{
		if (buffer && count > 0)
		{
			const size_t n = std::min(text.size(), count - 1);
			std::copy_n(text.data(), n, buffer);
			buffer[n] = CharT{};
		}
		return static_cast<int>(text.size());
	}
}

// --- C wide-string functions ----------------------------------------------------------------------

size_t wcslen(const char16_t* str)
{
	return std::char_traits<char16_t>::length(str);
}

size_t wcsnlen(const char16_t* str, size_t maxCount)
{
	size_t length = 0;
	while (length < maxCount && str[length] != u'\0')
		++length;
	return length;
}

int wcscmp(const char16_t* lhs, const char16_t* rhs)
{
	for (; *lhs == *rhs; ++lhs, ++rhs)
	{
		if (*lhs == u'\0')
			return 0;
	}
	return *lhs < *rhs ? -1 : 1;
}

int wcsncmp(const char16_t* lhs, const char16_t* rhs, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (lhs[i] != rhs[i])
			return lhs[i] < rhs[i] ? -1 : 1;
		if (lhs[i] == u'\0')
			return 0;
	}
	return 0;
}

int wcscoll(const char16_t* lhs, const char16_t* rhs)
{
	return wcscmp(lhs, rhs);
}

char16_t* wcscpy(char16_t* dest, const char16_t* src)
{
	char16_t* out = dest;
	while ((*out++ = *src++) != u'\0')
	{
	}
	return dest;
}

char16_t* wcsncpy(char16_t* dest, const char16_t* src, size_t count)
{
	size_t i = 0;
	for (; i < count && src[i] != u'\0'; ++i)
		dest[i] = src[i];
	for (; i < count; ++i)
		dest[i] = u'\0';
	return dest;
}

char16_t* wcscat(char16_t* dest, const char16_t* src)
{
	wcscpy(dest + wcslen(dest), src);
	return dest;
}

char16_t* wcsncat(char16_t* dest, const char16_t* src, size_t count)
{
	char16_t* out = dest + wcslen(dest);
	size_t i = 0;
	for (; i < count && src[i] != u'\0'; ++i)
		out[i] = src[i];
	out[i] = u'\0';
	return dest;
}

const char16_t* wcschr(const char16_t* str, char16_t ch)
{
	for (;; ++str)
	{
		if (*str == ch)
			return str;
		if (*str == u'\0')
			return nullptr;
	}
}

char16_t* wcschr(char16_t* str, char16_t ch)
{
	return const_cast<char16_t*>(wcschr(static_cast<const char16_t*>(str), ch));
}

const char16_t* wcsrchr(const char16_t* str, char16_t ch)
{
	const char16_t* found = nullptr;
	for (;; ++str)
	{
		if (*str == ch)
			found = str;
		if (*str == u'\0')
			return found;
	}
}

char16_t* wcsrchr(char16_t* str, char16_t ch)
{
	return const_cast<char16_t*>(wcsrchr(static_cast<const char16_t*>(str), ch));
}

const char16_t* wcsstr(const char16_t* str, const char16_t* subStr)
{
	const std::u16string_view haystack{str};
	const size_t pos = haystack.find(subStr);
	return pos == std::u16string_view::npos ? nullptr : str + pos;
}

char16_t* wcsstr(char16_t* str, const char16_t* subStr)
{
	return const_cast<char16_t*>(wcsstr(static_cast<const char16_t*>(str), subStr));
}

const char16_t* wcspbrk(const char16_t* str, const char16_t* charSet)
{
	for (; *str != u'\0'; ++str)
	{
		if (wcschr(charSet, *str))
			return str;
	}
	return nullptr;
}

char16_t* wcspbrk(char16_t* str, const char16_t* charSet)
{
	return const_cast<char16_t*>(wcspbrk(static_cast<const char16_t*>(str), charSet));
}

size_t wcsspn(const char16_t* str, const char16_t* charSet)
{
	size_t length = 0;
	while (str[length] != u'\0' && wcschr(charSet, str[length]))
		++length;
	return length;
}

size_t wcscspn(const char16_t* str, const char16_t* charSet)
{
	size_t length = 0;
	while (str[length] != u'\0' && !wcschr(charSet, str[length]))
		++length;
	return length;
}

char16_t* wcstok(char16_t* str, const char16_t* delimiters, char16_t** context)
{
	char16_t* start = str ? str : *context;
	if (!start)
		return nullptr;
	start += wcsspn(start, delimiters);
	if (*start == u'\0')
	{
		*context = start;
		return nullptr;
	}
	char16_t* end = start + wcscspn(start, delimiters);
	if (*end != u'\0')
		*end++ = u'\0';
	*context = end;
	return start;
}

long wcstol(const char16_t* str, char16_t** end, int base)
{
	return parseNumber<long>(str, end, [base](const char* s, char** e) { return std::strtol(s, e, base); });
}

unsigned long wcstoul(const char16_t* str, char16_t** end, int base)
{
	return parseNumber<unsigned long>(str, end, [base](const char* s, char** e) { return std::strtoul(s, e, base); });
}

long long wcstoll(const char16_t* str, char16_t** end, int base)
{
	return parseNumber<long long>(str, end, [base](const char* s, char** e) { return std::strtoll(s, e, base); });
}

unsigned long long wcstoull(const char16_t* str, char16_t** end, int base)
{
	return parseNumber<unsigned long long>(str, end, [base](const char* s, char** e) { return std::strtoull(s, e, base); });
}

double wcstod(const char16_t* str, char16_t** end)
{
	return parseNumber<double>(str, end, [](const char* s, char** e) { return strtod_l(s, e, classicLocale()); });
}

long double wcstold(const char16_t* str, char16_t** end)
{
	return parseNumber<long double>(str, end, [](const char* s, char** e) { return strtold_l(s, e, classicLocale()); });
}

char16_t* _wcstok(char16_t* str, const char16_t* delimiters)
{
	thread_local char16_t* context = nullptr;
	return wcstok(str, delimiters, &context);
}

float wcstof(const char16_t* str, char16_t** end)
{
	return parseNumber<float>(str, end, [](const char* s, char** e) { return strtof_l(s, e, classicLocale()); });
}

char16_t* wmemcpy(char16_t* dest, const char16_t* src, size_t count)
{
	std::memcpy(dest, src, count * sizeof(char16_t));
	return dest;
}

char16_t* wmemmove(char16_t* dest, const char16_t* src, size_t count)
{
	std::memmove(dest, src, count * sizeof(char16_t));
	return dest;
}

char16_t* wmemset(char16_t* dest, char16_t ch, size_t count)
{
	std::fill_n(dest, count, ch);
	return dest;
}

int wmemcmp(const char16_t* lhs, const char16_t* rhs, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (lhs[i] != rhs[i])
			return lhs[i] < rhs[i] ? -1 : 1;
	}
	return 0;
}

const char16_t* wmemchr(const char16_t* str, char16_t ch, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (str[i] == ch)
			return str + i;
	}
	return nullptr;
}

char16_t* wmemchr(char16_t* str, char16_t ch, size_t count)
{
	return const_cast<char16_t*>(wmemchr(static_cast<const char16_t*>(str), ch, count));
}

// --- printf family --------------------------------------------------------------------------------

int swprintf(char16_t* buffer, size_t count, const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = vswprintf(buffer, count, format, args);
	va_end(args);
	return result;
}

int swprintf(char16_t* buffer, const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	const std::u16string text = nppmac::crt::formatWide(format, args);
	va_end(args);
	return writeTruncated(buffer, text.size() + 1, text);
}

int vswprintf(char16_t* buffer, size_t count, const char16_t* format, va_list args)
{
	// ISO swprintf: -1 when the output does not fit, the buffer is still terminated.
	const std::u16string text = nppmac::crt::formatWide(format, args);
	writeTruncated(buffer, count, text);
	return text.size() < count ? static_cast<int>(text.size()) : -1;
}

int _snwprintf(char16_t* buffer, size_t count, const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = _vsnwprintf(buffer, count, format, args);
	va_end(args);
	return result;
}

int _vsnwprintf(char16_t* buffer, size_t count, const char16_t* format, va_list args)
{
	// Legacy CRT semantics: no terminator and -1 when the text does not fit; no terminator either
	// when it fits exactly.
	const std::u16string text = nppmac::crt::formatWide(format, args);
	const size_t n = std::min(text.size(), count);
	std::copy_n(text.data(), n, buffer);
	if (text.size() < count)
		buffer[text.size()] = u'\0';
	return text.size() <= count ? static_cast<int>(text.size()) : -1;
}

int _scwprintf(const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = _vscwprintf(format, args);
	va_end(args);
	return result;
}

int _vscwprintf(const char16_t* format, va_list args)
{
	return static_cast<int>(nppmac::crt::formatWide(format, args).size());
}

int vswprintf_s(char16_t* buffer, size_t size, const char16_t* format, va_list args)
{
	const std::u16string text = nppmac::crt::formatWide(format, args);
	if (!buffer || size == 0)
		return -1;
	if (text.size() >= size)
	{
		buffer[0] = u'\0';
		return -1;
	}
	writeTruncated(buffer, size, text);
	return static_cast<int>(text.size());
}

int swprintf_s(char16_t* buffer, size_t size, const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = vswprintf_s(buffer, size, format, args);
	va_end(args);
	return result;
}

int _snwprintf_s(char16_t* buffer, size_t size, size_t count, const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	const std::u16string text = nppmac::crt::formatWide(format, args);
	va_end(args);
	if (!buffer || size == 0)
		return -1;

	const size_t limit = count == _TRUNCATE ? size - 1 : std::min(count, size - 1);
	if (text.size() > limit && count != _TRUNCATE && count >= size)
	{
		buffer[0] = u'\0';
		return -1;
	}
	const size_t n = std::min(text.size(), limit);
	std::copy_n(text.data(), n, buffer);
	buffer[n] = u'\0';
	return text.size() > limit ? -1 : static_cast<int>(n);
}

namespace nppmac
{
	int vsnprintf(char* buffer, size_t count, const char* format, va_list args)
	{
		return writeTruncated(buffer, count, crt::formatNarrow(format, args));
	}

	int vsprintf(char* buffer, const char* format, va_list args)
	{
		const std::string text = crt::formatNarrow(format, args);
		std::memcpy(buffer, text.c_str(), text.size() + 1);
		return static_cast<int>(text.size());
	}

	int sprintf(char* buffer, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		const int result = vsprintf(buffer, format, args);
		va_end(args);
		return result;
	}

	int snprintf(char* buffer, size_t count, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		const int result = vsnprintf(buffer, count, format, args);
		va_end(args);
		return result;
	}

	int sprintf_s(char* buffer, size_t size, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		const int result = vsprintf_s(buffer, size, format, args);
		va_end(args);
		return result;
	}

	int vsprintf_s(char* buffer, size_t size, const char* format, va_list args)
	{
		const std::string text = crt::formatNarrow(format, args);
		if (!buffer || size == 0)
			return -1;
		if (text.size() >= size)
		{
			buffer[0] = '\0';
			return -1;
		}
		std::memcpy(buffer, text.c_str(), text.size() + 1);
		return static_cast<int>(text.size());
	}

	int snprintf_s(char* buffer, size_t size, size_t count, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		const std::string text = crt::formatNarrow(format, args);
		va_end(args);
		if (!buffer || size == 0)
			return -1;
		const size_t limit = count == _TRUNCATE ? size - 1 : std::min(count, size - 1);
		const size_t n = std::min(text.size(), limit);
		std::memcpy(buffer, text.data(), n);
		buffer[n] = '\0';
		return text.size() > limit ? -1 : static_cast<int>(n);
	}
}

// --- Microsoft CRT wide-string functions ----------------------------------------------------------

int _wcsicmp(const char16_t* lhs, const char16_t* rhs)
{
	for (;; ++lhs, ++rhs)
	{
		const char16_t a = toLower16(*lhs);
		const char16_t b = toLower16(*rhs);
		if (a != b)
			return a < b ? -1 : 1;
		if (a == u'\0')
			return 0;
	}
}

int _wcsnicmp(const char16_t* lhs, const char16_t* rhs, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		const char16_t a = toLower16(lhs[i]);
		const char16_t b = toLower16(rhs[i]);
		if (a != b)
			return a < b ? -1 : 1;
		if (a == u'\0')
			return 0;
	}
	return 0;
}

int wcsicmp(const char16_t* lhs, const char16_t* rhs)
{
	return _wcsicmp(lhs, rhs);
}

int wcsnicmp(const char16_t* lhs, const char16_t* rhs, size_t count)
{
	return _wcsnicmp(lhs, rhs, count);
}

int _wcsicoll(const char16_t* lhs, const char16_t* rhs)
{
	return _wcsicmp(lhs, rhs);
}

char16_t* _wcsdup(const char16_t* str)
{
	const size_t size = (wcslen(str) + 1) * sizeof(char16_t);
	auto* copy = static_cast<char16_t*>(std::malloc(size));
	if (copy)
		std::memcpy(copy, str, size);
	return copy;
}

char16_t* _wcslwr(char16_t* str)
{
	for (char16_t* p = str; *p != u'\0'; ++p)
		*p = toLower16(*p);
	return str;
}

char16_t* _wcsupr(char16_t* str)
{
	for (char16_t* p = str; *p != u'\0'; ++p)
		*p = toUpper16(*p);
	return str;
}

errno_t _wcslwr_s(char16_t* str, size_t size)
{
	if (!str || wcsnlen(str, size) == size)
		return EINVAL;
	_wcslwr(str);
	return 0;
}

errno_t _wcsupr_s(char16_t* str, size_t size)
{
	if (!str || wcsnlen(str, size) == size)
		return EINVAL;
	_wcsupr(str);
	return 0;
}

int _wtoi(const char16_t* str)
{
	return static_cast<int>(wcstol(str, nullptr, 10));
}

long _wtol(const char16_t* str)
{
	// Windows long is 32-bit.
	return static_cast<int>(wcstol(str, nullptr, 10));
}

long long _wtoi64(const char16_t* str)
{
	return wcstoll(str, nullptr, 10);
}

double _wtof(const char16_t* str)
{
	return wcstod(str, nullptr);
}

char16_t* _itow(int value, char16_t* buffer, int radix)
{
	// The CRT prints negative numbers with a sign only in base 10.
	if (radix != 10 && value < 0)
		return integerToString(static_cast<unsigned int>(value), buffer, radix);
	return integerToString(value, buffer, radix);
}

char16_t* _ltow(long value, char16_t* buffer, int radix)
{
	return _itow(static_cast<int>(value), buffer, radix);
}

char16_t* _ultow(unsigned long value, char16_t* buffer, int radix)
{
	return integerToString(static_cast<unsigned int>(value), buffer, radix);
}

char16_t* _i64tow(long long value, char16_t* buffer, int radix)
{
	if (radix != 10 && value < 0)
		return integerToString(static_cast<unsigned long long>(value), buffer, radix);
	return integerToString(value, buffer, radix);
}

char16_t* _ui64tow(unsigned long long value, char16_t* buffer, int radix)
{
	return integerToString(value, buffer, radix);
}

errno_t _itow_s(int value, char16_t* buffer, size_t size, int radix)
{
	char16_t temp[72]{};
	_itow(value, temp, radix);
	return wcscpy_s(buffer, size, temp);
}

namespace
{
	// A CRT mode string without its CRT-only parts: "t" (text) and ", ccs=..." (encoding); for pipes,
	// "b" (binary) too.
	template <typename Char>
	std::string posixMode(const Char* mode, bool pipe)
	{
		std::string result;
		for (const Char* p = mode; *p != '\0' && *p != ','; ++p)
		{
			if (*p != 't' && *p != ' ' && !(pipe && *p == 'b'))
				result.push_back(static_cast<char>(*p));
		}
		return result;
	}
}

FILE* _wfopen(const char16_t* fileName, const char16_t* mode)
{
	return std::fopen(nppmac::nativePath(std::u16string_view{fileName}).c_str(), posixMode(mode, false).c_str());
}

int _mbsicmp(const unsigned char* lhs, const unsigned char* rhs)
{
	return _wcsicmp(nppmac::toUtf16(reinterpret_cast<const char*>(lhs)).c_str(), nppmac::toUtf16(reinterpret_cast<const char*>(rhs)).c_str());
}

errno_t _wcserror_s(char16_t* buffer, size_t size, int error)
{
	if (!buffer || size == 0)
		return EINVAL;
	wcsncpy_s(buffer, size, nppmac::toUtf16(std::strerror(error)).c_str(), _TRUNCATE);
	return 0;
}

FILE* _wfreopen(const char16_t* fileName, const char16_t* mode, FILE* file)
{
	return std::freopen(nppmac::nativePath(std::u16string_view{fileName}).c_str(), posixMode(mode, false).c_str(), file);
}

FILE* _popen(const char* command, const char* mode)
{
	return popen(command, posixMode(mode, true).c_str());
}

FILE* _wpopen(const char16_t* command, const char16_t* mode)
{
	return popen(nppmac::toUtf8(command).c_str(), posixMode(mode, true).c_str());
}

int _pclose(FILE* file)
{
	return pclose(file);
}

int _wsystem(const char16_t* command)
{
	return std::system(command ? nppmac::toUtf8(command).c_str() : nullptr);
}

char16_t* _wgetenv(const char16_t* name)
{
	// The CRT keeps a wide copy of the environment; here, each variable asked for, kept until it changes.
	static std::mutex mutex;
	static std::map<std::u16string, std::u16string> values;

	const char* value = std::getenv(nppmac::toUtf8(name).c_str());
	if (!value)
		return nullptr;
	std::scoped_lock lock{mutex};
	std::u16string& wide = values[name];
	const std::u16string current = nppmac::toUtf16(value);
	if (wide != current)
		wide = current;
	return wide.data();
}

int _wstati64(const char16_t* path, struct stat* buffer)
{
	return stat(nppmac::nativePath(std::u16string_view{path}).c_str(), buffer);
}

errno_t _wfopen_s(FILE** file, const char16_t* fileName, const char16_t* mode)
{
	if (!file)
		return EINVAL;
	*file = _wfopen(fileName, mode);
	return *file ? 0 : errno;
}

namespace
{
	// strftime's text as UTF-16, when it fits the buffer with its terminator; 0 otherwise.
	size_t copyTime(char16_t* buffer, size_t size, std::string_view narrow)
	{
		if (narrow.empty())
			return 0;
		const std::u16string wide = nppmac::toUtf16(narrow);
		if (wide.size() >= size)
			return 0;
		std::copy(wide.begin(), wide.end(), buffer);
		buffer[wide.size()] = u'\0';
		return wide.size();
	}
}

size_t wcsftime(char16_t* buffer, size_t size, const char16_t* format, const struct tm* time)
{
	char narrow[1024]{};
	const size_t length = std::strftime(narrow, sizeof(narrow), nppmac::toUtf8(format).c_str(), time);
	return copyTime(buffer, size, std::string_view{narrow, length});
}

size_t wcsftime_l(char16_t* buffer, size_t size, const char16_t* format, const struct tm* time, locale_t locale)
{
	char narrow[1024]{};
	const size_t length = strftime_l(narrow, sizeof(narrow), nppmac::toUtf8(format).c_str(), time, locale);
	return copyTime(buffer, size, std::string_view{narrow, length});
}

size_t wcstombs(char* dest, const char16_t* src, size_t count)
{
	// The "ANSI" code page of this build is UTF-8.
	const std::string text = nppmac::toUtf8(src);
	if (!dest)
		return text.size();
	const size_t n = std::min(text.size(), count);
	std::memcpy(dest, text.data(), n);
	if (n < count)
		dest[n] = '\0';
	return n;
}

size_t mbstowcs(char16_t* dest, const char* src, size_t count)
{
	const std::u16string text = nppmac::toUtf16(src);
	if (!dest)
		return text.size();
	const size_t n = std::min(text.size(), count);
	std::copy_n(text.data(), n, dest);
	if (n < count)
		dest[n] = u'\0';
	return n;
}

namespace
{
	// _splitpath_s and _wsplitpath_s: a path's drive ("C:"), folder (with its last separator), name and
	// extension (with its dot).
	template <typename Char>
	errno_t splitPath(const Char* path, Char* drive, size_t driveSize, Char* dir, size_t dirSize, Char* fileName, size_t fileNameSize,
		Char* ext, size_t extSize)
	{
		using View = std::basic_string_view<Char>;
		if (!path)
			return EINVAL;

		View rest{path};
		View drivePart;
		if (rest.size() >= 2 && rest[1] == Char{':'})
		{
			drivePart = rest.substr(0, 2);
			rest.remove_prefix(2);
		}

		const Char separators[] = {Char{'/'}, Char{'\\'}, Char{}};
		const size_t lastSeparator = rest.find_last_of(separators);
		const View dirPart = lastSeparator == View::npos ? View{} : rest.substr(0, lastSeparator + 1);
		const View namePart = lastSeparator == View::npos ? rest : rest.substr(lastSeparator + 1);
		const size_t lastDot = namePart.find_last_of(Char{'.'});
		const View stemPart = lastDot == View::npos ? namePart : namePart.substr(0, lastDot);
		const View extPart = lastDot == View::npos ? View{} : namePart.substr(lastDot);

		auto copyPart = [](Char* dest, size_t destSize, View part) -> errno_t
		{
			if (!dest)
				return 0;
			if (part.size() >= destSize)
			{
				if (destSize > 0)
					dest[0] = Char{};
				return ERANGE;
			}
			std::copy(part.begin(), part.end(), dest);
			dest[part.size()] = Char{};
			return 0;
		};

		errno_t result = copyPart(drive, driveSize, drivePart);
		if (result == 0)
			result = copyPart(dir, dirSize, dirPart);
		if (result == 0)
			result = copyPart(fileName, fileNameSize, stemPart);
		if (result == 0)
			result = copyPart(ext, extSize, extPart);
		return result;
	}
}

errno_t _wsplitpath_s(const char16_t* path, char16_t* drive, size_t driveSize, char16_t* dir, size_t dirSize,
	char16_t* fileName, size_t fileNameSize, char16_t* ext, size_t extSize)
{
	return splitPath(path, drive, driveSize, dir, dirSize, fileName, fileNameSize, ext, extSize);
}

errno_t _splitpath_s(const char* path, char* drive, size_t driveSize, char* dir, size_t dirSize, char* fileName, size_t fileNameSize,
	char* ext, size_t extSize)
{
	return splitPath(path, drive, driveSize, dir, dirSize, fileName, fileNameSize, ext, extSize);
}

void _wsplitpath(const char16_t* path, char16_t* drive, char16_t* dir, char16_t* fileName, char16_t* ext)
{
	_wsplitpath_s(path, drive, drive ? _MAX_DRIVE : 0, dir, dir ? _MAX_DIR : 0, fileName, fileName ? _MAX_FNAME : 0, ext, ext ? _MAX_EXT : 0);
}

errno_t _wmakepath_s(char16_t* path, size_t size, const char16_t* drive, const char16_t* dir, const char16_t* fileName, const char16_t* ext)
{
	std::u16string result;
	if (drive && *drive)
		result += drive;
	if (dir && *dir)
	{
		result += dir;
		if (result.back() != u'/' && result.back() != u'\\')
			result += u'/';
	}
	if (fileName)
		result += fileName;
	if (ext && *ext)
	{
		if (*ext != u'.')
			result += u'.';
		result += ext;
	}
	return wcscpy_s(path, size, result.c_str());
}

// --- "Secure" variants ----------------------------------------------------------------------------

errno_t wcscpy_s(char16_t* dest, size_t destSize, const char16_t* src)
{
	if (!dest || destSize == 0)
		return EINVAL;
	if (!src)
	{
		dest[0] = u'\0';
		return EINVAL;
	}
	const size_t length = wcslen(src);
	if (length >= destSize)
	{
		dest[0] = u'\0';
		return ERANGE;
	}
	std::copy_n(src, length + 1, dest);
	return 0;
}

errno_t wcsncpy_s(char16_t* dest, size_t destSize, const char16_t* src, size_t count)
{
	if (!dest || destSize == 0)
		return EINVAL;
	if (!src)
	{
		dest[0] = u'\0';
		return EINVAL;
	}
	size_t length = wcsnlen(src, count == _TRUNCATE ? destSize - 1 : count);
	if (length >= destSize)
	{
		if (count != _TRUNCATE)
		{
			dest[0] = u'\0';
			return ERANGE;
		}
		length = destSize - 1;
	}
	std::copy_n(src, length, dest);
	dest[length] = u'\0';
	return count == _TRUNCATE && wcslen(src) > length ? STRUNCATE : 0;
}

errno_t wcscat_s(char16_t* dest, size_t destSize, const char16_t* src)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t existing = wcsnlen(dest, destSize);
	if (existing == destSize || !src)
	{
		dest[0] = u'\0';
		return EINVAL;
	}
	return wcscpy_s(dest + existing, destSize - existing, src) == 0 ? 0 : (dest[0] = u'\0', ERANGE);
}

errno_t wcsncat_s(char16_t* dest, size_t destSize, const char16_t* src, size_t count)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t existing = wcsnlen(dest, destSize);
	if (existing == destSize || !src)
	{
		dest[0] = u'\0';
		return EINVAL;
	}
	return wcsncpy_s(dest + existing, destSize - existing, src, count);
}

errno_t strcpy_s(char* dest, size_t destSize, const char* src)
{
	if (!dest || destSize == 0)
		return EINVAL;
	if (!src)
	{
		dest[0] = '\0';
		return EINVAL;
	}
	const size_t length = std::strlen(src);
	if (length >= destSize)
	{
		dest[0] = '\0';
		return ERANGE;
	}
	std::memcpy(dest, src, length + 1);
	return 0;
}

errno_t strncpy_s(char* dest, size_t destSize, const char* src, size_t count)
{
	if (!dest || destSize == 0)
		return EINVAL;
	if (!src)
	{
		dest[0] = '\0';
		return EINVAL;
	}
	size_t length = strnlen(src, count == _TRUNCATE ? destSize - 1 : count);
	if (length >= destSize)
	{
		if (count != _TRUNCATE)
		{
			dest[0] = '\0';
			return ERANGE;
		}
		length = destSize - 1;
	}
	std::memcpy(dest, src, length);
	dest[length] = '\0';
	return 0;
}

errno_t strcat_s(char* dest, size_t destSize, const char* src)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t existing = strnlen(dest, destSize);
	if (existing == destSize || !src)
	{
		dest[0] = '\0';
		return EINVAL;
	}
	return strcpy_s(dest + existing, destSize - existing, src) == 0 ? 0 : (dest[0] = '\0', ERANGE);
}

// --- nppmac:: helpers -----------------------------------------------------------------------------

namespace nppmac
{
	std::string nativePath(std::string_view path)
	{
		if (path.starts_with("\\\\?\\"))
			path.remove_prefix(4);
		std::string result{path};
		std::replace(result.begin(), result.end(), '\\', '/');
		return result;
	}

	std::string nativePath(std::u16string_view path)
	{
		return nativePath(std::string_view{toUtf8(path)});
	}

	Utf16RestBuffer::int_type Utf16RestBuffer::underflow()
	{
		if (gptr() < egptr())
			return traits_type::to_int_type(*gptr());
		if (_read)
			return traits_type::eof();
		_read = true;
		_text = toUtf16(std::string{std::istreambuf_iterator<char>{_source}, std::istreambuf_iterator<char>{}});
		setg(_text.data(), _text.data(), _text.data() + _text.size());
		return _text.empty() ? traits_type::eof() : traits_type::to_int_type(_text.front());
	}

	FILE* fopen(const char* path, const char* mode)
	{
		return std::fopen(nativePath(std::string_view{path}).c_str(), mode);
	}

	std::string toUtf8(std::u16string_view str)
	{
		std::string result;
		result.reserve(str.size());
		for (size_t i = 0; i < str.size(); ++i)
		{
			char32_t cp = str[i];
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < str.size() && str[i + 1] >= 0xDC00 && str[i + 1] <= 0xDFFF)
			{
				cp = 0x10000 + ((cp - 0xD800) << 10) + (str[i + 1] - 0xDC00);
				++i;
			}
			else if (cp >= 0xD800 && cp <= 0xDFFF)
			{
				cp = 0xFFFD; // unpaired surrogate
			}

			if (cp < 0x80)
			{
				result.push_back(static_cast<char>(cp));
			}
			else if (cp < 0x800)
			{
				result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else if (cp < 0x10000)
			{
				result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else
			{
				result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
				result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		return result;
	}

	std::u16string toUtf16(std::string_view str)
	{
		std::u16string result;
		result.reserve(str.size());
		size_t i = 0;
		while (i < str.size())
		{
			const unsigned char lead = static_cast<unsigned char>(str[i]);
			char32_t cp = 0xFFFD;
			size_t length = 1;
			if (lead < 0x80)
			{
				cp = lead;
			}
			else if ((lead & 0xE0) == 0xC0)
			{
				length = 2;
				cp = lead & 0x1F;
			}
			else if ((lead & 0xF0) == 0xE0)
			{
				length = 3;
				cp = lead & 0x0F;
			}
			else if ((lead & 0xF8) == 0xF0)
			{
				length = 4;
				cp = lead & 0x07;
			}

			bool valid = lead < 0x80 || length > 1;
			if (length > 1)
			{
				if (i + length > str.size())
				{
					valid = false;
				}
				else
				{
					for (size_t k = 1; k < length; ++k)
					{
						const unsigned char next = static_cast<unsigned char>(str[i + k]);
						if ((next & 0xC0) != 0x80)
						{
							valid = false;
							break;
						}
						cp = (cp << 6) | (next & 0x3F);
					}
				}
			}

			if (!valid)
			{
				result.push_back(0xFFFD);
				++i;
				continue;
			}

			if (cp >= 0x10000)
			{
				cp -= 0x10000;
				result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
				result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
			}
			else
			{
				result.push_back(static_cast<char16_t>(cp));
			}
			i += length;
		}
		return result;
	}

	u16stringstream& u16stringstream::operator<<(decltype(std::setfill(u'0')) fill)
	{
		// libc++'s setfill object stores the fill character as its only member.
		char16_t ch = u' ';
		static_assert(sizeof(fill) == sizeof(char16_t));
		std::memcpy(&ch, &fill, sizeof(ch));
		_stream.fill(static_cast<char>(ch));
		return *this;
	}

	u16stringstream& u16stringstream::operator>>(std::u16string& text)
	{
		std::string narrow;
		_stream >> narrow;
		text = toUtf16(narrow);
		return *this;
	}

	bool getline(u16stringstream& stream, std::u16string& line, char16_t delimiter)
	{
		std::string narrow;
		const bool ok = static_cast<bool>(std::getline(stream.narrow(), narrow, static_cast<char>(delimiter)));
		line = toUtf16(narrow);
		return ok;
	}

	namespace
	{
		template <typename T, typename Converter>
		T convertNumber(const std::u16string& str, size_t* pos, Converter converter)
		{
			const std::string shadow = asciiShadow(str.c_str());
			return converter(shadow, pos);
		}
	}

	int stoi(const std::u16string& str, size_t* pos, int base)
	{
		return convertNumber<int>(str, pos, [base](const std::string& s, size_t* p) { return std::stoi(s, p, base); });
	}

	long stol(const std::u16string& str, size_t* pos, int base)
	{
		return convertNumber<long>(str, pos, [base](const std::string& s, size_t* p) { return std::stol(s, p, base); });
	}

	unsigned long stoul(const std::u16string& str, size_t* pos, int base)
	{
		return convertNumber<unsigned long>(str, pos, [base](const std::string& s, size_t* p) { return std::stoul(s, p, base); });
	}

	long long stoll(const std::u16string& str, size_t* pos, int base)
	{
		return convertNumber<long long>(str, pos, [base](const std::string& s, size_t* p) { return std::stoll(s, p, base); });
	}

	unsigned long long stoull(const std::u16string& str, size_t* pos, int base)
	{
		return convertNumber<unsigned long long>(str, pos, [base](const std::string& s, size_t* p) { return std::stoull(s, p, base); });
	}

	float stof(const std::u16string& str, size_t* pos)
	{
		return convertNumber<float>(str, pos, [](const std::string& s, size_t* p) { return std::stof(s, p); });
	}

	double stod(const std::u16string& str, size_t* pos)
	{
		return convertNumber<double>(str, pos, [](const std::string& s, size_t* p) { return std::stod(s, p); });
	}

	// <locale> classification for char16_t: Unicode classification of the code unit; the locale
	// argument is ignored (upstream passes the classic or the system locale).
#define NPPMAC_CHAR_CLASS_IMPL(name, test) \
	bool name(char16_t ch, const std::locale&) \
	{ \
		return (ch < 0xD800 || ch > 0xDFFF) && test(static_cast<wint_t>(ch), unicodeLocale()) != 0; \
	}

	NPPMAC_CHAR_CLASS_IMPL(isdigit, iswdigit_l)
	NPPMAC_CHAR_CLASS_IMPL(isspace, iswspace_l)
	NPPMAC_CHAR_CLASS_IMPL(isalpha, iswalpha_l)
	NPPMAC_CHAR_CLASS_IMPL(isalnum, iswalnum_l)
	NPPMAC_CHAR_CLASS_IMPL(isupper, iswupper_l)
	NPPMAC_CHAR_CLASS_IMPL(islower, iswlower_l)
	NPPMAC_CHAR_CLASS_IMPL(ispunct, iswpunct_l)
	NPPMAC_CHAR_CLASS_IMPL(isxdigit, iswxdigit_l)
	NPPMAC_CHAR_CLASS_IMPL(iscntrl, iswcntrl_l)
	NPPMAC_CHAR_CLASS_IMPL(isprint, iswprint_l)
	NPPMAC_CHAR_CLASS_IMPL(isgraph, iswgraph_l)
	NPPMAC_CHAR_CLASS_IMPL(isblank, iswblank_l)
#undef NPPMAC_CHAR_CLASS_IMPL

	char16_t tolower(char16_t ch, const std::locale&)
	{
		return toLower16(ch);
	}

	char16_t toupper(char16_t ch, const std::locale&)
	{
		return toUpper16(ch);
	}
}

// --- std::ctype<char16_t> (nppmac/Char16Facets.h) -------------------------------------------------

namespace std
{
	locale::id ctype<char16_t>::id;
	locale::id numpunct<char16_t>::id;

	bool ctype<char16_t>::is(mask m, char16_t ch) const
	{
		const wint_t wc = static_cast<wint_t>(ch);
		const locale_t loc = unicodeLocale();
		return ((m & space) && iswspace_l(wc, loc)) || ((m & print) && iswprint_l(wc, loc))
			|| ((m & cntrl) && iswcntrl_l(wc, loc)) || ((m & upper) && iswupper_l(wc, loc))
			|| ((m & lower) && iswlower_l(wc, loc)) || ((m & alpha) && iswalpha_l(wc, loc))
			|| ((m & digit) && iswdigit_l(wc, loc)) || ((m & punct) && iswpunct_l(wc, loc))
			|| ((m & xdigit) && iswxdigit_l(wc, loc)) || ((m & blank) && iswblank_l(wc, loc));
	}

	const char16_t* ctype<char16_t>::is(const char16_t* low, const char16_t* high, mask* vec) const
	{
		static constexpr mask allMasks[] = {space, print, cntrl, upper, lower, alpha, digit, punct, xdigit, blank};
		for (; low != high; ++low, ++vec)
		{
			*vec = 0;
			for (const mask m : allMasks)
			{
				if (is(m, *low))
					*vec |= m;
			}
		}
		return high;
	}

	const char16_t* ctype<char16_t>::scan_is(mask m, const char16_t* low, const char16_t* high) const
	{
		while (low != high && !is(m, *low))
			++low;
		return low;
	}

	const char16_t* ctype<char16_t>::scan_not(mask m, const char16_t* low, const char16_t* high) const
	{
		while (low != high && is(m, *low))
			++low;
		return low;
	}

	char16_t ctype<char16_t>::toupper(char16_t ch) const
	{
		return toUpper16(ch);
	}

	const char16_t* ctype<char16_t>::toupper(char16_t* low, const char16_t* high) const
	{
		for (; low != high; ++low)
			*low = toUpper16(*low);
		return high;
	}

	char16_t ctype<char16_t>::tolower(char16_t ch) const
	{
		return toLower16(ch);
	}

	const char16_t* ctype<char16_t>::tolower(char16_t* low, const char16_t* high) const
	{
		for (; low != high; ++low)
			*low = toLower16(*low);
		return high;
	}

	char16_t ctype<char16_t>::widen(char ch) const
	{
		return static_cast<char16_t>(static_cast<unsigned char>(ch));
	}

	const char* ctype<char16_t>::widen(const char* low, const char* high, char16_t* dest) const
	{
		for (; low != high; ++low, ++dest)
			*dest = widen(*low);
		return high;
	}

	char ctype<char16_t>::narrow(char16_t ch, char defaultChar) const
	{
		return ch < 0x80 ? static_cast<char>(ch) : defaultChar;
	}

	const char16_t* ctype<char16_t>::narrow(const char16_t* low, const char16_t* high, char defaultChar, char* dest) const
	{
		for (; low != high; ++low, ++dest)
			*dest = narrow(*low, defaultChar);
		return high;
	}
}

void nppmac::installChar16Facets()
{
	std::locale withFacets{std::locale{}, new std::ctype<char16_t>};
	withFacets = std::locale{withFacets, new std::numpunct<char16_t>};
	withFacets = std::locale{withFacets, new std::num_get<char16_t>};
	withFacets = std::locale{withFacets, new std::num_put<char16_t>};
	// An unnamed locale: the C library's locale stays as it is.
	std::locale::global(withFacets);
}
