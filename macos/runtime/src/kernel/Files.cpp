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

// kernel32: files, directories, paths, find handles, file mappings.

#include "Immortal.h"
#include "Handles.h"
#include "Internal.h"
#include "platform/DirectoryWatcher.h"

#include <algorithm>
#include <cerrno>
#include <copyfile.h>
#include <cstddef>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <mutex>
#include <memory>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using nppmac::kernel::KernelObject;

namespace nppmac::kernel
{
	std::string toPosixPath(std::u16string_view path)
	{
		return nativePath(path);
	}

	std::string toPosixPath(const char16_t* path)
	{
		return path ? toPosixPath(std::u16string_view{path}) : std::string{};
	}

	std::u16string fromPosixPath(std::string_view path)
	{
		return toUtf16(path);
	}

	DWORD win32ErrorFromErrno(int error)
	{
		switch (error)
		{
			case 0:
				return ERROR_SUCCESS;
			case ENOENT:
				return ERROR_FILE_NOT_FOUND;
			case ENOTDIR:
				return ERROR_PATH_NOT_FOUND;
			case EACCES:
			case EPERM:
			case EROFS:
				return ERROR_ACCESS_DENIED;
			case EEXIST:
				return ERROR_ALREADY_EXISTS;
			case ENOTEMPTY:
				return ERROR_DIR_NOT_EMPTY;
			case EBADF:
				return ERROR_INVALID_HANDLE;
			case ENOMEM:
				return ERROR_NOT_ENOUGH_MEMORY;
			case ENOSPC:
				return ERROR_DISK_FULL;
			case EINVAL:
				return ERROR_INVALID_PARAMETER;
			case ENAMETOOLONG:
				return ERROR_FILENAME_EXCED_RANGE;
			case EBUSY:
				return ERROR_BUSY;
			case EISDIR:
				return ERROR_ACCESS_DENIED;
			case EXDEV:
				return ERROR_NOT_SAME_DEVICE;
			default:
				return ERROR_GEN_FAILURE;
		}
	}

	void setErrorFromErrno(int error)
	{
		SetLastError(win32ErrorFromErrno(error));
	}

	void setErrorFromErrno()
	{
		setErrorFromErrno(errno);
	}

	namespace
	{
		constexpr unsigned long long epochDifference = 116444736000000000ULL; // 1601 to 1970, in 100 ns
	}

	FILETIME toFileTime(const timespec& time)
	{
		const unsigned long long ticks = static_cast<unsigned long long>(time.tv_sec) * 10000000ULL + static_cast<unsigned long long>(time.tv_nsec) / 100 + epochDifference;
		FILETIME result{};
		result.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFF);
		result.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
		return result;
	}

	timespec toTimespec(const FILETIME& time)
	{
		const unsigned long long ticks = (static_cast<unsigned long long>(time.dwHighDateTime) << 32 | time.dwLowDateTime) - epochDifference;
		timespec result{};
		result.tv_sec = static_cast<time_t>(ticks / 10000000ULL);
		result.tv_nsec = static_cast<long>((ticks % 10000000ULL) * 100);
		return result;
	}

	FILETIME currentFileTime()
	{
		timespec now{};
		clock_gettime(CLOCK_REALTIME, &now);
		return toFileTime(now);
	}

	DWORD fileAttributes(const std::string& posixPath, const struct stat& info)
	{
		DWORD attributes = 0;
		if (S_ISDIR(info.st_mode))
			attributes |= FILE_ATTRIBUTE_DIRECTORY;
		if (access(posixPath.c_str(), W_OK) != 0 && errno == EACCES)
			attributes |= FILE_ATTRIBUTE_READONLY;
		const size_t slash = posixPath.find_last_of('/');
		const std::string_view name = slash == std::string::npos ? std::string_view{posixPath} : std::string_view{posixPath}.substr(slash + 1);
		if (name.size() > 1 && name[0] == '.' && name != "..")
			attributes |= FILE_ATTRIBUTE_HIDDEN;
		if (info.st_flags & UF_HIDDEN)
			attributes |= FILE_ATTRIBUTE_HIDDEN;
		return attributes == 0 ? FILE_ATTRIBUTE_NORMAL : attributes;
	}

	DWORD copyToBuffer(std::u16string_view text, LPWSTR buffer, DWORD size)
	{
		if (!buffer || text.size() >= size)
			return static_cast<DWORD>(text.size() + 1);
		std::copy(text.begin(), text.end(), buffer);
		buffer[text.size()] = u'\0';
		return static_cast<DWORD>(text.size());
	}

	DWORD copyToBuffer(std::string_view text, LPSTR buffer, DWORD size)
	{
		if (!buffer || text.size() >= size)
			return static_cast<DWORD>(text.size() + 1);
		std::copy(text.begin(), text.end(), buffer);
		buffer[text.size()] = '\0';
		return static_cast<DWORD>(text.size());
	}
}

using namespace nppmac::kernel;

namespace
{
	// The FSEvents watch behind ReadDirectoryChangesW: changes are kept until a read is pending, then
	// written into its buffer and completed by an APC on the thread that issued the read.
	class DirectoryWatch
	{
	public:
		DirectoryWatch(const std::string& folder, bool subtree, DWORD filter) : _filter{filter}
		{
			// The watcher's callback uses this object: the destructor stops it before anything else goes.
			_watcher = std::make_unique<nppmac::platform::DirectoryWatcher>(folder, subtree, [this](std::vector<nppmac::platform::DirectoryChange> changes)
			{
				std::lock_guard lock{_mutex};
				for (auto& change : changes)
				{
					if (wanted(change))
						_backlog.push_back(std::move(change));
				}
				deliver();
			});
		}

