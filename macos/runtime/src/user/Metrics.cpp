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

// user32 system metrics, colors, parameters, monitors, cursors and carets; dwmapi.

#include "gdi/Gdi.h"
#include "Window.h"

#include <dwmapi.h>

using namespace nppmac;

namespace
{
	// The Windows 10 default (light) palette; Notepad++ draws its own dark mode.
	COLORREF g_systemColors[COLOR_MENUBAR + 1] = {
		RGB(200, 200, 200), // COLOR_SCROLLBAR
		RGB(0, 0, 0),       // COLOR_BACKGROUND
		RGB(153, 180, 209), // COLOR_ACTIVECAPTION
		RGB(191, 205, 219), // COLOR_INACTIVECAPTION
		RGB(240, 240, 240), // COLOR_MENU
		RGB(255, 255, 255), // COLOR_WINDOW
		RGB(100, 100, 100), // COLOR_WINDOWFRAME
		RGB(0, 0, 0),       // COLOR_MENUTEXT
		RGB(0, 0, 0),       // COLOR_WINDOWTEXT
		RGB(0, 0, 0),       // COLOR_CAPTIONTEXT
		RGB(180, 180, 180), // COLOR_ACTIVEBORDER
		RGB(244, 247, 252), // COLOR_INACTIVEBORDER
		RGB(171, 171, 171), // COLOR_APPWORKSPACE
		RGB(0, 120, 215),   // COLOR_HIGHLIGHT
		RGB(255, 255, 255), // COLOR_HIGHLIGHTTEXT
		RGB(240, 240, 240), // COLOR_BTNFACE
		RGB(160, 160, 160), // COLOR_BTNSHADOW
		RGB(109, 109, 109), // COLOR_GRAYTEXT
		RGB(0, 0, 0),       // COLOR_BTNTEXT
		RGB(0, 0, 0),       // COLOR_INACTIVECAPTIONTEXT
		RGB(255, 255, 255), // COLOR_BTNHIGHLIGHT
		RGB(105, 105, 105), // COLOR_3DDKSHADOW
		RGB(227, 227, 227), // COLOR_3DLIGHT
		RGB(0, 0, 0),       // COLOR_INFOTEXT
		RGB(255, 255, 225), // COLOR_INFOBK
		RGB(0, 0, 0),       // (unused)
		RGB(0, 102, 204),   // COLOR_HOTLIGHT
		RGB(185, 209, 234), // COLOR_GRADIENTACTIVECAPTION
		RGB(215, 228, 242), // COLOR_GRADIENTINACTIVECAPTION
		RGB(0, 120, 215),   // COLOR_MENUHILIGHT
		RGB(240, 240, 240), // COLOR_MENUBAR
	};

	HCURSOR g_cursor = nullptr;

	HCURSOR systemCursor(ui::CursorShape shape)
	{
		static gdi::Icon* cursors[static_cast<int>(ui::CursorShape::help) + 1] = {};
		const int index = static_cast<int>(shape);
		if (!cursors[index])
		{
			auto* icon = new gdi::Icon;
			icon->_isIcon = false;
			icon->_shared = true;
			icon->_stock = true;
			icon->_cursorShape = index;
			gdi::trackObject(icon);
			cursors[index] = icon;
		}
		return reinterpret_cast<HCURSOR>(cursors[index]);
	}

	HICON systemIcon(int id)
	{
		static std::map<int, gdi::Icon*> icons;
		gdi::Icon*& icon = icons[id];
		if (!icon)
		{
			icon = new gdi::Icon;
			icon->_shared = true;
			icon->_stock = true;
			icon->_color = std::make_shared<gdi::Bitmap>();
			icon->_color->_width = 32;
			icon->_color->_height = 32;
			icon->_color->_ownedPixels.assign(32 * 32, 0);
			icon->_color->_pixels = icon->_color->_ownedPixels.data();
			gdi::trackObject(icon);
		}
		return reinterpret_cast<HICON>(icon);
	}
}

