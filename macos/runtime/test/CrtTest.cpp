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

// Tests of the runtime's C library layer: Microsoft printf semantics, char16_t string functions,
// "secure" functions, conversions and the nppmac:: std replacements.

#include <windows.h>
#include <strsafe.h>
#include <codecvt>
#include <cstdio>
#include <iostream>
#include <regex>
#include <string>
#include <unistd.h>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void check(bool condition, const char* expression, int line)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::cerr << "FAIL line " << line << ": " << expression << "\n";
		}
	}

	void checkEqual(const std::u16string& actual, const std::u16string& expected, int line)
	{
		++g_checks;
		if (actual != expected)
		{
			++g_failures;
			std::cerr << "FAIL line " << line << ": got \"" << nppmac::toUtf8(actual) << "\", expected \"" << nppmac::toUtf8(expected) << "\"\n";
		}
	}

	void checkEqual(const std::string& actual, const std::string& expected, int line)
	{
		++g_checks;
		if (actual != expected)
		{
			++g_failures;
			std::cerr << "FAIL line " << line << ": got \"" << actual << "\", expected \"" << expected << "\"\n";
		}
	}

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_EQUAL(actual, expected) checkEqual((actual), (expected), __LINE__)

	std::u16string wideFormat(const char16_t* format, ...)
	{
		char16_t buffer[256]{};
		va_list args;
		va_start(args, format);
		vswprintf(buffer, 256, format, args);
		va_end(args);
		return buffer;
	}

	void testWidePrintf()
	{
		CHECK_EQUAL(wideFormat(u"%d|%5d|%-5d|%05d", 42, 42, 42, 42), u"42|   42|42   |00042");
		CHECK_EQUAL(wideFormat(u"%s and %S", u"wide", "narrow"), u"wide and narrow");
		CHECK_EQUAL(wideFormat(u"%ls %hs %ws", u"l", "h", u"w"), u"l h w");
		CHECK_EQUAL(wideFormat(u"%c%C%lc", u'A', 'B', u'C'), u"ABC");
		CHECK_EQUAL(wideFormat(u"%.3s|%6.2s|", u"abcdef", u"xyz"), u"abc|    xy|");
		CHECK_EQUAL(wideFormat(u"%I64d %I64u %lld", -5LL, 18446744073709551615ULL, 7LL), u"-5 18446744073709551615 7");
		CHECK_EQUAL(wideFormat(u"%Iu %zu", static_cast<size_t>(123), static_cast<size_t>(456)), u"123 456");
		CHECK_EQUAL(wideFormat(u"%x %X %#x %o", 255, 255, 255, 8), u"ff FF 0xff 10");
		CHECK_EQUAL(wideFormat(u"%.2f %e %g", 3.14159, 1000.0, 0.5), u"3.14 1.000000e+03 0.5");
		CHECK_EQUAL(wideFormat(u"100%%"), u"100%");
		CHECK_EQUAL(wideFormat(u"%*d|%-*d|", 4, 1, 4, 2), u"   1|2   |");
		CHECK_EQUAL(wideFormat(u"%s", static_cast<const char16_t*>(nullptr)), u"(null)");
		CHECK_EQUAL(wideFormat(u"\u00e9%s\u4e2d", u"\u00fc"), u"\u00e9\u00fc\u4e2d");

		// Windows is LLP64: %lu reads 32 bits. A DWORD must format correctly.
		const DWORD dw = 4000000000U;
		CHECK_EQUAL(wideFormat(u"%lu", dw), u"4000000000");
		const LONG negative = -7;
		CHECK_EQUAL(wideFormat(u"%ld", negative), u"-7");
	}

	void testNarrowPrintf()
	{
		char buffer[128]{};
		nppmac::sprintf(buffer, "%s=%lu", "count", static_cast<DWORD>(3000000000U));
		CHECK_EQUAL(std::string{buffer}, std::string{"count=3000000000"});

		nppmac::sprintf(buffer, "%S and %ls", u"wide1", u"wide2");
		CHECK_EQUAL(std::string{buffer}, std::string{"wide1 and wide2"});

		CHECK(nppmac::snprintf(buffer, 4, "%d", 123456) == 6);
		CHECK_EQUAL(std::string{buffer}, std::string{"123"});

		char tooSmall[8]{}; // "small" is a macro in the Windows headers (rpcndr.h)
		CHECK(nppmac::sprintf_s(tooSmall, "%s", "toolongvalue") == -1);
		CHECK(tooSmall[0] == '\0');
	}

	void testSecureAndCounted()
	{
		char16_t buffer[8]{};
		CHECK(wcscpy_s(buffer, u"1234567") == 0);
		CHECK_EQUAL(std::u16string{buffer}, u"1234567");
		CHECK(wcscpy_s(buffer, u"12345678") != 0);
		CHECK(buffer[0] == u'\0');

		CHECK(wcsncpy_s(buffer, 8, u"abcdefghij", _TRUNCATE) == STRUNCATE);
		CHECK_EQUAL(std::u16string{buffer}, u"abcdefg");

		wcscpy_s(buffer, u"ab");
		CHECK(wcscat_s(buffer, u"cd") == 0);
		CHECK_EQUAL(std::u16string{buffer}, u"abcd");

		CHECK(swprintf(buffer, 4, u"%d", 12345) == -1);
		CHECK_EQUAL(std::u16string{buffer}, u"123");

		CHECK(swprintf(buffer, u"%s=%d", u"key", 7) == 5);
		CHECK_EQUAL(std::u16string{buffer}, u"key=7");

		CHECK(_snwprintf_s(buffer, 8, _TRUNCATE, u"%s", u"0123456789") == -1);
		CHECK_EQUAL(std::u16string{buffer}, u"0123456");

		CHECK(StringCchCopyW(buffer, 8, u"0123456789") == STRSAFE_E_INSUFFICIENT_BUFFER);
		CHECK_EQUAL(std::u16string{buffer}, u"0123456");
		CHECK(StringCchPrintfW(buffer, 8, u"%d-%s", 7, u"x") == S_OK);
		CHECK_EQUAL(std::u16string{buffer}, u"7-x");
	}

	void testStringFunctions()
	{
		CHECK(wcslen(u"hello") == 5);
		CHECK(wcscmp(u"abc", u"abd") < 0);
		CHECK(wcsncmp(u"abcx", u"abcy", 3) == 0);
		CHECK(_wcsicmp(u"Notepad++", u"NOTEPAD++") == 0);
		CHECK(_wcsicmp(u"\u00c9t\u00e9", u"\u00e9T\u00c9") == 0);
		CHECK(_wcsnicmp(u"ABCdef", u"abcXYZ", 3) == 0);
		CHECK(wcschr(u"a/b/c", u'/') != nullptr);
		CHECK_EQUAL(std::u16string{wcsrchr(u"a/b/c", u'/')}, u"/c");
		CHECK_EQUAL(std::u16string{wcsstr(u"hello world", u"wor")}, u"world");
		CHECK(wcsspn(u"   x", u" ") == 3);

		char16_t* end = nullptr;
		CHECK(wcstol(u"  -42rest", &end, 10) == -42);
		CHECK_EQUAL(std::u16string{end}, u"rest");
		CHECK(wcstoul(u"ff", nullptr, 16) == 255);
		CHECK(_wtoi(u"123") == 123);
		CHECK(wcstod(u"2.5x", nullptr) == 2.5);

		char16_t number[40]{};
		CHECK_EQUAL(std::u16string{_itow(-123, number, 10)}, u"-123");
		CHECK_EQUAL(std::u16string{_itow(255, number, 16)}, u"ff");

		char16_t lower[] = u"MiXeD \u00c9";
		CHECK_EQUAL(std::u16string{_wcslwr(lower)}, u"mixed \u00e9");

		char16_t tokens[] = u"a,b,,c";
		char16_t* context = nullptr;
		std::u16string joined;
		for (char16_t* token = wcstok(tokens, u",", &context); token; token = wcstok(nullptr, u",", &context))
			joined += std::u16string{token} + u"|";
		CHECK_EQUAL(joined, u"a|b|c|");

		char16_t drive[_MAX_DRIVE]{};
		char16_t dir[_MAX_DIR]{};
		char16_t fname[_MAX_FNAME]{};
		char16_t ext[_MAX_EXT]{};
		CHECK(_wsplitpath_s(u"/Users/me/file.tar.gz", drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT) == 0);
		CHECK_EQUAL(std::u16string{dir}, u"/Users/me/");
		CHECK_EQUAL(std::u16string{fname}, u"file.tar");
		CHECK_EQUAL(std::u16string{ext}, u".gz");
		CHECK(_wsplitpath_s(u"C:\\dir\\x.txt", drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT) == 0);
		CHECK_EQUAL(std::u16string{drive}, u"C:");
		CHECK_EQUAL(std::u16string{dir}, u"\\dir\\");
	}

	void testConversionsAndStd()
	{
		const std::u16string mixed = u"a\u00e9\u4e2d\U0001F600";
		CHECK_EQUAL(nppmac::toUtf16(nppmac::toUtf8(mixed)), mixed);
		CHECK_EQUAL(nppmac::toUtf8(u"\u00e9"), std::string{"\xc3\xa9"});
		CHECK_EQUAL(nppmac::toUtf16("\xff"), std::u16string{u"\ufffd"});

		CHECK_EQUAL(nppmac::to_u16string(1234), u"1234");
		CHECK(nppmac::stoi(std::u16string{u"77"}) == 77);
		CHECK(nppmac::stoi(std::string{"78"}) == 78);

		nppmac::u16stringstream ss;
		ss << u"value=" << 42 << u' ' << std::hex << 255;
		CHECK_EQUAL(ss.str(), u"value=42 ff");

		nppmac::u16stringstream strings;
		char16_t buffer16[8] = u"buf";
		strings << std::u16string{u"text"} << u'!' << buffer16;
		CHECK_EQUAL(strings.str(), u"text!buf");
		CHECK(strings.getloc() == std::locale::classic());

		nppmac::u16stringstream hex;
		hex << std::uppercase << std::setfill(char16_t('0')) << std::setw(2) << std::hex << 10;
		CHECK_EQUAL(hex.str(), u"0A");

		nppmac::u16stringstream lines{u"one\ntwo"};
		std::u16string line;
		std::u16string all;
		while (nppmac::getline(lines, line))
			all += line + u";";
		CHECK_EQUAL(all, u"one;two;");

		// UTF-8 to and from UTF-16 (std::wstring_convert of a wchar_t codecvt once migrated).
		nppmac::wstring_convert<std::codecvt_utf8_utf16<char16_t>> utf16;
		CHECK_EQUAL(utf16.from_bytes("caf\xC3\xA9 \xF0\x9F\x98\x80"), u"caf\u00E9 \U0001F600");
		CHECK(utf16.to_bytes(u"caf\u00E9") == "caf\xC3\xA9");
		nppmac::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> ucs(new std::codecvt_utf8<char16_t>);
		CHECK_EQUAL(ucs.from_bytes(std::string{"\xF0\x9F\x98\x80"}), u"\U0001F600");

		// Wide file streams (std::wifstream once migrated): byte for byte, as Windows' classic locale
		// reads (a BOM through get()), or UTF-16 with codecvt_utf16 imbued. (libc++'s consume_header only
		// takes a big-endian BOM, Microsoft's either: little-endian text says so.)
		{
			char path[] = "/tmp/npp-wide-stream-XXXXXX";
			const int fd = mkstemp(path);
			const char bytes[] = "\xFF\xFE" "h\0" "\xE9\0" "\n\0" "x\0";
			write(fd, bytes, sizeof(bytes) - 1);
			close(fd);
			nppmac::wifstream bom{std::string{path}, std::ios::binary};
			CHECK(bom.get() == 0xFF && bom.get() == 0xFE && bom.get() == u'h');
			nppmac::wifstream utf16{nppmac::toUtf16(path), std::ios::binary};
			utf16.imbue(std::locale(utf16.getloc(), new std::codecvt_utf16<char16_t, 0x10FFFF, std::codecvt_mode(std::consume_header | std::little_endian)>));
			std::u16string line;
			CHECK(static_cast<bool>(nppmac::getline(utf16, line)) && line == u"h\u00E9");
			CHECK(static_cast<bool>(nppmac::getline(utf16, line)) && line == u"x");
			unlink(path);
		}

		// Regular expressions on UTF-16 text (std::wregex once migrated).
		const std::u16string sentence = u"Hello World, hello again 123";
		CHECK_EQUAL(std::regex_replace(sentence, nppmac::wregex(u"hello", std::regex_constants::icase), u"bye"), u"bye World, bye again 123");
		nppmac::wsmatch found;
		CHECK(std::regex_search(sentence, found, nppmac::wregex(u"(\\d+)")) && found.position(1) == 25 && found[1].str() == u"123");
		const nppmac::wregex word(u"[[:alpha:]]+");
		int words = 0;
		for (nppmac::wsregex_iterator it(sentence.begin(), sentence.end(), word), end; it != end; ++it)
			++words;
		CHECK(words == 4);
		CHECK(std::regex_match(u"a.c", nppmac::wregex(u"a.c")));
		CHECK(!std::regex_match(u"a\nc", nppmac::wregex(u"a.c")));

		CHECK(nppmac::isdigit(u'7', std::locale::classic()));
		CHECK(!nppmac::isdigit(u'x', std::locale::classic()));
		CHECK(nppmac::tolower(u'\u00c9', std::locale::classic()) == u'\u00e9');
		CHECK(nppmac::isspace(' '));

		CHECK_EQUAL(nppmac::format(u"{}-{}", 1, u"two"), u"1-two");

		nppmac::filesystem::path p{u"/tmp/dir/file.txt"};
		CHECK_EQUAL(std::u16string{p.c_str()}, u"/tmp/dir/file.txt");
		CHECK_EQUAL(p.parent_path().wstring(), u"/tmp/dir");
		CHECK_EQUAL(p.extension().wstring(), u".txt");
		p.replace_extension(".xml");
		CHECK_EQUAL(p.wstring(), u"/tmp/dir/file.xml");
		CHECK_EQUAL((nppmac::filesystem::path{u"/a"} / u"b").wstring(), u"/a/b");
	}
}

int main()
{
	testWidePrintf();
	testNarrowPrintf();
	testSecureAndCounted();
	testStringFunctions();
	testConversionsAndStd();

	std::cout << (g_checks - g_failures) << "/" << g_checks << " CRT checks passed\n";
	return g_failures == 0 ? 0 : 1;
}
