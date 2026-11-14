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

// The Microsoft CRT's <io.h>: its low-level file functions by their underscore names, on POSIX's;
// the wide forms (C++) take UTF-16 paths, '\' read as '/'.

#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef _O_RDONLY
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR O_RDWR
#define _O_APPEND O_APPEND
#define _O_CREAT O_CREAT
#define _O_TRUNC O_TRUNC
#define _O_EXCL O_EXCL
#define _O_BINARY 0
#define _O_TEXT 0
#endif

static inline int _access(const char* path, int mode)
{
	// Mode 6 (read and write) and the others are POSIX's: 0 exists, 2 write, 4 read.
	return access(path, mode & (R_OK | W_OK));
}

static inline int _close(int fd)
{
	return close(fd);
}

static inline int _read(int fd, void* buffer, unsigned count)
{
	return (int)read(fd, buffer, count);
}

static inline int _write(int fd, const void* buffer, unsigned count)
{
	return (int)write(fd, buffer, count);
}

static inline long _lseek(int fd, long offset, int origin)
{
	return (long)lseek(fd, offset, origin);
}

static inline long long _lseeki64(int fd, long long offset, int origin)
{
	return (long long)lseek(fd, offset, origin);
}

static inline int _isatty(int fd)
{
	return isatty(fd);
}

static inline int _dup(int fd)
{
	return dup(fd);
}

static inline int _dup2(int fd, int target)
{
	return dup2(fd, target);
}

static inline int _commit(int fd)
{
	return fsync(fd);
}

static inline int _unlink(const char* path)
{
	return unlink(path);
}

static inline int _chmod(const char* path, int mode)
{
	return chmod(path, (mode_t)mode);
}

static inline long _filelength(int fd)
{
	struct stat info;
	return fstat(fd, &info) == 0 ? (long)info.st_size : -1L;
}

#define _open open

#ifdef __cplusplus
#include <string>

inline int _waccess(const char16_t* path, int mode)
{
	return _access(nppmac::nativePath(std::u16string_view{path}).c_str(), mode);
}

inline int _wunlink(const char16_t* path)
{
	return unlink(nppmac::nativePath(std::u16string_view{path}).c_str());
}

inline int _wchmod(const char16_t* path, int mode)
{
	return chmod(nppmac::nativePath(std::u16string_view{path}).c_str(), static_cast<mode_t>(mode));
}

inline int _wopen(const char16_t* path, int flags, int mode = 0)
{
	return open(nppmac::nativePath(std::u16string_view{path}).c_str(), flags, mode);
}
#endif
