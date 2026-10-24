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

#include "DirectoryWatcher.h"

#include <CoreServices/CoreServices.h>

#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <unordered_map>

namespace nppmac::platform
{
	namespace
	{
		constexpr CFTimeInterval latency = 0.1;

		std::string canonicalPath(const std::string& path)
		{
			char resolved[PATH_MAX]{};
			if (::realpath(path.c_str(), resolved))
				return resolved;
			return path;
		}

		bool exists(const std::string& path)
		{
			struct stat info{};
			return ::lstat(path.c_str(), &info) == 0;
		}
	}

	struct DirectoryWatcher::Impl
	{
		std::string _folder; // canonical, as FSEvents reports paths
		bool _subtree = false;
		Callback _callback;
		FSEventStreamRef _stream = nullptr;
		dispatch_queue_t _queue = nullptr;

		// Whether each path reported so far existed after its last event. FSEvents flags pile up over a
		// path's history (a file renamed and then deleted still says renamed), so a change is decided by
		// what was there before and what is there now, with the flags only telling how it came about.
		// Used on the watcher's serial queue only.
		std::unordered_map<std::string, bool> _existed;

		// Turns one FSEvents event into the Windows actions it stands for.
		void addChanges(const std::string& path, FSEventStreamEventFlags flags, std::vector<DirectoryChange>& changes)
		{
			if (path.size() <= _folder.size() || path.compare(0, _folder.size(), _folder) != 0 || path[_folder.size()] != '/')
				return;
			const std::string relative = path.substr(_folder.size() + 1);
			if (!_subtree && relative.find('/') != std::string::npos)
				return;
			const bool present = exists(path);
			const bool isDirectory = (flags & kFSEventStreamEventFlagItemIsDir) != 0;
			const bool renamed = (flags & kFSEventStreamEventFlagItemRenamed) != 0;
			auto known = _existed.find(relative);
			// A path not seen before was there already, unless it has just been created or moved in.
			const bool existed = known != _existed.end() ? known->second : !(flags & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRenamed));
			_existed[relative] = present;

			if (present && !existed)
				changes.push_back({renamed ? FileChange::renamedNew : FileChange::added, relative, isDirectory});
			else if (present && existed && (flags & (kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemInodeMetaMod | kFSEventStreamEventFlagItemXattrMod)))
				changes.push_back({FileChange::modified, relative, isDirectory});
			else if (!present && existed)
				changes.push_back({renamed && !(flags & kFSEventStreamEventFlagItemRemoved) ? FileChange::renamedOld : FileChange::removed, relative, isDirectory});
		}

		static void callback(ConstFSEventStreamRef, void* info, size_t count, void* paths, const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
		{
			auto* self = static_cast<Impl*>(info);
			auto pathArray = static_cast<CFArrayRef>(paths);
			std::vector<DirectoryChange> changes;
			for (size_t i = 0; i < count; ++i)
			{
				auto path = static_cast<CFStringRef>(CFArrayGetValueAtIndex(pathArray, static_cast<CFIndex>(i)));
				char buffer[PATH_MAX * 4]{};
				if (CFStringGetFileSystemRepresentation(path, buffer, sizeof(buffer)))
					self->addChanges(buffer, flags[i], changes);
			}
			if (!changes.empty())
				self->_callback(std::move(changes));
		}
	};

	DirectoryWatcher::DirectoryWatcher(const std::string& folder, bool subtree, Callback callback) : _impl{std::make_unique<Impl>()}
	{
		_impl->_folder = canonicalPath(folder);
		while (_impl->_folder.size() > 1 && _impl->_folder.back() == '/')
			_impl->_folder.pop_back();
		_impl->_subtree = subtree;
		_impl->_callback = std::move(callback);

		CFStringRef path = CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, _impl->_folder.c_str());
		CFArrayRef paths = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(&path), 1, &kCFTypeArrayCallBacks);
		FSEventStreamContext context{0, _impl.get(), nullptr, nullptr, nullptr};
		_impl->_stream = FSEventStreamCreate(kCFAllocatorDefault, &Impl::callback, &context, paths, kFSEventStreamEventIdSinceNow, latency,
			kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagUseCFTypes | kFSEventStreamCreateFlagNoDefer);
		CFRelease(paths);
		CFRelease(path);
		if (!_impl->_stream)
			return;
		_impl->_queue = dispatch_queue_create("nppmac.directory-watcher", DISPATCH_QUEUE_SERIAL);
		FSEventStreamSetDispatchQueue(_impl->_stream, _impl->_queue);
		if (!FSEventStreamStart(_impl->_stream))
		{
			FSEventStreamInvalidate(_impl->_stream);
			FSEventStreamRelease(_impl->_stream);
			_impl->_stream = nullptr;
		}
	}

	DirectoryWatcher::~DirectoryWatcher()
	{
		if (_impl->_stream)
		{
			// After Stop and Invalidate no callback runs any more.
			FSEventStreamStop(_impl->_stream);
			FSEventStreamInvalidate(_impl->_stream);
			FSEventStreamRelease(_impl->_stream);
		}
		if (_impl->_queue)
		{
			// Waits for a callback already running; ARC releases the queue.
			dispatch_sync(_impl->_queue, ^{});
		}
	}

	bool DirectoryWatcher::isWatching() const
	{
		return _impl->_stream != nullptr;
	}
}