		~DirectoryWatch()
		{
			_watcher.reset();
		}

		bool isWatching() const {
			return _watcher->isWatching();
		}

		void read(BYTE* buffer, DWORD length, DWORD filter, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE completion)
		{
			std::lock_guard lock{_mutex};
			_filter = filter;
			_request = Request{buffer, length, overlapped, completion, currentThreadObject()};
			deliver();
		}

		void cancel()
		{
			std::lock_guard lock{_mutex};
			if (!_request)
				return;
			complete(ERROR_OPERATION_ABORTED, 0);
		}

	private:
		struct Request
		{
			BYTE* _buffer = nullptr;
			DWORD _length = 0;
			LPOVERLAPPED _overlapped = nullptr;
			LPOVERLAPPED_COMPLETION_ROUTINE _completion = nullptr;
			std::shared_ptr<KernelObject> _thread;
		};

		bool wanted(const nppmac::platform::DirectoryChange& change) const
		{
			if (change._change == nppmac::platform::FileChange::modified)
				return (_filter & (FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY)) != 0;
			return (_filter & (change._isDirectory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME)) != 0;
		}

		static DWORD action(nppmac::platform::FileChange change)
		{
			switch (change)
			{
				case nppmac::platform::FileChange::added:
					return FILE_ACTION_ADDED;
				case nppmac::platform::FileChange::removed:
					return FILE_ACTION_REMOVED;
				case nppmac::platform::FileChange::renamedOld:
					return FILE_ACTION_RENAMED_OLD_NAME;
				case nppmac::platform::FileChange::renamedNew:
					return FILE_ACTION_RENAMED_NEW_NAME;
				case nppmac::platform::FileChange::modified:
				default:
					return FILE_ACTION_MODIFIED;
			}
		}

		// Under _mutex: fills the pending read with as many changes as fit, the rest wait for the next.
		void deliver()
		{
			if (!_request || _backlog.empty())
				return;
			DWORD used = 0;
			FILE_NOTIFY_INFORMATION* previous = nullptr;
			while (!_backlog.empty())
			{
				// Names are relative and '\' separated, as Windows reports them.
				std::u16string name = nppmac::toUtf16(_backlog.front()._relativePath);
				std::replace(name.begin(), name.end(), u'/', u'\\');
				const DWORD nameBytes = static_cast<DWORD>(name.size() * sizeof(char16_t));
				const DWORD recordSize = (static_cast<DWORD>(offsetof(FILE_NOTIFY_INFORMATION, FileName)) + nameBytes + sizeof(DWORD) - 1) & ~static_cast<DWORD>(sizeof(DWORD) - 1);
				if (used + recordSize > _request->_length)
					break;
				auto* record = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(_request->_buffer + used);
				record->NextEntryOffset = 0;
				record->Action = action(_backlog.front()._change);
				record->FileNameLength = nameBytes;
				std::memcpy(record->FileName, name.data(), nameBytes);
				if (previous)
					previous->NextEntryOffset = static_cast<DWORD>(reinterpret_cast<BYTE*>(record) - reinterpret_cast<BYTE*>(previous));
				previous = record;
				used += recordSize;
				_backlog.pop_front();
			}
			if (used == 0)
			{
				// A name longer than the whole buffer: dropped, as Windows would lose it too.
				_backlog.pop_front();
				return;
			}
			complete(ERROR_SUCCESS, used);
		}

		// Under _mutex: ends the pending read through its completion routine, on the issuing thread.
		void complete(DWORD error, DWORD bytes)
		{
			Request request = std::move(*_request);
			_request.reset();
			if (request._overlapped)
			{
				request._overlapped->Internal = error;
				request._overlapped->InternalHigh = bytes;
			}
			if (request._completion)
			{
				LPOVERLAPPED_COMPLETION_ROUTINE completion = request._completion;
				LPOVERLAPPED overlapped = request._overlapped;
				queueApc(request._thread, [completion, overlapped, error, bytes] { completion(error, bytes, overlapped); });
			}
			else if (request._overlapped && request._overlapped->hEvent)
			{
				SetEvent(request._overlapped->hEvent);
			}
		}

		std::mutex _mutex;
		DWORD _filter = 0;
		std::deque<nppmac::platform::DirectoryChange> _backlog;
		std::optional<Request> _request;
		std::unique_ptr<nppmac::platform::DirectoryWatcher> _watcher;
	};

	class File : public KernelObject
	{
	public:
		File(int fd, std::string path, bool deleteOnClose) : _fd{fd}, _path{std::move(path)}, _deleteOnClose{deleteOnClose} {}

		~File() override
		{
			if (_fd >= 0)
				::close(_fd);
			if (_deleteOnClose)
				::unlink(_path.c_str());
		}

		int fd() const {
			return _fd;
		}

		const std::string& path() const {
			return _path;
		}

		// Set by the first ReadDirectoryChangesW on a folder.
		std::shared_ptr<DirectoryWatch> _watch;
		// An end of a pipe (CreatePipe): reads return what is there, as on Windows.
		bool _pipe = false;

	private:
		int _fd = -1;
		std::string _path;
		bool _deleteOnClose = false;
	};

	class FindHandle : public KernelObject
	{
	public:
		std::string _directory;
		std::u16string _pattern;
		std::vector<std::string> _names;
		size_t _next = 0;
	};

	class FileMapping : public KernelObject
	{
	public:
		std::shared_ptr<File> _file;
		unsigned long long _size = 0;
		bool _writable = false;
	};

