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

// Tests of initialization files (GetPrivateProfileString and family), where plugins keep settings.

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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

#define CHECK(expression) check((expression), #expression, __LINE__)
}

int main()
{
	char folderTemplate[] = "/tmp/npp-profile.XXXXXX";
	const std::string folder = ::mkdtemp(folderTemplate);
	const std::u16string file = nppmac::toUtf16(folder + "/plugin.ini");
	char16_t buffer[256]{};

	// Missing file: the default, without its trailing blanks.
	CHECK(GetPrivateProfileStringW(u"Main", u"Key", u"none  ", buffer, 256, file.c_str()) == 4);
	CHECK(std::u16string{buffer} == u"none");
	CHECK(GetPrivateProfileIntW(u"Main", u"Count", 7, file.c_str()) == 7);

	CHECK(WritePrivateProfileStringW(u"Main", u"Key", u"value", file.c_str()));
	CHECK(WritePrivateProfileStringW(u"Main", u"Count", u"42", file.c_str()));
	CHECK(WritePrivateProfileStringW(u"Other", u"Quoted", u"\"spaced value \"", file.c_str()));
	CHECK(GetPrivateProfileStringW(u"main", u"KEY", u"", buffer, 256, file.c_str()) == 5); // names ignore case
	CHECK(std::u16string{buffer} == u"value");
	CHECK(GetPrivateProfileIntW(u"Main", u"Count", 0, file.c_str()) == 42);
	CHECK(GetPrivateProfileStringW(u"Other", u"Quoted", u"", buffer, 256, file.c_str()) == 13); // quotes stripped
	CHECK(std::u16string{buffer} == u"spaced value ");

	// Lists: section names, then a section's keys, each terminated and the list terminated again.
	CHECK(GetPrivateProfileSectionNamesW(buffer, 256, file.c_str()) == 11);
	CHECK(std::u16string(buffer, 11) == std::u16string(u"Main\0Other\0", 11));
	CHECK(GetPrivateProfileStringW(u"Main", nullptr, u"", buffer, 256, file.c_str()) == 10);
	CHECK(std::u16string(buffer, 10) == std::u16string(u"Key\0Count\0", 10));

	// A value cut to a small buffer.
	CHECK(GetPrivateProfileStringW(u"Main", u"Key", u"", buffer, 3, file.c_str()) == 2);
	CHECK(std::u16string{buffer} == u"va");

	// Deleting a key, then a section.
	CHECK(WritePrivateProfileStringW(u"Main", u"Key", nullptr, file.c_str()));
	CHECK(GetPrivateProfileStringW(u"Main", u"Key", u"gone", buffer, 256, file.c_str()) == 4);
	CHECK(WritePrivateProfileStringW(u"Other", nullptr, nullptr, file.c_str()));
	CHECK(GetPrivateProfileSectionNamesW(buffer, 256, file.c_str()) == 5);

	// ANSI.
	CHECK(WritePrivateProfileStringA("Narrow", "Name", "text", nppmac::toUtf8(file).c_str()));
	char narrow[64]{};
	CHECK(GetPrivateProfileStringA("Narrow", "Name", "", narrow, 64, nppmac::toUtf8(file).c_str()) == 4);
	CHECK(std::string{narrow} == "text");

	std::system(("rm -rf '" + folder + "'").c_str());
	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
