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

// Watches a folder for changes with FSEvents, for ReadDirectoryChangesW. Plain C++ types only: the
// kernel side includes this without CoreServices.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nppmac::platform
{
	enum class FileChange
	{
		added,
		removed,
		modified,
		renamedOld,
		renamedNew,
	};

	struct DirectoryChange
	{
		FileChange _change = FileChange::modified;
		std::string _relativePath; // UTF-8, '/' separated, relative to the watched folder
		bool _isDirectory = false;
	};

	class DirectoryWatcher
	{
	public:
		// The callback runs on a background queue, with changes in the order they happened.
		using Callback = std::function<void(std::vector<DirectoryChange>)>;

		DirectoryWatcher(const std::string& folder, bool subtree, Callback callback);
		~DirectoryWatcher();

		DirectoryWatcher(const DirectoryWatcher&) = delete;
		DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

		bool isWatching() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
