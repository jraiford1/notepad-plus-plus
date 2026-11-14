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

// Helpers shared by the kernel32 implementation.

#pragma once

#include <windows.h>

#include <ctime>
#include <string>
#include <sys/stat.h>

namespace nppmac::kernel
{
	// A Win32 path as a macOS one: UTF-8, '\\' read as '/', the "\\\\?\\" long-path prefix dropped.
	std::string toPosixPath(const char16_t* path);
	std::string toPosixPath(std::u16string_view path);

	// A macOS path as Notepad++ sees it: UTF-16 with '/' separators.
	std::u16string fromPosixPath(std::string_view path);

	// Sets the Win32 last error from errno (or from the given errno value).
	void setErrorFromErrno(int error);
	void setErrorFromErrno();
	DWORD win32ErrorFromErrno(int error);

	// FILETIME is 100 ns intervals since 1601-01-01 UTC.
	FILETIME toFileTime(const timespec& time);
	timespec toTimespec(const FILETIME& time);
	FILETIME currentFileTime();

	DWORD fileAttributes(const std::string& posixPath, const struct stat& info);

	// Copies a result string into a caller's buffer with the Win32 conventions: returns the length
	// without the terminator on success, or the size needed including the terminator when the
	// buffer is too small.
	DWORD copyToBuffer(std::u16string_view text, LPWSTR buffer, DWORD size);
	DWORD copyToBuffer(std::string_view text, LPSTR buffer, DWORD size);

	// The descriptor of a file or pipe handle, or -1.
	int fileDescriptor(HANDLE handle);

	// Processes started by CreateProcess and ShellExecute: their exit code (STILL_ACTIVE while they run)
	// and their end. False for other handles.
	bool childExitCode(HANDLE handle, DWORD& exitCode);
	bool terminateChild(HANDLE handle, UINT exitCode);
	// Starts a program, as ShellExecute does an executable; its process handle, or null.
	HANDLE startProcess(std::u16string_view program, std::u16string_view parameters, const char16_t* directory);
}
