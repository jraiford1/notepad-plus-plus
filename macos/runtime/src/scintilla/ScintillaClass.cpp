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

// The "Scintilla" window class (Scintilla_RegisterClasses), over Scintilla's own Cocoa view.
// SCI_* messages go to Scintilla, as does the direct function upstream fetches with
// SCI_GETDIRECTFUNCTION; Scintilla's notifications reach the parent as WM_NOTIFY and WM_COMMAND.
//
// Both paths go through sendToScintilla, which converts font sizes: Notepad++ gives them in
// Windows points, which are 96/72 of a pixel, and the runtime's pixel is a macOS point, the unit
// Scintilla's Cocoa layer sizes fonts in. Without this, text would be a quarter smaller than on
// Windows.

#include "user/Window.h"
#include "ScintillaBridge.h"

#include "Scintilla.h"

namespace
{
	using nppmac::user::Window;
	using nppmac::user::fromHandle;

	struct ScintillaData
	{
		void* _view = nullptr;
		SciFnDirect _direct = nullptr;  // Scintilla's own direct function and pointer
		sptr_t _directPointer = 0;
		bool _swallowCharacters = false; // the key's characters already went to Scintilla with the key
	};

	constexpr intptr_t windowsPointsPerInch = 72;
	constexpr intptr_t windowsPixelsPerInch = 96;

	intptr_t toScintillaFontSize(intptr_t size)
	{
		return size * windowsPixelsPerInch / windowsPointsPerInch;
	}

	intptr_t fromScintillaFontSize(intptr_t size)
	{
		return (size * windowsPointsPerInch + windowsPixelsPerInch / 2) / windowsPixelsPerInch;
	}

	sptr_t sendToScintilla(const ScintillaData& data, unsigned int message, uptr_t wParam, sptr_t lParam)
	{
		switch (message)
		{
			case SCI_STYLESETSIZE:
				return data._direct(data._directPointer, SCI_STYLESETSIZEFRACTIONAL, wParam, toScintillaFontSize(lParam * SC_FONT_SIZE_MULTIPLIER));
			case SCI_STYLESETSIZEFRACTIONAL:
				return data._direct(data._directPointer, message, wParam, toScintillaFontSize(lParam));
			case SCI_STYLEGETSIZE:
			{
				const intptr_t size = fromScintillaFontSize(data._direct(data._directPointer, SCI_STYLEGETSIZEFRACTIONAL, wParam, 0));
				return (size + SC_FONT_SIZE_MULTIPLIER / 2) / SC_FONT_SIZE_MULTIPLIER;
			}
			case SCI_STYLEGETSIZEFRACTIONAL:
				return fromScintillaFontSize(data._direct(data._directPointer, message, wParam, lParam));
			default:
				return data._direct(data._directPointer, message, wParam, lParam);
		}
	}

	// What SCI_GETDIRECTFUNCTION hands out; the pointer is the window's ScintillaData.
	sptr_t directFunction(sptr_t pointer, unsigned int message, uptr_t wParam, sptr_t lParam)
	{
		return sendToScintilla(*reinterpret_cast<const ScintillaData*>(pointer), message, wParam, lParam);
	}

	ScintillaData* dataOf(Window* window)
	{
		return window ? static_cast<ScintillaData*>(window->_controlData.get()) : nullptr;
	}

	void notify(intptr_t windowId, unsigned message, uintptr_t wParam, uintptr_t lParam)
	{
		HWND hwnd = reinterpret_cast<HWND>(windowId);
		Window* window = fromHandle(hwnd);
		if (!window || !window->_parent || window->_destroying)
			return;
		if (message == nppmac::scintilla::cocoaNotify)
		{
			auto* notification = reinterpret_cast<SCNotification*>(lParam);
			notification->nmhdr.hwndFrom = hwnd;
			notification->nmhdr.idFrom = window->_id;
			SendMessageW(window->_parent->_hwnd, WM_NOTIFY, window->_id, static_cast<LPARAM>(lParam));
		}
		else if (message == nppmac::scintilla::cocoaCommand)
		{
			SendMessageW(window->_parent->_hwnd, WM_COMMAND, wParam, reinterpret_cast<LPARAM>(hwnd));
		}
	}

