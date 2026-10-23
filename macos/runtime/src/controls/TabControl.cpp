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

// The tab control (SysTabControl32): tabs in rows, horizontal or vertical, drawn by the control or by
// its owner (TCS_OWNERDRAWFIXED, as Notepad++'s document tabs are).

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		struct Tab
		{
			std::u16string _text;
			int _image = -1;
			LPARAM _data = 0;
			DWORD _state = 0;
		};

		struct TabControl
		{
			std::vector<Tab> _tabs;
			int _selected = -1;
			int _focus = -1;
			int _fixedWidth = 0;
			int _fixedHeight = 0;
			int _paddingX = 6;
			int _paddingY = 3;
			int _minWidth = -1;
			HIMAGELIST _images = nullptr;
			HWND _tooltips = nullptr;
			std::vector<RECT> _rects; // laid out on demand
			int _rows = 1;
		};

		LONG styleOf(HWND hwnd)
		{
			return GetWindowLongW(hwnd, GWL_STYLE);
		}

		int tabHeight(HWND hwnd, const TabControl& tabs)
		{
			if (tabs._fixedHeight > 0)
				return tabs._fixedHeight;
			int height = fontHeight(hwnd) + 2 * tabs._paddingY + 2;
			if (tabs._images)
			{
				int imageWidth = 0;
				int imageHeight = 0;
				ImageList_GetIconSize(tabs._images, &imageWidth, &imageHeight);
				height = std::max(height, imageHeight + 2 * tabs._paddingY);
			}
			return height;
		}

		int tabWidth(HWND hwnd, const TabControl& tabs, const Tab& tab)
		{
			if ((styleOf(hwnd) & TCS_FIXEDWIDTH) && tabs._fixedWidth > 0)
				return tabs._fixedWidth;
			int width = textWidth(hwnd, tab._text) + 2 * tabs._paddingX;
			if (tabs._images && tab._image >= 0)
			{
				int imageWidth = 0;
				int imageHeight = 0;
				ImageList_GetIconSize(tabs._images, &imageWidth, &imageHeight);
				width += imageWidth + tabs._paddingX;
			}
			if (tabs._fixedWidth > 0 && (styleOf(hwnd) & TCS_OWNERDRAWFIXED))
				width = std::max(width, tabs._fixedWidth);
			const int minimum = tabs._minWidth >= 0 ? tabs._minWidth : 40;
			return std::max(width, minimum);
		}

		// Tabs run along the top (or the left with TCS_VERTICAL), wrapping into rows with TCS_MULTILINE.
		void layout(HWND hwnd, TabControl& tabs)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const bool vertical = (styleOf(hwnd) & TCS_VERTICAL) != 0;
			const bool multiLine = (styleOf(hwnd) & TCS_MULTILINE) != 0;
			const int extent = vertical ? client.bottom : client.right;
			const int thickness = tabHeight(hwnd, tabs);
			tabs._rects.clear();
			int position = 0;
			int row = 0;
			for (const Tab& tab : tabs._tabs)
			{
				const int length = tabWidth(hwnd, tabs, tab);
				if (multiLine && position > 0 && position + length > extent)
				{
					position = 0;
					++row;
				}
				if (vertical)
					tabs._rects.push_back({row * thickness, position, (row + 1) * thickness, position + length});
				else
					tabs._rects.push_back({position, row * thickness, position + length, (row + 1) * thickness});
				position += length;
			}
			tabs._rows = row + 1;
		}

		int hitTest(HWND hwnd, TabControl& tabs, POINT point)
		{
			layout(hwnd, tabs);
			for (size_t i = 0; i < tabs._rects.size(); ++i)
			{
				if (PtInRect(&tabs._rects[i], point))
					return static_cast<int>(i);
			}
			return -1;
		}

		void paint(HWND hwnd, TabControl& tabs, HDC hdc)
		{
			layout(hwnd, tabs);
			RECT client{};
			GetClientRect(hwnd, &client);
			FillRect(hdc, &client, GetSysColorBrush(COLOR_BTNFACE));
			HGDIOBJ oldFont = SelectObject(hdc, fontOf(hwnd));
			const bool ownerDraw = (styleOf(hwnd) & TCS_OWNERDRAWFIXED) != 0;
			for (size_t i = 0; i < tabs._tabs.size(); ++i)
			{
				const Tab& tab = tabs._tabs[i];
				RECT rect = tabs._rects[i];
				const bool selected = static_cast<int>(i) == tabs._selected;
				if (ownerDraw)
				{
					DRAWITEMSTRUCT item{};
					item.CtlType = ODT_TAB;
					item.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
					item.itemID = static_cast<UINT>(i);
					item.itemAction = ODA_DRAWENTIRE;
					item.itemState = selected ? ODS_SELECTED : 0;
					item.hwndItem = hwnd;
					item.hDC = hdc;
					item.rcItem = rect;
					item.itemData = static_cast<ULONG_PTR>(tab._data);
					SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
					continue;
				}
				FillRect(hdc, &rect, GetSysColorBrush(selected ? COLOR_WINDOW : COLOR_BTNFACE));
				FrameRect(hdc, &rect, GetSysColorBrush(COLOR_BTNSHADOW));
				RECT textRect = rect;
				InflateRect(&textRect, -tabs._paddingX, 0);
				if (tabs._images && tab._image >= 0)
				{
					int imageWidth = 0;
					int imageHeight = 0;
					ImageList_GetIconSize(tabs._images, &imageWidth, &imageHeight);
					ImageList_Draw(tabs._images, tab._image, hdc, textRect.left, (rect.top + rect.bottom - imageHeight) / 2, ILD_TRANSPARENT);
					textRect.left += imageWidth + tabs._paddingX;
				}
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
				DrawTextW(hdc, tab._text.c_str(), static_cast<int>(tab._text.size()), &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
			}
			SelectObject(hdc, oldFont);
		}

		void readItem(const Tab& tab, TCITEMW& item)
		{
			if ((item.mask & TCIF_TEXT) && item.pszText && item.cchTextMax > 0)
				lstrcpynW(item.pszText, tab._text.c_str(), item.cchTextMax);
			if (item.mask & TCIF_IMAGE)
				item.iImage = tab._image;
			if (item.mask & TCIF_PARAM)
				item.lParam = tab._data;
			if (item.mask & TCIF_STATE)
				item.dwState = tab._state & item.dwStateMask;
		}

		void writeItem(Tab& tab, const TCITEMW& item)
		{
			if ((item.mask & TCIF_TEXT) && item.pszText)
				tab._text = item.pszText;
			if (item.mask & TCIF_IMAGE)
				tab._image = item.iImage;
			if (item.mask & TCIF_PARAM)
				tab._data = item.lParam;
			if (item.mask & TCIF_STATE)
				tab._state = (tab._state & ~item.dwStateMask) | (item.dwState & item.dwStateMask);
		}

		// Clicking a tab selects it, asking the parent first (TCN_SELCHANGING may refuse).
		void selectByUser(HWND hwnd, TabControl& tabs, int index)
		{
			if (index < 0 || index == tabs._selected)
				return;
			NMHDR header{};
			if (sendNotify(hwnd, GetParent(hwnd), TCN_SELCHANGING, &header))
				return;
			tabs._selected = index;
			tabs._focus = index;
			InvalidateRect(hwnd, nullptr, TRUE);
			sendNotify(hwnd, GetParent(hwnd), TCN_SELCHANGE, &header);
		}

		LRESULT CALLBACK tabProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			TabControl* tabs = dataOf<TabControl>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<TabControl>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *tabs, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *tabs, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SIZE:
					InvalidateRect(hwnd, nullptr, TRUE);
					return 0;
				case WM_LBUTTONDOWN:
				{
					const int index = hitTest(hwnd, *tabs, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
					selectByUser(hwnd, *tabs, index);
					return 0;
				}
				case WM_RBUTTONUP:
				{
					NMHDR header{};
					sendNotify(hwnd, GetParent(hwnd), NM_RCLICK, &header);
					return 0;
				}
				case WM_GETDLGCODE:
					return DLGC_WANTARROWS;

				case TCM_GETITEMCOUNT:
					return static_cast<LRESULT>(tabs->_tabs.size());
				case TCM_GETITEMW:
				{
					if (wParam >= tabs->_tabs.size())
						return FALSE;
					readItem(tabs->_tabs[wParam], *reinterpret_cast<TCITEMW*>(lParam));
					return TRUE;
				}
				case TCM_SETITEMW:
				{
					if (wParam >= tabs->_tabs.size())
						return FALSE;
					writeItem(tabs->_tabs[wParam], *reinterpret_cast<const TCITEMW*>(lParam));
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TCM_INSERTITEMW:
				{
					Tab tab;
					writeItem(tab, *reinterpret_cast<const TCITEMW*>(lParam));
					const size_t index = std::min<size_t>(wParam, tabs->_tabs.size());
					tabs->_tabs.insert(tabs->_tabs.begin() + static_cast<std::ptrdiff_t>(index), tab);
					if (tabs->_selected < 0)
						tabs->_selected = 0;
					else if (static_cast<int>(index) <= tabs->_selected && tabs->_tabs.size() > 1)
						++tabs->_selected;
					InvalidateRect(hwnd, nullptr, TRUE);
					return static_cast<LRESULT>(index);
				}
				case TCM_DELETEITEM:
				{
					if (wParam >= tabs->_tabs.size())
						return FALSE;
					tabs->_tabs.erase(tabs->_tabs.begin() + static_cast<std::ptrdiff_t>(wParam));
					if (tabs->_tabs.empty())
						tabs->_selected = -1;
					else if (static_cast<int>(wParam) < tabs->_selected || tabs->_selected >= static_cast<int>(tabs->_tabs.size()))
						--tabs->_selected;
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TCM_DELETEALLITEMS:
					tabs->_tabs.clear();
					tabs->_selected = -1;
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				case TCM_GETCURSEL:
					return tabs->_selected;
				case TCM_SETCURSEL:
				{
					const int previous = tabs->_selected;
					if (static_cast<int>(wParam) >= static_cast<int>(tabs->_tabs.size()))
						return -1;
					tabs->_selected = static_cast<int>(wParam);
					InvalidateRect(hwnd, nullptr, TRUE);
					return previous;
				}
				case TCM_GETCURFOCUS:
					return tabs->_focus >= 0 ? tabs->_focus : tabs->_selected;
				case TCM_SETCURFOCUS:
					tabs->_focus = static_cast<int>(wParam);
					selectByUser(hwnd, *tabs, static_cast<int>(wParam));
					return 0;
				case TCM_GETITEMRECT:
				{
					layout(hwnd, *tabs);
					if (wParam >= tabs->_rects.size())
						return FALSE;
					*reinterpret_cast<RECT*>(lParam) = tabs->_rects[wParam];
					return TRUE;
				}
				case TCM_HITTEST:
				{
					auto* info = reinterpret_cast<TCHITTESTINFO*>(lParam);
					const int index = hitTest(hwnd, *tabs, info->pt);
					info->flags = index < 0 ? TCHT_NOWHERE : TCHT_ONITEM;
					return index;
				}
				case TCM_ADJUSTRECT:
				{
					// Between the whole control and its display area: the rows of tabs.
					layout(hwnd, *tabs);
					auto* rect = reinterpret_cast<RECT*>(lParam);
					const int tabsExtent = tabs->_rows * tabHeight(hwnd, *tabs);
					const bool vertical = (styleOf(hwnd) & TCS_VERTICAL) != 0;
					if (wParam)
					{
						if (vertical)
							rect->left -= tabsExtent;
						else
							rect->top -= tabsExtent;
					}
					else
					{
						if (vertical)
							rect->left += tabsExtent;
						else
							rect->top += tabsExtent;
					}
					return 0;
				}
				case TCM_SETITEMSIZE:
				{
					const LRESULT previous = MAKELONG(tabs->_fixedWidth, tabs->_fixedHeight);
					tabs->_fixedWidth = LOWORD(lParam);
					tabs->_fixedHeight = HIWORD(lParam);
					InvalidateRect(hwnd, nullptr, TRUE);
					return previous;
				}
				case TCM_SETPADDING:
					tabs->_paddingX = LOWORD(lParam);
					tabs->_paddingY = HIWORD(lParam);
					return 0;
				case TCM_SETMINTABWIDTH:
				{
					const int previous = tabs->_minWidth;
					tabs->_minWidth = static_cast<int>(lParam);
					return previous;
				}
				case TCM_GETROWCOUNT:
					layout(hwnd, *tabs);
					return tabs->_rows;
				case TCM_SETIMAGELIST:
				{
					HIMAGELIST previous = tabs->_images;
					tabs->_images = reinterpret_cast<HIMAGELIST>(lParam);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TCM_GETIMAGELIST:
					return reinterpret_cast<LRESULT>(tabs->_images);
				case TCM_SETTOOLTIPS:
					tabs->_tooltips = reinterpret_cast<HWND>(wParam);
					return 0;
				case TCM_GETTOOLTIPS:
					return reinterpret_cast<LRESULT>(tabs->_tooltips);
				case TCM_SETEXTENDEDSTYLE:
				case TCM_GETEXTENDEDSTYLE:
				case TCM_HIGHLIGHTITEM:
				case TCM_DESELECTALL:
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerTabControl()
	{
		registerControl(WC_TABCONTROLW, tabProc);
	}
}
