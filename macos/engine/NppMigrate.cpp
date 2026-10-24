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

#include "MigrationRules.h"

#include <algorithm>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
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
		int _line = 0;
		size_t _changes = 0;
		bool _matchedFile = false;
	};

	bool isCodeFile(const fs::path& path)
	{
		static const std::vector<std::string> codeExtensions = {".h", ".hpp", ".hxx", ".inl", ".c", ".cpp", ".cxx", ".cc"};
		const std::string ext = path.extension().string();
		return std::find(codeExtensions.begin(), codeExtensions.end(), ext) != codeExtensions.end();
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

	bool readTextRules(const fs::path& path, std::vector<TextRule>& rules)
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
			if (fnmatch(rule._files.c_str(), path.c_str(), FNM_PATHNAME) != 0)
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
	if (argc < 3 || argc % 2 == 0)
	{
		std::cerr << "usage: npp-migrate <sourceRoot> <outputRoot> [--report <file.tsv>] [--fixups <file>]\n";
		return exitUsage;
	}

	const fs::path sourceRoot{argv[1]};
	const fs::path outputRoot{argv[2]};
	fs::path reportPath;
	fs::path fixupPath;
	fs::path textRulesPath;
	std::string textBase;
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
	for (const fs::path& file : files)
	{
		const fs::path relative = fs::relative(file, sourceRoot);
		std::string content;
		if (!readFile(file, content))
		{
			std::cerr << "npp-migrate: cannot read " << file << "\n";
			return exitIoError;
		}

		applyTextRules(textRules, textBase + "/" + relative.generic_string(), content);
		if (isCodeFile(file))
		{
			if (!applyFixups(fixups, relative.generic_string(), fixupPath, content, appliedFixups))
				return exitFixupError;
			const nppmigrate::MigrationResult result = nppmigrate::migrateSource(content);
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
		const bool forThisRun = rule._files.compare(0, textBase.size() + 1, textBase + "/") == 0;
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
		<< textChanges << " text changes\n";
	return exitOk;
}
