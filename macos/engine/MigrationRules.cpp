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

#include "MigrationRules.h"

#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>

namespace nppmigrate
{
	namespace
	{
		struct RawToken
		{
			clang::tok::TokenKind _kind = clang::tok::unknown;
			bool _atStartOfLine = false;
			size_t _offset = 0;
			size_t _length = 0;
			std::string_view _text;
		};

		struct Edit
		{
			size_t _offset = 0;
			size_t _length = 0;
			std::string _replacement;
			RuleId _rule = RuleId::wideCharType;
		};

		// std types that libc++ provides for char16_t as well: only the name changes, std:: stays.
		const std::unordered_map<std::string_view, std::string_view> g_stdTypeRenames = {
			{"wstring", "u16string"},
			{"wstring_view", "u16string_view"},
		};

		// std names that libc++ cannot provide for char16_t (no ctype<char16_t> facet, no to_u16string):
		// the runtime supplies them in namespace nppmac. These names are distinctive enough to be
		// replaced unqualified too, as upstream often brings them in with "using namespace std".
		const std::unordered_map<std::string_view, std::string_view> g_stdReplacements = {
			{"wstringstream", "nppmac::u16stringstream"},
			{"wostringstream", "nppmac::u16ostringstream"},
			{"wistringstream", "nppmac::u16istringstream"},
			{"to_wstring", "nppmac::to_u16string"},
			{"getline", "nppmac::getline"}, // forwards to std::getline for narrow streams
			{"stoi", "nppmac::stoi"},       // the sto* family forwards to std for narrow strings
			{"stol", "nppmac::stol"},
			{"stoul", "nppmac::stoul"},
			{"stoll", "nppmac::stoll"},
			{"stoull", "nppmac::stoull"},
			{"stof", "nppmac::stof"},
			{"stod", "nppmac::stod"},
			{"fopen", "nppmac::fopen"},         // reads '\\' in paths as a separator
			{"ifstream", "nppmac::ifstream"},   // file streams that also open UTF-16 paths
			{"ofstream", "nppmac::ofstream"},
			// The narrow printf family formats as on Windows (LLP64: %ld and %lu are 32-bit, %I64d, ...).
			{"sprintf", "nppmac::sprintf"},
			{"snprintf", "nppmac::snprintf"},
			{"_snprintf", "nppmac::snprintf"},
			{"vsprintf", "nppmac::vsprintf"},
			{"vsnprintf", "nppmac::vsnprintf"},
			{"_vsnprintf", "nppmac::vsnprintf"},
			{"sprintf_s", "nppmac::sprintf_s"},
			{"_snprintf_s", "nppmac::snprintf_s"},
		};

		// The same, for names that are also common identifiers: only replaced when written std::name.
		const std::unordered_map<std::string_view, std::string_view> g_stdQualifiedReplacements = {
			{"fstream", "nppmac::fstream"},
			{"format", "nppmac::format"},       // also takes a UTF-16 format string
		};

		// C wide-string functions. The runtime declares char16_t overloads at global scope,
		// so a std:: qualified call only has to drop its qualifier.
		const std::unordered_set<std::string_view> g_wideCFunctions = {
			"wcslen", "wcsnlen", "wcscmp", "wcsncmp", "wcscpy", "wcsncpy", "wcscat", "wcsncat",
			"wcschr", "wcsrchr", "wcsstr", "wcspbrk", "wcsspn", "wcscspn", "wcstok", "wcscoll",
			"wcstol", "wcstoul", "wcstoll", "wcstoull", "wcstod", "wcstof",
			"wmemcpy", "wmemmove", "wmemset", "wmemcmp", "wmemchr",
		};

