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

// The Windows SDK's <versionhelpers.h>: whether this is a given Windows version or later, as the
// runtime reports its version (GetVersionEx).

#pragma once

#include <windows.h>

static inline BOOL IsWindowsVersionOrGreater(WORD major, WORD minor, WORD servicePack)
{
	OSVERSIONINFOEXW info;
	ZeroMemory(&info, sizeof(info));
	info.dwOSVersionInfoSize = sizeof(info);
	GetVersionExW((LPOSVERSIONINFOW)&info);
	if (info.dwMajorVersion != major)
		return info.dwMajorVersion > major;
	if (info.dwMinorVersion != minor)
		return info.dwMinorVersion > minor;
	return info.wServicePackMajor >= servicePack;
}

static inline BOOL IsWindowsXPOrGreater(void) { return IsWindowsVersionOrGreater(5, 1, 0); }
static inline BOOL IsWindowsXPSP1OrGreater(void) { return IsWindowsVersionOrGreater(5, 1, 1); }
static inline BOOL IsWindowsXPSP2OrGreater(void) { return IsWindowsVersionOrGreater(5, 1, 2); }
static inline BOOL IsWindowsXPSP3OrGreater(void) { return IsWindowsVersionOrGreater(5, 1, 3); }
static inline BOOL IsWindowsVistaOrGreater(void) { return IsWindowsVersionOrGreater(6, 0, 0); }
static inline BOOL IsWindowsVistaSP1OrGreater(void) { return IsWindowsVersionOrGreater(6, 0, 1); }
static inline BOOL IsWindowsVistaSP2OrGreater(void) { return IsWindowsVersionOrGreater(6, 0, 2); }
static inline BOOL IsWindows7OrGreater(void) { return IsWindowsVersionOrGreater(6, 1, 0); }
static inline BOOL IsWindows7SP1OrGreater(void) { return IsWindowsVersionOrGreater(6, 1, 1); }
static inline BOOL IsWindows8OrGreater(void) { return IsWindowsVersionOrGreater(6, 2, 0); }
static inline BOOL IsWindows8Point1OrGreater(void) { return IsWindowsVersionOrGreater(6, 3, 0); }
static inline BOOL IsWindowsThresholdOrGreater(void) { return IsWindowsVersionOrGreater(10, 0, 0); }
static inline BOOL IsWindows10OrGreater(void) { return IsWindowsVersionOrGreater(10, 0, 0); }
static inline BOOL IsWindowsServer(void) { return FALSE; }