	struct MappedView
	{
		void* _address = nullptr;
		size_t _length = 0;
	};

	nppmac::Immortal<std::mutex> g_viewsMutexStorage;

	std::mutex& g_viewsMutex = g_viewsMutexStorage._value;

	std::vector<MappedView>& views()
	{
		static auto* list = new std::vector<MappedView>;
		return *list;
	}

	// Win32 wildcard match (* and ?), case-insensitive as the default macOS file system is.
	bool matchesPattern(std::u16string_view name, std::u16string_view pattern)
	{
		if (pattern == u"*" || pattern == u"*.*")
			return true;
		size_t n = 0;
		size_t p = 0;
		size_t starPattern = std::u16string_view::npos;
		size_t starName = 0;
		while (n < name.size())
		{
			if (p < pattern.size() && (pattern[p] == u'?' || nppmac::tolower(pattern[p], std::locale::classic()) == nppmac::tolower(name[n], std::locale::classic())))
			{
				++n;
				++p;
			}
			else if (p < pattern.size() && pattern[p] == u'*')
			{
				starPattern = p++;
				starName = n;
			}
			else if (starPattern != std::u16string_view::npos)
			{
				p = starPattern + 1;
				n = ++starName;
			}
			else
			{
				return false;
			}
		}
		while (p < pattern.size() && pattern[p] == u'*')
			++p;
		// "name.*" also matches "name" without an extension, as on Windows.
		if (p + 2 == pattern.size() && pattern[p] == u'.' && pattern[p + 1] == u'*')
			return true;
		return p == pattern.size();
	}

	bool fillFindData(const std::string& directory, const std::string& name, WIN32_FIND_DATAW* data)
	{
		const std::string fullPath = directory.empty() || directory == "/" ? directory + name : directory + "/" + name;
		struct stat info{};
		if (::stat(fullPath.c_str(), &info) != 0 && ::lstat(fullPath.c_str(), &info) != 0)
			return false;

		std::memset(data, 0, sizeof(*data));
		data->dwFileAttributes = fileAttributes(fullPath, info);
		data->ftCreationTime = toFileTime(info.st_birthtimespec);
		data->ftLastAccessTime = toFileTime(info.st_atimespec);
		data->ftLastWriteTime = toFileTime(info.st_mtimespec);
		data->nFileSizeHigh = static_cast<DWORD>(static_cast<unsigned long long>(info.st_size) >> 32);
		data->nFileSizeLow = static_cast<DWORD>(info.st_size & 0xFFFFFFFF);
		const std::u16string wideName = nppmac::toUtf16(name);
		wcsncpy_s(data->cFileName, MAX_PATH, wideName.c_str(), _TRUNCATE);
		return true;
	}

	bool findNext(FindHandle& find, WIN32_FIND_DATAW* data)
	{
		while (find._next < find._names.size())
		{
			const std::string& name = find._names[find._next++];
			if (matchesPattern(nppmac::toUtf16(name), find._pattern) && fillFindData(find._directory, name, data))
				return true;
		}
		return false;
	}
}

// --- File handles ---------------------------------------------------------------------------------

HANDLE WINAPI CreateFileW(LPCWSTR fileName, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags, HANDLE)
{
	const std::string path = toPosixPath(fileName);
	if (path.empty())
	{
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	const bool wantsWrite = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
	const bool wantsRead = (access & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) != 0 || !wantsWrite;
	int openFlags = O_CLOEXEC;
	openFlags |= wantsRead && wantsWrite ? O_RDWR : (wantsWrite ? O_WRONLY : O_RDONLY);

	struct stat info{};
	const bool exists = ::stat(path.c_str(), &info) == 0;
	switch (disposition)
	{
		case CREATE_NEW:
			openFlags |= O_CREAT | O_EXCL;
			break;
		case CREATE_ALWAYS:
			openFlags |= O_CREAT | O_TRUNC;
			break;
		case OPEN_ALWAYS:
			openFlags |= O_CREAT;
			break;
		case TRUNCATE_EXISTING:
			openFlags |= O_TRUNC;
			break;
		case OPEN_EXISTING:
		default:
			break;
	}

	// Directories open read-only (FILE_FLAG_BACKUP_SEMANTICS), for times and change notifications.
	if (exists && S_ISDIR(info.st_mode))
	{
		if (!(flags & FILE_FLAG_BACKUP_SEMANTICS))
		{
			SetLastError(ERROR_ACCESS_DENIED);
			return INVALID_HANDLE_VALUE;
		}
		openFlags = O_RDONLY | O_CLOEXEC;
	}

	const int fd = ::open(path.c_str(), openFlags, 0666);
	if (fd < 0)
	{
		setErrorFromErrno();
		return INVALID_HANDLE_VALUE;
	}

	HANDLE handle = createHandle(std::make_shared<File>(fd, path, (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0));
	// CREATE_ALWAYS and OPEN_ALWAYS report whether the file was already there.
	SetLastError((disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS) && exists ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return handle;
}

HANDLE WINAPI CreateFileA(LPCSTR fileName, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile)
{
	return CreateFileW(nppmac::toUtf16(fileName ? fileName : "").c_str(), access, share, security, disposition, flags, templateFile);
}

BOOL WINAPI ReadFile(HANDLE handle, LPVOID buffer, DWORD toRead, LPDWORD read, LPOVERLAPPED)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	size_t total = 0;
	while (total < toRead)
	{
		// A pipe gives what it has; its writer gone, the read fails with ERROR_BROKEN_PIPE.
		if (file->_pipe && total > 0)
			break;
		const ssize_t n = ::read(file->fd(), static_cast<char*>(buffer) + total, toRead - total);
		if (n == 0 && file->_pipe)
		{
			if (read)
				*read = 0;
			SetLastError(ERROR_BROKEN_PIPE);
			return FALSE;
		}
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			setErrorFromErrno();
			if (read)
				*read = static_cast<DWORD>(total);
			return FALSE;
		}
		if (n == 0)
			break;
		total += static_cast<size_t>(n);
	}
	if (read)
		*read = static_cast<DWORD>(total);
	return TRUE;
}

BOOL WINAPI WriteFile(HANDLE handle, LPCVOID buffer, DWORD toWrite, LPDWORD written, LPOVERLAPPED)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	size_t total = 0;
	while (total < toWrite)
	{
		const ssize_t n = ::write(file->fd(), static_cast<const char*>(buffer) + total, toWrite - total);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			setErrorFromErrno();
			if (written)
				*written = static_cast<DWORD>(total);
			return FALSE;
		}
		total += static_cast<size_t>(n);
	}
	if (written)
		*written = static_cast<DWORD>(total);
	return TRUE;
}

