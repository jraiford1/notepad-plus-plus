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

// Tests of what Help > Debug Info and the toolbar's accent color read from the system: the registry
// values the runtime fills from macOS, and DwmGetColorizationColor.

#include <windows.h>
#include <dwmapi.h>
#include <cstdlib>
#include <iostream>
#include <string>

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

	std::u16string readString(const char16_t* keyPath, const char16_t* name)
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
			return {};
		char16_t value[128]{};
		DWORD size = sizeof(value) - sizeof(char16_t);
		DWORD type = REG_SZ;
		const LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
		RegCloseKey(key);
		return status == ERROR_SUCCESS ? std::u16string{value} : std::u16string{};
	}
}

int main()
{
	const char16_t* currentVersion = u"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
	const std::u16string product = readString(currentVersion, u"ProductName");
	const std::u16string version = readString(currentVersion, u"DisplayVersion");
	const std::u16string build = readString(currentVersion, u"CurrentBuildNumber");
	CHECK(product == u"macOS");
	CHECK(!version.empty() && version.find(u'.') != std::u16string::npos);
	CHECK(!build.empty());

	// The first graphics processor, where Debug Info lists display adapters.
	const std::u16string adapter = readString(u"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E968-E325-11CE-BFC1-08002BE10318}\\0000", u"DriverDesc");
	CHECK(!adapter.empty());

	DWORD color = 0;
	BOOL opaque = FALSE;
	CHECK(DwmGetColorizationColor(&color, &opaque) == S_OK);
	CHECK((color >> 24) == 0xFF);

	std::cout << nppmac::toUtf8(product) << " " << nppmac::toUtf8(version) << " (" << nppmac::toUtf8(build) << "), "
		<< nppmac::toUtf8(adapter) << ", accent " << std::hex << (color & 0xFFFFFF) << std::dec << "\n";
	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
