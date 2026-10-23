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

// The migration rules turn upstream Windows source into source that compiles on macOS.
// They only handle differences that a runtime library cannot hide: the width of wchar_t
// (2 bytes on Windows, 4 on macOS) and the standard library types built on it.
// Everything a function can fix (secure CRT, wide C functions, Win32 API) belongs to the runtime instead.
//
// The rules work on clang raw-lexer tokens of one file at a time, so comments, string contents
// and line structure are preserved exactly: a line in the output is the same line in upstream source.

#pragma once

#include <map>
#include <string>

namespace nppmigrate
{
	enum class RuleId
	{
		wideCharType,       // wchar_t -> char16_t
		wideLiteral,        // L"..." / L'x' / LR"(...)" -> u"..." / u'x' / uR"(...)"
		wideTokenPaste,     // L ## x -> u ## x (inside macro definitions)
		wideStdType,        // std::wstring family -> std::u16string family
		wideStdReplacement, // std types libc++ cannot provide for char16_t -> nppmac:: equivalents
		wideStdCFunction,   // std::wcslen etc. -> ::wcslen (the runtime declares char16_t overloads)
		wideStdCharClass,   // std::isdigit(c, loc) etc. -> nppmac::isdigit (no ctype<char16_t> in libc++)
		wideStdFilesystem,  // std::filesystem -> nppmac::filesystem (UTF-16 paths, as on Windows)
		ruleCount           // keep last
	};

	const char* ruleName(RuleId rule);

	struct MigrationResult
	{
		std::string _text;
		std::map<RuleId, size_t> _ruleCounts;

		size_t totalRewrites() const;
	};

	// Rewrites one source file. The result is a pure function of the input text.
	MigrationResult migrateSource(const std::string& source);
}
