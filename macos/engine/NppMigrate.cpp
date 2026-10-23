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

#include "MigrationRules.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
	for (int i = 3; i + 1 < argc; i += 2)
	{
		const std::string option{argv[i]};
		if (option == "--report")
			reportPath = argv[i + 1];
		else if (option == "--fixups")
			fixupPath = argv[i + 1];
		else
		{
			std::cerr << "unknown option: " << option << "\n";
			return exitUsage;
		}
	}

	std::vector<Fixup> fixups;
	if (!fixupPath.empty() && !readFixups(fixupPath, fixups))
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
	for (const fs::path& file : files)
	{
		const fs::path relative = fs::relative(file, sourceRoot);
		std::string content;
		if (!readFile(file, content))
		{
			std::cerr << "npp-migrate: cannot read " << file << "\n";
			return exitIoError;
		}

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

	std::cout << "npp-migrate: " << files.size() << " files, " << migratedFiles << " migrated, "
		<< changedFiles << " changed, " << totalRewrites << " rewrites, " << appliedFixups << " fixups\n";
	return exitOk;
}
