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

// The macOS and Linux stand-in for WinGUp, Notepad++'s updater, as far as Plugins Admin uses it:
// Notepad++ runs it as it quits (updater/gup.exe), with WinGUp's command line:
//
//     gup.exe -unzipTo [-clean] "<notepad++.exe>" "<plugins folder>" "<folder> <package> <sha-256>"...
//     gup.exe -clean "<notepad++.exe>" "<plugins folder>" "<folder>"...
//     gup.exe -moveFolder "<notepad++.exe>" "<from folder>" "<to folder>" "<folder>"...
//
// It waits for Notepad++ to end, removes, installs (each package checked against its SHA-256 before it
// is unzipped) or moves the plugin folders, then starts Notepad++ again. A package is a URL, or a path
// relative to the program folder (the plugins the app carries, in plugins-catalog/).
//
// On Linux a plugins folder that is not the user's (a system-wide install's, as Program Files is on
// Windows, where WinGUp runs as an administrator) is changed by gup run again through pkexec, the
// desktop's administrator prompt, with -elevated before the rest: it only changes the folders. Notepad++
// starts again as the user.

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <libproc.h>
#else
#include <climits>
#include <dirent.h>
#endif
#include <cerrno>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace
{
	enum class Operation
	{
		install,
		remove,
		move,
	};

	// Runs a program and waits for it; true when it exits with 0.
	bool runProgram(const std::vector<std::string>& arguments)
	{
		std::vector<char*> argv;
		for (const std::string& argument : arguments)
			argv.push_back(const_cast<char*>(argument.c_str()));
		argv.push_back(nullptr);
		pid_t pid = 0;
		if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
			return false;
		int status = 0;
		while (waitpid(pid, &status, 0) < 0)
		{
			if (errno != EINTR)
				return false;
		}
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}

	std::string runForOutput(const std::vector<std::string>& arguments)
	{
		std::string command;
		for (const std::string& argument : arguments)
		{
			std::string quoted = "'";
			for (const char c : argument)
				quoted += c == '\'' ? std::string{"'\\''"} : std::string(1, c);
			command += quoted + "' ";
		}
		command += "2>&1";
		std::string output;
		if (FILE* pipe = ::popen(command.c_str(), "r"))
		{
			char buffer[512];
			while (size_t n = std::fread(buffer, 1, sizeof(buffer), pipe))
				output.append(buffer, n);
			::pclose(pipe);
		}
		return output;
	}

	// A test run (NPPMAC_TEST_RUN=1) shows nothing and starts nothing: it reports on stderr only.
	bool testRun()
	{
		return std::getenv("NPPMAC_TEST_RUN") != nullptr;
	}

	void report(const std::string& message)
	{
		std::fprintf(stderr, "gup: %s\n", message.c_str());
		if (testRun())
			return;
		// The user sees it too: an alert, as WinGUp shows its errors (gup run through pkexec has no
		// desktop to show it on: the gup that ran it shows what it says).
#ifdef __APPLE__
		runProgram({"/usr/bin/osascript", "-e", "display alert \"Notepad++ plugins\" message \"" + message + "\" as warning"});
#else
		if (std::getenv("PKEXEC_UID"))
			return;
		runProgram({"zenity", "--warning", "--title=Notepad++ plugins", "--no-markup", "--text=" + message});
#endif
	}

	// What holds the program: the app bundle whose Resources folder holds notepad++.exe on the Mac, the
	// program folder on Linux.
	fs::path bundleOf(const fs::path& notepadPath)
	{
#ifdef __APPLE__
		return notepadPath.parent_path().parent_path().parent_path();
#else
		std::error_code error;
		const fs::path folder = fs::canonical(notepadPath.parent_path(), error);
		return error ? notepadPath.parent_path() : folder;
#endif
	}

	// Waits (up to a minute) until no process runs the program's executable.
	void waitForNotepadToEnd(const fs::path& bundle)
	{
#ifdef __APPLE__
		const std::string executables = (bundle / "Contents" / "MacOS").string() + "/";
#else
		const std::string executables = bundle.string() + "/";
#endif
		for (int attempt = 0; attempt < 600; ++attempt)
		{
			bool running = false;
#ifdef __APPLE__
			std::vector<pid_t> pids(4096);
			const int bytes = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
			for (int i = 0; i < bytes / static_cast<int>(sizeof(pid_t)) && !running; ++i)
			{
				char path[PROC_PIDPATHINFO_MAXSIZE]{};
				if (pids[static_cast<size_t>(i)] != getpid() && proc_pidpath(pids[static_cast<size_t>(i)], path, sizeof(path)) > 0)
					running = std::string{path}.compare(0, executables.size(), executables) == 0;
			}
#else
			if (DIR* processes = ::opendir("/proc"))
			{
				while (const dirent* entry = ::readdir(processes))
				{
					const std::string name = entry->d_name;
					if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos || std::stol(name) == getpid())
						continue;
					char path[PATH_MAX]{};
					const ssize_t length = ::readlink(("/proc/" + name + "/exe").c_str(), path, sizeof(path) - 1);
					if (length > 0 && std::string{path, static_cast<size_t>(length)}.compare(0, executables.size(), executables) == 0)
					{
						running = true;
						break;
					}
				}
				::closedir(processes);
			}
#endif
			if (!running)
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds{100});
		}
	}

	std::string sha256Of(const fs::path& file)
	{
#ifdef __APPLE__
		std::ifstream in{file, std::ios::binary};
		const std::string bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
		unsigned char digest[CC_SHA256_DIGEST_LENGTH]{};
		CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
		std::string hex;
		char pair[3]{};
		for (const unsigned char byte : digest)
		{
			std::snprintf(pair, sizeof(pair), "%02x", byte);
			hex += pair;
		}
		return hex;
#else
		const std::string output = runForOutput({"sha256sum", "--", file.string()});
		return output.size() >= 64 ? output.substr(0, 64) : std::string{};
#endif
	}

	std::string lowercase(std::string text)
	{
		for (char& c : text)
		{
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return text;
	}

	// Gets a package into a temporary file: downloaded, or copied from the program folder.
	bool fetchPackage(const std::string& package, const fs::path& programFolder, const fs::path& target)
	{
		if (package.compare(0, 8, "https://") == 0 || package.compare(0, 7, "http://") == 0)
		{
#ifdef __APPLE__
			return runProgram({"/usr/bin/curl", "-fsSL", "-o", target.string(), package});
#else
			return runProgram({"curl", "-fsSL", "-o", target.string(), package}) || runProgram({"wget", "-q", "-O", target.string(), package});
#endif
		}
		std::error_code error;
		fs::copy_file(programFolder / package, target, fs::copy_options::overwrite_existing, error);
		return !error;
	}

	bool install(const fs::path& root, const std::string& folder, const std::string& package, const std::string& sha256,
		const fs::path& programFolder, const fs::path& work)
	{
		const fs::path zip = work / (folder + ".zip");
		if (!fetchPackage(package, programFolder, zip))
		{
			report("Cannot get the package of " + folder + ".");
			return false;
		}
		if (lowercase(sha256Of(zip)) != lowercase(sha256))
		{
			report("The package of " + folder + " is not the one expected (its SHA-256 differs); it is not installed.");
			return false;
		}
		std::error_code error;
		fs::create_directories(root / folder, error);
#ifdef __APPLE__
		const bool unzipped = runProgram({"/usr/bin/ditto", "-x", "-k", zip.string(), (root / folder).string()});
#else
		const bool unzipped = runProgram({"unzip", "-o", "-q", zip.string(), "-d", (root / folder).string()});
#endif
		if (!unzipped)
		{
			report("Cannot unzip the package of " + folder + ".");
			return false;
		}
		return true;
	}

	// A bundle whose resources are sealed by an ad-hoc signature is signed again, as its plugins changed.
	void resealBundle([[maybe_unused]] const fs::path& bundle)
	{
#ifdef __APPLE__
		const std::string details = runForOutput({"/usr/bin/codesign", "-dv", bundle.string()});
		if (details.find("Signature=adhoc") != std::string::npos && details.find("Sealed Resources version") != std::string::npos)
			runProgram({"/usr/bin/codesign", "--force", "--deep", "--sign", "-", bundle.string()});
#endif
	}

#ifdef __APPLE__
	// On the Mac the plugins are not in the app (W73): what Notepad++ names <program folder>/plugins is
	// Documents/Notepad++/plugins, as for Notepad++ itself (redirectPlugins, in the runtime's Startup.cpp),
	// or NPPMAC_TEST_PLUGINS_FOLDER; test runs without it keep them in the app.
	std::string pluginsFolder(const std::string& path, const fs::path& programFolder)
	{
		const char* testFolder = std::getenv("NPPMAC_TEST_PLUGINS_FOLDER");
		const bool inTestFolder = testFolder && *testFolder;
		const char* home = std::getenv("HOME");
		if (!inTestFolder && (testRun() || !home || !*home))
			return path;
		const std::string plugins = (programFolder / "plugins").string();
		if (!path.starts_with(plugins) || (path.size() > plugins.size() && path[plugins.size()] != '/'))
			return path;
		const std::string folder = inTestFolder ? std::string{testFolder} : std::string{home} + "/Documents/Notepad++/plugins";
		return folder + path.substr(plugins.size());
	}
#endif

	int usage()
	{
		std::fprintf(stderr, "usage: gup.exe -unzipTo [-clean] | -clean | -moveFolder <notepad++.exe> <folders and plugins>...\n");
		return 2;
	}

#ifndef __APPLE__
	// Whether this user may change the folder (or make it, in a folder this user may change).
	bool mayChange(fs::path folder)
	{
		std::error_code error;
		while (!folder.empty() && !fs::exists(folder, error))
			folder = folder.parent_path();
		return !folder.empty() && ::access(folder.c_str(), W_OK) == 0;
	}

	// Runs this gup again through pkexec, as an administrator, for the folders' changes: its report is
	// shown here, where there is a desktop to show it on.
	void changeAsAdministrator(const std::vector<std::string>& arguments)
	{
		char self[PATH_MAX]{};
		const ssize_t length = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
		if (length <= 0)
			return;
		std::vector<std::string> command{"pkexec", std::string{self, static_cast<size_t>(length)}, "-elevated"};
		command.insert(command.end(), arguments.begin(), arguments.end());
		const std::string output = runForOutput(command);
		std::fprintf(stderr, "%s", output.c_str());
		size_t start = 0;
		while ((start = output.find("gup: ", start)) != std::string::npos)
		{
			const size_t end = output.find('\n', start);
			report(output.substr(start + 5, end == std::string::npos ? std::string::npos : end - start - 5));
			start = end == std::string::npos ? output.size() : end;
		}
	}

	// Starts the program again, without waiting for it.
	void startAgain(const fs::path& programFolder)
	{
		const std::string program = (programFolder / "notepad++").string();
		char* argv[] = {const_cast<char*>(program.c_str()), nullptr};
		posix_spawnattr_t attributes;
		posix_spawnattr_init(&attributes);
		posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
		pid_t pid = 0;
		posix_spawn(&pid, program.c_str(), nullptr, &attributes, argv, environ);
		posix_spawnattr_destroy(&attributes);
	}
#endif
}

