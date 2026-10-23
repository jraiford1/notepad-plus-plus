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

// Shared by the runtime's common controls, which are drawn with GDI in the Windows style so that
// upstream's subclassing, owner drawing and custom drawing (its dark mode) work as on Windows.

#pragma once

#include "gdi/Gdi.h"
#include "user/Window.h"

#include <commctrl.h>
#include <windowsx.h>

namespace nppmac::controls
{
	// The control's own data, created on WM_NCCREATE.
	template <typename T>
	T* dataOf(HWND hwnd)
	{
		user::Window* window = user::fromHandle(hwnd);
		return window ? static_cast<T*>(window->_controlData.get()) : nullptr;
	}

	template <typename T>
	T* createData(HWND hwnd)
	{
		user::Window* window = user::fromHandle(hwnd);
		auto data = std::make_shared<T>();
		window->_controlData = data;
		return data.get();
	}

	// The window that receives the control's notifications: its parent at creation, unless changed.
	inline HWND notifyWindow(HWND hwnd, HWND notifyOverride)
	{
		return notifyOverride ? notifyOverride : GetParent(hwnd);
	}

	inline LRESULT sendNotify(HWND hwnd, HWND target, UINT code, NMHDR* header)
	{
		header->hwndFrom = hwnd;
		header->idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(hwnd));
		header->code = code;
		return SendMessageW(target, WM_NOTIFY, header->idFrom, reinterpret_cast<LPARAM>(header));
	}

	inline HFONT fontOf(HWND hwnd)
	{
		user::Window* window = user::fromHandle(hwnd);
		return window && window->_font ? window->_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	}

	inline int fontHeight(HWND hwnd)
	{
		HDC hdc = GetDC(hwnd);
		HGDIOBJ old = SelectObject(hdc, fontOf(hwnd));
		TEXTMETRICW metrics{};
		GetTextMetricsW(hdc, &metrics);
		SelectObject(hdc, old);
		ReleaseDC(hwnd, hdc);
		return metrics.tmHeight;
	}

	inline int textWidth(HWND hwnd, const std::u16string& text)
	{
		HDC hdc = GetDC(hwnd);
		HGDIOBJ old = SelectObject(hdc, fontOf(hwnd));
		SIZE size{};
		GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
		SelectObject(hdc, old);
		ReleaseDC(hwnd, hdc);
		return size.cx;
	}

	// A ComboLBox tells its combo box which item was clicked (wParam: the index).
	constexpr UINT comboListChosen = WM_APP + 0x3F01;

	// Asks the parent for the control's colors with a WM_CTLCOLOR* message, as Windows controls do:
	// the parent sets the DC's text and background colors and returns the background brush.
	inline HBRUSH controlColors(HWND hwnd, HDC hdc, UINT message)
	{
		HWND parent = GetParent(hwnd);
		const WPARAM dc = reinterpret_cast<WPARAM>(hdc);
		const LPARAM control = reinterpret_cast<LPARAM>(hwnd);
		auto brush = reinterpret_cast<HBRUSH>(parent ? SendMessageW(parent, message, dc, control) : 0);
		return brush ? brush : reinterpret_cast<HBRUSH>(DefWindowProcW(hwnd, message, dc, control));
	}

	// WM_COMMAND to the parent with the control's id and a notification code.
	inline void sendCommand(HWND hwnd, WORD code)
	{
		if (HWND parent = GetParent(hwnd))
			SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), code), reinterpret_cast<LPARAM>(hwnd));
	}

	inline std::u16string windowText(HWND hwnd)
	{
		std::u16string text(static_cast<size_t>(GetWindowTextLengthW(hwnd)), u'\0');
		if (!text.empty())
			GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()) + 1);
		return text;
	}

	void registerControl(const char16_t* name, WNDPROC proc, UINT style = CS_DBLCLKS, LPCWSTR cursor = IDC_ARROW);
}
