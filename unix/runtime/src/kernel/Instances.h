// This file is part of Notepad++ project (macOS and Linux build)
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


// Where the program's copies find each other: the named objects that Windows shares between processes
// (a named mutex says whether another copy has it), and each copy's window server (user/RemoteWindows.cpp).

#pragma once

#include <string>
#include <string_view>

namespace nppmac::kernel
{
	// A folder of the user's own (0700): the user's runtime folder on Linux, their temporary folder on the
	// Mac; NPPMAC_INSTANCE_DIR names another. A test run (NPPMAC_TEST_RUN) without it has none: its
	// copies never meet another, the user's own included. Empty when there is none.
	const std::string& instanceDirectory();

	// Claims a name for this process, as long as it lives (a named mutex's): false when another live
	// process holds it. Always true without an instance folder.
	bool claimName(std::u16string_view name);
}
