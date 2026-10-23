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

// user32 clipboard over the macOS general pasteboard. Text formats map to plain text; any other
// format keeps its bytes under a private pasteboard type named after the format.

#include "Window.h"
#include "platform/Platform.h"

#include <map>
#include <unordered_set>

namespace nppmac::user
{
	namespace
	{
		constexpr const char* textType = "public.utf8-plain-text";
		constexpr const char* privatePrefix = "io.github.notepad-plus-plus-macos.format.";

		struct ClipboardState
		{
			bool _open = false;
			HWND _owner = nullptr;
			bool _emptied = false;
			// Data set while the clipboard is open, written to the pasteboard when it closes.
			std::map<UINT, HANDLE> _pending;
			// Data read while open: handles stay valid until the clipboard closes.
			std::map<UINT, HANDLE> _read;
			std::unordered_set<HWND> _listeners;
			long _lastChange = -1;
			void* _timer = nullptr;
		};

		ClipboardState& state()
		{
			static auto* clipboard = new ClipboardState;
			return *clipboard;
		}

		std::map<UINT, std::u16string>& registeredFormats()
		{
			static auto* map = new std::map<UINT, std::u16string>;
			return *map;
		}

		UINT g_nextFormat = 0xC000;

		std::string pasteboardType(UINT format)
		{
			if (format == CF_UNICODETEXT || format == CF_TEXT || format == CF_OEMTEXT)
				return textType;
			auto it = registeredFormats().find(format);
			const std::string name = it != registeredFormats().end() ? toUtf8(it->second) : std::to_string(format);
			std::string type = privatePrefix;
			for (const char ch : name)
				type.push_back(std::isalnum(static_cast<unsigned char>(ch)) ? ch : '-');
			return type;
		}

		HANDLE globalFrom(const void* data, size_t size)
		{
			HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size);
			std::memcpy(GlobalLock(handle), data, size);
			GlobalUnlock(handle);
			return handle;
		}

		void pollPasteboard(void*, uintptr_t)
		{
			ClipboardState& clipboard = state();
			const long change = platform::pasteboardChangeCount();
			if (change == clipboard._lastChange)
				return;
			clipboard._lastChange = change;
			const std::unordered_set<HWND> listeners = clipboard._listeners;
			for (HWND listener : listeners)
			{
				if (IsWindow(listener))
					PostMessageW(listener, WM_CLIPBOARDUPDATE, 0, 0);
			}
		}
	}
}

using namespace nppmac::user;

UINT WINAPI RegisterClipboardFormatW(LPCWSTR name)
{
	for (const auto& [format, formatName] : registeredFormats())
	{
		if (_wcsicmp(formatName.c_str(), name) == 0)
			return format;
	}
	const UINT format = g_nextFormat++;
	registeredFormats()[format] = name;
	return format;
}

int WINAPI GetClipboardFormatNameW(UINT format, LPWSTR name, int size)
{
	auto it = registeredFormats().find(format);
	if (it == registeredFormats().end() || size <= 0)
		return 0;
	lstrcpynW(name, it->second.c_str(), size);
	return lstrlenW(name);
}

BOOL WINAPI OpenClipboard(HWND owner)
{
	ClipboardState& clipboard = state();
	if (clipboard._open)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}
	clipboard._open = true;
	clipboard._owner = owner;
	clipboard._emptied = false;
	return TRUE;
}

BOOL WINAPI CloseClipboard()
{
	ClipboardState& clipboard = state();
	if (!clipboard._open)
		return FALSE;
	if (clipboard._emptied || !clipboard._pending.empty())
	{
		std::vector<std::pair<std::string, std::vector<unsigned char>>> items;
		for (const auto& [format, handle] : clipboard._pending)
		{
			const auto* data = static_cast<const BYTE*>(GlobalLock(handle));
			const size_t size = GlobalSize(handle);
			std::vector<unsigned char> bytes;
			if (format == CF_UNICODETEXT)
			{
				const std::string text = nppmac::toUtf8(std::u16string_view{reinterpret_cast<const char16_t*>(data), wcsnlen(reinterpret_cast<const char16_t*>(data), size / 2)});
				bytes.assign(text.begin(), text.end());
			}
			else if (format == CF_TEXT || format == CF_OEMTEXT)
			{
				bytes.assign(data, data + strnlen(reinterpret_cast<const char*>(data), size));
			}
			else
			{
				bytes.assign(data, data + size);
			}
			GlobalUnlock(handle);
			// Unicode text wins over the narrow kinds when both are given.
			const std::string type = pasteboardType(format);
			auto existing = std::find_if(items.begin(), items.end(), [&type](const auto& item) { return item.first == type; });
			if (existing == items.end())
				items.emplace_back(type, std::move(bytes));
			else if (format == CF_UNICODETEXT)
				existing->second = std::move(bytes);
			GlobalFree(handle);
		}
		nppmac::platform::setPasteboard(items);
		clipboard._pending.clear();
	}
	for (const auto& [format, handle] : clipboard._read)
		GlobalFree(handle);
	clipboard._read.clear();
	clipboard._open = false;
	return TRUE;
}

