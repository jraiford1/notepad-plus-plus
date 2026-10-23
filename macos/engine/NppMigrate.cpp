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
//     npp-migrate <sourceRoot> <outputRoot> [--report <file.tsv>]
//
// A file is only written when its content changes, so an incremental build only recompiles
// what upstream actually changed.

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
}

int main(int argc, char* argv[])
{
	if (argc != 3 && argc != 5)
	{
		std::cerr << "usage: npp-migrate <sourceRoot> <outputRoot> [--report <file.tsv>]\n";
		return exitUsage;
	}

	const fs::path sourceRoot{argv[1]};
	const fs::path outputRoot{argv[2]};
	fs::path reportPath;
	if (argc == 5)
	{
		if (std::string{argv[3]} != "--report")
		{
			std::cerr << "unknown option: " << argv[3] << "\n";
			return exitUsage;
		}
		reportPath = argv[4];
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

	std::cout << "npp-migrate: " << files.size() << " files, " << migratedFiles << " migrated, "
		<< changedFiles << " changed, " << totalRewrites << " rewrites\n";
	return exitOk;
}
