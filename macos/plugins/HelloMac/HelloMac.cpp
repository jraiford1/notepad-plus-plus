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
#include "resource.h"

#include <string>

namespace
{
	enum Command
	{
		commandInsertGreeting,
		commandAbout,
		commandInsertResourceGreeting,
		commandCount,
	};

	HMODULE g_module = nullptr;
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

	// A string from the plugin's own resources, and the name of its folder: its module handle (from
	// DllMain) at work.
	void insertResourceGreeting()
	{
		wchar_t greeting[100]{};
		::LoadString(g_module, IDS_GREETING, greeting, 100);
		wchar_t path[MAX_PATH]{};
		::GetModuleFileName(g_module, path, MAX_PATH);
		std::wstring folder = path;
		folder = folder.substr(0, folder.find_last_of(L"\\/"));
		folder = folder.substr(folder.find_last_of(L"\\/") + 1);
		const std::wstring text = std::wstring{greeting} + L" in " + folder;
		char utf8[300]{};
		::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8, 300, nullptr, nullptr);
		::SendMessage(currentScintilla(), SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(utf8));
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

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
		g_module = static_cast<HMODULE>(module);
	return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData nppData)
{
	g_nppData = nppData;
	addCommand(commandInsertGreeting, L"Insert greeting", insertGreeting);
	addCommand(commandAbout, L"About HelloMac", about);
	addCommand(commandInsertResourceGreeting, L"Insert greeting from resources", insertResourceGreeting);
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
