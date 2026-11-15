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


#include "Instances.h"

#include "nppmac/WideString.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nppmac::kernel
{
	namespace
	{
		std::string baseDirectory()
		{
			if (const char* chosen = std::getenv("NPPMAC_INSTANCE_DIR"); chosen && *chosen)
				return chosen;
			if (std::getenv("NPPMAC_TEST_RUN"))
				return {};
#ifdef __APPLE__
			char temporary[1024]{};
			if (confstr(_CS_DARWIN_USER_TEMP_DIR, temporary, sizeof(temporary)) > 0)
				return std::string{temporary} + "nppmac";
#else
			if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
				return std::string{runtime} + "/nppmac";
#endif
			return "/tmp/nppmac-" + std::to_string(getuid());
		}

		// The folder, made the user's own; empty if it cannot be (another user's, or not a folder).
		std::string makeDirectory()
		{
			std::string directory = baseDirectory();
			if (directory.empty())
				return {};
			while (directory.size() > 1 && directory.back() == '/')
				directory.pop_back();
			::mkdir(directory.c_str(), 0700);
			struct stat info{};
			if (::lstat(directory.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid())
				return {};
			if ((info.st_mode & 0077) != 0)
				::chmod(directory.c_str(), 0700);
			return directory;
		}
	}

	const std::string& instanceDirectory()
	{
		static const std::string directory = makeDirectory();
		return directory;
	}

	bool claimName(std::u16string_view name)
	{
		const std::string& directory = instanceDirectory();
		if (directory.empty() || name.empty())
			return true;
		// The name spelled safely as a file name: letters and digits as they are, the rest as %XX.
		std::string file = directory + "/name-";
		for (const unsigned char ch : nppmac::toUtf8(name))
		{
			if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
				file.push_back(static_cast<char>(ch));
			else
			{
				char escaped[4];
				std::snprintf(escaped, sizeof(escaped), "%%%02X", ch);
				file += escaped;
			}
		}
		file += ".lock";
		// The lock lasts as long as this process: the descriptor stays open, and is not inherited.
		const int descriptor = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if (descriptor < 0)
			return true;
		if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0)
		{
			::close(descriptor);
			return errno != EWOULDBLOCK;
		}
		return true;
	}
}
