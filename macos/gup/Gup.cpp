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

// The macOS stand-in for WinGUp, Notepad++'s updater, as far as Plugins Admin uses it: Notepad++ runs
// it as it quits (updater/gup.exe), with WinGUp's command line:
//
//     gup.exe -unzipTo [-clean] "<notepad++.exe>" "<plugins folder>" "<folder> <package> <sha-256>"...
//     gup.exe -clean "<notepad++.exe>" "<plugins folder>" "<folder>"...
//     gup.exe -moveFolder "<notepad++.exe>" "<from folder>" "<to folder>" "<folder>"...
//
// It waits for Notepad++ to end, removes, installs (each package checked against its SHA-256 before it
// is unzipped) or moves the plugin folders, then starts Notepad++ again. A package is a URL, or a path
// relative to the program folder (the plugins the app carries, in plugins-catalog/).

#include <CommonCrypto/CommonDigest.h>
#include <libproc.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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
		if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
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
		// The user sees it too: an alert, as WinGUp shows its errors.
		runProgram({"/usr/bin/osascript", "-e", "display alert \"Notepad++ plugins\" message \"" + message + "\" as warning"});
	}

	// The app bundle whose Resources folder holds notepad++.exe.
	fs::path bundleOf(const fs::path& notepadPath)
	{
		return notepadPath.parent_path().parent_path().parent_path();
	}

	// Waits (up to a minute) until no process runs the bundle's executable.
	void waitForNotepadToEnd(const fs::path& bundle)
	{
		const std::string executables = (bundle / "Contents" / "MacOS").string() + "/";
		for (int attempt = 0; attempt < 600; ++attempt)
		{
			std::vector<pid_t> pids(4096);
			const int bytes = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
			bool running = false;
			for (int i = 0; i < bytes / static_cast<int>(sizeof(pid_t)) && !running; ++i)
			{
				char path[PROC_PIDPATHINFO_MAXSIZE]{};
				if (pids[static_cast<size_t>(i)] != getpid() && proc_pidpath(pids[static_cast<size_t>(i)], path, sizeof(path)) > 0)
					running = std::string{path}.compare(0, executables.size(), executables) == 0;
			}
			if (!running)
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds{100});
		}
	}

	std::string sha256Of(const fs::path& file)
	{
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
			return runProgram({"/usr/bin/curl", "-fsSL", "-o", target.string(), package});
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
		if (!runProgram({"/usr/bin/ditto", "-x", "-k", zip.string(), (root / folder).string()}))
		{
			report("Cannot unzip the package of " + folder + ".");
			return false;
		}
		return true;
	}

	// A bundle whose resources are sealed by an ad-hoc signature is signed again, as its plugins changed.
	void resealBundle(const fs::path& bundle)
	{
		const std::string details = runForOutput({"/usr/bin/codesign", "-dv", bundle.string()});
		if (details.find("Signature=adhoc") != std::string::npos && details.find("Sealed Resources version") != std::string::npos)
			runProgram({"/usr/bin/codesign", "--force", "--deep", "--sign", "-", bundle.string()});
	}

	int usage()
	{
		std::fprintf(stderr, "usage: gup.exe -unzipTo [-clean] | -clean | -moveFolder <notepad++.exe> <folders and plugins>...\n");
		return 2;
	}
}

int main(int argc, char* argv[])
{
	std::vector<std::string> arguments{argv + 1, argv + argc};
	if (arguments.empty())
		return usage();

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
	waitForNotepadToEnd(bundle);

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
	// Notepad++ again, as WinGUp restarts it.
	if (!testRun())
		runProgram({"/usr/bin/open", bundle.string()});
	return 0;
}