namespace nppmac::user
{
	HCURSOR loadSystemCursor(LPCWSTR id)
	{
		static const std::pair<LPCWSTR, ui::CursorShape> shapes[] = {
			{IDC_IBEAM, ui::CursorShape::iBeam}, {IDC_WAIT, ui::CursorShape::wait}, {IDC_CROSS, ui::CursorShape::cross},
			{IDC_UPARROW, ui::CursorShape::upArrow}, {IDC_SIZENWSE, ui::CursorShape::sizeNWSE}, {IDC_SIZENESW, ui::CursorShape::sizeNESW},
			{IDC_SIZEWE, ui::CursorShape::sizeWE}, {IDC_SIZENS, ui::CursorShape::sizeNS}, {IDC_SIZEALL, ui::CursorShape::sizeAll},
			{IDC_NO, ui::CursorShape::no}, {IDC_HAND, ui::CursorShape::hand}, {IDC_APPSTARTING, ui::CursorShape::appStarting},
			{IDC_HELP, ui::CursorShape::help},
		};
		for (const auto& [cursorId, shape] : shapes)
		{
			if (cursorId == id)
				return systemCursor(shape);
		}
		return systemCursor(ui::CursorShape::arrow);
	}

	HICON loadSystemIcon(LPCWSTR id)
	{
		return systemIcon(static_cast<int>(reinterpret_cast<uintptr_t>(id)));
	}
}

// --- Colors ---------------------------------------------------------------------------------------

DWORD WINAPI GetSysColor(int index)
{
	return index >= 0 && index <= COLOR_MENUBAR ? g_systemColors[index] : 0;
}

BOOL WINAPI SetSysColors(int count, const INT* indices, const COLORREF* colors)
{
	for (int i = 0; i < count; ++i)
	{
		if (indices[i] >= 0 && indices[i] <= COLOR_MENUBAR)
			g_systemColors[indices[i]] = colors[i];
	}
	return TRUE;
}

// --- Metrics --------------------------------------------------------------------------------------

int WINAPI GetSystemMetrics(int index)
{
	const ui::Rect screen = ui::mainScreenRect();
	const ui::Rect work = ui::mainScreenWorkArea();
	switch (index)
	{
		case SM_CXSCREEN:
		case SM_CXVIRTUALSCREEN:
		case SM_CXFULLSCREEN:
			return screen.width();
		case SM_CYSCREEN:
		case SM_CYVIRTUALSCREEN:
			return screen.height();
		case SM_CYFULLSCREEN:
			return work.height();
		case SM_XVIRTUALSCREEN:
		case SM_YVIRTUALSCREEN:
			return 0;
		case SM_CXMAXIMIZED:
			return work.width();
		case SM_CYMAXIMIZED:
			return work.height();
		case SM_CXVSCROLL:
		case SM_CYHSCROLL:
		case SM_CXHSCROLL:
		case SM_CYVSCROLL:
			return 15;
		case SM_CYCAPTION:
			return 28;
		case SM_CYSMCAPTION:
			return 22;
		case SM_CXBORDER:
		case SM_CYBORDER:
			return 1;
		case SM_CXEDGE:
		case SM_CYEDGE:
			return 2;
		case SM_CXFRAME:
		case SM_CYFRAME:
		case SM_CXFIXEDFRAME:
		case SM_CYFIXEDFRAME:
			return 0;
		case SM_CXPADDEDBORDER:
			return 0;
		case SM_CXICON:
		case SM_CYICON:
			return 32;
		case SM_CXSMICON:
		case SM_CYSMICON:
			return 16;
		case SM_CXCURSOR:
		case SM_CYCURSOR:
			return 32;
		case SM_CYMENU:
			return 0; // the menu bar is the macOS menu bar, outside the window
		case SM_CXMENUCHECK:
		case SM_CYMENUCHECK:
			return 15;
		case SM_CXMIN:
			return 136;
		case SM_CYMIN:
			return 39;
		case SM_CXMINTRACK:
			return 136;
		case SM_CYMINTRACK:
			return 39;
		case SM_CXDOUBLECLK:
		case SM_CYDOUBLECLK:
			return 4;
		case SM_CXDRAG:
		case SM_CYDRAG:
			return 4;
		case SM_CMOUSEBUTTONS:
			return 3;
		case SM_MOUSEWHEELPRESENT:
		case SM_MOUSEPRESENT:
			return 1;
		case SM_SWAPBUTTON:
			return 0;
		case SM_CMONITORS:
			return 1;
		case SM_SAMEDISPLAYFORMAT:
			return 1;
		case SM_REMOTESESSION:
			return 0;
		case SM_CXFOCUSBORDER:
		case SM_CYFOCUSBORDER:
			return 1;
		default:
			return 0;
	}
}

