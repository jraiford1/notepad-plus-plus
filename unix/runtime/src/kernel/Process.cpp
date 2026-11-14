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

// kernel32 processes: CreateProcess, as plugins run tools with it (and Notepad++ its updater through
// ShellExecute). A command line is split as Windows splits it; "cmd /c <command>" runs the command in
// the POSIX shell. A process handle is signaled when the process ends.

#include "Handles.h"
#include "Internal.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <shellapi.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

using nppmac::kernel::KernelObject;

namespace
{
	class ChildProcess : public KernelObject
	{
	public:
		explicit ChildProcess(pid_t pid) : _pid{pid} {}

		bool isSignaled() const override {
			return _ended;
		}

		// Under the wait lock.
		void ended(int status)
		{
			_exitCode = WIFEXITED(status) ? static_cast<DWORD>(WEXITSTATUS(status)) : static_cast<DWORD>(128 + WTERMSIG(status));
			_ended = true;
		}

		pid_t _pid = 0;
		DWORD _exitCode = STILL_ACTIVE;

	private:
		bool _ended = false;
	};

	// Windows' command line splitting (CommandLineToArgvW): blanks separate arguments, quotes group,
	// backslashes are literal except before a quote.
	std::vector<std::u16string> splitCommandLine(std::u16string_view line)
	{
		std::vector<std::u16string> arguments;
		size_t i = 0;
		while (i < line.size())
		{
			while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
				++i;
			if (i >= line.size())
				break;
			std::u16string argument;
			bool quoted = false;
			while (i < line.size() && (quoted || (line[i] != u' ' && line[i] != u'\t')))
			{
				if (line[i] == u'\\')
				{
					size_t slashes = 0;
					while (i < line.size() && line[i] == u'\\')
					{
						++slashes;
						++i;
					}
					if (i < line.size() && line[i] == u'"')
					{
						argument.append(slashes / 2, u'\\');
						if (slashes % 2)
						{
							argument += u'"';
							++i;
						}
					}
					else
					{
						argument.append(slashes, u'\\');
					}
				}
				else if (line[i] == u'"')
				{
					if (quoted && i + 1 < line.size() && line[i + 1] == u'"')
					{
						argument += u'"';
						i += 2;
						continue;
					}
					quoted = !quoted;
					++i;
				}
				else
				{
					argument += line[i++];
				}
			}
			arguments.push_back(std::move(argument));
		}
		return arguments;
	}

	std::u16string lowerName(std::u16string_view path)
	{
		const size_t slash = path.find_last_of(u"\\/");
		std::u16string name{slash == std::u16string_view::npos ? path : path.substr(slash + 1)};
		for (char16_t& c : name)
		{
			if (c >= u'A' && c <= u'Z')
				c = static_cast<char16_t>(c - u'A' + u'a');
		}
		return name;
	}

	// The program to run: a path as given, or a name looked up on PATH; "x.exe" is also tried as "x".
	std::string programPath(std::u16string_view program)
	{
		std::string path = nppmac::kernel::toPosixPath(program);
		struct stat info{};
		if (path.find('/') != std::string::npos)
		{
			if (::stat(path.c_str(), &info) != 0 && path.size() > 4 && path.compare(path.size() - 4, 4, ".exe") == 0)
				path.erase(path.size() - 4);
			return path;
		}
		if (path.size() > 4 && path.compare(path.size() - 4, 4, ".exe") == 0)
			path.erase(path.size() - 4);
		return path;
	}

