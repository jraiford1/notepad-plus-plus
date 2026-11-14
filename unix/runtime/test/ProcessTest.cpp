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

// Tests of processes: command line splitting, CreateProcess with its output in a pipe, waiting for a
// process and its exit code, ShellExecuteEx of an executable.

#include <windows.h>
#include <shellapi.h>
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

	// Runs a command line with its output in a pipe; returns the output, and the exit code through code.
	std::string run(std::u16string commandLine, DWORD& code)
	{
		HANDLE readPipe = nullptr;
		HANDLE writePipe = nullptr;
		CreatePipe(&readPipe, &writePipe, nullptr, 0);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writePipe;
		startup.hStdError = writePipe;
		PROCESS_INFORMATION information{};
		const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information);
		CloseHandle(writePipe);
		std::string output;
		if (started)
		{
			char buffer[256];
			DWORD read = 0;
			while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
				output.append(buffer, read);
			CHECK(GetLastError() == ERROR_BROKEN_PIPE);
			CHECK(WaitForSingleObject(information.hProcess, 5000) == WAIT_OBJECT_0);
			GetExitCodeProcess(information.hProcess, &code);
			CloseHandle(information.hProcess);
			CloseHandle(information.hThread);
		}
		CloseHandle(readPipe);
		return output;
	}
}

int main()
{
	int count = 0;
	LPWSTR* arguments = CommandLineToArgvW(u"prog \"a b\" c\\\\\"d\" \"e\"\"f\" g\\h", &count);
	CHECK(count == 5);
	if (count == 5)
	{
		CHECK(std::u16string{arguments[1]} == u"a b");
		CHECK(std::u16string{arguments[2]} == u"c\\d");
		CHECK(std::u16string{arguments[3]} == u"e\"f");
		CHECK(std::u16string{arguments[4]} == u"g\\h");
	}
	LocalFree(arguments);

	DWORD code = 99;
	CHECK(run(u"cmd /c echo hello from a child", code) == "hello from a child\n");
	CHECK(code == 0);
	CHECK(run(u"/bin/sh -c \"exit 3\"", code).empty());
	CHECK(code == 3);
	CHECK(run(u"/bin/echo \"two words\" three", code) == "two words three\n");

	// A missing program fails to start.
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION information{};
	std::u16string missing = u"no-such-program-anywhere";
	CHECK(!CreateProcessW(nullptr, missing.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &information));

	// ShellExecuteEx of an executable runs it, and gives its process.
	SHELLEXECUTEINFOW execute{};
	execute.cbSize = sizeof(execute);
	execute.fMask = SEE_MASK_NOCLOSEPROCESS;
	execute.lpVerb = u"open";
	execute.lpFile = u"/usr/bin/true";
	CHECK(ShellExecuteExW(&execute));
	CHECK(execute.hProcess != nullptr);
	if (execute.hProcess)
	{
		CHECK(WaitForSingleObject(execute.hProcess, 5000) == WAIT_OBJECT_0);
		GetExitCodeProcess(execute.hProcess, &code);
		CHECK(code == 0);
		CloseHandle(execute.hProcess);
	}

	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
