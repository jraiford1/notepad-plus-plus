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

// printf formatting with Microsoft CRT semantics, for both the narrow and the wide function families.
//
// What differs from the macOS C library:
// - Windows is LLP64: "l" is 32-bit (%ld, %lu, %lx read an int), "ll" and "I64" are 64-bit,
//   "I", "z", "t", "j" are pointer-sized.
// - In the wide family %s and %c take wide (char16_t) arguments and %S / %C narrow ones; in the
//   narrow family it is the other way round. "h" forces narrow, "l" and "w" force wide.
// - Narrow output is UTF-8, wide output UTF-16.

#pragma once

#include <cstdarg>
#include <string>

namespace nppmac::crt
{
	enum class FormatFamily
	{
		narrow,
		wide,
	};

	std::string formatNarrow(const char* format, va_list args);
	std::u16string formatWide(const char16_t* format, va_list args);
}