		// <locale> character classification: std::isdigit(c, loc) and friends need a ctype<char16_t>
		// facet libc++ does not have. The runtime's versions handle char16_t and forward everything else.
		const std::unordered_set<std::string_view> g_stdCharClassFunctions = {
			"isdigit", "isspace", "isalpha", "isalnum", "isupper", "islower", "ispunct", "isxdigit",
			"iscntrl", "isprint", "isgraph", "isblank", "tolower", "toupper",
		};

		clang::LangOptions makeLangOptions()
		{
			clang::LangOptions langOpts;
			langOpts.CPlusPlus = 1;
			langOpts.CPlusPlus11 = 1;
			langOpts.CPlusPlus14 = 1;
			langOpts.CPlusPlus17 = 1;
			langOpts.CPlusPlus20 = 1;
			langOpts.Char8 = 1;
			langOpts.WChar = 1;
			langOpts.RawStringLiterals = 1;
			langOpts.LineComment = 1;
			langOpts.Digraphs = 1;
			return langOpts;
		}

		std::vector<RawToken> lexFile(const std::string& source)
		{
			static const clang::LangOptions langOpts = makeLangOptions();
			const clang::SourceLocation fileLoc = clang::SourceLocation::getFromRawEncoding(1);
			const char* bufStart = source.data();
			clang::Lexer lexer{fileLoc, langOpts, bufStart, bufStart, bufStart + source.size()};

			std::vector<RawToken> tokens;
			clang::Token tok;
			for (;;)
			{
				lexer.LexFromRawLexer(tok);
				if (tok.is(clang::tok::eof))
					break;

				RawToken raw;
				raw._kind = tok.getKind();
				raw._offset = tok.getLocation().getRawEncoding() - fileLoc.getRawEncoding();
				raw._length = tok.getLength();
				raw._atStartOfLine = tok.isAtStartOfLine();
				raw._text = std::string_view{source}.substr(raw._offset, raw._length);
				tokens.push_back(raw);
			}
			return tokens;
		}

		bool isIdentifier(const RawToken& token, std::string_view text = {})
		{
			return token._kind == clang::tok::raw_identifier && (text.empty() || token._text == text);
		}

		bool isWideLiteral(const RawToken& token)
		{
			switch (token._kind)
			{
				case clang::tok::wide_string_literal:
				case clang::tok::wide_char_constant:
					return true;

				default:
					return false;
			}
		}
	}

	const char* ruleName(RuleId rule)
	{
		switch (rule)
		{
			case RuleId::wideCharType:
				return "wideCharType";
			case RuleId::wideLiteral:
				return "wideLiteral";
			case RuleId::wideTokenPaste:
				return "wideTokenPaste";
			case RuleId::wideStdType:
				return "wideStdType";
			case RuleId::wideStdReplacement:
				return "wideStdReplacement";
			case RuleId::wideStdCFunction:
				return "wideStdCFunction";
			case RuleId::wideStdCharClass:
				return "wideStdCharClass";
			case RuleId::wideStdFilesystem:
				return "wideStdFilesystem";
			case RuleId::ruleCount:
				break;
		}
		return "unknown";
	}

	size_t MigrationResult::totalRewrites() const
	{
		size_t total = 0;
		for (const auto& [rule, count] : _ruleCounts)
			total += count;
		return total;
	}

