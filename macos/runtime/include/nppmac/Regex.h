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

// Regular expressions on UTF-16 text: std::wregex and its family (wsmatch, wsregex_iterator...) for
// char16_t, as plugins use them on what were wide strings. libc++ has no std::regex_traits<char16_t>
// (there is no ctype<char16_t> facet), so the traits here classify through regex_traits<wchar_t>
// (UTF-32 on macOS, which holds every UTF-16 code unit). The engine renames std::wregex and the rest
// to these; the std algorithms (regex_match, regex_search, regex_replace) take them as they are, and
// positions are in UTF-16 code units, as on Windows.

#pragma once

#include <regex>
#include <string>

namespace nppmac
{
	class u16regex_traits
	{
	public:
		using char_type = char16_t;
		using string_type = std::u16string;
		using locale_type = std::locale;
		using char_class_type = std::regex_traits<wchar_t>::char_class_type;

		static std::size_t length(const char_type* text) {
			return std::char_traits<char16_t>::length(text);
		}

		char_type translate(char_type c) const {
			return c;
		}

		char_type translate_nocase(char_type c) const {
			const wchar_t lower = _wide.translate_nocase(static_cast<wchar_t>(c));
			return lower <= 0xFFFF ? static_cast<char_type>(lower) : c;
		}

		// Collation by code unit (Windows' wregex collates by the locale only with regex::collate).
		template <class Iterator>
		string_type transform(Iterator first, Iterator last) const {
			return string_type(first, last);
		}

		template <class Iterator>
		string_type transform_primary(Iterator first, Iterator last) const {
			string_type text(first, last);
			for (char16_t& c : text)
				c = translate_nocase(c);
			return text;
		}

		template <class Iterator>
		string_type lookup_collatename(Iterator first, Iterator last) const {
			const std::wstring name = widen(first, last);
			const std::wstring element = _wide.lookup_collatename(name.begin(), name.end());
			string_type result;
			for (const wchar_t c : element)
				result += static_cast<char16_t>(c);
			return result;
		}

		template <class Iterator>
		char_class_type lookup_classname(Iterator first, Iterator last, bool icase = false) const {
			const std::wstring name = widen(first, last);
			return _wide.lookup_classname(name.begin(), name.end(), icase);
		}

		bool isctype(char_type c, char_class_type classes) const {
			return _wide.isctype(static_cast<wchar_t>(c), classes);
		}

		int value(char_type c, int radix) const {
			return _wide.value(static_cast<wchar_t>(c), radix);
		}

		locale_type imbue(locale_type locale) {
			return _wide.imbue(locale);
		}

		locale_type getloc() const {
			return _wide.getloc();
		}

	private:
		template <class Iterator>
		static std::wstring widen(Iterator first, Iterator last) {
			std::wstring text;
			for (; first != last; ++first)
				text += static_cast<wchar_t>(*first);
			return text;
		}

		std::regex_traits<wchar_t> _wide;
	};

	using wregex = std::basic_regex<char16_t, u16regex_traits>;
	using wcmatch = std::match_results<const char16_t*>;
	using wsmatch = std::match_results<std::u16string::const_iterator>;
	using wcsub_match = std::sub_match<const char16_t*>;
	using wssub_match = std::sub_match<std::u16string::const_iterator>;
	using wcregex_iterator = std::regex_iterator<const char16_t*, char16_t, u16regex_traits>;
	using wsregex_iterator = std::regex_iterator<std::u16string::const_iterator, char16_t, u16regex_traits>;
	using wcregex_token_iterator = std::regex_token_iterator<const char16_t*, char16_t, u16regex_traits>;
	using wsregex_token_iterator = std::regex_token_iterator<std::u16string::const_iterator, char16_t, u16regex_traits>;
}

// libc++ declares what '.' matches for every character type but defines it for char and wchar_t
// only: the runtime defines it for char16_t (Regex.cpp).
template <>
void std::__match_any_but_newline<char16_t>::__exec(std::__state<char16_t>& state) const;
