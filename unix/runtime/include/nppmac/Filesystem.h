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

// nppmac::filesystem is std::filesystem with a Windows-shaped path: upstream passes path::c_str()
// to Win32 W functions and calls path::wstring(), both of which are UTF-16 on Windows. On macOS
// std::filesystem::path is UTF-8, so the migration engine sends std::filesystem here instead.
// Everything other than path is std::filesystem itself, which accepts this path through its
// conversion to std::filesystem::path.

#pragma once

#include <filesystem>
#include <iterator>
#include <ostream>

#include "nppmac/WideString.h"
#include <string>
#include <string_view>

namespace nppmac::filesystem
{
	using namespace std::filesystem;

	class path
	{
	public:
		using value_type = char16_t;
		using string_type = std::u16string;
		using format = std::filesystem::path::format;

		path() = default;
		path(const std::filesystem::path& p) : _path{p} {}
		path(std::filesystem::path&& p) : _path{std::move(p)} {}
		// Upstream's paths use '\\' separators; they become '/' here, as everywhere a path enters macOS.
		path(const char16_t* p) : _path{nativePath(std::u16string_view{p})} {}
		path(const std::u16string& p) : _path{nativePath(std::u16string_view{p})} {}
		path(std::u16string_view p) : _path{nativePath(p)} {}
		path(const char* p) : _path{nativePath(std::string_view{p})} {}
		path(const std::string& p) : _path{nativePath(std::string_view{p})} {}
		// The format (native or generic) changes nothing: '\\' is a separator either way, as on Windows.
		template <typename Source>
		path(const Source& p, format) : path(p) {}
		template <typename Iterator>
		path(Iterator first, Iterator last, format = format::auto_format)
			: path(std::basic_string<typename std::iterator_traits<Iterator>::value_type>(first, last)) {}

		operator const std::filesystem::path&() const {
			return _path;
		}

		const std::filesystem::path& stdPath() const {
			return _path;
		}

		// The pointer stays valid until the path is modified, as with std::filesystem::path::c_str().
		const char16_t* c_str() const {
			_wide = _path.u16string();
			return _wide.c_str();
		}

		std::u16string native() const {
			return _path.u16string();
		}

		std::u16string wstring() const {
			return _path.u16string();
		}

		std::u16string u16string() const {
			return _path.u16string();
		}

		std::string string() const {
			return _path.string();
		}

		std::u8string u8string() const {
			return _path.u8string();
		}

		operator std::u16string() const {
			return _path.u16string();
		}

		path lexically_normal() const {
			return _path.lexically_normal();
		}

		bool empty() const noexcept {
			return _path.empty();
		}

		path filename() const {
			return _path.filename();
		}

		path stem() const {
			return _path.stem();
		}

		path extension() const {
			return _path.extension();
		}

		path parent_path() const {
			return _path.parent_path();
		}

		path root_path() const {
			return _path.root_path();
		}

		bool has_filename() const {
			return _path.has_filename();
		}

		bool has_stem() const {
			return _path.has_stem();
		}

		bool has_root_path() const {
			return _path.has_root_path();
		}

		bool has_relative_path() const {
			return _path.has_relative_path();
		}

		bool has_extension() const {
			return _path.has_extension();
		}

		bool has_parent_path() const {
			return _path.has_parent_path();
		}

		bool is_absolute() const {
			return _path.is_absolute();
		}

		bool is_relative() const {
			return _path.is_relative();
		}

		path& replace_extension(const path& replacement = {}) {
			_path.replace_extension(replacement._path);
			return *this;
		}

		path& replace_filename(const path& replacement) {
			_path.replace_filename(replacement._path);
			return *this;
		}

		path& remove_filename() {
			_path.remove_filename();
			return *this;
		}

		path& operator/=(const path& other) {
			_path /= other._path;
			return *this;
		}

		path& operator+=(const path& other) {
			_path += other._path;
			return *this;
		}

		friend path operator/(const path& lhs, const path& rhs) {
			return lhs._path / rhs._path;
		}

		friend bool operator==(const path& lhs, const path& rhs) {
			return lhs._path == rhs._path;
		}

		// Written quoted, as std::filesystem::path is.
		friend std::ostream& operator<<(std::ostream& out, const path& p) {
			return out << p._path;
		}

	private:
		std::filesystem::path _path;
		mutable std::u16string _wide;
	};
}