int WINAPI GetSystemMetricsForDpi(int index, UINT)
{
	return GetSystemMetrics(index);
}

UINT WINAPI GetDpiForWindow(HWND)
{
	return 96;
}

UINT WINAPI GetDpiForSystem()
{
	return 96;
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT parameter, PVOID value, UINT)
{
	switch (action)
	{
		case SPI_GETNONCLIENTMETRICS:
		{
			auto* metrics = static_cast<NONCLIENTMETRICSW*>(value);
			const UINT size = metrics->cbSize;
			std::memset(metrics, 0, size);
			metrics->cbSize = size;
			metrics->iBorderWidth = 1;
			metrics->iScrollWidth = 15;
			metrics->iScrollHeight = 15;
			metrics->iCaptionWidth = 28;
			metrics->iCaptionHeight = 28;
			metrics->iSmCaptionWidth = 22;
			metrics->iSmCaptionHeight = 22;
			metrics->iMenuWidth = 19;
			metrics->iMenuHeight = 19;
			LOGFONTW font{};
			font.lfHeight = -12;
			font.lfWeight = FW_NORMAL;
			font.lfCharSet = DEFAULT_CHARSET;
			wcscpy_s(font.lfFaceName, u"Segoe UI");
			metrics->lfCaptionFont = font;
			metrics->lfSmCaptionFont = font;
			metrics->lfMenuFont = font;
			metrics->lfStatusFont = font;
			metrics->lfMessageFont = font;
			return TRUE;
		}
		case SPI_GETICONTITLELOGFONT:
		{
			auto* font = static_cast<LOGFONTW*>(value);
			std::memset(font, 0, sizeof(*font));
			font->lfHeight = -12;
			font->lfWeight = FW_NORMAL;
			wcscpy_s(font->lfFaceName, u"Segoe UI");
			return TRUE;
		}
		case SPI_GETWORKAREA:
		{
			const ui::Rect work = ui::mainScreenWorkArea();
			*static_cast<RECT*>(value) = {work._left, work._top, work._right, work._bottom};
			return TRUE;
		}
		case SPI_GETWHEELSCROLLLINES:
		case SPI_GETWHEELSCROLLCHARS:
			*static_cast<UINT*>(value) = 3;
			return TRUE;
		case SPI_GETFONTSMOOTHING:
		case SPI_GETDRAGFULLWINDOWS:
		case SPI_GETCLIENTAREAANIMATION:
		case SPI_GETMENUANIMATION:
		case SPI_GETCOMBOBOXANIMATION:
		case SPI_GETLISTBOXSMOOTHSCROLLING:
		case SPI_GETGRADIENTCAPTIONS:
		case SPI_GETFLATMENU:
			*static_cast<BOOL*>(value) = TRUE;
			return TRUE;
		case SPI_GETFONTSMOOTHINGTYPE:
			*static_cast<UINT*>(value) = FE_FONTSMOOTHINGCLEARTYPE;
			return TRUE;
		case SPI_GETHIGHCONTRAST:
		{
			auto* contrast = static_cast<HIGHCONTRASTW*>(value);
			contrast->dwFlags = 0;
			return TRUE;
		}
		case SPI_GETKEYBOARDCUES:
		case SPI_GETSCREENREADER:
			*static_cast<BOOL*>(value) = FALSE;
			return TRUE;
		case SPI_GETCARETWIDTH:
		case SPI_GETFOCUSBORDERWIDTH:
		case SPI_GETFOCUSBORDERHEIGHT:
			*static_cast<UINT*>(value) = 1;
			return TRUE;
		case SPI_GETMOUSEHOVERTIME:
			*static_cast<UINT*>(value) = 400;
			return TRUE;
		case SPI_GETKEYBOARDDELAY:
			*static_cast<int*>(value) = 1;
			return TRUE;
		default:
			(void)parameter;
			return FALSE;
	}
}

// --- Monitors -------------------------------------------------------------------------------------

namespace
{
	const HMONITOR g_mainMonitor = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x10001));
}

HMONITOR WINAPI MonitorFromWindow(HWND, DWORD)
{
	return g_mainMonitor;
}