BOOL WINAPI EmptyClipboard()
{
	ClipboardState& clipboard = state();
	if (!clipboard._open)
		return FALSE;
	for (const auto& [format, handle] : clipboard._pending)
		GlobalFree(handle);
	clipboard._pending.clear();
	clipboard._emptied = true;
	return TRUE;
}

HANDLE WINAPI SetClipboardData(UINT format, HANDLE data)
{
	ClipboardState& clipboard = state();
	if (!clipboard._open || !data)
		return nullptr;
	if (auto it = clipboard._pending.find(format); it != clipboard._pending.end())
		GlobalFree(it->second);
	clipboard._pending[format] = data;
	return data;
}

HANDLE WINAPI GetClipboardData(UINT format)
{
	ClipboardState& clipboard = state();
	if (!clipboard._open)
		return nullptr;
	if (auto it = clipboard._read.find(format); it != clipboard._read.end())
		return it->second;
	const std::vector<unsigned char> bytes = nppmac::platform::pasteboardData(pasteboardType(format));
	if (bytes.empty() && !IsClipboardFormatAvailable(format))
		return nullptr;
	HANDLE handle = nullptr;
	if (format == CF_UNICODETEXT)
	{
		const std::u16string text = nppmac::toUtf16(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
		handle = globalFrom(text.c_str(), (text.size() + 1) * sizeof(char16_t));
	}
	else if (format == CF_TEXT || format == CF_OEMTEXT)
	{
		std::string text{bytes.begin(), bytes.end()};
		handle = globalFrom(text.c_str(), text.size() + 1);
	}
	else
	{
		handle = globalFrom(bytes.data(), bytes.size());
	}
	clipboard._read[format] = handle;
	return handle;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT format)
{
	const std::string wanted = pasteboardType(format);
	for (const std::string& type : nppmac::platform::pasteboardTypes())
	{
		if (type == wanted || (wanted == textType && (type == "NSStringPboardType" || type == "public.plain-text")))
			return TRUE;
	}
	return FALSE;
}

int WINAPI CountClipboardFormats()
{
	return static_cast<int>(nppmac::platform::pasteboardTypes().size());
}

UINT WINAPI EnumClipboardFormats(UINT format)
{
	// Text is the format every program understands; it is the one listed.
	if (format == 0 && IsClipboardFormatAvailable(CF_UNICODETEXT))
		return CF_UNICODETEXT;
	return 0;
}

DWORD WINAPI GetClipboardSequenceNumber()
{
	return static_cast<DWORD>(nppmac::platform::pasteboardChangeCount());
}

HWND WINAPI GetClipboardOwner()
{
	return state()._owner;
}

BOOL WINAPI AddClipboardFormatListener(HWND hwnd)
{
	ClipboardState& clipboard = state();
	clipboard._listeners.insert(hwnd);
	if (!clipboard._timer)
	{
		clipboard._lastChange = nppmac::platform::pasteboardChangeCount();
		clipboard._timer = nppmac::ui::startTimer(500, pollPasteboard, nullptr, 0);
	}
	return TRUE;
}

BOOL WINAPI RemoveClipboardFormatListener(HWND hwnd)
{
	state()._listeners.erase(hwnd);
	return TRUE;
}

HWND WINAPI SetClipboardViewer(HWND)
{
	return nullptr;
}

BOOL WINAPI ChangeClipboardChain(HWND, HWND)
{
	return TRUE;
}
