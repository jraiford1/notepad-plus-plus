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

// kernel32 and user32 string functions: lstr*, code page conversions, case mapping, comparisons.

#include "Immortal.h"
#include "Internal.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cerrno>
#include <iconv.h>
#include <mutex>
#include <unordered_map>

namespace
{
	// The "ANSI" code page reported to Notepad++: the one a Windows system set to the same
	// language would use. It is what Notepad++ means by "ANSI" for documents without a BOM.
	UINT ansiCodePageForLanguage(const std::string& language)
	{
		static const std::pair<const char*, UINT> codePages[] = {
			{"ja", 932}, {"zh-Hans", 936}, {"zh-CN", 936}, {"zh-SG", 936}, {"zh", 950}, {"ko", 949},
			{"th", 874}, {"vi", 1258}, {"ru", 1251}, {"uk", 1251}, {"be", 1251}, {"bg", 1251}, {"sr", 1251},
			{"mk", 1251}, {"kk", 1251}, {"el", 1253}, {"tr", 1254}, {"az", 1254}, {"he", 1255}, {"ar", 1256},
			{"fa", 1256}, {"ur", 1256}, {"lt", 1257}, {"lv", 1257}, {"et", 1257}, {"cs", 1250}, {"pl", 1250},
			{"hu", 1250}, {"sk", 1250}, {"sl", 1250}, {"hr", 1250}, {"ro", 1250}, {"sq", 1250}, {"bs", 1250},
		};
		for (const auto& [prefix, codePage] : codePages)
		{
			const std::string_view tag{prefix};
			if (language.compare(0, tag.size(), tag) == 0 && (language.size() == tag.size() || language[tag.size()] == '-'))
				return codePage;
		}
		return 1252;
	}

	UINT ansiCodePage()
	{
		static const UINT codePage = ansiCodePageForLanguage(nppmac::platform::preferredLanguage());
		return codePage;
	}

	UINT resolveCodePage(UINT codePage)
	{
		switch (codePage)
		{
			case CP_ACP:
			case CP_THREAD_ACP:
				return ansiCodePage();
			case CP_OEMCP:
				return 437;
			case CP_MACCP:
				return 10000;
			default:
				return codePage;
		}
	}

	std::string iconvName(UINT codePage)
	{
		switch (codePage)
		{
			case CP_UTF8:
				return "UTF-8";
			case CP_UTF7:
				return "UTF-7";
			case 1200:
				return "UTF-16LE";
			case 1201:
				return "UTF-16BE";
			case 12000:
				return "UTF-32LE";
			case 12001:
				return "UTF-32BE";
			case 10000:
				return "MACINTOSH";
			case 20866:
				return "KOI8-R";
			case 21866:
				return "KOI8-U";
			case 20932:
			case 51932:
				return "EUC-JP";
			case 51936:
				return "EUC-CN";
			case 51949:
				return "EUC-KR";
			case 936:
				return "GBK";
			case 54936:
				return "GB18030";
			case 950:
				return "BIG5";
			case 50220:
			case 50221:
			case 50222:
				return "ISO-2022-JP";
			case 20127:
				return "ASCII";
			default:
				break;
		}
		if (codePage >= 28591 && codePage <= 28606)
			return "ISO-8859-" + std::to_string(codePage - 28590);
		return "CP" + std::to_string(codePage);
	}

	// iconv descriptors are cached per (from, to) pair and used under one lock.
	nppmac::Immortal<std::mutex> g_iconvMutexStorage;
	std::mutex& g_iconvMutex = g_iconvMutexStorage._value;

	iconv_t descriptor(const std::string& to, const std::string& from)
	{
		static std::unordered_map<std::string, iconv_t> cache;
		const std::string key = to + "<" + from;
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
		iconv_t cd = iconv_open(to.c_str(), from.c_str());
		cache.emplace(key, cd);
		return cd;
	}