BOOL WINAPI FlushFileBuffers(HANDLE handle)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	if (::fsync(file->fd()) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI SetFilePointerEx(HANDLE handle, LARGE_INTEGER distance, PLARGE_INTEGER newPosition, DWORD method)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	const int whence = method == FILE_BEGIN ? SEEK_SET : (method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
	const off_t position = ::lseek(file->fd(), static_cast<off_t>(distance.QuadPart), whence);
	if (position < 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	if (newPosition)
		newPosition->QuadPart = position;
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE handle, LONG distance, PLONG distanceHigh, DWORD method)
{
	LARGE_INTEGER move{};
	move.QuadPart = distanceHigh ? (static_cast<LONGLONG>(*distanceHigh) << 32) | static_cast<DWORD>(distance) : distance;
	LARGE_INTEGER position{};
	if (!SetFilePointerEx(handle, move, &position, method))
		return INVALID_SET_FILE_POINTER;
	if (distanceHigh)
		*distanceHigh = position.HighPart;
	SetLastError(ERROR_SUCCESS);
	return position.LowPart;
}

BOOL WINAPI SetEndOfFile(HANDLE handle)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	const off_t position = ::lseek(file->fd(), 0, SEEK_CUR);
	if (position < 0 || ::ftruncate(file->fd(), position) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetFileSizeEx(HANDLE handle, PLARGE_INTEGER size)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	struct stat info{};
	if (::fstat(file->fd(), &info) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	size->QuadPart = info.st_size;
	return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE handle, LPDWORD sizeHigh)
{
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(handle, &size))
		return INVALID_FILE_SIZE;
	if (sizeHigh)
		*sizeHigh = static_cast<DWORD>(size.HighPart);
	return size.LowPart;
}

DWORD WINAPI GetFileType(HANDLE handle)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FILE_TYPE_UNKNOWN;
	struct stat info{};
	::fstat(file->fd(), &info);
	if (S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode))
		return FILE_TYPE_PIPE;
	if (S_ISCHR(info.st_mode))
		return FILE_TYPE_CHAR;
	return FILE_TYPE_DISK;
}

BOOL WINAPI GetFileTime(HANDLE handle, LPFILETIME creation, LPFILETIME lastAccess, LPFILETIME lastWrite)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	struct stat info{};
	if (::fstat(file->fd(), &info) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	if (creation)
		*creation = toFileTime(info.st_birthtimespec);
	if (lastAccess)
		*lastAccess = toFileTime(info.st_atimespec);
	if (lastWrite)
		*lastWrite = toFileTime(info.st_mtimespec);
	return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE handle, const FILETIME*, const FILETIME* lastAccess, const FILETIME* lastWrite)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	struct stat info{};
	::fstat(file->fd(), &info);
	timespec times[2] = {info.st_atimespec, info.st_mtimespec};
	if (lastAccess)
		times[0] = toTimespec(*lastAccess);
	if (lastWrite)
		times[1] = toTimespec(*lastWrite);
	if (::futimens(file->fd(), times) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetFileInformationByHandle(HANDLE handle, LPBY_HANDLE_FILE_INFORMATION information)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	struct stat info{};
	if (::fstat(file->fd(), &info) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	std::memset(information, 0, sizeof(*information));
	information->dwFileAttributes = fileAttributes(file->path(), info);
	information->ftCreationTime = toFileTime(info.st_birthtimespec);
	information->ftLastAccessTime = toFileTime(info.st_atimespec);
	information->ftLastWriteTime = toFileTime(info.st_mtimespec);
	information->dwVolumeSerialNumber = static_cast<DWORD>(info.st_dev);
	information->nFileSizeHigh = static_cast<DWORD>(static_cast<unsigned long long>(info.st_size) >> 32);
	information->nFileSizeLow = static_cast<DWORD>(info.st_size & 0xFFFFFFFF);
	information->nNumberOfLinks = static_cast<DWORD>(info.st_nlink);
	information->nFileIndexHigh = static_cast<DWORD>(info.st_ino >> 32);
	information->nFileIndexLow = static_cast<DWORD>(info.st_ino & 0xFFFFFFFF);
	return TRUE;
}

DWORD WINAPI GetFinalPathNameByHandleW(HANDLE handle, LPWSTR buffer, DWORD size, DWORD)
{
	auto file = lookup<File>(handle);
	if (!file)
		return 0;
	char resolved[PATH_MAX]{};
	if (::fcntl(file->fd(), F_GETPATH, resolved) != 0)
	{
		setErrorFromErrno();
		return 0;
	}
	return copyToBuffer(fromPosixPath(resolved), buffer, size);
}

BOOL WINAPI LockFile(HANDLE, DWORD, DWORD, DWORD, DWORD)
{
	return TRUE;
}

BOOL WINAPI UnlockFile(HANDLE, DWORD, DWORD, DWORD, DWORD)
{
	return TRUE;
}

BOOL WINAPI CancelIo(HANDLE handle)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	if (file->_watch)
		file->_watch->cancel();
	return TRUE;
}

