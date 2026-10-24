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

// The libraries LoadLibrary loaded (plugins), for the parts of the runtime that need their files.

#pragma once

#include <windows.h>

#include <string>

namespace nppmac::kernel
{
	// The POSIX path of a library LoadLibrary loaded, or empty for any other module.
	std::string libraryPath(HMODULE module);
}
