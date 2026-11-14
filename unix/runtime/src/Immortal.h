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

// A global the runtime never destroys. Upstream's own static objects call Win32 functions from their
// destructors when the program exits (NppDarkMode's theme deletes its pens and brushes), and the
// order in which statics of different files are destroyed is not defined: whatever those functions
// use must still be there. A locked destroyed mutex throws, and a throw from a destructor aborts.

#pragma once

namespace nppmac
{
	template <typename T>
	union Immortal
	{
		constexpr Immortal() : _value{} {}
		~Immortal() {}

		T _value;
	};
}