// --- Change notifications ------------------------------------------------------------------------

namespace
{
	// A change notification is an event, set when the folder changes (any change: the filter is not
	// narrowed down), with the watcher that sets it.
	nppmac::Immortal<std::mutex> g_changeNotificationsMutexStorage;
	std::mutex& g_changeNotificationsMutex = g_changeNotificationsMutexStorage._value;

	std::unordered_map<HANDLE, std::unique_ptr<nppmac::platform::DirectoryWatcher>>& changeNotifications()
	{
		static auto* notifications = new std::unordered_map<HANDLE, std::unique_ptr<nppmac::platform::DirectoryWatcher>>;
		return *notifications;
	}
}

HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR path, BOOL subtree, DWORD)
{
	const std::string folder = toPosixPath(path ? path : u"");
	struct stat info{};
	if (::stat(folder.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
	{
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	auto watcher = std::make_unique<nppmac::platform::DirectoryWatcher>(folder, subtree != FALSE, [event](std::vector<nppmac::platform::DirectoryChange>)
	{
		SetEvent(event);
	});
	if (!watcher->isWatching())
	{
		CloseHandle(event);
		SetLastError(ERROR_INVALID_FUNCTION);
		return INVALID_HANDLE_VALUE;
	}
	std::lock_guard lock{g_changeNotificationsMutex};
	changeNotifications()[event] = std::move(watcher);
	return event;
}

HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR path, BOOL subtree, DWORD filter)
{
	return FindFirstChangeNotificationW(nppmac::toUtf16(path ? path : "").c_str(), subtree, filter);
}

BOOL WINAPI FindNextChangeNotification(HANDLE notification)
{
	std::lock_guard lock{g_changeNotificationsMutex};
	if (!changeNotifications().contains(notification))
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	// Watching goes on; the next change sets the event again.
	return ResetEvent(notification);
}

BOOL WINAPI FindCloseChangeNotification(HANDLE notification)
{
	std::unique_ptr<nppmac::platform::DirectoryWatcher> watcher;
	{
		std::lock_guard lock{g_changeNotificationsMutex};
		const auto found = changeNotifications().find(notification);
		if (found == changeNotifications().end())
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return FALSE;
		}
		watcher = std::move(found->second);
		changeNotifications().erase(found);
	}
	// Stopped before its event goes away.
	watcher.reset();
	return CloseHandle(notification);
}

BOOL WINAPI ReadDirectoryChangesW(HANDLE handle, LPVOID buffer, DWORD length, BOOL subtree, DWORD filter, LPDWORD returned, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE completion)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	struct stat info{};
	if (::fstat(file->fd(), &info) != 0 || !S_ISDIR(info.st_mode))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	// Only the asynchronous form, the one Notepad++ uses: the call returns at once and the changes
	// arrive through the completion routine (or the overlapped event).
	if (!overlapped || !buffer || length < sizeof(FILE_NOTIFY_INFORMATION))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	if (!file->_watch)
	{
		file->_watch = std::make_shared<DirectoryWatch>(file->path(), subtree != FALSE, filter);
		if (!file->_watch->isWatching())
		{
			file->_watch.reset();
			SetLastError(ERROR_ACCESS_DENIED);
			return FALSE;
		}
	}
	if (returned)
		*returned = 0;
	file->_watch->read(static_cast<BYTE*>(buffer), length, filter, overlapped, completion);
	SetLastError(ERROR_IO_PENDING);
	return TRUE;
}

// --- File mappings --------------------------------------------------------------------------------

HANDLE WINAPI CreateFileMappingW(HANDLE fileHandle, LPSECURITY_ATTRIBUTES, DWORD protect, DWORD sizeHigh, DWORD sizeLow, LPCWSTR)
{
	auto file = lookup<File>(fileHandle);
	if (!file)
		return nullptr;
	auto mapping = std::make_shared<FileMapping>();
	mapping->_file = file;
	mapping->_writable = (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) != 0;
	mapping->_size = (static_cast<unsigned long long>(sizeHigh) << 32) | sizeLow;
	if (mapping->_size == 0)
	{
		struct stat info{};
		::fstat(file->fd(), &info);
		mapping->_size = static_cast<unsigned long long>(info.st_size);
	}
	return createHandle(mapping);
}

LPVOID WINAPI MapViewOfFile(HANDLE mappingHandle, DWORD, DWORD offsetHigh, DWORD offsetLow, SIZE_T bytes)
{
	auto mapping = lookup<FileMapping>(mappingHandle);
	if (!mapping)
		return nullptr;
	const off_t offset = static_cast<off_t>((static_cast<unsigned long long>(offsetHigh) << 32) | offsetLow);
	const size_t length = bytes ? bytes : static_cast<size_t>(mapping->_size - static_cast<unsigned long long>(offset));
	if (length == 0)
	{
		SetLastError(ERROR_FILE_INVALID);
		return nullptr;
	}
	void* address = ::mmap(nullptr, length, PROT_READ | (mapping->_writable ? PROT_WRITE : 0), MAP_SHARED, mapping->_file->fd(), offset);
	if (address == MAP_FAILED)
	{
		setErrorFromErrno();
		return nullptr;
	}
	std::lock_guard lock{g_viewsMutex};
	views().push_back({address, length});
	return address;
}

