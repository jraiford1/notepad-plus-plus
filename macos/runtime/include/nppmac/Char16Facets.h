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

// libc++ declares std::ctype only for char and wchar_t. After migration, upstream code (pugixml's
// wide stream overloads) instantiates std::basic_istream<char16_t> and std::basic_ostream<char16_t>,
// which need std::ctype<char16_t> to be a complete type. Notepad++ never streams char16_t text
// through them (text formatting uses nppmac::u16stringstream), so this facet exists to make those
// templates compile; it classifies like the classic "C" locale for the BMP.
//
// A future libc++ that ships its own std::ctype<char16_t> will make this a compile error, which
// is the intended signal to delete this file.

#pragma once

#include <locale>

namespace std
{
	template <>
	class ctype<char16_t> : public locale::facet, public ctype_base
	{
	public:
		using char_type = char16_t;

		explicit ctype(size_t refs = 0) : locale::facet(refs) {}

		bool is(mask m, char16_t ch) const;
		const char16_t* is(const char16_t* low, const char16_t* high, mask* vec) const;
		const char16_t* scan_is(mask m, const char16_t* low, const char16_t* high) const;
		const char16_t* scan_not(mask m, const char16_t* low, const char16_t* high) const;
		char16_t toupper(char16_t ch) const;
		const char16_t* toupper(char16_t* low, const char16_t* high) const;
		char16_t tolower(char16_t ch) const;
		const char16_t* tolower(char16_t* low, const char16_t* high) const;
		char16_t widen(char ch) const;
		const char* widen(const char* low, const char* high, char16_t* dest) const;
		char narrow(char16_t ch, char defaultChar) const;
		const char16_t* narrow(const char16_t* low, const char16_t* high, char defaultChar, char* dest) const;

		static locale::id id;

	protected:
		~ctype() override = default;
	};
}
