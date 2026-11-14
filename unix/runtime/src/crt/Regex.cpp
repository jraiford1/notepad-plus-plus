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

// What '.' matches in a char16_t regular expression (nppmac/Regex.h): any character but a line end,
// as libc++ defines it for wchar_t.

#include "nppmac/Regex.h"

template <>
void std::__match_any_but_newline<char16_t>::__exec(std::__state<char16_t>& state) const
{
	if (state.__current_ == state.__last_)
	{
		state.__do_ = std::__state<char16_t>::__reject;
		state.__node_ = nullptr;
		return;
	}
	switch (*state.__current_)
	{
		case u'\r':
		case u'\n':
		case 0x2028: // line separator
		case 0x2029: // paragraph separator
			state.__do_ = std::__state<char16_t>::__reject;
			state.__node_ = nullptr;
			break;
		default:
			state.__do_ = std::__state<char16_t>::__accept_and_consume;
			++state.__current_;
			state.__node_ = this->first();
			break;
	}
}
