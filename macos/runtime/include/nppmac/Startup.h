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

// Process start-up for programs built on the runtime.

#pragma once

namespace nppmac
{
	// Sets up the CRT command line (__argc, __argv, __wargv, GetCommandLineW) and AppKit.
	void startup(int argc, char* argv[]);

	// The command line after the program name, as WinMain's pCmdLine receives it.
	char16_t* commandLineArguments();
}