int main(int argc, char* argv[])
{
	std::vector<std::string> arguments{argv + 1, argv + argc};
	// Run again as an administrator (Linux): change the folders, nothing more.
	const bool elevated = !arguments.empty() && arguments.front() == "-elevated";
	if (elevated)
		arguments.erase(arguments.begin());
	if (arguments.empty())
		return usage();
	// Notepad++ joins paths with '\', as on Windows: its plugins folder is "<program folder>\plugins". A '\'
	// is a separator here too, as for the runtime's file functions; it was taken as part of a name, and the
	// plugins went to a new folder named "...\plugins" beside the program's files.
	for (std::string& argument : arguments)
		std::replace(argument.begin(), argument.end(), '\\', '/');
	const std::vector<std::string> allArguments = arguments;

	Operation operation = Operation::remove;
	bool clean = false;
	size_t next = 0;
	if (arguments[next] == "-unzipTo")
	{
		operation = Operation::install;
		++next;
		if (next < arguments.size() && arguments[next] == "-clean")
		{
			clean = true;
			++next;
		}
	}
	else if (arguments[next] == "-clean")
	{
		operation = Operation::remove;
		clean = true;
		++next;
	}
	else if (arguments[next] == "-moveFolder")
	{
		operation = Operation::move;
		++next;
	}
	else
	{
		return usage();
	}
	if (next + 2 > arguments.size())
		return usage();

	const fs::path notepadPath = arguments[next++];
	const fs::path programFolder = notepadPath.parent_path();
	const fs::path bundle = bundleOf(notepadPath);
#ifdef __APPLE__
	for (size_t i = next; i < arguments.size(); ++i)
		arguments[i] = pluginsFolder(arguments[i], programFolder);
#endif
	if (!elevated)
		waitForNotepadToEnd(bundle);
#ifndef __APPLE__
	// A folder that is not the user's: its changes are made as an administrator; Notepad++ starts again
	// as the user.
	const fs::path changedFolder = operation == Operation::move ? fs::path{next + 1 < arguments.size() ? arguments[next + 1] : ""} : fs::path{arguments[next]};
	if (!elevated && geteuid() != 0 && !mayChange(changedFolder))
	{
		changeAsAdministrator(allArguments);
		if (!testRun())
			startAgain(programFolder);
		return 0;
	}
#endif

	bool changed = false;
	if (operation == Operation::move)
	{
		if (next + 1 > arguments.size())
			return usage();
		const fs::path from = arguments[next++];
		const fs::path to = arguments[next++];
		std::error_code error;
		fs::create_directories(to, error);
		for (; next < arguments.size(); ++next)
		{
			fs::remove_all(to / arguments[next], error);
			fs::rename(from / arguments[next], to / arguments[next], error);
			if (error)
				report("Cannot move the plugin " + arguments[next] + ".");
			changed = true;
		}
	}
	else
	{
		const fs::path root = arguments[next++];
		char workTemplate[] = "/tmp/npp-gup.XXXXXX";
		const fs::path work = ::mkdtemp(workTemplate);
		for (; next < arguments.size(); ++next)
		{
			// "<folder>", or "<folder> <package> <sha-256>" to install.
			std::string folder = arguments[next];
			std::string package;
			std::string sha256;
			if (operation == Operation::install)
			{
				const size_t last = folder.find_last_of(' ');
				const size_t middle = last == std::string::npos ? std::string::npos : folder.find_last_of(' ', last - 1);
				if (middle == std::string::npos)
					continue;
				sha256 = folder.substr(last + 1);
				package = folder.substr(middle + 1, last - middle - 1);
				folder = folder.substr(0, middle);
			}
			std::error_code error;
			if (clean)
				fs::remove_all(root / folder, error);
			if (operation == Operation::install)
				install(root, folder, package, sha256, programFolder, work);
			changed = true;
		}
		std::error_code error;
		fs::remove_all(work, error);
	}

	if (changed)
		resealBundle(bundle);
	// Notepad++ again, as WinGUp restarts it (by the gup the user ran, when this one is an administrator's).
	if (!testRun() && !elevated)
	{
#ifdef __APPLE__
		runProgram({"/usr/bin/open", bundle.string()});
#else
		startAgain(programFolder);
#endif
	}
	return 0;
}
