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

// std::from_chars as Visual C++'s library has it, floating point included. Apple's C++ library has the
// floating-point overloads only from macOS 26 on, later than the app's deployment target, so the
// migration engine makes std::from_chars nppmac::from_chars: floating point is read here, the same way
// (the C locale, no leading space or '+', no "0x" before hexadecimal), and integers go on to
// std::from_chars.

#pragma once

#include <charconv>
#include <type_traits>

namespace nppmac
{
	std::from_chars_result fromCharsFloat(const char* first, const char* last, float& value, std::chars_format format);
	std::from_chars_result fromCharsFloat(const char* first, const char* last, double& value, std::chars_format format);
	std::from_chars_result fromCharsFloat(const char* first, const char* last, long double& value, std::chars_format format);

	template <typename T>
		requires std::is_floating_point_v<T>
	std::from_chars_result from_chars(const char* first, const char* last, T& value, std::chars_format format = std::chars_format::general)
	{
		return fromCharsFloat(first, last, value, format);
	}

	template <typename T>
		requires std::is_integral_v<T>
	std::from_chars_result from_chars(const char* first, const char* last, T& value, int base = 10)
	{
		return std::from_chars(first, last, value, base);
	}
}