HMONITOR WINAPI MonitorFromRect(LPCRECT, DWORD)
{
	return g_mainMonitor;
}

HMONITOR WINAPI MonitorFromPoint(POINT, DWORD)
{
	return g_mainMonitor;
}

BOOL WINAPI GetMonitorInfoW(HMONITOR, LPMONITORINFO info)
{
	const ui::Rect screen = ui::mainScreenRect();
	const ui::Rect work = ui::mainScreenWorkArea();
	info->rcMonitor = {screen._left, screen._top, screen._right, screen._bottom};
	info->rcWork = {work._left, work._top, work._right, work._bottom};
	info->dwFlags = MONITORINFOF_PRIMARY;
	if (info->cbSize >= sizeof(MONITORINFOEXW))
		wcscpy_s(reinterpret_cast<MONITORINFOEXW*>(info)->szDevice, u"\\\\.\\DISPLAY1");
	return TRUE;
}

BOOL WINAPI EnumDisplayMonitors(HDC, LPRECT, MONITORENUMPROC callback, LPARAM parameter)
{
	const ui::Rect screen = ui::mainScreenRect();
	RECT rect{screen._left, screen._top, screen._right, screen._bottom};
	callback(g_mainMonitor, nullptr, &rect, parameter);
	return TRUE;
}

// --- Cursors and icons ----------------------------------------------------------------------------

HCURSOR WINAPI SetCursor(HCURSOR cursor)
{
	HCURSOR previous = g_cursor;
	g_cursor = cursor;
	auto* icon = gdi::objectAs<gdi::Icon>(cursor, gdi::ObjectType::icon);
	if (!cursor)
		ui::hideCursor(true);
	else
		ui::setCursor(icon && icon->_cursorShape >= 0 ? static_cast<ui::CursorShape>(icon->_cursorShape) : ui::CursorShape::arrow);
	return previous;
}

HCURSOR WINAPI GetCursor()
{
	return g_cursor;
}

int WINAPI ShowCursor(BOOL show)
{
	static int counter = 0;
	counter += show ? 1 : -1;
	ui::hideCursor(counter < 0);
	return counter;
}

// --- Carets: custom-drawn text fields are drawn by their owners; the caret is not shown ------------

BOOL WINAPI CreateCaret(HWND, HBITMAP, int, int)
{
	return TRUE;
}

BOOL WINAPI DestroyCaret()
{
	return TRUE;
}

BOOL WINAPI ShowCaret(HWND)
{
	return TRUE;
}

BOOL WINAPI HideCaret(HWND)
{
	return TRUE;
}

BOOL WINAPI SetCaretPos(int, int)
{
	return TRUE;
}

UINT WINAPI GetCaretBlinkTime()
{
	return 530;
}

UINT WINAPI GetDoubleClickTime()
{
	return 500;
}

// --- Window effects ---------------------------------------------------------------------------------

BOOL WINAPI SetLayeredWindowAttributes(HWND, COLORREF, BYTE, DWORD)
{
	return TRUE;
}

HRESULT WINAPI DwmGetColorizationColor(DWORD* color, BOOL* opaque)
{
	*color = 0xFF0078D7;
	*opaque = TRUE;
	return S_OK;
}

HRESULT WINAPI DwmSetWindowAttribute(HWND, DWORD, LPCVOID, DWORD)
{
	return S_OK;
}

HRESULT WINAPI DwmGetWindowAttribute(HWND, DWORD, PVOID, DWORD)
{
	return E_NOTIMPL;
}

HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND, const MARGINS*)
{
	return S_OK;
}

HRESULT WINAPI DwmIsCompositionEnabled(BOOL* enabled)
{
	*enabled = TRUE;
	return S_OK;
}

BOOL WINAPI AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL, DWORD exStyle)
{
	// The macOS title bar is the only non-client area.
	if ((style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD))
		rect->top -= (exStyle & WS_EX_TOOLWINDOW) ? 22 : 28;
	return TRUE;
}

BOOL WINAPI AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu)
{
	return AdjustWindowRectEx(rect, style, menu, 0);
}

BOOL WINAPI AdjustWindowRectExForDpi(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle, UINT)
{
	return AdjustWindowRectEx(rect, style, menu, exStyle);
}
