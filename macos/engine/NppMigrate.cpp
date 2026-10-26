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

// npp-migrate: mirrors an upstream source tree into an output tree, applying the migration
// rules to every C/C++ source file and copying every other file unchanged.
//
//     npp-migrate <sourceRoot> <outputRoot> [--report <file.tsv>] [--fixups <file>]
//                 [--text-rules <file> --text-base <path of sourceRoot under PowerEditor/>]
//                 [--plugin-text-rules <file>] [--source-encoding <encoding>]
//     npp-migrate --rc-wide-strings <preprocessed .rc> <output .rc>
//
// The second form is for a plugin's resources: its .rc includes the plugin's migrated headers, whose
// wide strings are char16_t literals (u"..."), which llvm-rc does not read; they become L"..." again
// in the preprocessed script, which llvm-rc then compiles without preprocessing it.
//
// A file is only written when its content changes, so an incremental build only recompiles
// what upstream actually changed.
//
// Fixups are the few places where upstream's logic, not just its types, assumes Windows. Each
// one names a file and replaces an exact piece of upstream text, before the rules run:
//
//     file: MISC/Common/Common.cpp
//     <<<<
//     upstream text
//     ====
//     replacement
//     >>>>
//
// Lines starting with '#' outside a fixup are comments. A fixup whose text is not found exactly
// once stops the migration, so a change upstream is noticed instead of silently undone.
//
// Text rules are the places where upstream's words, not its code, name Windows the operating
// system: menus, dialogs, messages and their translations, in any file. A rule names its files (a
// glob under PowerEditor/), the lines it changes (those containing a text) and the change, either
// text or, with "regex:", an ECMAScript regular expression:
//
//     files: installer/nativeLang/*.xml
//     line: id="7133"
//     replace: Windows => macOS
//
// Lines starting with '#' are comments. A rule that changes nothing in the files of this run stops
// the migration, as a fixup does.
//
// Plugins are migrated with --plugin-text-rules: rules for the code many plugins share (their
// template), which may change nothing in a given plugin, and whose globs match at any depth.