	// Starts a process; its handle, or null with the last error set.
	HANDLE spawn(std::u16string_view applicationName, std::u16string_view commandLine, const char16_t* directory,
		int input, int output, int error, const std::vector<std::string>* environment)
	{
		std::vector<std::u16string> arguments = splitCommandLine(commandLine);
		std::u16string program = applicationName.empty() ? (arguments.empty() ? std::u16string{} : arguments.front()) : std::u16string{applicationName};
		if (program.empty())
		{
			SetLastError(ERROR_FILE_NOT_FOUND);
			return nullptr;
		}

		std::vector<std::string> argv;
		const std::u16string name = lowerName(program);
		const bool runSwitch = arguments.size() >= 2 && (arguments[1] == u"/c" || arguments[1] == u"/C" || arguments[1] == u"/k" || arguments[1] == u"/K");
		if ((name == u"cmd" || name == u"cmd.exe") && runSwitch)
		{
			// The command after /c, as typed, to the POSIX shell.
			const size_t switchAt = commandLine.find(arguments[1]);
			std::u16string_view rest = commandLine.substr(switchAt + arguments[1].size());
			while (!rest.empty() && rest.front() == u' ')
				rest.remove_prefix(1);
			argv = {"/bin/sh", "-c", nppmac::toUtf8(rest)};
		}
		else
		{
			argv.push_back(programPath(program));
			for (size_t i = 1; i < arguments.size(); ++i)
				argv.push_back(nppmac::toUtf8(arguments[i]));
		}

		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_init(&actions);
		const int devNull = ::open("/dev/null", O_RDWR | O_CLOEXEC);
		posix_spawn_file_actions_adddup2(&actions, input >= 0 ? input : devNull, STDIN_FILENO);
		posix_spawn_file_actions_adddup2(&actions, output >= 0 ? output : devNull, STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&actions, error >= 0 ? error : devNull, STDERR_FILENO);
		std::string workingDirectory;
		if (directory && *directory)
		{
			workingDirectory = nppmac::kernel::toPosixPath(directory);
			posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str());
		}
		posix_spawnattr_t attributes;
		posix_spawnattr_init(&attributes);
		// Its own process group, so that it is not stopped with the program's terminal signals.
		posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attributes, 0);

		std::vector<char*> argPointers;
		for (std::string& argument : argv)
			argPointers.push_back(argument.data());
		argPointers.push_back(nullptr);
		std::vector<char*> envPointers;
		if (environment)
		{
			for (const std::string& variable : *environment)
				envPointers.push_back(const_cast<char*>(variable.c_str()));
			envPointers.push_back(nullptr);
		}

		pid_t pid = 0;
		const bool searchPath = argv.front().find('/') == std::string::npos;
		const int result = searchPath
			? posix_spawnp(&pid, argv.front().c_str(), &actions, &attributes, argPointers.data(), environment ? envPointers.data() : environ)
			: posix_spawn(&pid, argv.front().c_str(), &actions, &attributes, argPointers.data(), environment ? envPointers.data() : environ);
		posix_spawn_file_actions_destroy(&actions);
		posix_spawnattr_destroy(&attributes);
		if (devNull >= 0)
			::close(devNull);
		if (result != 0)
		{
			nppmac::kernel::setErrorFromErrno(result);
			return nullptr;
		}

		auto child = std::make_shared<ChildProcess>(pid);
		// The process's end, collected as it comes; waits on the handle are woken then.
		std::thread{[child]
		{
			int status = 0;
			while (::waitpid(child->_pid, &status, 0) < 0 && errno == EINTR)
				;
			std::lock_guard lock{nppmac::kernel::waitMutex()};
			child->ended(status);
			nppmac::kernel::notifyWaiters();
		}}.detach();
		return nppmac::kernel::createHandle(child);
	}

	// CreateProcess's environment block: variables, each terminated, the block terminated again.
	template <typename Char>
	std::vector<std::string> environmentBlock(const Char* block)
	{
		std::vector<std::string> variables;
		for (const Char* p = block; *p; p += std::char_traits<Char>::length(p) + 1)
		{
			if constexpr (sizeof(Char) == 1)
				variables.emplace_back(p);
			else
				variables.push_back(nppmac::toUtf8(std::u16string_view{p}));
		}
		return variables;
	}

	BOOL createProcess(std::u16string_view applicationName, std::u16string_view commandLine, const char16_t* directory,
		const std::vector<std::string>* environment, DWORD startFlags, HANDLE input, HANDLE output, HANDLE error,
		LPPROCESS_INFORMATION information)
	{
		const bool standardHandles = (startFlags & STARTF_USESTDHANDLES) != 0;
		HANDLE process = spawn(applicationName, commandLine, directory,
			standardHandles ? nppmac::kernel::fileDescriptor(input) : -1,
			standardHandles ? nppmac::kernel::fileDescriptor(output) : -1,
			standardHandles ? nppmac::kernel::fileDescriptor(error) : -1, environment);
		if (!process)
			return FALSE;
		auto child = nppmac::kernel::lookup<ChildProcess>(process);
		if (information)
		{
			information->hProcess = process;
			// There is no separate thread object: its handle is the process's, signaled at the same time.
			information->hThread = nppmac::kernel::duplicateHandle(process);
			information->dwProcessId = static_cast<DWORD>(child->_pid);
			information->dwThreadId = static_cast<DWORD>(child->_pid);
		}
		else
		{
			CloseHandle(process);
		}
		return TRUE;
	}
}

