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

// The bridge between the Win32 side of the runtime (.cpp, which includes <windows.h>) and macOS
// services that need AppKit or Foundation (.mm, which must not include <windows.h>). Plain C++
// types only.

#pragma once

#include <string>
#include <vector>

namespace nppmac::platform
{
	// The first preferred language, as a BCP 47 tag ("en-US", "ja-JP", "zh-Hans-CN").
	std::string preferredLanguage();

	// Whether the system appearance is dark.
	bool isDarkAppearance();

	// UTF-8 POSIX paths.
	std::string executablePath();
	std::string resourcesDirectory();     // Contents/Resources: the program directory Notepad++ sees
	std::string applicationSupportDirectory();
	std::string documentsDirectory();
	std::string desktopDirectory();
	std::string homeDirectory();
	std::string userName();
	std::string computerName();

	// Opens a URL or a file with its default application. Returns false on failure.
	bool openUrlOrFile(const std::string& urlOrPath, const std::string& applicationPath = {});
	bool revealInFinder(const std::vector<std::string>& paths);
	bool moveToTrash(const std::string& path);
	void beep();

	enum class AlertStyle
	{
		information,
		warning,
		critical,
	};

	// Shows a modal alert; returns the index of the chosen button (buttons are listed left to right
	// as on Windows; on the Mac the first is the rightmost). escapeButton is chosen by Escape.
	int showAlert(const std::string& title, const std::string& message, const std::vector<std::string>& buttons,
		AlertStyle style, int defaultButton, int escapeButton);
}
