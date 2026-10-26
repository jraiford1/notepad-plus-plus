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

#include "MigrationRules.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	struct TestCase
	{
		const char* _name = nullptr;
		std::string _input;
		std::string _expected;
		nppmigrate::Language _language = nppmigrate::Language::cpp;
	};

	const std::vector<TestCase> g_testCases = {
		{"wchar_t type", "const wchar_t* p = nullptr;", "const char16_t* p = nullptr;"},
		{"wide string literal", "auto s = L\"abc\";", "auto s = u\"abc\";"},
		{"wide char literal", "if (c == L'\\\\')", "if (c == u'\\\\')"},
		{"wide raw string literal", "auto r = LR\"(a\"b)\";", "auto r = uR\"(a\"b)\";"},
		{"concatenated literals", "L\"a\" \"b\" L\"c\"", "u\"a\" \"b\" u\"c\""},
		{"narrow literal untouched", "auto s = \"L\"; auto t = u8\"x\";", "auto s = \"L\"; auto t = u8\"x\";"},
		{"comment untouched", "// wchar_t L\"x\" std::wstring\nint i;", "// wchar_t L\"x\" std::wstring\nint i;"},
		{"string content untouched", "auto s = \"wchar_t wstring\";", "auto s = \"wchar_t wstring\";"},
		{"token paste", "#define TEXT(x) L ## x", "#define TEXT(x) u ## x"},
		{"token paste without spaces", "#define TEXT(x) L##x", "#define TEXT(x) u##x"},
		{"std::wstring", "std::wstring s;", "std::u16string s;"},
		{"unqualified wstring", "using namespace std;\nwstring s;", "using namespace std;\nu16string s;"},
		{"wstring_view", "std::wstring_view v;", "std::u16string_view v;"},
		{"wstring npos", "if (pos == std::wstring::npos)", "if (pos == std::u16string::npos)"},
		{"wstringstream", "std::wstringstream ss;", "nppmac::u16stringstream ss;"},
		{"wregex", "std::wregex re(L\"a+\"); std::wsmatch m;", "nppmac::wregex re(u\"a+\"); nppmac::wsmatch m;"},
		{"wregex unqualified", "wsregex_iterator it;", "nppmac::wsregex_iterator it;"},
		{"wifstream", "std::wifstream in{path};", "nppmac::wifstream in{path};"},
		{"C file", "FILE* f = fopen(name, \"r\"); const wchar_t* w = L\"x\";", "FILE* f = fopen(name, \"r\"); const char16_t* w = u\"x\";", nppmigrate::Language::c},
		{"wstring_convert", "std::wstring_convert<std::codecvt_utf8<wchar_t>> c;", "nppmac::wstring_convert<std::codecvt_utf8<char16_t>> c;"},
		{"unqualified to_wstring", "auto s = to_wstring(42);", "auto s = nppmac::to_u16string(42);"},
		{"std::to_wstring", "auto s = std::to_wstring(42);", "auto s = nppmac::to_u16string(42);"},
		{"std::getline", "while (std::getline(ss, line))", "while (nppmac::getline(ss, line))"},
		{"member getline untouched", "file.getline(buf, n);", "file.getline(buf, n);"},
		{"wchar_t template argument", "std::basic_istream<wchar_t>& s; reinterpret_cast<const wchar_t*>(p)", "std::basic_istream<char16_t>& s; reinterpret_cast<const char16_t*>(p)"},
		{"std::isdigit with locale", "std::isdigit(c, loc) && std::tolower(c)", "nppmac::isdigit(c, loc) && nppmac::tolower(c)"},
		{"unqualified isdigit untouched", "isdigit(c)", "isdigit(c)"},
		{"literal after line continuation", "#define TXT \\\nL\"a\"", "#define TXT \\\nu\"a\""},
		{"literal after CRLF line continuation", "#define TXT \\\r\nL\"a\"", "#define TXT \\\r\nu\"a\""},
		{"std::filesystem", "namespace fs = std::filesystem; std::filesystem::path p;", "namespace fs = nppmac::filesystem; nppmac::filesystem::path p;"},
		{"std::stoi", "int n = std::stoi(s); long m = stol(t);", "int n = nppmac::stoi(s); long m = nppmac::stol(t);"},
		{"ifstream", "std::ifstream in(path); ofstream out;", "nppmac::ifstream in(path); nppmac::ofstream out;"},
		{"std::format", "auto s = std::format(L\"{}\", n);", "auto s = nppmac::format(u\"{}\", n);"},
		{"include with Windows separators", "#include \"Docking\\Dlg.h\"\n#include \"a\\\\b.h\"", "#include \"Docking/Dlg.h\"\n#include \"a/b.h\""},
		{"include directive untouched", "#include <fstream>\n#include <format>\n#include \"wstring.h\"\nstd::wstring s;", "#include <fstream>\n#include <format>\n#include \"wstring.h\"\nstd::u16string s;"},
		{"unqualified format untouched", "EolType format; format = f(format);", "EolType format; format = f(format);"},
		{"unqualified fstream untouched", "fstream f;", "fstream f;"},
		{"sprintf family", "sprintf(buf, \"%lu\", n); ::snprintf(b, 4, f); std::sprintf(c, f);", "nppmac::sprintf(buf, \"%lu\", n); ::nppmac::snprintf(b, 4, f); nppmac::sprintf(c, f);"},
		{"fopen", "FILE* f = fopen(path, \"rb\"); f = ::fopen(p, m);", "FILE* f = nppmac::fopen(path, \"rb\"); f = ::nppmac::fopen(p, m);"},
		{"std::wcslen", "size_t n = std::wcslen(p);", "size_t n = ::wcslen(p);"},
		{"unqualified wcslen untouched", "size_t n = wcslen(p);", "size_t n = wcslen(p);"},
		{"member named wstring untouched", "obj.wstring = 1; ptr->to_wstring();", "obj.wstring = 1; ptr->to_wstring();"},
		{"other namespace untouched", "pugi::wstring a; Foo<int>::wstring b;", "pugi::wstring a; Foo<int>::wstring b;"},
		{"global qualified", "::wstring a;", "::u16string a;"},
		{"identifier containing wchar_t untouched", "int mywchar_t = 0;", "int mywchar_t = 0;"},
		{"line structure kept", "L\"a\"\r\n\twchar_t c;\r\n", "u\"a\"\r\n\tchar16_t c;\r\n"},
	};
}

int main()
{
	int failures = 0;
	for (const TestCase& testCase : g_testCases)
	{
		const std::string actual = nppmigrate::migrateSource(testCase._input, testCase._language)._text;
		if (actual != testCase._expected)
		{
			++failures;
			std::cerr << "FAIL: " << testCase._name << "\n  expected: " << testCase._expected << "\n  actual:   " << actual << "\n";
		}
		else if (nppmigrate::migrateSource(actual, testCase._language)._text != actual && testCase._input != testCase._expected)
		{
			// Running the rules on their own output must change nothing.
			++failures;
			std::cerr << "FAIL (not idempotent): " << testCase._name << "\n";
		}
	}

	std::cout << (g_testCases.size() - failures) << "/" << g_testCases.size() << " migration rule tests passed\n";
	return failures == 0 ? 0 : 1;
}
