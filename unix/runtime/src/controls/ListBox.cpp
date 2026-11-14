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

// The list box control (ListBox), and the drop-down list of combo boxes (ComboLBox), which is the
// same list in a popup window that reports to its combo box. Single, multiple and extended
// selection, sorting, owner-drawn items (fixed and variable height), a vertical scroll bar drawn
// inside the client area, and the LB_* messages.

#include "ControlSupport.h"

#include <cwctype>

namespace nppmac::controls
{
	namespace
	{
		constexpr int borderWidth = 1;
		constexpr int scrollBarWidth = 12;
		constexpr int minimumThumb = 16;
		constexpr int wheelLines = 3;

		struct Item
		{
			std::u16string _text;
			ULONG_PTR _data = 0;
			bool _selected = false;
			int _height = 0; // LBS_OWNERDRAWVARIABLE
		};

		struct ListBox
		{
			std::vector<Item> _items;
			int _selection = LB_ERR;   // single selection
			int _caret = 0;            // the focused item
			int _anchor = 0;           // extended selection
			int _top = 0;              // the first visible item
			int _itemHeight = 0;
			bool _focused = false;
			bool _tracking = false;    // mouse selection in progress
			bool _draggingThumb = false;
			int _thumbGrab = 0;        // where the thumb was grabbed, from its top
			HWND _combo = nullptr;     // ComboLBox: the combo box it belongs to
			std::u16string _typed;     // incremental search
			DWORD _typedTime = 0;
		};

		LONG styleOf(HWND hwnd)
		{
			return GetWindowLongW(hwnd, GWL_STYLE);
		}

