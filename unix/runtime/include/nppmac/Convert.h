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

// std::wstring_convert as Windows code means it: there wchar_t is UTF-16, so a converter between wide
// and byte strings (codecvt_utf8<wchar_t>, codecvt_utf8_utf16<wchar_t>...) converts UTF-8 to and from
// UTF-16. Migrated, wchar_t is char16_t, the default element: whatever the codecvt (libc++'s
// codecvt_utf8<char16_t> is UCS-2, and throws on characters outside the BMP), the conversion is
// UTF-8 to and from UTF-16. The engine renames wstring_convert to this; other element types
// (char32_t...) go to std::wstring_convert as they are.

#pragma once

#include <locale>
#include <memory>
#include <string>

#include "nppmac/WideString.h"

namespace nppmac
{
	template <class Codecvt, class Elem = char16_t, class WideAlloc = std::allocator<Elem>, class ByteAlloc = std::allocator<char>>
	class wstring_convert : public std::wstring_convert<Codecvt, Elem, WideAlloc, ByteAlloc>
	{
	public:
		using std::wstring_convert<Codecvt, Elem, WideAlloc, ByteAlloc>::wstring_convert;
	};

	template <class Codecvt, class WideAlloc, class ByteAlloc>
	class wstring_convert<Codecvt, char16_t, WideAlloc, ByteAlloc>
	{
	public:
		using byte_string = std::string;
		using wide_string = std::u16string;

		wstring_convert() = default;

		// The facet is the converter's to delete, as std::wstring_convert's is.
		explicit wstring_convert(Codecvt* facet) : _facet{facet} {}

		// Text that is not valid is replaced (U+FFFD) rather than thrown on, so these are not needed.
		explicit wstring_convert(const byte_string&, const wide_string& = {}) {}

		wide_string from_bytes(char byte) {
			return from_bytes(std::string_view{&byte, 1});
		}

		wide_string from_bytes(const char* text) {
			return from_bytes(std::string_view{text});
		}

		wide_string from_bytes(const byte_string& text) {
			return from_bytes(std::string_view{text});
		}

		wide_string from_bytes(const char* first, const char* last) {
			return from_bytes(std::string_view{first, static_cast<size_t>(last - first)});
		}

		byte_string to_bytes(char16_t unit) {
			return to_bytes(std::u16string_view{&unit, 1});
		}

		byte_string to_bytes(const char16_t* text) {
			return to_bytes(std::u16string_view{text});
		}

		byte_string to_bytes(const wide_string& text) {
			return to_bytes(std::u16string_view{text});
		}

		byte_string to_bytes(const char16_t* first, const char16_t* last) {
			return to_bytes(std::u16string_view{first, static_cast<size_t>(last - first)});
		}

		size_t converted() const noexcept {
			return _converted;
		}

	private:
		wide_string from_bytes(std::string_view text) {
			_converted = text.size();
			return toUtf16(text);
		}

		byte_string to_bytes(std::u16string_view text) {
			_converted = text.size();
			return toUtf8(text);
		}

		std::unique_ptr<Codecvt> _facet;
		size_t _converted = 0;
	};
}
