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

// Tests of ReadDirectoryChangesW the way Notepad++'s CReadDirectoryChanges uses it: a server thread
// in an alertable SleepEx, work handed to it by QueueUserAPC, overlapped reads completed by APC, and
// CancelIo ending the pending read with ERROR_OPERATION_ABORTED.

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void check(bool condition, const char* expression, int line)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::cerr << "FAIL line " << line << ": " << expression << "\n";
		}
	}

#define CHECK(expression) check((expression), #expression, __LINE__)

	struct Change
	{
		DWORD _action = 0;
		std::string _name;
	};

	struct Server
	{
		std::u16string _folder;
		HANDLE _directory = INVALID_HANDLE_VALUE;
		std::vector<BYTE> _buffer = std::vector<BYTE>(16384);
		OVERLAPPED _overlapped{};
		bool _terminate = false;
		bool _aborted = false;
		bool _readAccepted = false;
		std::mutex _mutex;
		std::vector<Change> _changes; // under _mutex
		HANDLE _changed = nullptr;
	};

	void beginRead(Server* server);

	VOID CALLBACK completion(DWORD error, DWORD bytes, LPOVERLAPPED overlapped)
	{
		auto* server = static_cast<Server*>(overlapped->hEvent);
		if (error == ERROR_OPERATION_ABORTED)
		{
			server->_aborted = true;
			return;
		}
		std::vector<BYTE> copy(server->_buffer.begin(), server->_buffer.begin() + bytes);
		beginRead(server);
		std::lock_guard lock{server->_mutex};
		for (BYTE* base = copy.data(); bytes != 0;)
		{
			auto& record = *reinterpret_cast<FILE_NOTIFY_INFORMATION*>(base);
			const std::u16string name{reinterpret_cast<const char16_t*>(record.FileName), record.FileNameLength / sizeof(char16_t)};
			server->_changes.push_back({record.Action, nppmac::toUtf8(name)});
			if (!record.NextEntryOffset)
				break;
			base += record.NextEntryOffset;
		}
		SetEvent(server->_changed);
	}

	void beginRead(Server* server)
	{
		DWORD bytes = 0;
		server->_overlapped.hEvent = server;
		server->_readAccepted = ::ReadDirectoryChangesW(server->_directory, server->_buffer.data(), static_cast<DWORD>(server->_buffer.size()), TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, &bytes, &server->_overlapped, completion) != FALSE;
	}

	VOID CALLBACK addDirectory(ULONG_PTR parameter)
	{
		auto* server = reinterpret_cast<Server*>(parameter);
		server->_directory = ::CreateFileW(server->_folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
		beginRead(server);
	}

	VOID CALLBACK terminate(ULONG_PTR parameter)
	{
		auto* server = reinterpret_cast<Server*>(parameter);
		::CancelIo(server->_directory);
		::CloseHandle(server->_directory);
		server->_terminate = true;
	}

	DWORD WINAPI serverThread(LPVOID parameter)
	{
		auto* server = static_cast<Server*>(parameter);
		while (!server->_terminate || !server->_aborted)
			::SleepEx(INFINITE, TRUE);
		return 0;
	}

	// Waits until a change with this action and name has been reported.
	bool waitFor(Server& server, DWORD action, const std::string& name)
	{
		for (int attempt = 0; attempt < 50; ++attempt)
		{
			{
				std::lock_guard lock{server._mutex};
				for (const auto& change : server._changes)
				{
					if (change._action == action && change._name == name)
						return true;
				}
			}
			::WaitForSingleObject(server._changed, 100);
		}
		return false;
	}

	void writeFile(const std::string& path, const char* text)
	{
		FILE* file = std::fopen(path.c_str(), "w");
		std::fputs(text, file);
		std::fclose(file);
	}
}

int main()
{
	char folderTemplate[] = "/tmp/npp-directory-changes.XXXXXX";
	const std::string folder = ::mkdtemp(folderTemplate);

	Server server;
	server._folder = nppmac::toUtf16(folder);
	server._changed = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);

	DWORD id = 0;
	HANDLE thread = ::CreateThread(nullptr, 0, serverThread, &server, 0, &id);
	CHECK(::QueueUserAPC(addDirectory, thread, reinterpret_cast<ULONG_PTR>(&server)) != 0);

	// The APC runs on the server thread; give it time to start the watch.
	for (int attempt = 0; attempt < 50 && !server._readAccepted; ++attempt)
		::Sleep(20);
	CHECK(server._readAccepted);
	::Sleep(300);

	writeFile(folder + "/added.txt", "one");
	CHECK(waitFor(server, FILE_ACTION_ADDED, "added.txt"));

	::mkdir((folder + "/sub").c_str(), 0755);
	CHECK(waitFor(server, FILE_ACTION_ADDED, "sub"));
	writeFile(folder + "/sub/inner.txt", "two");
	// Names below the folder are relative and '\' separated, as Windows reports them.
	CHECK(waitFor(server, FILE_ACTION_ADDED, "sub\\inner.txt"));

	::rename((folder + "/added.txt").c_str(), (folder + "/renamed.txt").c_str());
	CHECK(waitFor(server, FILE_ACTION_RENAMED_OLD_NAME, "added.txt"));
	CHECK(waitFor(server, FILE_ACTION_RENAMED_NEW_NAME, "renamed.txt"));

	::unlink((folder + "/renamed.txt").c_str());
	CHECK(waitFor(server, FILE_ACTION_REMOVED, "renamed.txt"));

	// Termination: CancelIo completes the pending read with ERROR_OPERATION_ABORTED, which ends the thread.
	CHECK(::QueueUserAPC(terminate, thread, reinterpret_cast<ULONG_PTR>(&server)) != 0);
	CHECK(::WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
	CHECK(server._aborted);
	::CloseHandle(thread);
	::CloseHandle(server._changed);

	// An alertable sleep with nothing queued just times out; with an APC queued it ends early.
	CHECK(::SleepEx(10, TRUE) == 0);
	::QueueUserAPC([](ULONG_PTR) {}, ::GetCurrentThread(), 0);
	CHECK(::SleepEx(INFINITE, TRUE) == WAIT_IO_COMPLETION);

	// A change notification (FindFirstChangeNotification): set by a change, reset to wait for the next.
	HANDLE notification = FindFirstChangeNotificationW(nppmac::toUtf16(folder).c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
	CHECK(notification != INVALID_HANDLE_VALUE);
	if (notification != INVALID_HANDLE_VALUE)
	{
		CHECK(WaitForSingleObject(notification, 0) == WAIT_TIMEOUT);
		writeFile(folder + "/noticed.txt", "three");
		CHECK(WaitForSingleObject(notification, 5000) == WAIT_OBJECT_0);
		CHECK(FindNextChangeNotification(notification));
		CHECK(WaitForSingleObject(notification, 0) == WAIT_TIMEOUT);
		::unlink((folder + "/noticed.txt").c_str());
		CHECK(WaitForSingleObject(notification, 5000) == WAIT_OBJECT_0);
		CHECK(FindCloseChangeNotification(notification));
	}
	CHECK(FindFirstChangeNotificationW(u"/no/such/folder", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME) == INVALID_HANDLE_VALUE);

	std::system(("rm -rf '" + folder + "'").c_str());

	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
