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


// Other copies of the program's windows (Window.h). Each copy listens on a socket of its own in the
// instance folder, <pid>.sock; a request is one connection: a header, its data, and the answer. The
// copy runs what it is asked on its GUI thread, as a message sent from another thread; a copy waiting
// for an answer runs meanwhile what is sent to it, since the other may call back first (Notepad++
// confirms what it received with the sender's window before it acts on it).

#include "Window.h"
#include "kernel/Instances.h"
#include "platform/Platform.h"

#include <cerrno>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace nppmac::user
{
	namespace
	{
		constexpr uint32_t requestMagic = 0x6D70706E; // "nppm"
		constexpr uint32_t maxData = 64u << 20;

		enum class Request : uint32_t
		{
			find = 1, // FindWindow: the data is the class name and the window name
			send,     // SendMessage with numbers
			copyData, // WM_COPYDATA: the data is the COPYDATASTRUCT's
			query,    // a RemoteQuery, in _message
		};

		struct Header
		{
			uint32_t _magic = requestMagic;
			uint32_t _request = 0;
			uint32_t _senderPid = 0;
			uint32_t _message = 0;
			uint64_t _hwnd = 0; // the window, as its own copy numbers it
			uint64_t _wParam = 0;
			int64_t _lParam = 0;
			uint64_t _dataTag = 0; // WM_COPYDATA: dwData
			uint32_t _size = 0;    // the data's, after the header
			uint32_t _reserved = 0;
		};

		struct Reply
		{
			int64_t _result = 0;
			uint32_t _answered = 0;
			uint32_t _reserved = 0;
		};

		// --- Handles for other copies' windows ---------------------------------------------------

		// Far above this copy's own (WindowManager numbers them from 0x10010 up).
		constexpr uintptr_t remoteBase = uintptr_t{1} << 40;

		struct Remote
		{
			pid_t _pid = 0;
			uint64_t _hwnd = 0;
		};

		std::mutex g_remotesMutex;
		uintptr_t g_nextRemote = remoteBase;

		std::map<uintptr_t, Remote>& remotes()
		{
			static auto* map = new std::map<uintptr_t, Remote>;
			return *map;
		}

		HWND remoteHandle(pid_t pid, uint64_t hwnd)
		{
			std::lock_guard lock{g_remotesMutex};
			for (const auto& [handle, remote] : remotes())
			{
				if (remote._pid == pid && remote._hwnd == hwnd)
					return reinterpret_cast<HWND>(handle);
			}
			const uintptr_t handle = g_nextRemote;
			g_nextRemote += 4;
			remotes()[handle] = {pid, hwnd};
			return reinterpret_cast<HWND>(handle);
		}

		std::optional<Remote> remoteFor(HWND hwnd)
		{
			const auto value = reinterpret_cast<uintptr_t>(hwnd);
			if (value < remoteBase)
				return std::nullopt;
			std::lock_guard lock{g_remotesMutex};
			const auto it = remotes().find(value);
			if (it == remotes().end())
				return std::nullopt;
			return it->second;
		}

		// --- Sockets --------------------------------------------------------------------------------

		// The sockets' folder: the instance folder, or, when its path leaves no room for a socket's name
		// (a socket's path has at most 103 bytes on the Mac), a short one of the user's own made from it.
		std::string makeSocketDirectory()
		{
			const std::string& directory = kernel::instanceDirectory();
			if (directory.empty() || directory.size() + sizeof("/4194304.sock") <= sizeof(sockaddr_un{}.sun_path))
				return directory;
			uint32_t hash = 2166136261u;
			for (const unsigned char ch : directory)
				hash = (hash ^ ch) * 16777619u;
			char name[64];
			std::snprintf(name, sizeof(name), "/tmp/nppmac-%u-%08x", static_cast<unsigned>(getuid()), hash);
			::mkdir(name, 0700);
			struct stat info{};
			if (::lstat(name, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0077) != 0)
				return {};
			return name;
		}

		const std::string& socketDirectory()
		{
			static const std::string directory = makeSocketDirectory();
			return directory;
		}

		std::string socketPath(pid_t pid)
		{
			return socketDirectory() + "/" + std::to_string(pid) + ".sock";
		}

		bool fillAddress(sockaddr_un& address, const std::string& path)
		{
			if (path.size() >= sizeof(address.sun_path))
				return false;
			std::memset(&address, 0, sizeof(address));
			address.sun_family = AF_UNIX;
			std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
			return true;
		}

		int newSocket()
		{
			const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
			if (descriptor < 0)
				return -1;
			::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
			int on = 1;
			::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
			return descriptor;
		}

		bool writeAll(int descriptor, const void* data, size_t size)
		{
#ifdef MSG_NOSIGNAL
			constexpr int flags = MSG_NOSIGNAL;
#else
			constexpr int flags = 0;
#endif
			const auto* bytes = static_cast<const char*>(data);
			while (size > 0)
			{
				const ssize_t written = ::send(descriptor, bytes, size, flags);
				if (written < 0)
				{
					if (errno == EINTR)
						continue;
					return false;
				}
				bytes += written;
				size -= static_cast<size_t>(written);
			}
			return true;
		}

		bool readAll(int descriptor, void* data, size_t size)
		{
			auto* bytes = static_cast<char*>(data);
			while (size > 0)
			{
				const ssize_t got = ::recv(descriptor, bytes, size, 0);
				if (got < 0 && errno == EINTR)
					continue;
				if (got <= 0)
					return false;
				bytes += got;
				size -= static_cast<size_t>(got);
			}
			return true;
		}

		using Clock = std::chrono::steady_clock;

		// Waits until the answer can be read; the GUI thread runs meanwhile what is sent to it.
		bool waitForAnswer(int descriptor, Clock::time_point deadline)
		{
			for (;;)
			{
				const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
				if (left <= 0)
					return false;
				pollfd entry{descriptor, POLLIN, 0};
				const int ready = ::poll(&entry, 1, onGuiThread() ? 10 : static_cast<int>(std::min<long long>(left, 1000)));
				if (ready > 0)
					return true;
				if (ready < 0 && errno != EINTR)
					return false;
				processSentMessages();
			}
		}

		// One request to another copy: its answer, or nothing when it cannot be reached in time.
		std::optional<int64_t> call(pid_t pid, Header header, const void* data, int timeoutMilliseconds)
		{
			sockaddr_un address{};
			if (!fillAddress(address, socketPath(pid)))
				return std::nullopt;
			const int descriptor = newSocket();
			if (descriptor < 0)
				return std::nullopt;
			header._magic = requestMagic;
			header._senderPid = static_cast<uint32_t>(getpid());
			std::optional<int64_t> result;
			if (::connect(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 && writeAll(descriptor, &header, sizeof(header))
				&& (header._size == 0 || writeAll(descriptor, data, header._size)))
			{
				const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMilliseconds < 0 ? 30000 : timeoutMilliseconds);
				Reply reply;
				if (waitForAnswer(descriptor, deadline) && readAll(descriptor, &reply, sizeof(reply)) && reply._answered)
					result = reply._result;
			}
			::close(descriptor);
			return result;
		}

		// The other copies running: their sockets in the instance folder (a dead copy's is removed).
		std::vector<pid_t> otherCopies()
		{
			std::vector<pid_t> pids;
			const std::string& directory = socketDirectory();
			DIR* folder = directory.empty() ? nullptr : ::opendir(directory.c_str());
			if (!folder)
				return pids;
			while (const dirent* entry = ::readdir(folder))
			{
				const std::string name = entry->d_name;
				const size_t dot = name.find(".sock");
				if (dot == std::string::npos || dot == 0 || dot + 5 != name.size() || name.find_first_not_of("0123456789") != dot)
					continue;
				const pid_t pid = static_cast<pid_t>(std::stol(name.substr(0, dot)));
				if (pid == getpid())
					continue;
				if (::kill(pid, 0) != 0 && errno == ESRCH)
				{
					::unlink((directory + "/" + name).c_str());
					continue;
				}
				pids.push_back(pid);
			}
			::closedir(folder);
			return pids;
		}

		// --- Serving ----------------------------------------------------------------------------------

		std::u16string readText(const std::vector<char>& data, size_t& offset, bool& present)
		{
			uint32_t length = 0;
			present = false;
			if (offset + sizeof(length) > data.size())
				return {};
			std::memcpy(&length, data.data() + offset, sizeof(length));
			offset += sizeof(length);
			if (length == UINT32_MAX)
				return {};
			const size_t bytes = static_cast<size_t>(length) * sizeof(char16_t);
			if (offset + bytes > data.size())
				return {};
			std::u16string text(length, u'\0');
			std::memcpy(text.data(), data.data() + offset, bytes);
			offset += bytes;
			present = true;
			return text;
		}

		void appendText(std::vector<char>& data, const char16_t* text)
		{
			const uint32_t length = text ? static_cast<uint32_t>(std::char_traits<char16_t>::length(text)) : UINT32_MAX;
			const auto* lengthBytes = reinterpret_cast<const char*>(&length);
			data.insert(data.end(), lengthBytes, lengthBytes + sizeof(length));
			if (text)
				data.insert(data.end(), reinterpret_cast<const char*>(text), reinterpret_cast<const char*>(text + length));
		}

		LRESULT serve(const Header& header, std::vector<char>& data, bool& answered)
		{
			answered = true;
			const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(header._hwnd));
			// Only this copy's top-level windows are served.
			const auto served = [hwnd]
			{
				Window* window = fromHandle(hwnd);
				return window && window->isTopLevel();
			};
			switch (static_cast<Request>(header._request))
			{
				case Request::find:
				{
					size_t offset = 0;
					bool hasClass = false;
					bool hasName = false;
					const std::u16string className = readText(data, offset, hasClass);
					const std::u16string windowName = readText(data, offset, hasName);
					if (!hasClass)
						return 0;
					return callOnGuiThread([&]
					{
						return reinterpret_cast<LRESULT>(FindWindowExW(nullptr, nullptr, className.c_str(), hasName ? windowName.c_str() : nullptr));
					});
				}
				case Request::send:
					return callOnGuiThread([&]
					{
						return served() ? SendMessageW(hwnd, header._message, static_cast<WPARAM>(header._wParam), static_cast<LPARAM>(header._lParam)) : 0;
					});
				case Request::copyData:
				{
					COPYDATASTRUCT copy{static_cast<ULONG_PTR>(header._dataTag), static_cast<DWORD>(data.size()), data.empty() ? nullptr : data.data()};
					const HWND sender = header._wParam ? remoteHandle(static_cast<pid_t>(header._senderPid), header._wParam) : nullptr;
					return callOnGuiThread([&]
					{
						return served() ? SendMessageW(hwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&copy)) : 0;
					});
				}
				case Request::query:
					return callOnGuiThread([&]() -> LRESULT
					{
						if (!served())
							return 0;
						switch (static_cast<RemoteQuery>(header._message))
						{
							case RemoteQuery::isWindow:
								return 1;
							case RemoteQuery::isIconic:
								return IsIconic(hwnd);
							case RemoteQuery::isZoomed:
								return IsZoomed(hwnd);
							case RemoteQuery::show:
								return ShowWindow(hwnd, static_cast<int>(header._wParam));
							case RemoteQuery::activate:
								platform::activateApplication();
								return SetForegroundWindow(hwnd);
						}
						return 0;
					});
			}
			answered = false;
			return 0;
		}

		void serveConnection(int descriptor)
		{
			// A request comes at once: a copy that stops sending one is not waited for.
			timeval timeout{5, 0};
			::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			Header header;
			if (readAll(descriptor, &header, sizeof(header)) && header._magic == requestMagic && header._size <= maxData)
			{
				std::vector<char> data(header._size);
				if (header._size == 0 || readAll(descriptor, data.data(), data.size()))
				{
					Reply reply;
					bool answered = false;
					reply._result = static_cast<int64_t>(serve(header, data, answered));
					reply._answered = answered ? 1 : 0;
					writeAll(descriptor, &reply, sizeof(reply));
				}
			}
			::close(descriptor);
		}

		const std::string* g_serverPath = nullptr;
	}

	void startWindowServer()
	{
		if (socketDirectory().empty() || g_serverPath)
			return;
		const std::string path = socketPath(getpid());
		sockaddr_un address{};
		if (!fillAddress(address, path))
			return;
		const int descriptor = newSocket();
		if (descriptor < 0)
			return;
		::unlink(path.c_str());
		if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(descriptor, 16) != 0)
		{
			::close(descriptor);
			return;
		}
		::chmod(path.c_str(), 0600);
		g_serverPath = new std::string(path);
		std::atexit([] { ::unlink(g_serverPath->c_str()); });
		std::thread([descriptor]
		{
			for (;;)
			{
				const int connection = ::accept(descriptor, nullptr, nullptr);
				if (connection < 0)
				{
					if (errno == EINTR || errno == ECONNABORTED)
						continue;
					return;
				}
				::fcntl(connection, F_SETFD, FD_CLOEXEC);
				std::thread(serveConnection, connection).detach();
			}
		}).detach();
	}

	HWND findRemoteWindow(const char16_t* className, const char16_t* windowName)
	{
		// A class atom names a class of this copy only.
		if (!className || (reinterpret_cast<uintptr_t>(className) >> 16) == 0)
			return nullptr;
		const std::vector<pid_t> copies = otherCopies();
		if (copies.empty())
			return nullptr;
		std::vector<char> data;
		appendText(data, className);
		appendText(data, windowName);
		for (const pid_t pid : copies)
		{
			Header header;
			header._request = static_cast<uint32_t>(Request::find);
			header._size = static_cast<uint32_t>(data.size());
			const std::optional<int64_t> found = call(pid, header, data.data(), 2000);
			if (found && *found)
				return remoteHandle(pid, static_cast<uint64_t>(*found));
		}
		return nullptr;
	}

	bool isRemoteWindow(HWND hwnd)
	{
		return remoteFor(hwnd).has_value();
	}

	DWORD remoteWindowProcess(HWND hwnd)
	{
		const std::optional<Remote> remote = remoteFor(hwnd);
		return remote ? static_cast<DWORD>(remote->_pid) : 0;
	}

	LRESULT sendToRemoteWindow(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, int timeoutMilliseconds, bool* delivered)
	{
		if (delivered)
			*delivered = false;
		const std::optional<Remote> remote = remoteFor(hwnd);
		if (!remote)
		{
			SetLastError(ERROR_INVALID_WINDOW_HANDLE);
			return 0;
		}
		Header header;
		header._hwnd = remote->_hwnd;
		header._message = message;
		const void* data = nullptr;
		if (message == WM_COPYDATA)
		{
			const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
			if (!copy || copy->cbData > maxData)
				return 0;
			header._request = static_cast<uint32_t>(Request::copyData);
			// The sender's window, a window of this copy's (the receiver answers through it).
			header._wParam = isRemoteWindow(reinterpret_cast<HWND>(wParam)) ? 0 : static_cast<uint64_t>(wParam);
			header._dataTag = copy->dwData;
			header._size = copy->cbData;
			data = copy->lpData;
		}
		else
		{
			// Numbers only: what a message points to is not in the other copy (Notepad++ sends none).
			header._request = static_cast<uint32_t>(Request::send);
			header._wParam = static_cast<uint64_t>(wParam);
			header._lParam = static_cast<int64_t>(lParam);
		}
		const std::optional<int64_t> result = call(remote->_pid, header, data, timeoutMilliseconds);
		if (delivered)
			*delivered = result.has_value();
		return result ? static_cast<LRESULT>(*result) : 0;
	}

	LRESULT askRemoteWindow(HWND hwnd, RemoteQuery query, WPARAM argument)
	{
		const std::optional<Remote> remote = remoteFor(hwnd);
		if (!remote)
			return 0;
		Header header;
		header._request = static_cast<uint32_t>(Request::query);
		header._message = static_cast<uint32_t>(query);
		header._hwnd = remote->_hwnd;
		header._wParam = static_cast<uint64_t>(argument);
		return static_cast<LRESULT>(call(remote->_pid, header, nullptr, 5000).value_or(0));
	}
}