BOOL WINAPI UnmapViewOfFile(LPCVOID address)
{
	std::lock_guard lock{g_viewsMutex};
	auto it = std::find_if(views().begin(), views().end(), [address](const MappedView& view) { return view._address == address; });
	if (it == views().end())
	{
		SetLastError(ERROR_INVALID_ADDRESS);
		return FALSE;
	}
	::munmap(it->_address, it->_length);
	views().erase(it);
	return TRUE;
}

// --- Attributes and find handles ------------------------------------------------------------------

DWORD WINAPI GetFileAttributesW(LPCWSTR fileName)
{
	const std::string path = toPosixPath(fileName);
	struct stat info{};
	if (path.empty() || ::stat(path.c_str(), &info) != 0)
	{
		setErrorFromErrno(path.empty() ? ENOENT : errno);
		return INVALID_FILE_ATTRIBUTES;
	}
	return fileAttributes(path, info);
}

DWORD WINAPI GetFileAttributesA(LPCSTR fileName)
{
	return GetFileAttributesW(nppmac::toUtf16(fileName ? fileName : "").c_str());
}

BOOL WINAPI GetFileAttributesExW(LPCWSTR fileName, GET_FILEEX_INFO_LEVELS, LPVOID information)
{
	const std::string path = toPosixPath(fileName);
	struct stat info{};
	if (path.empty() || ::stat(path.c_str(), &info) != 0)
	{
		setErrorFromErrno(path.empty() ? ENOENT : errno);
		return FALSE;
	}
	auto* data = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(information);
	data->dwFileAttributes = fileAttributes(path, info);
	data->ftCreationTime = toFileTime(info.st_birthtimespec);
	data->ftLastAccessTime = toFileTime(info.st_atimespec);
	data->ftLastWriteTime = toFileTime(info.st_mtimespec);
	data->nFileSizeHigh = static_cast<DWORD>(static_cast<unsigned long long>(info.st_size) >> 32);
	data->nFileSizeLow = static_cast<DWORD>(info.st_size & 0xFFFFFFFF);
	return TRUE;
}