	// Converts with iconv, replacing invalid or unrepresentable input when allowed.
	// Returns false (and leaves output partial) on invalid input when strict.
	bool convert(const std::string& to, const std::string& from, const char* input, size_t inputSize,
		std::string& output, bool strict, const char* replacement, size_t replacementSize, size_t inputUnit, bool* usedReplacement)
	{
		std::lock_guard lock{g_iconvMutex};
		iconv_t cd = descriptor(to, from);
		if (cd == reinterpret_cast<iconv_t>(-1))
			return false;
		iconv(cd, nullptr, nullptr, nullptr, nullptr);

		output.clear();
		char buffer[4096];
		char* in = const_cast<char*>(input);
		size_t inLeft = inputSize;
		while (inLeft > 0)
		{
			char* out = buffer;
			size_t outLeft = sizeof(buffer);
			const size_t result = iconv(cd, &in, &inLeft, &out, &outLeft);
			output.append(buffer, static_cast<size_t>(out - buffer));
			if (result == static_cast<size_t>(-1))
			{
				if (errno == E2BIG)
					continue;
				if (strict)
					return false;
				// EILSEQ or EINVAL: replace one input unit and go on.
				output.append(replacement, replacementSize);
				if (usedReplacement)
					*usedReplacement = true;
				const size_t skip = std::min(inputUnit, inLeft);
				in += skip;
				inLeft -= skip;
				iconv(cd, nullptr, nullptr, nullptr, nullptr);
			}
		}
		char* out = buffer;
		size_t outLeft = sizeof(buffer);
		iconv(cd, nullptr, nullptr, &out, &outLeft);
		output.append(buffer, static_cast<size_t>(out - buffer));
		return true;
	}

	char16_t lowerChar(char16_t ch)
	{
		return nppmac::tolower(ch, std::locale::classic());
	}

	char16_t upperChar(char16_t ch)
	{
		return nppmac::toupper(ch, std::locale::classic());
	}
}

// --- Code pages -----------------------------------------------------------------------------------

UINT WINAPI GetACP()
{
	return ansiCodePage();
}

UINT WINAPI GetOEMCP()
{
	return 437;
}

BOOL WINAPI IsValidCodePage(UINT codePage)
{
	if (codePage == CP_UTF8 || codePage == 1200 || codePage == 1201)
		return TRUE;
	std::lock_guard lock{g_iconvMutex};
	return descriptor("UTF-16LE", iconvName(codePage)) != reinterpret_cast<iconv_t>(-1);
}

BOOL WINAPI GetCPInfo(UINT codePage, LPCPINFO info)
{
	const UINT resolved = resolveCodePage(codePage);
	if (!IsValidCodePage(resolved))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	std::memset(info, 0, sizeof(*info));
	const bool doubleByte = resolved == 932 || resolved == 936 || resolved == 949 || resolved == 950 || resolved == 54936;
	info->MaxCharSize = resolved == CP_UTF8 ? 4 : (doubleByte ? 2 : 1);
	info->DefaultChar[0] = '?';
	if (doubleByte)
	{
		// Lead byte ranges, as Windows reports them.
		const BYTE ranges932[] = {0x81, 0x9F, 0xE0, 0xFC};
		const BYTE rangesOther[] = {0x81, 0xFE};
		const BYTE* ranges = resolved == 932 ? ranges932 : rangesOther;
		const size_t count = resolved == 932 ? 4 : 2;
		std::copy_n(ranges, count, info->LeadByte);
	}
	return TRUE;
}

BOOL WINAPI GetCPInfoExW(UINT codePage, DWORD, LPCPINFOEXW info)
{
	CPINFO basic{};
	if (!GetCPInfo(codePage, &basic))
		return FALSE;
	std::memset(info, 0, sizeof(*info));
	info->MaxCharSize = basic.MaxCharSize;
	std::copy(std::begin(basic.DefaultChar), std::end(basic.DefaultChar), info->DefaultChar);
	std::copy(std::begin(basic.LeadByte), std::end(basic.LeadByte), info->LeadByte);
	info->UnicodeDefaultChar = u'?';
	info->CodePage = resolveCodePage(codePage);
	const std::u16string name = nppmac::toUtf16(iconvName(info->CodePage));
	wcsncpy_s(info->CodePageName, MAX_PATH, name.c_str(), _TRUNCATE);
	return TRUE;
}