	void contextMenu(intptr_t windowId, int screenX, int screenY)
	{
		HWND hwnd = reinterpret_cast<HWND>(windowId);
		if (Window* window = fromHandle(hwnd))
			nppmac::user::postMessage(window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(screenX, screenY));
	}

	LRESULT CALLBACK scintillaProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Window* window = fromHandle(hwnd);
		ScintillaData* data = dataOf(window);
		if (!data || !data->_view)
			return DefWindowProcW(hwnd, message, wParam, lParam);

		if (message == SCI_GETDIRECTFUNCTION)
			return reinterpret_cast<LRESULT>(&directFunction);
		if (message == SCI_GETDIRECTPOINTER)
			return reinterpret_cast<LRESULT>(data);
		if (message >= SCI_START && message < 5000)
			return sendToScintilla(*data, message, wParam, lParam);

		switch (message)
		{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYUP:
			{
				if (void* event = nppmac::user::currentNativeEvent())
				{
					data->_swallowCharacters = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
					nppmac::ui::forwardKey(window->_view, event);
				}
				return 0;
			}
			case WM_CHAR:
			case WM_SYSCHAR:
			{
				if (data->_swallowCharacters)
					return 0;
				const char16_t ch = static_cast<char16_t>(wParam);
				if (ch >= 0x20 || ch == u'\t' || ch == u'\r')
					nppmac::scintilla::insertText(data->_view, std::u16string(1, ch == u'\r' ? u'\n' : ch));
				return 0;
			}
			case WM_SETFOCUS:
				nppmac::scintilla::message(data->_view, SCI_SETFOCUS, 1, 0);
				return 0;
			case WM_KILLFOCUS:
				nppmac::scintilla::message(data->_view, SCI_SETFOCUS, 0, 0);
				return 0;
			case WM_GETDLGCODE:
				return DLGC_HASSETSEL | DLGC_WANTALLKEYS;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
				ValidateRect(hwnd, nullptr);
				return 0;
			case WM_GETTEXTLENGTH:
				return nppmac::scintilla::message(data->_view, SCI_GETLENGTH, 0, 0);
			case WM_DESTROY:
				nppmac::scintilla::forgetView(data->_view);
				return 0;
			case WM_SETCURSOR:
				return TRUE; // Scintilla sets its own cursors
			default:
				return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}
}

namespace nppmac::user
{
	void registerScintillaClass()
	{
		auto builtIn = std::make_shared<BuiltInClass>();
		builtIn->_createNativeView = [](Window& window, const CREATESTRUCTW&) -> void*
		{
			auto data = std::make_shared<ScintillaData>();
			data->_view = nppmac::scintilla::createView(reinterpret_cast<intptr_t>(window._hwnd), notify, contextMenu);
			window._controlData = data;
			data->_direct = reinterpret_cast<SciFnDirect>(nppmac::scintilla::message(data->_view, SCI_GETDIRECTFUNCTION, 0, 0));
			data->_directPointer = nppmac::scintilla::message(data->_view, SCI_GETDIRECTPOINTER, 0, 0);
			nppmac::scintilla::message(data->_view, SCI_SETIDENTIFIER, window._id, 0);
			return data->_view;
		};
		registerBuiltInClass(u"Scintilla", scintillaProc, CS_DBLCLKS, sizeof(LONG_PTR), LoadCursorW(nullptr, IDC_IBEAM), nullptr, builtIn);
	}
}

int Scintilla_RegisterClasses(void*)
{
	nppmac::user::ensureBuiltInClasses();
	return 1;
}

int Scintilla_ReleaseResources(void)
{
	return 1;
}
