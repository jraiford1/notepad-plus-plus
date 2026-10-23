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

// user32 MessageBox on NSAlert.

#include <windows.h>

#include <string>
#include <vector>

#include "platform/Platform.h"

namespace
{
	struct ButtonSet
	{
		std::vector<std::string> _labels;
		std::vector<int> _results;
		int _escape = 0;
	};

	ButtonSet buttonsFor(UINT type)
	{
		switch (type & MB_TYPEMASK)
		{
			case MB_OKCANCEL:
				return {{"OK", "Cancel"}, {IDOK, IDCANCEL}, 1};
			case MB_ABORTRETRYIGNORE:
				return {{"Abort", "Retry", "Ignore"}, {IDABORT, IDRETRY, IDIGNORE}, 0};
			case MB_YESNOCANCEL:
				return {{"Yes", "No", "Cancel"}, {IDYES, IDNO, IDCANCEL}, 2};
			case MB_YESNO:
				return {{"Yes", "No"}, {IDYES, IDNO}, 1};
			case MB_RETRYCANCEL:
				return {{"Retry", "Cancel"}, {IDRETRY, IDCANCEL}, 1};
			case MB_CANCELTRYCONTINUE:
				return {{"Cancel", "Try Again", "Continue"}, {IDCANCEL, IDTRYAGAIN, IDCONTINUE}, 0};
			case MB_OK:
			default:
				return {{"OK"}, {IDOK}, 0};
		}
	}
}

int WINAPI MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type)
{
	const ButtonSet buttons = buttonsFor(type);
	int defaultButton = static_cast<int>((type & MB_DEFMASK) >> 8);
	if (defaultButton >= static_cast<int>(buttons._labels.size()))
		defaultButton = 0;

	nppmac::platform::AlertStyle style = nppmac::platform::AlertStyle::information;
	switch (type & MB_ICONMASK)
	{
		case MB_ICONHAND:
			style = nppmac::platform::AlertStyle::critical;
			break;
		case MB_ICONEXCLAMATION:
			style = nppmac::platform::AlertStyle::warning;
			break;
		default:
			break;
	}

	const std::string title = nppmac::toUtf8(caption ? caption : u"Error");
	const std::string message = nppmac::toUtf8(text ? text : u"");
	if (std::getenv("NPPMAC_TRACE_MESSAGEBOX"))
		std::fprintf(stderr, "nppmac: MessageBox \"%s\": %s\n", title.c_str(), message.c_str());
	const int chosen = nppmac::platform::showAlert(title, message, buttons._labels, style, defaultButton, buttons._escape);
	return buttons._results[static_cast<size_t>(chosen)];
}

int WINAPI MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type)
{
	const std::u16string wideText = nppmac::toUtf16(text ? text : "");
	const std::u16string wideCaption = nppmac::toUtf16(caption ? caption : "");
	return MessageBoxW(window, wideText.c_str(), caption ? wideCaption.c_str() : nullptr, type);
}

int WINAPI MessageBoxExW(HWND window, LPCWSTR text, LPCWSTR caption, UINT type, WORD)
{
	return MessageBoxW(window, text, caption, type);
}

BOOL WINAPI MessageBeep(UINT)
{
	nppmac::platform::beep();
	return TRUE;
}