		bool hasStrings(HWND hwnd)
		{
			const LONG style = styleOf(hwnd);
			return !(style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (style & LBS_HASSTRINGS);
		}

		bool isMultiple(HWND hwnd)
		{
			return (styleOf(hwnd) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
		}

		bool isOwnerDraw(HWND hwnd)
		{
			return (styleOf(hwnd) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0;
		}

		HWND ownerOf(HWND hwnd, const ListBox& list)
		{
			return list._combo ? list._combo : GetParent(hwnd);
		}

		// The control id used in notifications and owner drawing: a combo list uses its combo's.
		UINT idOf(HWND hwnd, const ListBox& list)
		{
			return static_cast<UINT>(GetDlgCtrlID(list._combo ? list._combo : hwnd));
		}

		void notify(HWND hwnd, const ListBox& list, WORD code)
		{
			if (list._combo)
				return; // the combo box reports for its list
			if (styleOf(hwnd) & LBS_NOTIFY)
				sendCommand(hwnd, code);
		}

		RECT listArea(HWND hwnd)
		{
			RECT area{};
			GetClientRect(hwnd, &area);
			if ((styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
				InflateRect(&area, -borderWidth, -borderWidth);
			return area;
		}

		int itemHeight(const ListBox& list, int index)
		{
			if (index >= 0 && index < static_cast<int>(list._items.size()) && list._items[static_cast<size_t>(index)]._height > 0)
				return list._items[static_cast<size_t>(index)]._height;
			return list._itemHeight;
		}

		int visibleCount(HWND hwnd, const ListBox& list)
		{
			const RECT area = listArea(hwnd);
			int height = area.bottom - area.top;
			int count = 0;
			for (int i = list._top; i < static_cast<int>(list._items.size()); ++i)
			{
				height -= itemHeight(list, i);
				if (height < 0)
					break;
				++count;
			}
			return std::max(1, count);
		}

		bool needsScrollBar(HWND hwnd, const ListBox& list)
		{
			if (!(styleOf(hwnd) & WS_VSCROLL))
				return false;
			const RECT area = listArea(hwnd);
			int total = 0;
			for (int i = 0; i < static_cast<int>(list._items.size()); ++i)
				total += itemHeight(list, i);
			return total > area.bottom - area.top || (styleOf(hwnd) & LBS_DISABLENOSCROLL);
		}

		RECT itemsArea(HWND hwnd, const ListBox& list)
		{
			RECT area = listArea(hwnd);
			if (needsScrollBar(hwnd, list))
				area.right -= scrollBarWidth;
			return area;
		}

		RECT itemRect(HWND hwnd, const ListBox& list, int index)
		{
			const RECT area = itemsArea(hwnd, list);
			int top = area.top;
			if (index >= list._top)
			{
				for (int i = list._top; i < index; ++i)
					top += itemHeight(list, i);
			}
			else
			{
				for (int i = index; i < list._top; ++i)
					top -= itemHeight(list, i);
			}
			return {area.left, top, area.right, top + itemHeight(list, index)};
		}

		int maximumTop(HWND hwnd, const ListBox& list)
		{
			// The last top that still fills the list.
			const RECT area = listArea(hwnd);
			int height = area.bottom - area.top;
			int top = static_cast<int>(list._items.size());
			while (top > 0 && height - itemHeight(list, top - 1) >= 0)
			{
				height -= itemHeight(list, top - 1);
				--top;
			}
			return std::max(0, top);
		}

		void setTop(HWND hwnd, ListBox& list, int top)
		{
			top = std::clamp(top, 0, maximumTop(hwnd, list));
			if (top != list._top)
			{
				list._top = top;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
		}

		void ensureVisible(HWND hwnd, ListBox& list, int index)
		{
			if (index < 0 || index >= static_cast<int>(list._items.size()))
				return;
			if (index < list._top)
				setTop(hwnd, list, index);
			else if (index >= list._top + visibleCount(hwnd, list))
				setTop(hwnd, list, index - visibleCount(hwnd, list) + 1);
		}

		// The scroll bar: its track and thumb, in client coordinates.
		void scrollBarGeometry(HWND hwnd, const ListBox& list, RECT& track, RECT& thumb)
		{
			const RECT area = listArea(hwnd);
			track = {area.right - scrollBarWidth, area.top, area.right, area.bottom};
			const int count = static_cast<int>(list._items.size());
			const int visible = visibleCount(hwnd, list);
			const int trackHeight = track.bottom - track.top;
			if (count <= visible)
			{
				thumb = {};
				return;
			}
			const int thumbHeight = std::max(minimumThumb, trackHeight * visible / count);
			const int range = std::max(1, maximumTop(hwnd, list));
			const int thumbTop = track.top + (trackHeight - thumbHeight) * list._top / range;
			thumb = {track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight};
		}

		int indexAt(HWND hwnd, const ListBox& list, POINT point)
		{
			const RECT area = itemsArea(hwnd, list);
			if (point.y < area.top)
				return std::max(0, list._top - 1);
			int top = area.top;
			for (int i = list._top; i < static_cast<int>(list._items.size()); ++i)
			{
				const int height = itemHeight(list, i);
				if (point.y < top + height)
					return i;
				top += height;
			}
			return static_cast<int>(list._items.size()) - 1;
		}

		void measure(HWND hwnd, ListBox& list, int index)
		{
			const LONG style = styleOf(hwnd);
			if (!(style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)))
				return;
			if ((style & LBS_OWNERDRAWFIXED) && index >= 0)
				return;
			MEASUREITEMSTRUCT measured{};
			measured.CtlType = ODT_LISTBOX;
			measured.CtlID = idOf(hwnd, list);
			measured.itemID = static_cast<UINT>(std::max(0, index));
			measured.itemHeight = static_cast<UINT>(list._itemHeight);
			RECT client{};
			GetClientRect(hwnd, &client);
			measured.itemWidth = static_cast<UINT>(client.right);
			if (index >= 0)
				measured.itemData = list._items[static_cast<size_t>(index)]._data;
			SendMessageW(ownerOf(hwnd, list), WM_MEASUREITEM, measured.CtlID, reinterpret_cast<LPARAM>(&measured));
			if (index < 0)
				list._itemHeight = static_cast<int>(std::max<UINT>(1, measured.itemHeight));
			else
				list._items[static_cast<size_t>(index)]._height = static_cast<int>(std::max<UINT>(1, measured.itemHeight));
		}

		void drawItem(HWND hwnd, ListBox& list, HDC hdc, int index, const RECT& rect)
		{
			const Item& item = list._items[static_cast<size_t>(index)];
			const bool selected = isMultiple(hwnd) ? item._selected : index == list._selection;
			const bool enabled = IsWindowEnabled(hwnd) != FALSE;
			if (isOwnerDraw(hwnd))
			{
				DRAWITEMSTRUCT draw{};
				draw.CtlType = ODT_LISTBOX;
				draw.CtlID = idOf(hwnd, list);
				draw.itemID = static_cast<UINT>(index);
				draw.itemAction = ODA_DRAWENTIRE;
				draw.itemState = (selected ? ODS_SELECTED : 0) | (list._focused && index == list._caret ? ODS_FOCUS : 0) | (enabled ? 0 : ODS_DISABLED);
				draw.hwndItem = hwnd;
				draw.hDC = hdc;
				draw.rcItem = rect;
				draw.itemData = item._data;
				SendMessageW(ownerOf(hwnd, list), WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
				return;
			}
			const COLORREF normalText = GetTextColor(hdc);
			if (selected)
			{
				FillRect(hdc, &rect, GetSysColorBrush(COLOR_HIGHLIGHT));
				SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
			}
			else if (!enabled)
			{
				SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
			}
			RECT text{rect.left + 2, rect.top, rect.right - 2, rect.bottom};
			const UINT format = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | ((styleOf(hwnd) & LBS_USETABSTOPS) ? DT_EXPANDTABS : 0);
			DrawTextW(hdc, item._text.c_str(), static_cast<int>(item._text.size()), &text, format);
			SetTextColor(hdc, normalText);
			if (list._focused && index == list._caret && isMultiple(hwnd))
				DrawFocusRect(hdc, &rect);
		}

		void paint(HWND hwnd, ListBox& list, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			HBRUSH background = controlColors(hwnd, hdc, WM_CTLCOLORLISTBOX);
			FillRect(hdc, &client, background);
			if ((styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
			{
				HBRUSH frame = CreateSolidBrush(controlPalette()._frame);
				FrameRect(hdc, &client, frame);
				DeleteObject(frame);
			}

			const RECT area = itemsArea(hwnd, list);
			const int itemsClip = SaveDC(hdc);
			IntersectClipRect(hdc, area.left, area.top, area.right, area.bottom);
			int top = area.top;
			for (int i = list._top; i < static_cast<int>(list._items.size()) && top < area.bottom; ++i)
			{
				const RECT rect{area.left, top, area.right, top + itemHeight(list, i)};
				drawItem(hwnd, list, hdc, i, rect);
				top = rect.bottom;
			}
			RestoreDC(hdc, itemsClip);

			if (needsScrollBar(hwnd, list))
			{
				RECT track{};
				RECT thumb{};
				scrollBarGeometry(hwnd, list, track, thumb);
				HBRUSH trackBrush = CreateSolidBrush(controlPalette()._track);
				FillRect(hdc, &track, trackBrush);
				DeleteObject(trackBrush);
				if (!IsRectEmpty(&thumb))
				{
					HBRUSH thumbBrush = CreateSolidBrush(list._draggingThumb ? controlPalette()._thumbActive : controlPalette()._thumb);
					FillRect(hdc, &thumb, thumbBrush);
					DeleteObject(thumbBrush);
				}
			}
			RestoreDC(hdc, saved);
		}

		int insertItem(HWND hwnd, ListBox& list, int index, const std::u16string& text, ULONG_PTR data)
		{
			Item item;
			if (hasStrings(hwnd))
				item._text = text;
			item._data = data;
			if ((styleOf(hwnd) & LBS_SORT) && hasStrings(hwnd))
			{
				index = 0;
				while (index < static_cast<int>(list._items.size()) && lstrcmpiW(list._items[static_cast<size_t>(index)]._text.c_str(), text.c_str()) <= 0)
					++index;
			}
			else if (index < 0 || index > static_cast<int>(list._items.size()))
			{
				index = static_cast<int>(list._items.size());
			}
			list._items.insert(list._items.begin() + index, item);
			if (list._selection != LB_ERR && list._selection >= index)
				++list._selection;
			measure(hwnd, list, index);
			InvalidateRect(hwnd, nullptr, FALSE);
			return index;
		}

		int findString(const ListBox& list, int start, const char16_t* text, bool exact)
		{
			const int count = static_cast<int>(list._items.size());
			if (count == 0 || !text)
				return LB_ERR;
			const size_t length = std::u16string_view{text}.size();
			for (int n = 1; n <= count; ++n)
			{
				const int i = (start + n + count) % count;
				const std::u16string& candidate = list._items[static_cast<size_t>(i)]._text;
				const bool found = exact ? lstrcmpiW(candidate.c_str(), text) == 0
					: candidate.size() >= length && CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, candidate.c_str(), static_cast<int>(length), text, static_cast<int>(length)) == CSTR_EQUAL;
				if (found)
					return i;
			}
			return LB_ERR;
		}

		void selectSingle(HWND hwnd, ListBox& list, int index)
		{
			if (index == list._selection)
				return;
			list._selection = index;
			list._caret = std::max(0, index);
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		// What the user did: select the item (and with Shift or Ctrl, ranges and toggles).
		void userSelect(HWND hwnd, ListBox& list, int index, bool shift, bool control)
		{
			if (index < 0 || index >= static_cast<int>(list._items.size()))
				return;
			const LONG style = styleOf(hwnd);
			if (style & LBS_NOSEL)
				return;
			if (style & LBS_EXTENDEDSEL)
			{
				if (shift)
				{
					if (!control)
					{
						for (Item& item : list._items)
							item._selected = false;
					}
					const int from = std::min(list._anchor, index);
					const int to = std::max(list._anchor, index);
					for (int i = from; i <= to; ++i)
						list._items[static_cast<size_t>(i)]._selected = true;
				}
				else if (control)
				{
					list._items[static_cast<size_t>(index)]._selected = !list._items[static_cast<size_t>(index)]._selected;
					list._anchor = index;
				}
				else
				{
					for (Item& item : list._items)
						item._selected = false;
					list._items[static_cast<size_t>(index)]._selected = true;
					list._anchor = index;
				}
				list._caret = index;
			}
			else if (style & LBS_MULTIPLESEL)
			{
				list._items[static_cast<size_t>(index)]._selected = !list._items[static_cast<size_t>(index)]._selected;
				list._caret = index;
			}
			else
			{
				const bool changed = index != list._selection;
				selectSingle(hwnd, list, index);
				ensureVisible(hwnd, list, index);
				if (changed)
					notify(hwnd, list, LBN_SELCHANGE);
				return;
			}
			ensureVisible(hwnd, list, index);
			InvalidateRect(hwnd, nullptr, FALSE);
			notify(hwnd, list, LBN_SELCHANGE);
		}

		void keyDown(HWND hwnd, ListBox& list, WPARAM key)
		{
			const int count = static_cast<int>(list._items.size());
			if (count == 0)
				return;
			const int current = isMultiple(hwnd) ? list._caret : (list._selection == LB_ERR ? -1 : list._selection);
			const int page = std::max(1, visibleCount(hwnd, list) - 1);
			int target = current;
			switch (key)
			{
				case VK_UP:
					target = std::max(0, current - 1);
					break;
				case VK_DOWN:
					target = std::min(count - 1, current + 1);
					break;
				case VK_PRIOR:
					target = std::max(0, current - page);
					break;
				case VK_NEXT:
					target = std::min(count - 1, current + page);
					break;
				case VK_HOME:
					target = 0;
					break;
				case VK_END:
					target = count - 1;
					break;
				case VK_SPACE:
					if (styleOf(hwnd) & LBS_MULTIPLESEL)
						userSelect(hwnd, list, list._caret, false, false);
					return;
				default:
					return;
			}
			if (styleOf(hwnd) & LBS_MULTIPLESEL)
			{
				// Multiple selection moves the focus only.
				list._caret = target;
				ensureVisible(hwnd, list, target);
				InvalidateRect(hwnd, nullptr, FALSE);
				return;
			}
			userSelect(hwnd, list, target, (GetKeyState(VK_SHIFT) & 0x8000) != 0, false);
		}

		// Typing selects the next item starting with what was typed.
		void typeAhead(HWND hwnd, ListBox& list, char16_t ch)
		{
			if (ch < 0x20 || !hasStrings(hwnd))
				return;
			const DWORD now = GetTickCount();
			if (now - list._typedTime > 1000)
				list._typed.clear();
			list._typedTime = now;
			list._typed += ch;
			const int start = list._typed.size() == 1 ? (list._selection == LB_ERR ? -1 : list._selection) : std::max(0, list._selection) - 1;
			const int found = findString(list, start, list._typed.c_str(), false);
			if (found != LB_ERR)
				userSelect(hwnd, list, found, false, false);
		}

		LRESULT copyText(const std::u16string& text, LPARAM buffer)
		{
			if (!buffer)
				return LB_ERR;
			auto* out = reinterpret_cast<char16_t*>(buffer);
			std::copy(text.begin(), text.end(), out);
			out[text.size()] = u'\0';
			return static_cast<LRESULT>(text.size());
		}

		bool validIndex(const ListBox& list, WPARAM index)
		{
			return static_cast<int>(index) >= 0 && static_cast<size_t>(static_cast<int>(index)) < list._items.size();
		}

		LRESULT CALLBACK listBoxProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			ListBox* list = dataOf<ListBox>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					ListBox* created = createData<ListBox>(hwnd);
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					char16_t className[16]{};
					GetClassNameW(hwnd, className, 16);
					if (_wcsicmp(className, u"ComboLBox") == 0)
						created->_combo = static_cast<HWND>(create->lpCreateParams);
					created->_itemHeight = fontHeight(hwnd) + 2;
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_CREATE:
					measure(hwnd, *list, -1);
					return 0;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *list, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *list, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SETFONT:
					DefWindowProcW(hwnd, message, wParam, lParam);
					if (!(styleOf(hwnd) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)))
						list->_itemHeight = fontHeight(hwnd) + 2;
					if (LOWORD(lParam))
						InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_SIZE:
				case WM_ENABLE:
					setTop(hwnd, *list, list->_top);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_GETDLGCODE:
					return DLGC_WANTARROWS | DLGC_WANTCHARS;
				case WM_SETFOCUS:
					list->_focused = true;
					InvalidateRect(hwnd, nullptr, FALSE);
					notify(hwnd, *list, LBN_SETFOCUS);
					return 0;
				case WM_KILLFOCUS:
					list->_focused = false;
					InvalidateRect(hwnd, nullptr, FALSE);
					notify(hwnd, *list, LBN_KILLFOCUS);
					return 0;
				case WM_KEYDOWN:
					keyDown(hwnd, *list, wParam);
					return 0;
				case WM_CHAR:
					typeAhead(hwnd, *list, static_cast<char16_t>(wParam));
					return 0;
				case WM_MOUSEWHEEL:
					setTop(hwnd, *list, list->_top - GET_WHEEL_DELTA_WPARAM(wParam) * wheelLines / WHEEL_DELTA);
					return 0;
				case WM_LBUTTONDOWN:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (list->_combo)
					{
						// A click outside the drop-down list closes it.
						RECT client{};
						GetClientRect(hwnd, &client);
						if (!PtInRect(&client, point))
						{
							SendMessageW(list->_combo, CB_SHOWDROPDOWN, FALSE, 0);
							return 0;
						}
					}
					if (needsScrollBar(hwnd, *list))
					{
						RECT track{};
						RECT thumb{};
						scrollBarGeometry(hwnd, *list, track, thumb);
						if (PtInRect(&track, point))
						{
							if (PtInRect(&thumb, point))
							{
								list->_draggingThumb = true;
								list->_thumbGrab = point.y - thumb.top;
								SetCapture(hwnd);
							}
							else
							{
								const int page = std::max(1, visibleCount(hwnd, *list) - 1);
								setTop(hwnd, *list, list->_top + (point.y < thumb.top ? -page : page));
							}
							InvalidateRect(hwnd, nullptr, FALSE);
							return 0;
						}
					}
					if (!list->_combo && GetFocus() != hwnd)
						SetFocus(hwnd);
					if (!list->_items.empty())
						userSelect(hwnd, *list, indexAt(hwnd, *list, point), (wParam & MK_SHIFT) != 0, (wParam & MK_CONTROL) != 0);
					list->_tracking = true;
					if (!list->_combo)
						SetCapture(hwnd);
					return 0;
				}
				case WM_MOUSEMOVE:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (list->_draggingThumb)
					{
						RECT track{};
						RECT thumb{};
						scrollBarGeometry(hwnd, *list, track, thumb);
						const int free = (track.bottom - track.top) - (thumb.bottom - thumb.top);
						if (free > 0)
							setTop(hwnd, *list, (point.y - list->_thumbGrab - track.top) * maximumTop(hwnd, *list) / free);
						return 0;
					}
					// A drop-down list follows the mouse; a list box follows a drag.
					RECT area = itemsArea(hwnd, *list);
					if (list->_combo && PtInRect(&area, point) && !list->_items.empty())
						selectSingle(hwnd, *list, indexAt(hwnd, *list, point));
					else if (list->_tracking && !list->_combo && !isMultiple(hwnd) && !list->_items.empty())
						userSelect(hwnd, *list, indexAt(hwnd, *list, point), false, false);
					return 0;
				}
				case WM_LBUTTONUP:
				{
					if (list->_draggingThumb)
					{
						list->_draggingThumb = false;
						ReleaseCapture();
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					const bool wasTracking = list->_tracking;
					list->_tracking = false;
					if (list->_combo)
					{
						const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
						RECT area = itemsArea(hwnd, *list);
						if (wasTracking && PtInRect(&area, point))
							SendMessageW(list->_combo, comboListChosen, static_cast<WPARAM>(list->_selection), 0);
						return 0;
					}
					if (GetCapture() == hwnd)
						ReleaseCapture();
					return 0;
				}
				case WM_LBUTTONDBLCLK:
					if (!list->_combo)
					{
						notify(hwnd, *list, LBN_DBLCLK);
						return 0;
					}
					return SendMessageW(hwnd, WM_LBUTTONDOWN, wParam, lParam);
				case WM_CAPTURECHANGED:
					list->_tracking = false;
					list->_draggingThumb = false;
					return 0;
				case LB_ADDSTRING:
				case LB_INSERTSTRING:
				{
					const bool strings = hasStrings(hwnd);
					const std::u16string text = strings && lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"";
					const ULONG_PTR data = strings ? 0 : static_cast<ULONG_PTR>(lParam);
					const int index = message == LB_ADDSTRING ? -1 : static_cast<int>(wParam);
					if (message == LB_INSERTSTRING && styleOf(hwnd) & LBS_SORT)
					{
						// LB_INSERTSTRING does not sort.
						const LONG style = styleOf(hwnd);
						SetWindowLongW(hwnd, GWL_STYLE, style & ~LBS_SORT);
						const int inserted = insertItem(hwnd, *list, index, text, data);
						SetWindowLongW(hwnd, GWL_STYLE, style);
						return inserted;
					}
					return insertItem(hwnd, *list, index, text, data);
				}
				case LB_DELETESTRING:
				{
					if (!validIndex(*list, wParam))
						return LB_ERR;
					const int index = static_cast<int>(wParam);
					const Item removed = list->_items[static_cast<size_t>(index)];
					if (isOwnerDraw(hwnd))
					{
						DELETEITEMSTRUCT deleted{ODT_LISTBOX, idOf(hwnd, *list), static_cast<UINT>(index), hwnd, removed._data};
						SendMessageW(ownerOf(hwnd, *list), WM_DELETEITEM, deleted.CtlID, reinterpret_cast<LPARAM>(&deleted));
					}
					list->_items.erase(list->_items.begin() + index);
					if (list->_selection == index)
						list->_selection = LB_ERR;
					else if (list->_selection > index)
						--list->_selection;
					list->_caret = std::clamp(list->_caret, 0, std::max(0, static_cast<int>(list->_items.size()) - 1));
					setTop(hwnd, *list, list->_top);
					InvalidateRect(hwnd, nullptr, FALSE);
					return static_cast<LRESULT>(list->_items.size());
				}
				case LB_RESETCONTENT:
					list->_items.clear();
					list->_selection = LB_ERR;
					list->_caret = list->_anchor = list->_top = 0;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case LB_GETCOUNT:
					return static_cast<LRESULT>(list->_items.size());
				case LB_GETTEXT:
					if (!validIndex(*list, wParam))
						return LB_ERR;
					if (!hasStrings(hwnd))
					{
						*reinterpret_cast<ULONG_PTR*>(lParam) = list->_items[wParam]._data;
						return sizeof(ULONG_PTR);
					}
					return copyText(list->_items[wParam]._text, lParam);
				case LB_GETTEXTLEN:
					if (!validIndex(*list, wParam))
						return LB_ERR;
					return hasStrings(hwnd) ? static_cast<LRESULT>(list->_items[wParam]._text.size()) : sizeof(ULONG_PTR);
				case LB_SETITEMDATA:
					if (!validIndex(*list, wParam))
						return LB_ERR;
					list->_items[wParam]._data = static_cast<ULONG_PTR>(lParam);
					return TRUE;
				case LB_GETITEMDATA:
					if (!validIndex(*list, wParam))
						return LB_ERR;
					return static_cast<LRESULT>(list->_items[wParam]._data);
				case LB_SETCURSEL:
				{
					if (isMultiple(hwnd))
						return LB_ERR;
					const int index = validIndex(*list, wParam) ? static_cast<int>(wParam) : LB_ERR;
					selectSingle(hwnd, *list, index);
					ensureVisible(hwnd, *list, index);
					return index == LB_ERR && static_cast<int>(wParam) != -1 ? LB_ERR : index;
				}
				case LB_GETCURSEL:
					return isMultiple(hwnd) ? list->_caret : list->_selection;
				case LB_SETSEL:
				{
					if (!isMultiple(hwnd))
						return LB_ERR;
					const int index = static_cast<int>(lParam);
					if (index == -1)
					{
						for (Item& item : list->_items)
							item._selected = wParam != 0;
					}
					else if (validIndex(*list, static_cast<WPARAM>(index)))
					{
						list->_items[static_cast<size_t>(index)]._selected = wParam != 0;
					}
					else
					{
						return LB_ERR;
					}
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case LB_GETSEL:
					if (!validIndex(*list, wParam))
						return LB_ERR;
					return isMultiple(hwnd) ? list->_items[wParam]._selected : static_cast<int>(wParam) == list->_selection;
				case LB_GETSELCOUNT:
				{
					if (!isMultiple(hwnd))
						return LB_ERR;
					return std::count_if(list->_items.begin(), list->_items.end(), [](const Item& item) { return item._selected; });
				}
				case LB_GETSELITEMS:
				{
					if (!isMultiple(hwnd))
						return LB_ERR;
					auto* out = reinterpret_cast<int*>(lParam);
					int count = 0;
					for (int i = 0; i < static_cast<int>(list->_items.size()) && count < static_cast<int>(wParam); ++i)
					{
						if (list->_items[static_cast<size_t>(i)]._selected)
							out[count++] = i;
					}
					return count;
				}
				case LB_SELITEMRANGE:
				case LB_SELITEMRANGEEX:
				{
					if (!isMultiple(hwnd))
						return LB_ERR;
					int first = message == LB_SELITEMRANGE ? LOWORD(lParam) : static_cast<int>(wParam);
					int last = message == LB_SELITEMRANGE ? HIWORD(lParam) : static_cast<int>(lParam);
					bool select = message == LB_SELITEMRANGE ? wParam != 0 : first <= last;
					if (first > last)
						std::swap(first, last);
					for (int i = std::max(0, first); i <= last && i < static_cast<int>(list->_items.size()); ++i)
						list->_items[static_cast<size_t>(i)]._selected = select;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case LB_FINDSTRING:
				case LB_FINDSTRINGEXACT:
					return findString(*list, static_cast<int>(wParam), reinterpret_cast<LPCWSTR>(lParam), message == LB_FINDSTRINGEXACT);
				case LB_SELECTSTRING:
				{
					const int found = findString(*list, static_cast<int>(wParam), reinterpret_cast<LPCWSTR>(lParam), false);
					if (found != LB_ERR && !isMultiple(hwnd))
					{
						selectSingle(hwnd, *list, found);
						ensureVisible(hwnd, *list, found);
					}
					return found;
				}
				case LB_SETTOPINDEX:
					if (!validIndex(*list, wParam) && !list->_items.empty())
						return LB_ERR;
					setTop(hwnd, *list, static_cast<int>(wParam));
					return 0;
				case LB_GETTOPINDEX:
					return list->_top;
				case LB_SETITEMHEIGHT:
				{
					const int height = LOWORD(lParam);
					if (height <= 0)
						return LB_ERR;
					if ((styleOf(hwnd) & LBS_OWNERDRAWVARIABLE) && validIndex(*list, wParam))
						list->_items[wParam]._height = height;
					else
						list->_itemHeight = height;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case LB_GETITEMHEIGHT:
					return itemHeight(*list, static_cast<int>(wParam));
				case LB_GETITEMRECT:
				{
					if (!validIndex(*list, wParam) && !(wParam == 0 && list->_items.empty()))
						return LB_ERR;
					*reinterpret_cast<RECT*>(lParam) = itemRect(hwnd, *list, static_cast<int>(wParam));
					return 1;
				}
				case LB_ITEMFROMPOINT:
				{
					if (list->_items.empty())
						return MAKELRESULT(0, 1);
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					RECT area = itemsArea(hwnd, *list);
					return MAKELRESULT(indexAt(hwnd, *list, point), PtInRect(&area, point) ? 0 : 1);
				}
				case LB_SETCARETINDEX:
					if (!validIndex(*list, wParam))
						return LB_ERR;
					list->_caret = static_cast<int>(wParam);
					ensureVisible(hwnd, *list, list->_caret);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case LB_GETCARETINDEX:
					return list->_caret;
				case LB_SETANCHORINDEX:
					list->_anchor = static_cast<int>(wParam);
					return 0;
				case LB_GETANCHORINDEX:
					return list->_anchor;
				case LB_INITSTORAGE:
					list->_items.reserve(list->_items.size() + wParam);
					return static_cast<LRESULT>(list->_items.capacity());
				case LB_SETHORIZONTALEXTENT:
				case LB_SETCOLUMNWIDTH:
					return 0;
				case LB_GETHORIZONTALEXTENT:
					return 0;
				case LB_SETTABSTOPS:
					return TRUE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerListBox()
	{
		registerControl(WC_LISTBOXW, listBoxProc);
		registerControl(u"ComboLBox", listBoxProc, CS_DBLCLKS | CS_SAVEBITS);
	}
}
