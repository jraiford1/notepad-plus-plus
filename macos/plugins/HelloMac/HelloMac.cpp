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

// A plugin written as for Windows, to show and test that plugins work on macOS: its sources are
// migrated and built by nppmac_add_plugin (macos/plugins/CMakeLists.txt).

#include <windows.h>

#include "PluginInterface.h"

namespace
{
	enum Command
	{
		commandInsertGreeting,
		commandAbout,
		commandCount,
	};

	NppData g_nppData{};
	FuncItem g_commands[commandCount];

	HWND currentScintilla()
	{
		int which = -1;
		::SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
		return which == 0 ? g_nppData._scintillaMainHandle : g_nppData._scintillaSecondHandle;
	}

	void insertGreeting()
	{
		::SendMessage(currentScintilla(), SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("Hello from a plugin on macOS"));
	}

	void about()
	{
		::MessageBox(g_nppData._nppHandle, L"HelloMac shows that Notepad++ plugins work on macOS.", L"HelloMac", MB_OK);
	}

	void addCommand(Command command, const wchar_t* name, PFUNCPLUGINCMD function)
	{
		lstrcpyn(g_commands[command]._itemName, name, menuItemSize);
		g_commands[command]._pFunc = function;
	}
}

extern "C" __declspec(dllexport) void setInfo(NppData nppData)
{
	g_nppData = nppData;
	addCommand(commandInsertGreeting, L"Insert greeting", insertGreeting);
	addCommand(commandAbout, L"About HelloMac", about);
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
	return L"HelloMac";
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
	*count = commandCount;
	return g_commands;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification*)
{
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
	return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
	return TRUE;
}
