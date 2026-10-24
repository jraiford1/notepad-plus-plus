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

// The common controls the runtime provides. Controls without their own implementation yet (W5) are
// plain windows that paint their background, so that the windows upstream builds from them exist.

#include "Window.h"

namespace nppmac::controls
{
	void registerStatusBar();
	void registerToolbar();
	void registerRebar();
	void registerTabControl();
	void registerTooltip();
	void registerButton();
	void registerStatic();
	void registerEdit();
	void registerListBox();
	void registerComboBox();
	void registerSmallControls();

	void registerControl(const char16_t* name, WNDPROC proc, UINT style, LPCWSTR cursor)
	{
		user::registerBuiltInClass(name, proc, style, sizeof(LONG_PTR) * 2, LoadCursorW(nullptr, cursor), nullptr, nullptr);
	}
}

namespace nppmac::user
{
	namespace
	{
		LRESULT CALLBACK placeholderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
				case WM_PAINT:
				{
					PAINTSTRUCT paint{};
					HDC hdc = BeginPaint(hwnd, &paint);
					FillRect(hdc, &paint.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
					EndPaint(hwnd, &paint);
					return 0;
				}
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerControlClasses()
	{
		controls::registerStatusBar();
		controls::registerToolbar();
		controls::registerRebar();
		controls::registerTabControl();
		controls::registerTooltip();
		controls::registerButton();
		controls::registerStatic();
		controls::registerEdit();
		controls::registerListBox();
		controls::registerComboBox();
		controls::registerSmallControls();

		static const char16_t* const classes[] = {
			WC_BUTTONW, WC_STATICW, WC_EDITW, WC_COMBOBOXW, WC_COMBOBOXEXW, WC_LISTBOXW, WC_SCROLLBARW,
			WC_LISTVIEWW, WC_TREEVIEWW, WC_TABCONTROLW, WC_HEADERW, TOOLBARCLASSNAMEW, REBARCLASSNAMEW,
			STATUSCLASSNAMEW, TOOLTIPS_CLASSW, PROGRESS_CLASSW, TRACKBAR_CLASSW, UPDOWN_CLASSW, u"SysLink",
			WC_IPADDRESSW, HOTKEY_CLASSW, ANIMATE_CLASSW, MONTHCAL_CLASSW, DATETIMEPICK_CLASSW, WC_NATIVEFONTCTLW,
			WC_PAGESCROLLERW, u"ComboLBox",
		};
		for (const char16_t* name : classes)
			registerBuiltInClass(name, placeholderProc, CS_DBLCLKS, sizeof(LONG_PTR) * 2, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr);
	}

}

BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX*)
{
	nppmac::user::ensureBuiltInClasses();
	return TRUE;
}

void WINAPI InitCommonControls()
{
	nppmac::user::ensureBuiltInClasses();
}