int WINAPI MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR input, int inputSize, LPWSTR output, int outputSize)
{
	if (!input || inputSize == 0 || outputSize < 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const size_t length = inputSize < 0 ? std::strlen(input) + 1 : static_cast<size_t>(inputSize);
	const UINT resolved = resolveCodePage(codePage);

	std::u16string wide;
	if (resolved == CP_UTF8)
	{
		if (flags & MB_ERR_INVALID_CHARS)
		{
			std::string check;
			if (!convert("UTF-16LE", "UTF-8", input, length, check, true, nullptr, 0, 1, nullptr))
			{
				SetLastError(ERROR_NO_UNICODE_TRANSLATION);
				return 0;
			}
		}
		wide = nppmac::toUtf16(std::string_view{input, length});
	}
	else
	{
		std::string bytes;
		const char16_t replacement = u'?';
		if (!convert("UTF-16LE", iconvName(resolved), input, length, bytes, (flags & MB_ERR_INVALID_CHARS) != 0,
			reinterpret_cast<const char*>(&replacement), sizeof(replacement), 1, nullptr))
		{
			SetLastError((flags & MB_ERR_INVALID_CHARS) ? ERROR_NO_UNICODE_TRANSLATION : ERROR_INVALID_PARAMETER);
			return 0;
		}
		wide.assign(reinterpret_cast<const char16_t*>(bytes.data()), bytes.size() / sizeof(char16_t));
	}

	if (outputSize == 0)
		return static_cast<int>(wide.size());
	if (wide.size() > static_cast<size_t>(outputSize))
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	std::copy(wide.begin(), wide.end(), output);
	return static_cast<int>(wide.size());
}

int WINAPI WideCharToMultiByte(UINT codePage, DWORD, LPCWSTR input, int inputSize, LPSTR output, int outputSize, LPCSTR defaultChar, LPBOOL usedDefaultChar)
{
	if (!input || inputSize == 0 || outputSize < 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const size_t length = inputSize < 0 ? wcslen(input) + 1 : static_cast<size_t>(inputSize);
	const UINT resolved = resolveCodePage(codePage);

	std::string bytes;
	bool usedReplacement = false;
	if (resolved == CP_UTF8)
	{
		bytes = nppmac::toUtf8(std::u16string_view{input, length});
	}
	else
	{
		const char replacement = defaultChar ? *defaultChar : '?';
		convert(iconvName(resolved), "UTF-16LE", reinterpret_cast<const char*>(input), length * sizeof(char16_t), bytes, false,
			&replacement, 1, sizeof(char16_t), &usedReplacement);
	}
	if (usedDefaultChar)
		*usedDefaultChar = usedReplacement ? TRUE : FALSE;

	if (outputSize == 0)
		return static_cast<int>(bytes.size());
	if (bytes.size() > static_cast<size_t>(outputSize))
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	std::copy(bytes.begin(), bytes.end(), output);
	return static_cast<int>(bytes.size());
}

// --- lstr* ----------------------------------------------------------------------------------------

int WINAPI lstrlenW(LPCWSTR str)
{
	return str ? static_cast<int>(wcslen(str)) : 0;
}

int WINAPI lstrlenA(LPCSTR str)
{
	return str ? static_cast<int>(std::strlen(str)) : 0;
}

LPWSTR WINAPI lstrcpyW(LPWSTR dest, LPCWSTR src)
{
	return wcscpy(dest, src ? src : u"");
}

LPSTR WINAPI lstrcpyA(LPSTR dest, LPCSTR src)
{
	return std::strcpy(dest, src ? src : "");
}

LPWSTR WINAPI lstrcpynW(LPWSTR dest, LPCWSTR src, int count)
{
	if (count <= 0)
		return dest;
	int i = 0;
	for (; i < count - 1 && src && src[i] != u'\0'; ++i)
		dest[i] = src[i];
	dest[i] = u'\0';
	return dest;
}

LPSTR WINAPI lstrcpynA(LPSTR dest, LPCSTR src, int count)
{
	if (count <= 0)
		return dest;
	int i = 0;
	for (; i < count - 1 && src && src[i] != '\0'; ++i)
		dest[i] = src[i];
	dest[i] = '\0';
	return dest;
}

LPWSTR WINAPI lstrcatW(LPWSTR dest, LPCWSTR src)
{
	return wcscat(dest, src ? src : u"");
}

int WINAPI lstrcmpW(LPCWSTR lhs, LPCWSTR rhs)
{
	return wcscmp(lhs ? lhs : u"", rhs ? rhs : u"");
}

int WINAPI lstrcmpiW(LPCWSTR lhs, LPCWSTR rhs)
{
	return _wcsicmp(lhs ? lhs : u"", rhs ? rhs : u"");
}

int WINAPI lstrcmpA(LPCSTR lhs, LPCSTR rhs)
{
	return std::strcmp(lhs ? lhs : "", rhs ? rhs : "");
}

int WINAPI lstrcmpiA(LPCSTR lhs, LPCSTR rhs)
{
	return strcasecmp(lhs ? lhs : "", rhs ? rhs : "");
}

// --- Case and classification (user32) -------------------------------------------------------------

LPWSTR WINAPI CharLowerW(LPWSTR str)
{
	// A pointer whose high bits are zero is a single character, as in user32.
	if (reinterpret_cast<uintptr_t>(str) <= 0xFFFF)
		return reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(lowerChar(static_cast<char16_t>(reinterpret_cast<uintptr_t>(str)))));
	for (LPWSTR p = str; *p != u'\0'; ++p)
		*p = lowerChar(*p);
	return str;
}

LPWSTR WINAPI CharUpperW(LPWSTR str)
{
	if (reinterpret_cast<uintptr_t>(str) <= 0xFFFF)
		return reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(upperChar(static_cast<char16_t>(reinterpret_cast<uintptr_t>(str)))));
	for (LPWSTR p = str; *p != u'\0'; ++p)
		*p = upperChar(*p);
	return str;
}