namespace nppmac::kernel
{
	bool childExitCode(HANDLE handle, DWORD& exitCode)
	{
		std::shared_ptr<ChildProcess> child = std::dynamic_pointer_cast<ChildProcess>(lookupHandle(handle));
		if (!child)
			return false;
		std::lock_guard lock{waitMutex()};
		exitCode = child->_exitCode;
		return true;
	}

	bool terminateChild(HANDLE handle, UINT)
	{
		std::shared_ptr<ChildProcess> child = std::dynamic_pointer_cast<ChildProcess>(lookupHandle(handle));
		if (!child)
			return false;
		::kill(child->_pid, SIGTERM);
		return true;
	}

	HANDLE startProcess(std::u16string_view program, std::u16string_view parameters, const char16_t* directory)
	{
		// ShellExecute's executables: the program quoted, then its parameters as given.
		std::u16string commandLine = u"\"" + std::u16string{program} + u"\"";
		if (!parameters.empty())
			commandLine += u" " + std::u16string{parameters};
		return spawn(program, commandLine, directory, -1, -1, -1, nullptr);
	}
}

BOOL WINAPI CreateProcessW(LPCWSTR applicationName, LPWSTR commandLine, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD flags,
	LPVOID environment, LPCWSTR currentDirectory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION information)
{
	std::vector<std::string> variables;
	if (environment)
	{
		variables = (flags & CREATE_UNICODE_ENVIRONMENT) ? environmentBlock(static_cast<const char16_t*>(environment))
			: environmentBlock(static_cast<const char*>(environment));
	}
	return createProcess(applicationName ? applicationName : u"", commandLine ? commandLine : u"", currentDirectory,
		environment ? &variables : nullptr, startup ? startup->dwFlags : 0,
		startup ? startup->hStdInput : nullptr, startup ? startup->hStdOutput : nullptr, startup ? startup->hStdError : nullptr, information);
}

BOOL WINAPI CreateProcessA(LPCSTR applicationName, LPSTR commandLine, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD flags,
	LPVOID environment, LPCSTR currentDirectory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION information)
{
	std::vector<std::string> variables;
	if (environment)
	{
		variables = (flags & CREATE_UNICODE_ENVIRONMENT) ? environmentBlock(static_cast<const char16_t*>(environment))
			: environmentBlock(static_cast<const char*>(environment));
	}
	const std::u16string directory = currentDirectory ? nppmac::toUtf16(currentDirectory) : std::u16string{};
	return createProcess(nppmac::toUtf16(applicationName ? applicationName : ""), nppmac::toUtf16(commandLine ? commandLine : ""),
		currentDirectory ? directory.c_str() : nullptr, environment ? &variables : nullptr, startup ? startup->dwFlags : 0,
		startup ? startup->hStdInput : nullptr, startup ? startup->hStdOutput : nullptr, startup ? startup->hStdError : nullptr, information);
}

LPWSTR* WINAPI CommandLineToArgvW(LPCWSTR commandLine, int* count)
{
	const std::vector<std::u16string> arguments = splitCommandLine(commandLine ? commandLine : u"");
	// One block, freed with LocalFree: the pointers, then the strings.
	size_t size = (arguments.size() + 1) * sizeof(LPWSTR);
	for (const std::u16string& argument : arguments)
		size += (argument.size() + 1) * sizeof(char16_t);
	auto* block = static_cast<LPWSTR*>(LocalAlloc(LMEM_FIXED, size));
	if (!block)
		return nullptr;
	auto* text = reinterpret_cast<char16_t*>(block + arguments.size() + 1);
	for (size_t i = 0; i < arguments.size(); ++i)
	{
		block[i] = text;
		std::copy_n(arguments[i].c_str(), arguments[i].size() + 1, text);
		text += arguments[i].size() + 1;
	}
	block[arguments.size()] = nullptr;
	if (count)
		*count = static_cast<int>(arguments.size());
	return block;
}

// --- Job objects ---------------------------------------------------------------------------------

using nppmac::kernel::createHandle;
using nppmac::kernel::lookup;
using nppmac::kernel::lookupHandle;

