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

// macOS's key codes and Windows' virtual keys, for the Mac's window backend. Plain C++ types only: the
// AppKit side includes this.

#pragma once

#include <string_view>

namespace nppmac::ui
{
	// The Windows virtual key of a macOS key (0 when Windows has none): letters and digits by what the
	// layout makes of them, as Windows' are, other keys by their place.
	unsigned char virtualKeyForMacKey(unsigned short keyCode, std::u16string_view charactersIgnoringModifiers);
	// The macOS key whose key is a virtual key (letters and digits by their place on a US keyboard), or
	// -1 when there is none.
	int macKeyForVirtualKey(unsigned char virtualKey);
}