DWORD WINAPI CharLowerBuffW(LPWSTR str, DWORD length)
{
	for (DWORD i = 0; i < length; ++i)
		str[i] = lowerChar(str[i]);
	return length;
}

DWORD WINAPI CharUpperBuffW(LPWSTR str, DWORD length)
{
	for (DWORD i = 0; i < length; ++i)
		str[i] = upperChar(str[i]);
	return length;
}

BOOL WINAPI IsCharAlphaW(WCHAR ch)
{
	return nppmac::isalpha(ch, std::locale::classic()) ? TRUE : FALSE;
}

BOOL WINAPI IsCharAlphaNumericW(WCHAR ch)
{
	return nppmac::isalnum(ch, std::locale::classic()) ? TRUE : FALSE;
}

BOOL WINAPI IsCharLowerW(WCHAR ch)
{
	return nppmac::islower(ch, std::locale::classic()) ? TRUE : FALSE;
}

BOOL WINAPI IsCharUpperW(WCHAR ch)
{
	return nppmac::isupper(ch, std::locale::classic()) ? TRUE : FALSE;
}

int WINAPI CompareStringOrdinal(LPCWCH lhs, int lhsLength, LPCWCH rhs, int rhsLength, BOOL ignoreCase)
{
	const std::u16string_view a{lhs, lhsLength < 0 ? wcslen(lhs) : static_cast<size_t>(lhsLength)};
	const std::u16string_view b{rhs, rhsLength < 0 ? wcslen(rhs) : static_cast<size_t>(rhsLength)};
	const size_t common = std::min(a.size(), b.size());
	for (size_t i = 0; i < common; ++i)
	{
		const char16_t x = ignoreCase ? upperChar(a[i]) : a[i];
		const char16_t y = ignoreCase ? upperChar(b[i]) : b[i];
		if (x != y)
			return x < y ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
	}
	if (a.size() == b.size())
		return CSTR_EQUAL;
	return a.size() < b.size() ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
}