	MigrationResult migrateSource(const std::string& source)
	{
		const std::vector<RawToken> tokens = lexFile(source);
		std::vector<Edit> edits;

		bool inIncludeDirective = false;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			const RawToken& token = tokens[i];

			// Header names in #include / #include_next / #import are file names, never code.
			if (token._atStartOfLine)
				inIncludeDirective = false;
			if (token._atStartOfLine && token._kind == clang::tok::hash && i + 1 < tokens.size())
			{
				const std::string_view directive = tokens[i + 1]._text;
				inIncludeDirective = directive == "include" || directive == "include_next" || directive == "import";
			}
			if (inIncludeDirective)
				continue;

			if (isWideLiteral(token))
			{
				// The encoding prefix is the first 'L' of the spelling, for L"", L'' and LR"()" alike.
				// A token can start with an escaped newline (backslash, newline) after a macro line continuation.
				const size_t prefix = token._text.find('L');
				edits.push_back({token._offset + prefix, 1, "u", RuleId::wideLiteral});
				continue;
			}

			if (!isIdentifier(token) || token._text.find('\\') != std::string_view::npos)
				continue; // an identifier split by an escaped newline is never one of ours

			const RawToken* prev = i >= 1 ? &tokens[i - 1] : nullptr;
			const RawToken* prev2 = i >= 2 ? &tokens[i - 2] : nullptr;
			const RawToken* next = i + 1 < tokens.size() ? &tokens[i + 1] : nullptr;

			if (token._text == "L" && next && next->_kind == clang::tok::hashhash)
			{
				edits.push_back({token._offset, token._length, "u", RuleId::wideTokenPaste});
				continue;
			}

			if (token._text == "wchar_t")
			{
				edits.push_back({token._offset, token._length, "char16_t", RuleId::wideCharType});
				continue;
			}

			const bool isMember = prev && (prev->_kind == clang::tok::period || prev->_kind == clang::tok::arrow);
			const bool isQualified = prev && prev->_kind == clang::tok::coloncolon;
			const bool isStdQualified = isQualified && prev2 && isIdentifier(*prev2, "std");
			const bool isGlobalQualified = isQualified && (!prev2 || (!isIdentifier(*prev2) && prev2->_kind != clang::tok::greater));
			const bool isOtherQualified = isQualified && !isStdQualified && !isGlobalQualified;
			if (isMember || isOtherQualified)
				continue;

			if (auto it = g_stdTypeRenames.find(token._text); it != g_stdTypeRenames.end())
			{
				edits.push_back({token._offset, token._length, std::string{it->second}, RuleId::wideStdType});
				continue;
			}

			if (auto it = g_stdQualifiedReplacements.find(token._text); isStdQualified && it != g_stdQualifiedReplacements.end())
			{
				edits.push_back({prev2->_offset, token._offset + token._length - prev2->_offset, std::string{it->second}, RuleId::wideStdReplacement});
				continue;
			}

			if (auto it = g_stdReplacements.find(token._text); it != g_stdReplacements.end())
			{
				// Replace "std::name" as a whole, or a bare "name" brought in by "using namespace std".
				const size_t start = isStdQualified ? prev2->_offset : token._offset;
				const size_t length = token._offset + token._length - start;
				edits.push_back({start, length, std::string{it->second}, RuleId::wideStdReplacement});
				continue;
			}

			if (isStdQualified && token._text == "filesystem")
			{
				// std::filesystem::path is UTF-8 on macOS; nppmac::filesystem keeps the UTF-16 interface.
				edits.push_back({prev2->_offset, prev2->_length, "nppmac", RuleId::wideStdFilesystem});
				continue;
			}

			if (isStdQualified && g_stdCharClassFunctions.count(token._text) != 0)
			{
				edits.push_back({prev2->_offset, prev2->_length, "nppmac", RuleId::wideStdCharClass});
				continue;
			}

			if (isStdQualified && g_wideCFunctions.count(token._text) != 0)
			{
				// "std::wcslen" -> "::wcslen": keep the "::", drop "std".
				edits.push_back({prev2->_offset, prev2->_length, "", RuleId::wideStdCFunction});
				continue;
			}
		}

		MigrationResult result;
		result._text.reserve(source.size());
		size_t pos = 0;
		for (const Edit& edit : edits)
		{
			if (edit._offset < pos)
				continue; // overlapping edits cannot happen with the rules above; keep the first one
			result._text.append(source, pos, edit._offset - pos);
			result._text.append(edit._replacement);
			pos = edit._offset + edit._length;
			++result._ruleCounts[edit._rule];
		}
		result._text.append(source, pos, std::string::npos);
		return result;
	}
}