#include "MigrationRules.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <iconv.h>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	enum ExitCode
	{
		exitOk = 0,
		exitUsage = 1,
		exitIoError = 2,
		exitFixupError = 3,
	};

	struct Fixup
	{
		std::string _file;
		std::string _from;
		std::string _to;
		int _line = 0;
	};

	struct TextRule
	{
		std::string _files;
		std::string _anchor;
		std::string _from;
		std::string _to;
		bool _regex = false;
		bool _optional = false; // a plugin rule: '*' matches across folders, and it may change nothing
		int _line = 0;
		size_t _changes = 0;
		bool _matchedFile = false;
	};

	// A file's extension in lower case: Windows does not tell .H from .h, nor .CPP from .cpp.
	std::string extensionOf(const fs::path& path)
	{
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext;
	}

	bool isCodeFile(const fs::path& path)
	{
		static const std::vector<std::string> codeExtensions = {".h", ".hpp", ".hxx", ".inl", ".inc", ".c", ".cpp", ".cxx", ".cc"};
		const std::string ext = extensionOf(path);
		return std::find(codeExtensions.begin(), codeExtensions.end(), ext) != codeExtensions.end();
	}

	// A code file's language: C for .c files, and for the headers in a folder of C sources only (Lua's,
	// for one), which C++ files that include them read with the C library's names too. C++ otherwise.
	nppmigrate::Language languageOf(const fs::path& path)
	{
		const std::string ext = extensionOf(path);
		if (ext == ".c")
			return nppmigrate::Language::c;
		if (ext != ".h")
			return nppmigrate::Language::cpp;
		bool hasC = false;
		std::error_code error;
		for (const fs::directory_entry& entry : fs::directory_iterator{path.parent_path(), error})
		{
			const std::string siblingExt = extensionOf(entry.path());
			if (siblingExt == ".cpp" || siblingExt == ".cxx" || siblingExt == ".cc")
				return nppmigrate::Language::cpp;
			hasC = hasC || siblingExt == ".c";
		}
		return hasC ? nppmigrate::Language::c : nppmigrate::Language::cpp;
	}

	bool readFile(const fs::path& path, std::string& content)
	{
		std::ifstream in{path, std::ios::binary};
		if (!in)
			return false;
		std::ostringstream buffer;
		buffer << in.rdbuf();
		content = buffer.str();
		return true;
	}

	bool isUtf8(const std::string& content)
	{
		size_t i = 0;
		while (i < content.size())
		{
			const auto byte = static_cast<unsigned char>(content[i]);
			const size_t length = byte < 0x80 ? 1 : (byte >> 5) == 0x6 ? 2 : (byte >> 4) == 0xE ? 3 : (byte >> 3) == 0x1E ? 4 : 0;
			if (length == 0 || i + length > content.size())
				return false;
			for (size_t k = 1; k < length; ++k)
			{
				if ((static_cast<unsigned char>(content[i + k]) >> 6) != 0x2)
					return false;
			}
			i += length;
		}
		return true;
	}

	// Visual C++ reads a source that is not Unicode in the system's code page: a plugin written on a
	// Chinese or Japanese Windows has its strings in GBK or Shift JIS. A file that is not UTF-8 is
	// converted from the plugin's encoding (--source-encoding, an iconv name).
	bool convertFromEncoding(std::string& content, const std::string& encoding)
	{
		if (encoding.empty() || isUtf8(content))
			return false;
		iconv_t converter = iconv_open("UTF-8", encoding.c_str());
		if (converter == reinterpret_cast<iconv_t>(-1))
			return false;
		std::string result(content.size() * 4 + 16, '\0');
		char* in = content.data();
		size_t inLeft = content.size();
		char* out = result.data();
		size_t outLeft = result.size();
		const size_t converted = iconv(converter, &in, &inLeft, &out, &outLeft);
		iconv_close(converter);
		if (converted == static_cast<size_t>(-1))
			return false;
		result.resize(result.size() - outLeft);
		content = std::move(result);
		return true;
	}

	// Visual Studio saves sources and resource scripts in UTF-16 when they need it; clang and llvm-rc
	// read UTF-8. A file starting with a UTF-16 byte order mark is converted.
	bool convertFromUtf16(std::string& content)
	{
		if (content.size() < 2)
			return false;
		const auto byte = [&content](size_t i) { return static_cast<unsigned char>(content[i]); };
		const bool littleEndian = byte(0) == 0xFF && byte(1) == 0xFE;
		const bool bigEndian = byte(0) == 0xFE && byte(1) == 0xFF;
		if (!littleEndian && !bigEndian)
			return false;
		std::string utf8;
		for (size_t i = 2; i + 1 < content.size(); i += 2)
		{
			uint32_t unit = littleEndian ? (byte(i) | (byte(i + 1) << 8)) : ((byte(i) << 8) | byte(i + 1));
			if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < content.size())
			{
				const uint32_t low = littleEndian ? (byte(i + 2) | (byte(i + 3) << 8)) : ((byte(i + 2) << 8) | byte(i + 3));
				if (low >= 0xDC00 && low <= 0xDFFF)
				{
					unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
					i += 2;
				}
			}
			if (unit < 0x80)
			{
				utf8 += static_cast<char>(unit);
			}
			else if (unit < 0x800)
			{
				utf8 += static_cast<char>(0xC0 | (unit >> 6));
				utf8 += static_cast<char>(0x80 | (unit & 0x3F));
			}
			else if (unit < 0x10000)
			{
				utf8 += static_cast<char>(0xE0 | (unit >> 12));
				utf8 += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
				utf8 += static_cast<char>(0x80 | (unit & 0x3F));
			}
			else
			{
				utf8 += static_cast<char>(0xF0 | (unit >> 18));
				utf8 += static_cast<char>(0x80 | ((unit >> 12) & 0x3F));
				utf8 += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
				utf8 += static_cast<char>(0x80 | (unit & 0x3F));
			}
		}
		content = std::move(utf8);
		return true;
	}

	bool isResourceScript(const fs::path& path)
	{
		const std::string ext = extensionOf(path);
		return ext == ".rc" || ext == ".rc2";
	}

	// In a resource script, the files of ICON, BITMAP and other statements, and #include, are named with
	// Windows separators ("res\\icon.ico"): a quoted name ending in an extension gets '/' instead.
	size_t fixResourcePaths(std::string& content)
	{
		static const std::regex fileName{R"name("([^"\r\n]*\\[^"\r\n]*\.[A-Za-z0-9]{1,8})")name"};
		size_t changes = 0;
		std::string result;
		auto last = content.cbegin();
		for (std::sregex_iterator it{content.cbegin(), content.cend(), fileName}, end; it != end; ++it)
		{
			result.append(last, (*it)[1].first);
			std::string name;
			for (const char c : (*it)[1].str())
			{
				if (c != '\\')
					name += c;
				else if (name.empty() || name.back() != '/')
					name += '/';
			}
			result += name;
			last = (*it)[1].second;
			++changes;
		}
		result.append(last, content.cend());
		content = std::move(result);
		return changes;
	}

	// rc.exe takes a string literal that goes on over line ends (as long STRINGTABLE texts do, with their
	// own \r\n escapes); llvm-rc does not. The line ends inside a literal are dropped: its lines join.
	size_t joinResourceStringLines(std::string& content)
	{
		enum class State
		{
			code,
			string,
			lineComment,
			blockComment,
		};
		State state = State::code;
		size_t changes = 0;
		std::string result;
		result.reserve(content.size());
		for (size_t i = 0; i < content.size(); ++i)
		{
			const char c = content[i];
			const char next = i + 1 < content.size() ? content[i + 1] : '\0';
			switch (state)
			{
			case State::code:
				if (c == '"')
					state = State::string;
				else if (c == '/' && next == '/')
					state = State::lineComment;
				else if (c == '/' && next == '*')
					state = State::blockComment;
				break;
			case State::string:
				if (c == '\\' && next != '\0')
				{
					// An escape, or the preprocessor's line continuation: kept as it is.
					result += c;
					result += next;
					++i;
					continue;
				}
				if (c == '"' && next == '"')
				{
					result += "\"\"";
					++i;
					continue;
				}
				if (c == '"')
				{
					state = State::code;
				}
				else if (c == '\r' || c == '\n')
				{
					if (c == '\n')
						++changes;
					continue;
				}
				break;
			case State::lineComment:
				if (c == '\n')
					state = State::code;
				break;
			case State::blockComment:
				if (c == '*' && next == '/')
				{
					result += "*/";
					++i;
					state = State::code;
					continue;
				}
				break;
			}
			result += c;
		}
		content = std::move(result);
		return changes;
	}

	bool isIdentifierChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	// u"..." outside string literals becomes L"...".
	size_t restoreResourceWideStrings(std::string& content)
	{
		bool inString = false;
		size_t changes = 0;
		for (size_t i = 0; i < content.size(); ++i)
		{
			const char c = content[i];
			if (inString)
			{
				if (c == '\\' || (c == '"' && i + 1 < content.size() && content[i + 1] == '"'))
					++i; // an escape, or "" (a quote in an .rc string)
				else if (c == '"')
					inString = false;
			}
			else if (c == '"')
			{
				inString = true;
			}
			else if (c == 'u' && i + 1 < content.size() && content[i + 1] == '"' && (i == 0 || !isIdentifierChar(content[i - 1])))
			{
				content[i] = 'L';
				++changes;
			}
		}
		return changes;
	}

	bool writeIfChanged(const fs::path& path, const std::string& content)
	{
		std::string existing;
		if (readFile(path, existing) && existing == content)
			return true;

		fs::create_directories(path.parent_path());
		std::ofstream out{path, std::ios::binary | std::ios::trunc};
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
		return static_cast<bool>(out);
	}

	bool readFixups(const fs::path& path, std::vector<Fixup>& fixups)
	{
		std::ifstream in{path};
		if (!in)
		{
			std::cerr << "npp-migrate: cannot read " << path << "\n";
			return false;
		}
		enum class Part { none, from, to };
		Part part = Part::none;
		std::string file;
		Fixup fixup;
		std::string line;
		int lineNumber = 0;
		while (std::getline(in, line))
		{
			++lineNumber;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (part == Part::none)
			{
				if (line.starts_with("file: "))
				{
					file = line.substr(6);
				}
				else if (line == "<<<<")
				{
					if (file.empty())
					{
						std::cerr << path.string() << ":" << lineNumber << ": fixup without a file\n";
						return false;
					}
					fixup = Fixup{file, {}, {}, lineNumber};
					part = Part::from;
				}
				else if (!line.empty() && line[0] != '#')
				{
					std::cerr << path.string() << ":" << lineNumber << ": unexpected line\n";
					return false;
				}
			}
			else if (line == "====" && part == Part::from)
			{
				if (!fixup._from.empty())
					fixup._from.pop_back();
				part = Part::to;
			}
			else if (line == ">>>>" && part == Part::to)
			{
				if (!fixup._to.empty())
					fixup._to.pop_back();
				fixups.push_back(fixup);
				part = Part::none;
			}
			else
			{
				// Every line ends with \n here; the last one's is dropped when the part ends.
				(part == Part::from ? fixup._from : fixup._to) += line + '\n';
			}
		}
		if (part != Part::none)
		{
			std::cerr << path.string() << ":" << fixup._line << ": unterminated fixup\n";
			return false;
		}
		return true;
	}

	std::string trimmed(const std::string& text)
	{
		const size_t first = text.find_first_not_of(" \t\r");
		const size_t last = text.find_last_not_of(" \t\r");
		return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
	}

	bool readTextRules(const fs::path& path, std::vector<TextRule>& rules, bool optional = false)
	{
		std::ifstream in{path};
		if (!in)
		{
			std::cerr << "npp-migrate: cannot read " << path.string() << "\n";
			return false;
		}
		std::string line;
		int number = 0;
		TextRule rule;
		auto complain = [&](const char* what)
		{
			std::cerr << path.string() << ":" << number << ": " << what << "\n";
			return false;
		};
		while (std::getline(in, line))
		{
			++number;
			const std::string text = trimmed(line);
			if (text.empty() || text[0] == '#')
				continue;
			const size_t colon = text.find(':');
			if (colon == std::string::npos)
				return complain("expected files:, line:, replace: or regex:");
			const std::string key = text.substr(0, colon);
			const std::string value = trimmed(text.substr(colon + 1));
			if (key == "files")
			{
				rule = TextRule{};
				rule._files = value;
				rule._line = number;
			}
			else if (key == "line")
			{
				rule._anchor = value;
			}
			else if (key == "replace" || key == "regex")
			{
				const size_t arrow = value.find(" => ");
				if (rule._files.empty() || rule._anchor.empty() || arrow == std::string::npos)
					return complain("a rule is files:, line:, then replace: or regex: <from> => <to>");
				rule._from = value.substr(0, arrow);
				rule._to = value.substr(arrow + 4);
				rule._regex = key == "regex";
				rule._optional = optional;
				rules.push_back(rule);
			}
			else
			{
				return complain("expected files:, line:, replace: or regex:");
			}
		}
		return true;
	}

	// Applies the rules whose files include this one (its path under PowerEditor/).
	void applyTextRules(std::vector<TextRule>& rules, const std::string& path, std::string& content)
	{
		for (TextRule& rule : rules)
		{
			if (fnmatch(rule._files.c_str(), path.c_str(), rule._optional ? 0 : FNM_PATHNAME) != 0)
				continue;
			rule._matchedFile = true;
			std::regex pattern;
			if (rule._regex)
				pattern = std::regex{rule._from};
			std::string result;
			size_t start = 0;
			while (start < content.size())
			{
				size_t end = content.find('\n', start);
				end = end == std::string::npos ? content.size() : end + 1;
				std::string line = content.substr(start, end - start);
				if (line.find(rule._anchor) != std::string::npos)
				{
					const std::string before = line;
					if (rule._regex)
					{
						line = std::regex_replace(line, pattern, rule._to);
					}
					else
					{
						for (size_t at = line.find(rule._from); at != std::string::npos; at = line.find(rule._from, at + rule._to.size()))
							line.replace(at, rule._from.size(), rule._to);
					}
					if (line != before)
						++rule._changes;
				}
				result += line;
				start = end;
			}
			content = std::move(result);
		}
	}

	// Fixup texts are written with \n; upstream files may use \r\n.
	std::string withLineEndings(const std::string& text, bool crlf)
	{
		if (!crlf)
			return text;
		std::string result;
		for (const char ch : text)
		{
			if (ch == '\n')
				result += '\r';
			result += ch;
		}
		return result;
	}

	bool applyFixups(const std::vector<Fixup>& fixups, const std::string& relative, const fs::path& fixupPath, std::string& content, size_t& applied)
	{
		const bool crlf = content.find("\r\n") != std::string::npos;
		for (const Fixup& fixup : fixups)
		{
			if (fixup._file != relative)
				continue;
			const std::string from = withLineEndings(fixup._from, crlf);
			const size_t position = content.find(from);
			if (position == std::string::npos || content.find(from, position + 1) != std::string::npos)
			{
				std::cerr << fixupPath.string() << ":" << fixup._line << ": the text of this fixup is "
					<< (position == std::string::npos ? "no longer" : "more than once") << " in " << relative << "\n";
				return false;
			}
			content.replace(position, from.size(), withLineEndings(fixup._to, crlf));
			++applied;
		}
		return true;
	}
}