int WINAPI CompareStringW(LCID, DWORD flags, LPCWSTR lhs, int lhsLength, LPCWSTR rhs, int rhsLength)
{
	return CompareStringOrdinal(lhs, lhsLength, rhs, rhsLength, (flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE)) != 0);
}

int WINAPI CompareStringEx(LPCWSTR, DWORD flags, LPCWCH lhs, int lhsLength, LPCWCH rhs, int rhsLength, LPNLSVERSIONINFO, LPVOID, LPARAM)
{
	return CompareStringOrdinal(lhs, lhsLength, rhs, rhsLength, (flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE)) != 0);
}

int WINAPI LCMapStringEx(LPCWSTR, DWORD flags, LPCWSTR source, int sourceLength, LPWSTR dest, int destLength, LPNLSVERSIONINFO, LPVOID, LPARAM)
{
	std::u16string text{source, sourceLength < 0 ? wcslen(source) + 1 : static_cast<size_t>(sourceLength)};
	for (char16_t& ch : text)
	{
		if (flags & LCMAP_LOWERCASE)
			ch = lowerChar(ch);
		else if (flags & LCMAP_UPPERCASE)
			ch = upperChar(ch);
	}
	if (destLength == 0)
		return static_cast<int>(text.size());
	if (text.size() > static_cast<size_t>(destLength))
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	std::copy(text.begin(), text.end(), dest);
	return static_cast<int>(text.size());
}

BOOL WINAPI IsTextUnicode(const void* buffer, int size, LPINT result)
{
	// The statistical test Windows applies, reduced to its main signals: a UTF-16 BOM, or many
	// zero bytes at odd offsets (ASCII text in UTF-16LE).
	const auto* bytes = static_cast<const unsigned char*>(buffer);
	bool unicode = false;
	if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
	{
		unicode = true;
	}
	else if (size >= 2)
	{
		int oddZeros = 0;
		int evenZeros = 0;
		for (int i = 0; i + 1 < size; i += 2)
		{
			evenZeros += bytes[i] == 0;
			oddZeros += bytes[i + 1] == 0;
		}
		const int pairs = size / 2;
		unicode = oddZeros > pairs / 2 && evenZeros < pairs / 10;
	}
	if (result)
		*result = unicode ? IS_TEXT_UNICODE_STATISTICS : 0;
	return unicode ? TRUE : FALSE;
}

// --- wsprintf -------------------------------------------------------------------------------------

int WINAPIV wsprintfW(LPWSTR buffer, LPCWSTR format, ...)
{
	// user32's wsprintf writes at most 1024 characters.
	va_list args;
	va_start(args, format);
	const int result = _vsnwprintf(buffer, 1024, format, args);
	va_end(args);
	if (result < 0)
		buffer[1023] = u'\0';
	return result < 0 ? 1023 : result;
}

int WINAPIV wsprintfA(LPSTR buffer, LPCSTR format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = wvsprintfA(buffer, format, args);
	va_end(args);
	return result;
}

int WINAPI wvsprintfA(LPSTR buffer, LPCSTR format, va_list args)
{
	// At most 1024 characters, formatted as the Microsoft CRT does.
	const int result = nppmac::vsnprintf(buffer, 1024, format, args);
	if (result < 0 || result >= 1024)
	{
		buffer[1023] = '\0';
		return 1023;
	}
	return result;
}

int WINAPI wvsprintfW(LPWSTR buffer, LPCWSTR format, va_list args)
{
	const int result = _vsnwprintf(buffer, 1024, format, args);
	if (result < 0)
		buffer[1023] = u'\0';
	return result < 0 ? 1023 : result;
}