BOOL WINAPI SetFileAttributesW(LPCWSTR fileName, DWORD attributes)
{
	const std::string path = toPosixPath(fileName);
	struct stat info{};
	if (::stat(path.c_str(), &info) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	mode_t mode = info.st_mode & 07777;
	if (attributes & FILE_ATTRIBUTE_READONLY)
		mode &= ~static_cast<mode_t>(0222);
	else
		mode |= S_IWUSR;
	if (::chmod(path.c_str(), mode) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

HANDLE WINAPI FindFirstFileExW(LPCWSTR fileName, FINDEX_INFO_LEVELS, LPVOID findData, FINDEX_SEARCH_OPS, LPVOID, DWORD)
{
	const std::u16string pattern{fileName ? fileName : u""};
	const size_t separator = pattern.find_last_of(u"/\\");
	auto find = std::make_shared<FindHandle>();
	find->_directory = separator == std::u16string::npos ? std::string{"."} : toPosixPath(std::u16string_view{pattern}.substr(0, separator));
	if (separator == 0)
		find->_directory = "/";
	find->_pattern = separator == std::u16string::npos ? pattern : pattern.substr(separator + 1);

	// A pattern without wildcards names one file or directory.
	if (find->_pattern.find_first_of(u"*?") == std::u16string::npos)
	{
		find->_names.push_back(nppmac::toUtf8(find->_pattern));
	}
	else
	{
		DIR* dir = ::opendir(find->_directory.c_str());
		if (!dir)
		{
			setErrorFromErrno(errno == ENOENT ? ENOTDIR : errno);
			return INVALID_HANDLE_VALUE;
		}
		find->_names = {".", ".."};
		while (dirent* entry = ::readdir(dir))
		{
			const std::string name = entry->d_name;
			if (name != "." && name != "..")
				find->_names.push_back(name);
		}
		::closedir(dir);
		if (find->_directory == "/")
			find->_names.erase(find->_names.begin(), find->_names.begin() + 2);
	}

	if (!findNext(*find, static_cast<WIN32_FIND_DATAW*>(findData)))
	{
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	return createHandle(find);
}

HANDLE WINAPI FindFirstFileW(LPCWSTR fileName, LPWIN32_FIND_DATAW findData)
{
	return FindFirstFileExW(fileName, FindExInfoStandard, findData, FindExSearchNameMatch, nullptr, 0);
}

BOOL WINAPI FindNextFileW(HANDLE handle, LPWIN32_FIND_DATAW findData)
{
	auto find = lookup<FindHandle>(handle);
	if (!find)
		return FALSE;
	if (!findNext(*find, findData))
	{
		SetLastError(ERROR_NO_MORE_FILES);
		return FALSE;
	}
	return TRUE;
}

namespace
{
	void toFindDataA(const WIN32_FIND_DATAW& wide, LPWIN32_FIND_DATAA data)
	{
		data->dwFileAttributes = wide.dwFileAttributes;
		data->ftCreationTime = wide.ftCreationTime;
		data->ftLastAccessTime = wide.ftLastAccessTime;
		data->ftLastWriteTime = wide.ftLastWriteTime;
		data->nFileSizeHigh = wide.nFileSizeHigh;
		data->nFileSizeLow = wide.nFileSizeLow;
		data->dwReserved0 = wide.dwReserved0;
		data->dwReserved1 = wide.dwReserved1;
		copyToBuffer(nppmac::toUtf8(wide.cFileName), data->cFileName, MAX_PATH);
		copyToBuffer(nppmac::toUtf8(wide.cAlternateFileName), data->cAlternateFileName, 14);
	}
}

HANDLE WINAPI FindFirstFileA(LPCSTR fileName, LPWIN32_FIND_DATAA findData)
{
	WIN32_FIND_DATAW wide{};
	HANDLE handle = FindFirstFileW(nppmac::toUtf16(fileName ? fileName : "").c_str(), &wide);
	if (handle != INVALID_HANDLE_VALUE)
		toFindDataA(wide, findData);
	return handle;
}

BOOL WINAPI FindNextFileA(HANDLE handle, LPWIN32_FIND_DATAA findData)
{
	WIN32_FIND_DATAW wide{};
	if (!FindNextFileW(handle, &wide))
		return FALSE;
	toFindDataA(wide, findData);
	return TRUE;
}

BOOL WINAPI FindClose(HANDLE handle)
{
	return CloseHandle(handle);
}

// --- File and directory operations ----------------------------------------------------------------

BOOL WINAPI DeleteFileW(LPCWSTR fileName)
{
	if (::unlink(toPosixPath(fileName).c_str()) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI DeleteFileA(LPCSTR fileName)
{
	return DeleteFileW(nppmac::toUtf16(fileName ? fileName : "").c_str());
}

BOOL WINAPI CopyFileExW(LPCWSTR existing, LPCWSTR newFile, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD flags)
{
	const std::string source = toPosixPath(existing);
	const std::string target = toPosixPath(newFile);
	if ((flags & COPY_FILE_FAIL_IF_EXISTS) && ::access(target.c_str(), F_OK) == 0)
	{
		SetLastError(ERROR_FILE_EXISTS);
		return FALSE;
	}
	if (::copyfile(source.c_str(), target.c_str(), nullptr, COPYFILE_ALL) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI CopyFileW(LPCWSTR existing, LPCWSTR newFile, BOOL failIfExists)
{
	return CopyFileExW(existing, newFile, nullptr, nullptr, nullptr, failIfExists ? COPY_FILE_FAIL_IF_EXISTS : 0);
}

BOOL WINAPI MoveFileExW(LPCWSTR existing, LPCWSTR newFile, DWORD flags)
{
	const std::string source = toPosixPath(existing);
	if (!newFile)
		return DeleteFileW(existing);
	const std::string target = toPosixPath(newFile);
	if (!(flags & MOVEFILE_REPLACE_EXISTING) && ::access(target.c_str(), F_OK) == 0)
	{
		SetLastError(ERROR_ALREADY_EXISTS);
		return FALSE;
	}
	if (::rename(source.c_str(), target.c_str()) != 0)
	{
		if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED))
		{
			if (::copyfile(source.c_str(), target.c_str(), nullptr, COPYFILE_ALL | COPYFILE_MOVE) == 0)
				return TRUE;
		}
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI MoveFileW(LPCWSTR existing, LPCWSTR newFile)
{
	return MoveFileExW(existing, newFile, MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD, LPVOID, LPVOID)
{
	if (backup && !CopyFileW(replaced, backup, FALSE))
		return FALSE;
	return MoveFileExW(replacement, replaced, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI CreateDirectoryW(LPCWSTR pathName, LPSECURITY_ATTRIBUTES)
{
	if (::mkdir(toPosixPath(pathName).c_str(), 0777) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR pathName)
{
	if (::rmdir(toPosixPath(pathName).c_str()) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

// --- Paths ----------------------------------------------------------------------------------------

DWORD WINAPI GetCurrentDirectoryW(DWORD size, LPWSTR buffer)
{
	char path[PATH_MAX]{};
	if (!::getcwd(path, sizeof(path)))
	{
		setErrorFromErrno();
		return 0;
	}
	return copyToBuffer(fromPosixPath(path), buffer, size);
}

BOOL WINAPI SetCurrentDirectoryW(LPCWSTR pathName)
{
	if (::chdir(toPosixPath(pathName).c_str()) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

DWORD WINAPI GetFullPathNameW(LPCWSTR fileName, DWORD size, LPWSTR buffer, LPWSTR* filePart)
{
	std::string path = toPosixPath(fileName);
	if (path.empty())
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	if (path[0] != '/')
	{
		char cwd[PATH_MAX]{};
		::getcwd(cwd, sizeof(cwd));
		path = std::string{cwd} + "/" + path;
	}

	// Resolve "." and ".." and repeated separators lexically, as Windows does (no symlink resolution).
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= path.size())
	{
		size_t end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		const std::string part = path.substr(start, end - start);
		if (part == "..")
		{
			if (!parts.empty())
				parts.pop_back();
		}
		else if (!part.empty() && part != ".")
		{
			parts.push_back(part);
		}
		start = end + 1;
	}
	std::string normalized;
	for (const std::string& part : parts)
		normalized += "/" + part;
	if (normalized.empty())
		normalized = "/";
	if (path.back() == '/' && normalized != "/")
		normalized += "/";

	const std::u16string result = fromPosixPath(normalized);
	const DWORD length = copyToBuffer(result, buffer, size);
	if (filePart && buffer && length < size)
	{
		const size_t slash = result.find_last_of(u'/');
		*filePart = (slash == std::u16string::npos || slash + 1 == result.size()) ? nullptr : buffer + slash + 1;
	}
	return length;
}

DWORD WINAPI GetLongPathNameW(LPCWSTR shortPath, LPWSTR longPath, DWORD size)
{
	const std::u16string path{shortPath ? shortPath : u""};
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return 0;
	return copyToBuffer(path, longPath, size);
}

DWORD WINAPI GetShortPathNameW(LPCWSTR longPath, LPWSTR shortPath, DWORD size)
{
	return GetLongPathNameW(longPath, shortPath, size);
}

DWORD WINAPI GetTempPathW(DWORD size, LPWSTR buffer)
{
	const char* tmp = std::getenv("TMPDIR");
	std::string path = tmp && *tmp ? tmp : "/tmp/";
	if (path.back() != '/')
		path += '/';
	return copyToBuffer(fromPosixPath(path), buffer, size);
}

DWORD WINAPI GetTempPathA(DWORD size, LPSTR buffer)
{
	char16_t wide[MAX_PATH]{};
	if (!GetTempPathW(MAX_PATH, wide))
		return 0;
	return copyToBuffer(nppmac::toUtf8(wide), buffer, size);
}

UINT WINAPI GetTempFileNameW(LPCWSTR pathName, LPCWSTR prefix, UINT unique, LPWSTR tempFileName)
{
	std::string directory = toPosixPath(pathName);
	if (!directory.empty() && directory.back() != '/')
		directory += '/';
	const std::string prefixText = nppmac::toUtf8(std::u16string_view{prefix ? prefix : u""}.substr(0, 3));
	UINT number = unique ? unique : static_cast<UINT>(::arc4random() & 0xFFFF);
	for (int attempt = 0; attempt < 0x10000; ++attempt, number = (number + 1) & 0xFFFF)
	{
		char name[32]{};
		std::snprintf(name, sizeof(name), "%X.tmp", number);
		const std::string path = directory + prefixText + name;
		if (unique)
		{
			wcscpy_s(tempFileName, MAX_PATH, fromPosixPath(path).c_str());
			return unique;
		}
		const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
		if (fd >= 0)
		{
			::close(fd);
			wcscpy_s(tempFileName, MAX_PATH, fromPosixPath(path).c_str());
			return number;
		}
	}
	SetLastError(ERROR_FILE_EXISTS);
	return 0;
}

BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR directory, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total, PULARGE_INTEGER totalFree)
{
	struct statfs info{};
	if (::statfs(directory ? toPosixPath(directory).c_str() : ".", &info) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	if (freeToCaller)
		freeToCaller->QuadPart = static_cast<ULONGLONG>(info.f_bavail) * info.f_bsize;
	if (total)
		total->QuadPart = static_cast<ULONGLONG>(info.f_blocks) * info.f_bsize;
	if (totalFree)
		totalFree->QuadPart = static_cast<ULONGLONG>(info.f_bfree) * info.f_bsize;
	return TRUE;
}

UINT WINAPI GetDriveTypeW(LPCWSTR rootPath)
{
	struct statfs info{};
	if (::statfs(rootPath ? toPosixPath(rootPath).c_str() : "/", &info) != 0)
		return DRIVE_NO_ROOT_DIR;
	return (info.f_flags & MNT_LOCAL) ? DRIVE_FIXED : DRIVE_REMOTE;
}

// --- Pipes ----------------------------------------------------------------------------------------

namespace nppmac::kernel
{
	int fileDescriptor(HANDLE handle)
	{
		std::shared_ptr<File> file = std::dynamic_pointer_cast<File>(lookupHandle(handle));
		return file ? file->fd() : -1;
	}
}

BOOL WINAPI CreatePipe(PHANDLE readPipe, PHANDLE writePipe, LPSECURITY_ATTRIBUTES, DWORD)
{
	int fds[2]{};
	if (::pipe(fds) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	// Kept from children: a child gets a pipe only as one of its standard handles (CreateProcess).
	::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	auto reader = std::make_shared<File>(fds[0], std::string{}, false);
	auto writer = std::make_shared<File>(fds[1], std::string{}, false);
	reader->_pipe = true;
	writer->_pipe = true;
	*readPipe = createHandle(reader);
	*writePipe = createHandle(writer);
	return TRUE;
}

BOOL WINAPI PeekNamedPipe(HANDLE handle, LPVOID buffer, DWORD size, LPDWORD read, LPDWORD available, LPDWORD leftInMessage)
{
	auto file = lookup<File>(handle);
	if (!file)
		return FALSE;
	int count = 0;
	if (::ioctl(file->fd(), FIONREAD, &count) != 0)
	{
		setErrorFromErrno();
		return FALSE;
	}
	// A pipe cannot be read without taking the data: the bytes waiting are reported, none copied.
	(void)buffer;
	(void)size;
	if (read)
		*read = 0;
	if (available)
		*available = static_cast<DWORD>(count);
	if (leftInMessage)
		*leftInMessage = 0;
	if (count == 0)
	{
		// No data, and no writer left: the pipe is broken, as Windows reports it.
		pollfd poll{file->fd(), POLLIN, 0};
		if (::poll(&poll, 1, 0) > 0 && (poll.revents & POLLHUP))
		{
			SetLastError(ERROR_BROKEN_PIPE);
			return FALSE;
		}
	}
	return TRUE;
}

BOOL WINAPI SetHandleInformation(HANDLE handle, DWORD, DWORD)
{
	// Inheritance is decided by CreateProcess's standard handles alone.
	return lookupHandle(handle) ? TRUE : FALSE;
}
