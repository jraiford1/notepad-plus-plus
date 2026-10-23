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

// The status bar control (msctls_statusbar32).

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr int borderX = 0;
		constexpr int borderY = 2;
		constexpr int separator = 2;

		struct Part
		{
			int _right = -1;
			std::u16string _text;
			UINT _type = 0;
			LPARAM _itemData = 0; // owner-drawn parts
		};

		struct StatusBar
		{
			std::vector<Part> _parts{Part{}};
			int _minHeight = 0;
			bool _simple = false;
			Part _simplePart;
			COLORREF _background = CLR_DEFAULT;
		};

		int barHeight(HWND hwnd, const StatusBar& bar)
		{
			return std::max(bar._minHeight, fontHeight(hwnd) + 2 * borderY + 4);
		}

		// Docks the bar at the bottom of its parent, full width, unless told not to.
		void layout(HWND hwnd, const StatusBar& bar)
		{
			const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
			if (style & CCS_NOPARENTALIGN)
				return;
			RECT parent{};
			GetClientRect(GetParent(hwnd), &parent);
			const int height = barHeight(hwnd, bar);
			SetWindowPos(hwnd, nullptr, 0, parent.bottom - height, parent.right, height, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		RECT partRect(HWND hwnd, const StatusBar& bar, size_t index)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			if (bar._simple)
				return client;
			const int left = index == 0 ? 0 : bar._parts[index - 1]._right + separator;
			int right = bar._parts[index]._right;
			if (right < 0 || right > client.right)
				right = client.right;
			return {left, client.top + borderY, std::max(left, right), client.bottom - borderY};
		}

		void paint(HWND hwnd, StatusBar& bar, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			HBRUSH background = bar._background == CLR_DEFAULT ? GetSysColorBrush(COLOR_BTNFACE) : CreateSolidBrush(bar._background);
			FillRect(hdc, &client, background);
			if (bar._background != CLR_DEFAULT)
				DeleteObject(background);

			HGDIOBJ oldFont = SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
			const size_t count = bar._simple ? 1 : bar._parts.size();
			for (size_t i = 0; i < count; ++i)
			{
				const Part& part = bar._simple ? bar._simplePart : bar._parts[i];
				RECT rect = partRect(hwnd, bar, i);
				if (part._type & SBT_OWNERDRAW)
				{
					DRAWITEMSTRUCT item{};
					item.CtlType = 0;
					item.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
					item.itemID = static_cast<UINT>(i);
					item.hwndItem = hwnd;
					item.hDC = hdc;
					item.rcItem = rect;
					item.itemData = static_cast<ULONG_PTR>(part._itemData);
					SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
					continue;
				}
				RECT textRect = rect;
				textRect.left += 4;
				textRect.right -= 4;
				// A tab in the text centers what follows; a second one right-aligns it.
				std::u16string text = part._text;
				UINT format = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
				if (!text.empty() && text[0] == u'\t')
				{
					text.erase(0, 1);
					format |= DT_CENTER;
					if (!text.empty() && text[0] == u'\t')
					{
						text.erase(0, 1);
						format = (format & ~DT_CENTER) | DT_RIGHT;
					}
				}
				DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &textRect, format);
				if (!bar._simple && i + 1 < count && !(part._type & SBT_NOBORDERS))
				{
					RECT line{rect.right, rect.top + 2, rect.right + 1, rect.bottom - 2};
					FillRect(hdc, &line, GetSysColorBrush(COLOR_BTNSHADOW));
				}
			}
			SelectObject(hdc, oldFont);
		}

		LRESULT CALLBACK statusBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			StatusBar* bar = dataOf<StatusBar>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<StatusBar>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_CREATE:
					layout(hwnd, *bar);
					return 0;
				case WM_SIZE:
					// The parent sends WM_SIZE to have the bar follow its size.
					if (bar)
						layout(hwnd, *bar);
					return 0;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *bar, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *bar, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SETFONT:
					DefWindowProcW(hwnd, message, wParam, lParam);
					layout(hwnd, *bar);
					if (LOWORD(lParam))
						InvalidateRect(hwnd, nullptr, TRUE);
					return 0;
				case SB_SETPARTS:
				{
					const int count = std::clamp(static_cast<int>(wParam), 1, 256);
					const auto* rights = reinterpret_cast<const int*>(lParam);
					bar->_parts.resize(static_cast<size_t>(count));
					for (int i = 0; i < count; ++i)
						bar->_parts[static_cast<size_t>(i)]._right = rights[i];
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case SB_GETPARTS:
				{
					auto* rights = reinterpret_cast<int*>(lParam);
					for (size_t i = 0; rights && i < std::min<size_t>(wParam, bar->_parts.size()); ++i)
						rights[i] = bar->_parts[i]._right;
					return static_cast<LRESULT>(bar->_parts.size());
				}
				case SB_SETTEXTW:
				{
					const UINT index = LOBYTE(wParam);
					const UINT type = static_cast<UINT>(wParam) & 0xFF00;
					Part* part = nullptr;
					if (index == SB_SIMPLEID || bar->_simple)
						part = &bar->_simplePart;
					else if (index < bar->_parts.size())
						part = &bar->_parts[index];
					if (!part)
						return FALSE;
					part->_type = type;
					if (type & SBT_OWNERDRAW)
					{
						part->_itemData = lParam;
						part->_text.clear();
					}
					else
					{
						part->_text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"";
					}
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case SB_GETTEXTW:
				case SB_GETTEXTLENGTHW:
				{
					const UINT index = LOWORD(wParam);
					if (index >= bar->_parts.size())
						return 0;
					const Part& part = bar->_parts[index];
					if (message == SB_GETTEXTW && lParam)
						std::copy(part._text.c_str(), part._text.c_str() + part._text.size() + 1, reinterpret_cast<LPWSTR>(lParam));
					return MAKELONG(static_cast<WORD>(part._text.size()), static_cast<WORD>(part._type));
				}
				case SB_GETRECT:
				{
					if (wParam >= bar->_parts.size())
						return FALSE;
					*reinterpret_cast<RECT*>(lParam) = partRect(hwnd, *bar, wParam);
					return TRUE;
				}
				case SB_GETBORDERS:
				{
					auto* borders = reinterpret_cast<int*>(lParam);
					borders[0] = borderX;
					borders[1] = borderY;
					borders[2] = separator;
					return TRUE;
				}
				case SB_SETMINHEIGHT:
					bar->_minHeight = static_cast<int>(wParam);
					return 0;
				case SB_SIMPLE:
					bar->_simple = wParam != 0;
					InvalidateRect(hwnd, nullptr, TRUE);
					return 0;
				case SB_ISSIMPLE:
					return bar->_simple;
				case SB_SETBKCOLOR:
				{
					const COLORREF previous = bar->_background;
					bar->_background = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, TRUE);
					return static_cast<LRESULT>(previous);
				}
				case SB_SETICON:
				case SB_SETTIPTEXTW:
					return TRUE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerStatusBar()
	{
		registerControl(STATUSCLASSNAMEW, statusBarProc);
	}
}