int main(int argc, char* argv[])
{
	if (argc == 4 && std::string_view{argv[1]} == "--rc-wide-strings")
	{
		std::string content;
		if (!readFile(argv[2], content))
		{
			std::cerr << "npp-migrate: cannot read " << argv[2] << "\n";
			return exitIoError;
		}
		restoreResourceWideStrings(content);
		if (!writeIfChanged(argv[3], content))
		{
			std::cerr << "npp-migrate: cannot write " << argv[3] << "\n";
			return exitIoError;
		}
		return 0;
	}
	if (argc < 3 || argc % 2 == 0)
	{
		std::cerr << "usage: npp-migrate <sourceRoot> <outputRoot> [--report <file.tsv>] [--fixups <file>] [--text-rules <file> --text-base <path>] [--plugin-text-rules <file>] [--source-encoding <encoding>]\n";
		return exitUsage;
	}

	const fs::path sourceRoot{argv[1]};
	const fs::path outputRoot{argv[2]};
	fs::path reportPath;
	fs::path fixupPath;
	fs::path textRulesPath;
	fs::path pluginTextRulesPath;
	std::string textBase;
	std::string sourceEncoding;
	for (int i = 3; i + 1 < argc; i += 2)
	{
		const std::string option{argv[i]};
		if (option == "--report")
			reportPath = argv[i + 1];
		else if (option == "--fixups")
			fixupPath = argv[i + 1];
		else if (option == "--text-rules")
			textRulesPath = argv[i + 1];
		else if (option == "--text-base")
			textBase = argv[i + 1];
		else if (option == "--plugin-text-rules")
			pluginTextRulesPath = argv[i + 1];
		else if (option == "--source-encoding")
			sourceEncoding = argv[i + 1];
		else
		{
			std::cerr << "unknown option: " << option << "\n";
			return exitUsage;
		}
	}

	std::vector<Fixup> fixups;
	if (!fixupPath.empty() && !readFixups(fixupPath, fixups))
		return exitFixupError;
	std::vector<TextRule> textRules;
	if (!textRulesPath.empty() && (textBase.empty() || !readTextRules(textRulesPath, textRules)))
	{
		if (textBase.empty())
			std::cerr << "npp-migrate: --text-rules needs --text-base\n";
		return exitFixupError;
	}
	if (!pluginTextRulesPath.empty() && !readTextRules(pluginTextRulesPath, textRules, true))
		return exitFixupError;

	// Sort so that the report, like the output, does not depend on directory iteration order.
	std::vector<fs::path> files;
	for (const auto& entry : fs::recursive_directory_iterator{sourceRoot})
	{
		if (entry.is_regular_file())
			files.push_back(entry.path());
	}
	std::sort(files.begin(), files.end());

	std::ostringstream report;
	report << "file";
	constexpr int ruleCount = static_cast<int>(nppmigrate::RuleId::ruleCount);
	for (int i = 0; i < ruleCount; ++i)
		report << "\t" << nppmigrate::ruleName(static_cast<nppmigrate::RuleId>(i));
	report << "\n";

	size_t migratedFiles = 0;
	size_t changedFiles = 0;
	size_t totalRewrites = 0;
	size_t appliedFixups = 0;
	size_t utf16Files = 0;
	size_t reencodedFiles = 0;
	size_t resourcePaths = 0;
	for (const fs::path& file : files)
	{
		const fs::path relative = fs::relative(file, sourceRoot);
		std::string content;
		if (!readFile(file, content))
		{
			std::cerr << "npp-migrate: cannot read " << file << "\n";
			return exitIoError;
		}

		if (convertFromUtf16(content))
			++utf16Files;
		else if ((isCodeFile(file) || isResourceScript(file)) && convertFromEncoding(content, sourceEncoding))
			++reencodedFiles;
		if (isResourceScript(file))
			resourcePaths += fixResourcePaths(content) + joinResourceStringLines(content);
		applyTextRules(textRules, textBase + "/" + relative.generic_string(), content);
		if (isCodeFile(file))
		{
			if (!applyFixups(fixups, relative.generic_string(), fixupPath, content, appliedFixups))
				return exitFixupError;
			const nppmigrate::MigrationResult result = nppmigrate::migrateSource(content, languageOf(file));
			content = result._text;
			++migratedFiles;
			if (result.totalRewrites() != 0)
			{
				++changedFiles;
				totalRewrites += result.totalRewrites();
				report << relative.generic_string();
				for (int i = 0; i < ruleCount; ++i)
				{
					auto it = result._ruleCounts.find(static_cast<nppmigrate::RuleId>(i));
					report << "\t" << (it != result._ruleCounts.end() ? it->second : 0);
				}
				report << "\n";
			}
		}

		if (!writeIfChanged(outputRoot / relative, content))
		{
			std::cerr << "npp-migrate: cannot write " << (outputRoot / relative) << "\n";
			return exitIoError;
		}
	}

	if (!reportPath.empty() && !writeIfChanged(reportPath, report.str()))
	{
		std::cerr << "npp-migrate: cannot write " << reportPath << "\n";
		return exitIoError;
	}

	if (appliedFixups != fixups.size())
	{
		for (const Fixup& fixup : fixups)
		{
			if (std::find(files.begin(), files.end(), sourceRoot / fixup._file) == files.end())
				std::cerr << fixupPath.string() << ":" << fixup._line << ": no file " << fixup._file << "\n";
		}
		return exitFixupError;
	}

	// A rule for this run's files that changed nothing: upstream's text moved under it.
	size_t textChanges = 0;
	bool textRulesFailed = false;
	for (const TextRule& rule : textRules)
	{
		textChanges += rule._changes;
		const bool forThisRun = !rule._optional && rule._files.compare(0, textBase.size() + 1, textBase + "/") == 0;
		if (forThisRun && rule._changes == 0)
		{
			std::cerr << textRulesPath.string() << ":" << rule._line << ": "
				<< (rule._matchedFile ? "this rule changes nothing in " : "no file matches ") << rule._files << "\n";
			textRulesFailed = true;
		}
	}
	if (textRulesFailed)
		return exitFixupError;

	std::cout << "npp-migrate: " << files.size() << " files, " << migratedFiles << " migrated, "
		<< changedFiles << " changed, " << totalRewrites << " rewrites, " << appliedFixups << " fixups, "
		<< textChanges << " text changes" << (resourcePaths ? ", " + std::to_string(resourcePaths) + " resource script fixes" : std::string{}) << (utf16Files ? ", " + std::to_string(utf16Files) + " UTF-16 files converted" : std::string{})
		<< (reencodedFiles ? ", " + std::to_string(reencodedFiles) + " files converted from " + sourceEncoding : std::string{}) << "\n";
	return exitOk;
}
