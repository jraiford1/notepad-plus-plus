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

// Entry point of the macOS app: the runtime prepares the process (command line, AppKit), then
// upstream's own wWinMain (PowerEditor/src/winmain.cpp) runs exactly as on Windows.

#include <windows.h>

#include "nppmac/Startup.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nShowCmd);

int main(int argc, char* argv[])
{
	nppmac::startup(argc, argv);
	return wWinMain(GetModuleHandleW(nullptr), nullptr, nppmac::commandLineArguments(), SW_SHOWDEFAULT);
}