namespace
{
	// A job: the processes assigned to it, stopped together (each with its process group, so with what
	// it started) when the job is terminated, or when its last handle closes with KILL_ON_JOB_CLOSE.
	class Job : public KernelObject
	{
	public:
		~Job() override
		{
			if (_killOnClose)
				terminate();
		}

		void terminate()
		{
			std::lock_guard lock{_mutex};
			for (const pid_t pid : _processes)
				::killpg(pid, SIGKILL);
		}

		std::mutex _mutex;
		std::vector<pid_t> _processes;
		bool _killOnClose = false;
	};
}

HANDLE WINAPI CreateJobObjectW(LPSECURITY_ATTRIBUTES, LPCWSTR)
{
	return createHandle(std::make_shared<Job>());
}

BOOL WINAPI SetInformationJobObject(HANDLE handle, JOBOBJECTINFOCLASS infoClass, LPVOID info, DWORD length)
{
	std::shared_ptr<Job> job = lookup<Job>(handle);
	if (!job || !info)
		return FALSE;
	// The limits that matter here: killing the processes with the job.
	const JOBOBJECT_BASIC_LIMIT_INFORMATION* basic = nullptr;
	if (infoClass == JobObjectBasicLimitInformation && length >= sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION))
		basic = static_cast<const JOBOBJECT_BASIC_LIMIT_INFORMATION*>(info);
	else if (infoClass == JobObjectExtendedLimitInformation && length >= sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION))
		basic = &static_cast<const JOBOBJECT_EXTENDED_LIMIT_INFORMATION*>(info)->BasicLimitInformation;
	if (basic)
		job->_killOnClose = (basic->LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0;
	return TRUE;
}

BOOL WINAPI AssignProcessToJobObject(HANDLE handle, HANDLE process)
{
	std::shared_ptr<Job> job = lookup<Job>(handle);
	std::shared_ptr<ChildProcess> child = std::dynamic_pointer_cast<ChildProcess>(lookupHandle(process));
	if (!job || !child)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	std::lock_guard lock{job->_mutex};
	job->_processes.push_back(child->_pid);
	return TRUE;
}

BOOL WINAPI TerminateJobObject(HANDLE handle, UINT)
{
	std::shared_ptr<Job> job = lookup<Job>(handle);
	if (!job)
		return FALSE;
	job->terminate();
	return TRUE;
}

BOOL WINAPI IsProcessInJob(HANDLE process, HANDLE handle, PBOOL result)
{
	if (!result)
		return FALSE;
	*result = FALSE;
	// Notepad++ itself is in no job; a child is in the jobs it was assigned to.
	std::shared_ptr<ChildProcess> child = std::dynamic_pointer_cast<ChildProcess>(lookupHandle(process));
	if (child && handle)
	{
		if (std::shared_ptr<Job> job = lookup<Job>(handle))
		{
			std::lock_guard lock{job->_mutex};
			*result = std::find(job->_processes.begin(), job->_processes.end(), child->_pid) != job->_processes.end();
		}
	}
	return TRUE;
}

// --- Consoles ------------------------------------------------------------------------------------

// Ctrl+C (SIGINT) and Ctrl+Break (SIGTERM) for a process group: a child started by CreateProcess has
// its own. Group 0, the caller's console, is no one here: Notepad++ has no console.
BOOL WINAPI GenerateConsoleCtrlEvent(DWORD event, DWORD group)
{
	if (group == 0)
		return TRUE;
	return ::killpg(static_cast<pid_t>(group), event == CTRL_C_EVENT ? SIGINT : SIGTERM) == 0;
}

BOOL WINAPI SetConsoleCtrlHandler(PHANDLER_ROUTINE, BOOL)
{
	return TRUE;
}

BOOL WINAPI FreeConsole()
{
	return TRUE;
}

// --- Security descriptors: files and pipes have the user's permissions ----------------------------

BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor, DWORD revision)
{
	if (!descriptor)
		return FALSE;
	std::memset(descriptor, 0, sizeof(SECURITY_DESCRIPTOR));
	static_cast<SECURITY_DESCRIPTOR*>(descriptor)->Revision = static_cast<BYTE>(revision);
	return TRUE;
}

BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR descriptor, BOOL, PACL, BOOL)
{
	return descriptor != nullptr;
}
