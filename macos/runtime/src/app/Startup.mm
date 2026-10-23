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

#import <Cocoa/Cocoa.h>

#include <cstring>
#include <string>
#include <vector>

#include "nppmac/Startup.h"

namespace
{
	std::u16string g_commandLine;
	std::u16string g_commandLineArguments;
	std::vector<std::u16string> g_wideArguments;
	std::vector<char16_t*> g_wideArgv;

	// Quotes an argument the way CommandLineToArgvW parses it back.
	std::u16string quoteArgument(const std::u16string& argument)
	{
		if (!argument.empty() && argument.find_first_of(u" \t\"") == std::u16string::npos)
			return argument;

		std::u16string quoted = u"\"";
		size_t backslashes = 0;
		for (const char16_t ch : argument)
		{
			if (ch == u'\\')
			{
				++backslashes;
				continue;
			}
			if (ch == u'"')
				quoted.append(backslashes * 2 + 1, u'\\');
			else
				quoted.append(backslashes, u'\\');
			backslashes = 0;
			quoted.push_back(ch);
		}
		quoted.append(backslashes * 2, u'\\');
		quoted.push_back(u'"');
		return quoted;
	}
}

namespace nppmac
{
	void startup(int argc, char* argv[])
	{
		__argc = argc;
		__argv = argv;

		for (int i = 0; i < argc; ++i)
		{
			// Finder adds -psn_... when it launches an app; it is not an argument of the program.
			if (i > 0 && std::strncmp(argv[i], "-psn_", 5) == 0)
				continue;
			g_wideArguments.push_back(toUtf16(argv[i]));
		}
		for (std::u16string& argument : g_wideArguments)
			g_wideArgv.push_back(argument.data());
		g_wideArgv.push_back(nullptr);
		__wargv = g_wideArgv.data();
		__argc = static_cast<int>(g_wideArguments.size());

		for (size_t i = 0; i < g_wideArguments.size(); ++i)
		{
			const std::u16string quoted = quoteArgument(g_wideArguments[i]);
			if (i > 0)
			{
				g_commandLine += u' ';
				if (!g_commandLineArguments.empty())
					g_commandLineArguments += u' ';
				g_commandLineArguments += quoted;
			}
			g_commandLine += quoted;
		}

		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];
		[NSApp activateIgnoringOtherApps:YES];
	}

	char16_t* commandLineArguments()
	{
		return g_commandLineArguments.data();
	}

	const char16_t* commandLine()
	{
		return g_commandLine.c_str();
	}
}
