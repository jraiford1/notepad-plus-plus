// This file is part of Notepad++ project (macOS and Linux build)
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

// DirectoryWatcher on Linux: inotify. inotify watches one folder at a time, so a subtree is a watch per
// folder, added as folders appear; what was made in a new folder before its watch existed is reported
// from a look at the folder once it is watched.

#include "../DirectoryWatcher.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace nppmac::platform
{
	namespace
	{
		constexpr uint32_t watchedEvents = IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_MOVED_FROM
			| IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_EXCL_UNLINK;

		bool isDirectory(const std::string& path)
		{
			struct stat info{};
			return ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
		}
	}

	struct DirectoryWatcher::Impl
	{
		std::string _folder;
		bool _subtree = false;
		Callback _callback;
		int _inotify = -1;
		int _stop = -1; // an eventfd that ends the reading thread
		int _rootWatch = -1;
		std::unordered_map<int, std::string> _folders; // watch descriptor -> folder relative to _folder ("" for it)
		std::thread _thread;

		std::string join(const std::string& relative, const std::string& name) const
		{
			return relative.empty() ? name : relative + "/" + name;
		}

		std::string absolute(const std::string& relative) const
		{
			return relative.empty() ? _folder : _folder + "/" + relative;
		}

		// Watches a folder (relative to _folder) and, for a subtree, the folders in it; with report, also
		// reports what is already in them, made before their watch existed.
		void watch(const std::string& relative, bool report, std::vector<DirectoryChange>& changes)
		{
			const int descriptor = ::inotify_add_watch(_inotify, absolute(relative).c_str(), watchedEvents);
			if (descriptor < 0)
				return;
			_folders[descriptor] = relative;
			if (!_subtree && !report)
				return;
			DIR* folder = ::opendir(absolute(relative).c_str());
			if (!folder)
				return;
			while (const dirent* entry = ::readdir(folder))
			{
				const std::string name = entry->d_name;
				if (name == "." || name == "..")
					continue;
				const std::string path = join(relative, name);
				const bool directory = isDirectory(absolute(path));
				if (report)
					changes.push_back({FileChange::added, path, directory});
				if (_subtree && directory)
					watch(path, report, changes);
			}
			::closedir(folder);
		}

		void read()
		{
			alignas(inotify_event) char buffer[64 * 1024];
			pollfd waits[2] = {{_inotify, POLLIN, 0}, {_stop, POLLIN, 0}};
			for (;;)
			{
				if (::poll(waits, 2, -1) < 0)
				{
					if (errno == EINTR)
						continue;
					return;
				}
				if (waits[1].revents)
					return;
				const ssize_t length = ::read(_inotify, buffer, sizeof(buffer));
				if (length <= 0)
					continue;
				std::vector<DirectoryChange> changes;
				for (ssize_t offset = 0; offset < length;)
				{
					const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
					offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
					addChanges(*event, changes);
				}
				if (!changes.empty())
					_callback(std::move(changes));
			}
		}

		// Turns one inotify event into the Windows actions it stands for.
		void addChanges(const inotify_event& event, std::vector<DirectoryChange>& changes)
		{
			const auto folder = _folders.find(event.wd);
			if (folder == _folders.end())
				return;
			if (event.mask & IN_IGNORED)
			{
				_folders.erase(folder); // the folder went away
				return;
			}
			if (event.len == 0 || event.name[0] == '\0')
				return; // about the watched folder itself, reported by its parent
			const std::string path = join(folder->second, event.name);
			const bool directory = (event.mask & IN_ISDIR) != 0;
			if (event.mask & (IN_CREATE | IN_MOVED_TO))
			{
				changes.push_back({(event.mask & IN_CREATE) ? FileChange::added : FileChange::renamedNew, path, directory});
				if (directory && _subtree)
					watch(path, (event.mask & IN_CREATE) != 0, changes);
			}
			else if (event.mask & IN_MOVED_FROM)
				changes.push_back({FileChange::renamedOld, path, directory});
			else if (event.mask & IN_DELETE)
				changes.push_back({FileChange::removed, path, directory});
			else if (event.mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE))
			{
				// One write is often a modify and a close-write together: reported once.
				if (changes.empty() || changes.back()._change != FileChange::modified || changes.back()._relativePath != path)
					changes.push_back({FileChange::modified, path, directory});
			}
		}
	};

	DirectoryWatcher::DirectoryWatcher(const std::string& folder, bool subtree, Callback callback)
		: _impl(std::make_unique<Impl>())
	{
		char resolved[PATH_MAX]{};
		_impl->_folder = ::realpath(folder.c_str(), resolved) ? std::string{resolved} : folder;
		_impl->_subtree = subtree;
		_impl->_callback = std::move(callback);
		_impl->_inotify = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		_impl->_stop = ::eventfd(0, EFD_CLOEXEC);
		if (_impl->_inotify < 0 || _impl->_stop < 0)
			return;
		std::vector<DirectoryChange> ignored;
		_impl->watch("", false, ignored);
		const auto root = std::find_if(_impl->_folders.begin(), _impl->_folders.end(), [](const auto& entry) { return entry.second.empty(); });
		if (root == _impl->_folders.end())
			return;
		_impl->_rootWatch = root->first;
		_impl->_thread = std::thread([impl = _impl.get()] { impl->read(); });
	}

	DirectoryWatcher::~DirectoryWatcher()
	{
		if (_impl->_thread.joinable())
		{
			const uint64_t one = 1;
			[[maybe_unused]] const ssize_t written = ::write(_impl->_stop, &one, sizeof(one));
			_impl->_thread.join();
		}
		if (_impl->_inotify >= 0)
			::close(_impl->_inotify);
		if (_impl->_stop >= 0)
			::close(_impl->_stop);
	}

	bool DirectoryWatcher::isWatching() const
	{
		return _impl->_rootWatch >= 0;
	}
}
