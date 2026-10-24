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

	// Brings the program to the front, as a window it shows or a modal panel needs. A test run
	// (NPPMAC_TEST_RUN=1) never does: it must not take the keyboard from whoever uses the Mac.
	void activateApplication();

	// Whether the system appearance is dark.
	bool isDarkAppearance();

	// The accent color chosen in System Settings > Appearance, as 0xRRGGBB.
	unsigned accentColor();

	// The macOS version ("26.0.1") and build ("25A362").
	std::string systemVersion();
	std::string systemBuild();

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

	// The general pasteboard. Types are UTI strings; "public.utf8-plain-text" is text.
	long pasteboardChangeCount();
	std::vector<std::string> pasteboardTypes();
	std::vector<unsigned char> pasteboardData(const std::string& type);
	void setPasteboard(const std::vector<std::pair<std::string, std::vector<unsigned char>>>& items);

	enum class AlertStyle
	{
		information,
		warning,
		critical,
	};

	// The Open and Save panels. Paths are UTF-8 POSIX paths.
	struct FilePanelCheckBox
	{
		int _id = 0;
		std::string _label;
		bool _checked = false;
		bool _enabled = true;
	};

	struct FilePanelRequest
	{
		bool _save = false;
		bool _multipleSelection = false;
		bool _pickFolders = false;
		std::string _title;
		std::string _directory;
		std::string _fileName;
		std::vector<std::string> _fileTypeNames; // shown in the Save panel's file type menu
		int _fileTypeIndex = 0;
		std::vector<FilePanelCheckBox> _checkBoxes;
	};

	struct FilePanelResult
	{
		bool _ok = false;
		std::vector<std::string> _paths;
		int _fileTypeIndex = 0;
		std::vector<FilePanelCheckBox> _checkBoxes; // with their final states
	};

	// Runs the panel modally. NPPMAC_TEST_FILE_PANEL=<path> answers with that path instead, for tests.
	FilePanelResult runFilePanel(const FilePanelRequest& request);

	// The color panel, run modally with OK and Cancel. rgb is 0xRRGGBB in and out; returns false when
	// cancelled. NPPMAC_TEST_COLOR=RRGGBB answers without showing it, for tests.
	bool chooseColor(unsigned& rgb);

	// Printing: the paper and its printable area in points (top-left origin), and what to print.
	struct PrintSetup
	{
		double _paperWidth = 612;
		double _paperHeight = 792;
		double _printableLeft = 18;
		double _printableTop = 18;
		double _printableWidth = 576;
		double _printableHeight = 756;
		bool _offerSelection = false; // in: there is a selection to print
		bool _selectionOnly = false;  // out: the user chose to print it
		bool _pageRange = false;      // out: only pages _firstPage to _lastPage (from 1)
		int _firstPage = 1;
		int _lastPage = 1;
	};

	// Runs the print panel (showPanel), or takes the current printer's settings without it; false
	// when cancelled. The choice is kept for printPdf. Copies are the printer's business, as with
	// PD_USEDEVMODECOPIES. NPPMAC_TEST_PRINT_PDF=<path> answers without the panel, for tests.
	bool choosePrinter(PrintSetup& setup, bool showPanel);
	// Prints every page of a PDF with the settings choosePrinter kept, without another panel; with
	// NPPMAC_TEST_PRINT_PDF it is copied there instead.
	bool printPdf(const std::string& pdfPath, const std::string& jobName);

	// Shows a modal alert; returns the index of the chosen button (buttons are listed left to right
	// as on Windows; on the Mac the first is the rightmost). escapeButton is chosen by Escape.
	int showAlert(const std::string& title, const std::string& message, const std::vector<std::string>& buttons,
		AlertStyle style, int defaultButton, int escapeButton);
}
