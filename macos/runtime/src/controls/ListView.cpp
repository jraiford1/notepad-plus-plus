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

// The list view control (SysListView32), in the report view Notepad++ uses (other views show as a
// one-column report): columns in a Header child, items with sub-item texts, images, state and data,
// virtual lists (LVS_OWNERDATA), single and multiple selection, check boxes, sorting, custom and owner
// drawing, and a vertical scroll bar. Groups are accepted and not shown.

#include "ScrollBar.h"

#include <cwctype>

namespace nppmac::controls
{
	void registerHeader();

	namespace
	{
		constexpr int cellMargin = 6;
		constexpr int checkBoxSize = 13;
		constexpr int imageGap = 3;
		constexpr int wheelRows = 3;
		constexpr int horizontalWheelStep = 30;
		constexpr int dragThreshold = 4;

		struct Item
		{
			std::vector<std::u16string> _texts;     // the item's, then its sub-items'
			bool _textCallback = false;
			int _image = I_IMAGECALLBACK;
			UINT _state = 0;
			LPARAM _param = 0;
			int _indent = 0;
		};

		struct ListView
		{
			std::vector<Item> _items;
			int _virtualCount = 0;                 // LVS_OWNERDATA: items live with the parent
			std::vector<bool> _virtualSelected;
			int _focused = -1;
			int _selectionMark = -1;
			HWND _header = nullptr;
			DWORD _extendedStyle = 0;
			HIMAGELIST _smallImages = nullptr;
			HIMAGELIST _normalImages = nullptr;
			HIMAGELIST _stateImages = nullptr;
			COLORREF _background = CLR_NONE;
			COLORREF _textColor = CLR_NONE;
			COLORREF _textBackground = CLR_NONE;
			UINT _callbackMask = 0;
			int _top = 0;
			int _scrollX = 0;
			int _itemHeight = 0;
			bool _hasFocus = false;
			int _pressed = -1;                     // the mouse went down on it; a move may start a drag
			POINT _pressPoint{};
			VerticalScrollBar _scrollBar;
			std::u16string _typed;
			DWORD _typedTime = 0;
		};

		LONG styleOf(HWND hwnd)
		{
			return GetWindowLongW(hwnd, GWL_STYLE);
		}

		bool isVirtual(HWND hwnd)
		{
			return (styleOf(hwnd) & LVS_OWNERDATA) != 0;
		}

		bool isSingleSelection(HWND hwnd)
		{
			return (styleOf(hwnd) & LVS_SINGLESEL) != 0;
		}

		int itemCount(HWND hwnd, const ListView& list)
		{
			return isVirtual(hwnd) ? list._virtualCount : static_cast<int>(list._items.size());
		}

		bool validIndex(HWND hwnd, const ListView& list, int index)
		{
			return index >= 0 && index < itemCount(hwnd, list);
		}

		HWND parentOf(HWND hwnd)
		{
			return GetParent(hwnd);
		}

		// --- Columns (the header's items) ----------------------------------------------------------

		int columnCount(const ListView& list)
		{
			return list._header ? static_cast<int>(SendMessageW(list._header, HDM_GETITEMCOUNT, 0, 0)) : 0;
		}

		int columnWidth(const ListView& list, int column)
		{
			HDITEMW item{};
			item.mask = HDI_WIDTH;
			if (!list._header || !SendMessageW(list._header, HDM_GETITEMW, static_cast<WPARAM>(column), reinterpret_cast<LPARAM>(&item)))
				return 0;
			return item.cxy;
		}

		int columnFormat(const ListView& list, int column)
		{
			HDITEMW item{};
			item.mask = HDI_FORMAT;
			SendMessageW(list._header, HDM_GETITEMW, static_cast<WPARAM>(column), reinterpret_cast<LPARAM>(&item));
			return item.fmt;
		}

		bool showsHeader(HWND hwnd)
		{
			const LONG style = styleOf(hwnd);
			return (style & LVS_TYPEMASK) == LVS_REPORT && !(style & LVS_NOCOLUMNHEADER);
		}

		// --- Geometry ------------------------------------------------------------------------------

		RECT clientArea(HWND hwnd)
		{
			RECT area{};
			GetClientRect(hwnd, &area);
			if ((styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
				InflateRect(&area, -1, -1);
			return area;
		}

		int headerHeight(HWND hwnd, const ListView& list)
		{
			if (!list._header || !showsHeader(hwnd))
				return 0;
			RECT rect{};
			GetWindowRect(list._header, &rect);
			return rect.bottom - rect.top;
		}

		RECT rowsArea(HWND hwnd, ListView& list)
		{
			RECT area = clientArea(hwnd);
			area.top += headerHeight(hwnd, list);
			const int page = std::max(1, static_cast<int>(area.bottom - area.top) / std::max(1, list._itemHeight));
			list._scrollBar.update({area.right - VerticalScrollBar::width, area.top, area.right, area.bottom}, itemCount(hwnd, list), page, list._top);
			if (list._scrollBar.isNeeded() && !(styleOf(hwnd) & LVS_NOSCROLL))
				area.right -= VerticalScrollBar::width;
			return area;
		}

		int visibleRows(HWND hwnd, ListView& list)
		{
			const RECT area = rowsArea(hwnd, list);
			return std::max(1, static_cast<int>(area.bottom - area.top) / std::max(1, list._itemHeight));
		}

		void layoutHeader(HWND hwnd, ListView& list)
		{
			if (!list._header)
				return;
			if (!showsHeader(hwnd))
			{
				ShowWindow(list._header, SW_HIDE);
				return;
			}
			RECT area = clientArea(hwnd);
			WINDOWPOS position{};
			HDLAYOUT layout{&area, &position};
			SendMessageW(list._header, HDM_LAYOUT, 0, reinterpret_cast<LPARAM>(&layout));
			SetWindowPos(list._header, nullptr, position.x, position.y, position.cx, position.cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		}

		void setTop(HWND hwnd, ListView& list, int top)
		{
			const int maximum = std::max(0, itemCount(hwnd, list) - visibleRows(hwnd, list));
			top = std::clamp(top, 0, maximum);
			if (top != list._top)
			{
				list._top = top;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
		}

		void setScrollX(HWND hwnd, ListView& list, int x)
		{
			int total = 0;
			for (int column = 0; column < columnCount(list); ++column)
				total += columnWidth(list, column);
			const RECT area = rowsArea(hwnd, list);
			x = std::clamp(x, 0, std::max(0, total - static_cast<int>(area.right - area.left)));
			if (x == list._scrollX)
				return;
			list._scrollX = x;
			if (list._header)
				SendMessageW(list._header, headerScrolled, static_cast<WPARAM>(x), 0);
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		void ensureVisible(HWND hwnd, ListView& list, int index)
		{
			if (!validIndex(hwnd, list, index))
				return;
			if (index < list._top)
				setTop(hwnd, list, index);
			else if (index >= list._top + visibleRows(hwnd, list))
				setTop(hwnd, list, index - visibleRows(hwnd, list) + 1);
		}

		RECT rowRect(HWND hwnd, ListView& list, int index)
		{
			const RECT area = rowsArea(hwnd, list);
			const int top = area.top + (index - list._top) * list._itemHeight;
			return {area.left, top, area.right, top + list._itemHeight};
		}

		// A column's cell in a row, in client coordinates; with no columns, the whole row.
		RECT cellRect(HWND hwnd, ListView& list, int index, int column)
		{
			RECT row = rowRect(hwnd, list, index);
			if (columnCount(list) == 0)
				return row;
			int x = row.left - list._scrollX;
			for (int i = 0; i < column; ++i)
				x += columnWidth(list, i);
			return {x, row.top, x + columnWidth(list, column), row.bottom};
		}

		int smallImageSize(const ListView& list)
		{
			int width = 0;
			int height = 0;
			if (list._smallImages)
				ImageList_GetIconSize(list._smallImages, &width, &height);
			return width;
		}

		// --- Items ---------------------------------------------------------------------------------

		UINT stateOf(HWND hwnd, const ListView& list, int index)
		{
			if (!validIndex(hwnd, list, index))
				return 0;
			if (isVirtual(hwnd))
			{
				UINT state = 0;
				if (static_cast<size_t>(index) < list._virtualSelected.size() && list._virtualSelected[static_cast<size_t>(index)])
					state |= LVIS_SELECTED;
				if (index == list._focused)
					state |= LVIS_FOCUSED;
				return state;
			}
			UINT state = list._items[static_cast<size_t>(index)]._state & ~LVIS_FOCUSED;
			if (index == list._focused)
				state |= LVIS_FOCUSED;
			return state;
		}

		// The text of an item or sub-item, asking the parent for callback texts and virtual items.
		std::u16string textOf(HWND hwnd, ListView& list, int index, int subItem)
		{
			const bool callback = isVirtual(hwnd) || list._items[static_cast<size_t>(index)]._textCallback || subItem > 0;
			if (!isVirtual(hwnd))
			{
				const Item& item = list._items[static_cast<size_t>(index)];
				if (static_cast<size_t>(subItem) < item._texts.size() && !(subItem == 0 && item._textCallback))
					return item._texts[static_cast<size_t>(subItem)];
				if (!callback || subItem > 0)
					return {};
			}
			char16_t buffer[1024]{};
			NMLVDISPINFOW info{};
			info.item.mask = LVIF_TEXT;
			info.item.iItem = index;
			info.item.iSubItem = subItem;
			info.item.pszText = buffer;
			info.item.cchTextMax = 1024;
			if (!isVirtual(hwnd))
				info.item.lParam = list._items[static_cast<size_t>(index)]._param;
			sendNotify(hwnd, parentOf(hwnd), LVN_GETDISPINFOW, &info.hdr);
			return info.item.pszText ? info.item.pszText : u"";
		}

		int imageOf(HWND hwnd, ListView& list, int index)
		{
			if (!isVirtual(hwnd) && list._items[static_cast<size_t>(index)]._image != I_IMAGECALLBACK)
				return list._items[static_cast<size_t>(index)]._image;
			NMLVDISPINFOW info{};
			info.item.mask = LVIF_IMAGE;
			info.item.iItem = index;
			info.item.iImage = I_IMAGENONE;
			if (!isVirtual(hwnd))
				info.item.lParam = list._items[static_cast<size_t>(index)]._param;
			sendNotify(hwnd, parentOf(hwnd), LVN_GETDISPINFOW, &info.hdr);
			return info.item.iImage;
		}

		LPARAM paramOf(HWND hwnd, const ListView& list, int index)
		{
			return isVirtual(hwnd) || !validIndex(hwnd, list, index) ? 0 : list._items[static_cast<size_t>(index)]._param;
		}

		// Changes an item's state the way the user or LVM_SETITEMSTATE does, with LVN_ITEMCHANGING and
		// LVN_ITEMCHANGED. Returns false if the parent refused.
		bool changeState(HWND hwnd, ListView& list, int index, UINT state, UINT mask)
		{
			const UINT oldState = stateOf(hwnd, list, index);
			const UINT newState = (oldState & ~mask) | (state & mask);
			if (newState == oldState)
				return true;
			NMLISTVIEW change{};
			change.iItem = index;
			change.uChanged = LVIF_STATE;
			change.uOldState = oldState;
			change.uNewState = newState;
			change.lParam = paramOf(hwnd, list, index);
			if (!isVirtual(hwnd) && sendNotify(hwnd, parentOf(hwnd), LVN_ITEMCHANGING, &change.hdr))
				return false;
			if (isVirtual(hwnd))
			{
				if (static_cast<size_t>(index) >= list._virtualSelected.size())
					list._virtualSelected.resize(static_cast<size_t>(list._virtualCount), false);
				list._virtualSelected[static_cast<size_t>(index)] = (newState & LVIS_SELECTED) != 0;
			}
			else
			{
				list._items[static_cast<size_t>(index)]._state = newState & ~LVIS_FOCUSED;
			}
			if ((newState & LVIS_FOCUSED) && !(oldState & LVIS_FOCUSED))
				list._focused = index;
			else if (!(newState & LVIS_FOCUSED) && (oldState & LVIS_FOCUSED))
				list._focused = -1;
			InvalidateRect(hwnd, nullptr, FALSE);
			sendNotify(hwnd, parentOf(hwnd), LVN_ITEMCHANGED, &change.hdr);
			return true;
		}

		// For virtual lists, Windows reports a change to all items at once as item -1.
		void changeAll(HWND hwnd, ListView& list, UINT state, UINT mask)
		{
			if (isVirtual(hwnd))
			{
				std::fill(list._virtualSelected.begin(), list._virtualSelected.end(), (state & mask & LVIS_SELECTED) != 0);
				if ((mask & LVIS_FOCUSED) && !(state & LVIS_FOCUSED))
					list._focused = -1;
				NMLISTVIEW change{};
				change.iItem = -1;
				change.uChanged = LVIF_STATE;
				change.uNewState = state & mask;
				change.uOldState = ~state & mask;
				InvalidateRect(hwnd, nullptr, FALSE);
				sendNotify(hwnd, parentOf(hwnd), LVN_ITEMCHANGED, &change.hdr);
				return;
			}
			for (int i = 0; i < itemCount(hwnd, list); ++i)
				changeState(hwnd, list, i, state, mask);
		}

		// What a click or a key does to the selection.
		void userSelect(HWND hwnd, ListView& list, int index, bool shift, bool control)
		{
			if (!validIndex(hwnd, list, index))
				return;
			if (isSingleSelection(hwnd) || (!shift && !control))
			{
				for (int i = 0; i < itemCount(hwnd, list); ++i)
				{
					if (i != index && (stateOf(hwnd, list, i) & LVIS_SELECTED))
						changeState(hwnd, list, i, 0, LVIS_SELECTED);
				}
				changeState(hwnd, list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				list._selectionMark = index;
			}
			else if (shift)
			{
				const int anchor = list._selectionMark >= 0 ? list._selectionMark : index;
				const int from = std::min(anchor, index);
				const int to = std::max(anchor, index);
				for (int i = 0; i < itemCount(hwnd, list); ++i)
				{
					const bool inRange = i >= from && i <= to;
					if (inRange || !control)
						changeState(hwnd, list, i, inRange ? LVIS_SELECTED : 0, LVIS_SELECTED);
				}
				changeState(hwnd, list, index, LVIS_FOCUSED, LVIS_FOCUSED);
			}
			else
			{
				const UINT selected = stateOf(hwnd, list, index) & LVIS_SELECTED;
				changeState(hwnd, list, index, (selected ? 0 : LVIS_SELECTED) | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				list._selectionMark = index;
			}
			ensureVisible(hwnd, list, index);
		}

		bool hasCheckBoxes(const ListView& list)
		{
			return (list._extendedStyle & LVS_EX_CHECKBOXES) != 0;
		}

		bool isChecked(HWND hwnd, const ListView& list, int index)
		{
			return ((stateOf(hwnd, list, index) & LVIS_STATEIMAGEMASK) >> 12) == 2;
		}

		void toggleCheck(HWND hwnd, ListView& list, int index)
		{
			changeState(hwnd, list, index, INDEXTOSTATEIMAGEMASK(isChecked(hwnd, list, index) ? 1 : 2), LVIS_STATEIMAGEMASK);
		}

		// --- Hit testing ---------------------------------------------------------------------------

		struct Hit
		{
			int _item = -1;
			int _subItem = 0;
			UINT _flags = LVHT_NOWHERE;
		};

		Hit hitTest(HWND hwnd, ListView& list, POINT point)
		{
			Hit hit;
			const RECT area = rowsArea(hwnd, list);
			if (point.y < area.top)
			{
				hit._flags = LVHT_ABOVE;
				return hit;
			}
			if (point.y >= area.bottom)
			{
				hit._flags = LVHT_BELOW;
				return hit;
			}
			const int index = list._top + (point.y - area.top) / std::max(1, list._itemHeight);
			if (!validIndex(hwnd, list, index))
				return hit;
			hit._item = index;
			const int columns = columnCount(list);
			for (int column = 0; column < columns; ++column)
			{
				const RECT cell = cellRect(hwnd, list, index, column);
				if (point.x >= cell.left && point.x < cell.right)
					hit._subItem = column;
			}
			const RECT first = cellRect(hwnd, list, index, 0);
			int x = first.left + cellMargin / 2;
			if (hasCheckBoxes(list) && point.x >= x && point.x < x + checkBoxSize)
			{
				hit._flags = LVHT_ONITEMSTATEICON;
				return hit;
			}
			if (hasCheckBoxes(list))
				x += checkBoxSize + imageGap;
			const int image = smallImageSize(list);
			if (image > 0 && point.x >= x && point.x < x + image)
				hit._flags = LVHT_ONITEMICON;
			else
				hit._flags = LVHT_ONITEMLABEL;
			return hit;
		}

		LRESULT notifyActivate(HWND hwnd, UINT code, const Hit& hit, POINT point)
		{
			NMITEMACTIVATE activate{};
			activate.iItem = hit._item;
			activate.iSubItem = hit._subItem;
			activate.ptAction = point;
			activate.uKeyFlags = ((GetKeyState(VK_SHIFT) & 0x8000) ? LVKF_SHIFT : 0) | ((GetKeyState(VK_CONTROL) & 0x8000) ? LVKF_CONTROL : 0)
				| ((GetKeyState(VK_MENU) & 0x8000) ? LVKF_ALT : 0);
			return sendNotify(hwnd, parentOf(hwnd), code, &activate.hdr);
		}

		// --- Painting ------------------------------------------------------------------------------

		void drawCheckBox(HDC hdc, int x, int y, bool checked)
		{
			RECT box{x, y, x + checkBoxSize, y + checkBoxSize};
			HBRUSH fill = CreateSolidBrush(controlPalette()._boxFill);
			HBRUSH frame = CreateSolidBrush(controlPalette()._boxFrame);
			FillRect(hdc, &box, fill);
			FrameRect(hdc, &box, frame);
			DeleteObject(fill);
			DeleteObject(frame);
			if (checked)
			{
				HPEN pen = CreatePen(PS_SOLID, 2, controlPalette()._mark);
				HGDIOBJ oldPen = SelectObject(hdc, pen);
				const POINT mark[] = {{x + 3, y + 6}, {x + 5, y + 9}, {x + 10, y + 3}};
				Polyline(hdc, mark, 3);
				SelectObject(hdc, oldPen);
				DeleteObject(pen);
			}
		}

		void drawCell(HWND hwnd, ListView& list, HDC hdc, int index, int column, const RECT& cell, COLORREF text)
		{
			RECT content{cell.left + cellMargin / 2, cell.top, cell.right - cellMargin / 2, cell.bottom};
			if (column == 0)
			{
				if (hasCheckBoxes(list))
				{
					drawCheckBox(hdc, content.left, (cell.top + cell.bottom - checkBoxSize) / 2, isChecked(hwnd, list, index));
					content.left += checkBoxSize + imageGap;
				}
				if (list._smallImages)
				{
					const int image = imageOf(hwnd, list, index);
					const int size = smallImageSize(list);
					if (image >= 0)
						ImageList_Draw(list._smallImages, image, hdc, content.left, (cell.top + cell.bottom - size) / 2, ILD_TRANSPARENT);
					content.left += size + imageGap;
				}
			}
			const std::u16string value = textOf(hwnd, list, index, column);
			UINT format = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
			if (columnCount(list) > 0)
			{
				const int alignment = columnFormat(list, column) & LVCFMT_JUSTIFYMASK;
				format |= alignment == LVCFMT_RIGHT ? DT_RIGHT : (alignment == LVCFMT_CENTER ? DT_CENTER : DT_LEFT);
			}
			SetTextColor(hdc, text);
			DrawTextW(hdc, value.c_str(), static_cast<int>(value.size()), &content, format);
		}

		void paint(HWND hwnd, ListView& list, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			const COLORREF background = list._background != CLR_NONE ? list._background : GetSysColor(COLOR_WINDOW);
			const COLORREF textColor = list._textColor != CLR_NONE ? list._textColor : GetSysColor(COLOR_WINDOWTEXT);
			HBRUSH backgroundBrush = CreateSolidBrush(background);
			FillRect(hdc, &client, backgroundBrush);
			DeleteObject(backgroundBrush);
			if ((styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
			{
				HBRUSH frame = CreateSolidBrush(controlPalette()._frame);
				FrameRect(hdc, &client, frame);
				DeleteObject(frame);
			}

			NMLVCUSTOMDRAW custom{};
			custom.nmcd.dwDrawStage = CDDS_PREPAINT;
			custom.nmcd.hdc = hdc;
			custom.nmcd.rc = client;
			const LRESULT prePaint = (styleOf(hwnd) & LVS_OWNERDRAWFIXED) ? CDRF_DODEFAULT : sendNotify(hwnd, parentOf(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);

			const RECT area = rowsArea(hwnd, list);
			const int clip = SaveDC(hdc);
			IntersectClipRect(hdc, area.left, area.top, area.right, area.bottom);
			const bool fullRow = (list._extendedStyle & LVS_EX_FULLROWSELECT) != 0;
			const bool showSelection = list._hasFocus || (styleOf(hwnd) & LVS_SHOWSELALWAYS);
			const int columns = std::max(1, columnCount(list));
			const int last = std::min(itemCount(hwnd, list), list._top + visibleRows(hwnd, list) + 1);
			for (int index = list._top; index < last; ++index)
			{
				const RECT row = rowRect(hwnd, list, index);
				const UINT state = stateOf(hwnd, list, index);
				const bool selected = (state & LVIS_SELECTED) && showSelection;

				if (styleOf(hwnd) & LVS_OWNERDRAWFIXED)
				{
					DRAWITEMSTRUCT draw{};
					draw.CtlType = ODT_LISTVIEW;
					draw.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
					draw.itemID = static_cast<UINT>(index);
					draw.itemAction = ODA_DRAWENTIRE;
					draw.itemState = (selected ? ODS_SELECTED : 0) | (list._hasFocus && index == list._focused ? ODS_FOCUS : 0);
					draw.hwndItem = hwnd;
					draw.hDC = hdc;
					draw.rcItem = row;
					draw.itemData = static_cast<ULONG_PTR>(paramOf(hwnd, list, index));
					SendMessageW(parentOf(hwnd), WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
					continue;
				}

				COLORREF text = selected && list._hasFocus ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor;
				COLORREF cellBackground = selected ? (list._hasFocus ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE)) : list._textBackground;
				LRESULT itemResult = CDRF_DODEFAULT;
				if (prePaint & CDRF_NOTIFYITEMDRAW)
				{
					custom.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
					custom.nmcd.rc = row;
					custom.nmcd.dwItemSpec = static_cast<DWORD_PTR>(index);
					custom.nmcd.uItemState = (selected ? CDIS_SELECTED : 0) | (list._hasFocus && index == list._focused ? CDIS_FOCUS : 0);
					custom.nmcd.lItemlParam = paramOf(hwnd, list, index);
					custom.iSubItem = 0;
					custom.clrText = text;
					custom.clrTextBk = cellBackground == CLR_NONE ? background : cellBackground;
					itemResult = sendNotify(hwnd, parentOf(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);
					if (itemResult & CDRF_SKIPDEFAULT)
						continue;
					text = custom.clrText;
					if (custom.clrTextBk != background)
						cellBackground = custom.clrTextBk;
				}

				if (cellBackground != CLR_NONE && fullRow)
				{
					HBRUSH fill = CreateSolidBrush(cellBackground);
					FillRect(hdc, &row, fill);
					DeleteObject(fill);
				}
				for (int column = 0; column < columns; ++column)
				{
					const RECT cell = cellRect(hwnd, list, index, column);
					if (cell.right <= area.left || cell.left >= area.right)
						continue;
					COLORREF cellText = text;
					COLORREF fillColor = fullRow || column == 0 ? cellBackground : list._textBackground;
					if (itemResult & CDRF_NOTIFYSUBITEMDRAW)
					{
						custom.nmcd.dwDrawStage = CDDS_ITEMPREPAINT | CDDS_SUBITEM;
						custom.nmcd.rc = cell;
						custom.iSubItem = column;
						custom.clrText = text;
						custom.clrTextBk = fillColor == CLR_NONE ? background : fillColor;
						if (sendNotify(hwnd, parentOf(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr) & CDRF_SKIPDEFAULT)
							continue;
						cellText = custom.clrText;
						if (custom.clrTextBk != background)
							fillColor = custom.clrTextBk;
					}
					if (fillColor != CLR_NONE && !fullRow)
					{
						HBRUSH fill = CreateSolidBrush(fillColor);
						FillRect(hdc, &cell, fill);
						DeleteObject(fill);
					}
					if (!selected || fullRow || column == 0)
						drawCell(hwnd, list, hdc, index, column, cell, cellText);
					else
						drawCell(hwnd, list, hdc, index, column, cell, textColor);
				}
				if (list._hasFocus && index == list._focused)
				{
					RECT focus = fullRow ? row : cellRect(hwnd, list, index, 0);
					DrawFocusRect(hdc, &focus);
				}
				if (itemResult & CDRF_NOTIFYPOSTPAINT)
				{
					custom.nmcd.dwDrawStage = CDDS_ITEMPOSTPAINT;
					custom.nmcd.rc = row;
					sendNotify(hwnd, parentOf(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);
				}
			}
			RestoreDC(hdc, clip);
			if (list._scrollBar.isNeeded() && !(styleOf(hwnd) & LVS_NOSCROLL))
				list._scrollBar.paint(hdc);
			if (prePaint & CDRF_NOTIFYPOSTPAINT)
			{
				custom.nmcd.dwDrawStage = CDDS_POSTPAINT;
				custom.nmcd.rc = client;
				sendNotify(hwnd, parentOf(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);
			}
			RestoreDC(hdc, saved);
		}

		// --- Messages ------------------------------------------------------------------------------

		void applyItem(HWND hwnd, ListView& list, Item& item, const LVITEMW& source)
		{
			if (source.mask & LVIF_TEXT)
			{
				const size_t subItem = static_cast<size_t>(std::max(0, source.iSubItem));
				if (item._texts.size() <= subItem)
					item._texts.resize(subItem + 1);
				if (subItem == 0)
					item._textCallback = source.pszText == LPSTR_TEXTCALLBACKW;
				item._texts[subItem] = source.pszText && source.pszText != LPSTR_TEXTCALLBACKW ? source.pszText : u"";
			}
			if (source.iSubItem != 0)
				return;
			if (source.mask & LVIF_IMAGE)
				item._image = source.iImage;
			if (source.mask & LVIF_PARAM)
				item._param = source.lParam;
			if (source.mask & LVIF_INDENT)
				item._indent = source.iIndent;
			(void)hwnd;
			(void)list;
		}

		LRESULT getItem(HWND hwnd, ListView& list, LVITEMW& out)
		{
			if (!validIndex(hwnd, list, out.iItem))
				return FALSE;
			if (out.mask & LVIF_TEXT)
			{
				const std::u16string text = textOf(hwnd, list, out.iItem, out.iSubItem);
				if (out.pszText && out.cchTextMax > 0)
					lstrcpynW(out.pszText, text.c_str(), out.cchTextMax);
			}
			if (out.mask & LVIF_STATE)
				out.state = stateOf(hwnd, list, out.iItem) & out.stateMask;
			if (out.mask & LVIF_IMAGE)
				out.iImage = isVirtual(hwnd) ? I_IMAGECALLBACK : list._items[static_cast<size_t>(out.iItem)]._image;
			if (out.mask & LVIF_PARAM)
				out.lParam = paramOf(hwnd, list, out.iItem);
			if (out.mask & LVIF_INDENT)
				out.iIndent = isVirtual(hwnd) ? 0 : list._items[static_cast<size_t>(out.iItem)]._indent;
			if (out.mask & LVIF_GROUPID)
				out.iGroupId = I_GROUPIDNONE;
			return TRUE;
		}

		int nextItem(HWND hwnd, ListView& list, int start, UINT flags)
		{
			const int count = itemCount(hwnd, list);
			const UINT wanted = flags & (LVNI_SELECTED | LVNI_FOCUSED | LVNI_CUT | LVNI_DROPHILITED);
			const bool backwards = (flags & (LVNI_ABOVE | LVNI_PREVIOUS)) != 0;
			for (int i = backwards ? start - 1 : start + 1; i >= 0 && i < count; i += backwards ? -1 : 1)
			{
				if (wanted == 0 || (stateOf(hwnd, list, i) & wanted) == wanted)
					return i;
				if ((flags & (LVNI_ABOVE | LVNI_BELOW)) && wanted == 0)
					return i;
			}
			return -1;
		}

		void keyDown(HWND hwnd, ListView& list, WPARAM key)
		{
			NMLVKEYDOWN keyDown{};
			keyDown.wVKey = static_cast<WORD>(key);
			sendNotify(hwnd, parentOf(hwnd), LVN_KEYDOWN, &keyDown.hdr);

			const int count = itemCount(hwnd, list);
			if (count == 0)
				return;
			const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
			const int current = list._focused >= 0 ? list._focused : 0;
			const int page = std::max(1, visibleRows(hwnd, list) - 1);
			int target = -1;
			switch (key)
			{
				case VK_UP:
					target = std::max(0, current - 1);
					break;
				case VK_DOWN:
					target = list._focused < 0 ? 0 : std::min(count - 1, current + 1);
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
					if (hasCheckBoxes(list) && list._focused >= 0)
						toggleCheck(hwnd, list, list._focused);
					return;
				case VK_RETURN:
				{
					NMHDR header{};
					sendNotify(hwnd, parentOf(hwnd), NM_RETURN, &header);
					Hit hit;
					hit._item = list._focused;
					notifyActivate(hwnd, LVN_ITEMACTIVATE, hit, {});
					return;
				}
				case 'A':
					if (control && !isSingleSelection(hwnd))
						changeAll(hwnd, list, LVIS_SELECTED, LVIS_SELECTED);
					return;
				default:
					return;
			}
			userSelect(hwnd, list, target, shift, false);
		}

		// Type-ahead on the item texts.
		void character(HWND hwnd, ListView& list, char16_t ch)
		{
			if (ch < 0x20)
				return;
			const DWORD now = GetTickCount();
			if (now - list._typedTime > 1000)
				list._typed.clear();
			list._typedTime = now;
			list._typed += static_cast<char16_t>(std::towlower(ch));
			const int count = itemCount(hwnd, list);
			const int start = list._typed.size() == 1 ? list._focused + 1 : std::max(0, list._focused);
			for (int n = 0; n < count; ++n)
			{
				const int index = (start + n) % count;
				std::u16string text = textOf(hwnd, list, index, 0);
				std::transform(text.begin(), text.end(), text.begin(), [](char16_t c) { return static_cast<char16_t>(std::towlower(c)); });
				if (text.starts_with(list._typed))
				{
					userSelect(hwnd, list, index, false, false);
					return;
				}
			}
		}

		LRESULT CALLBACK listViewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			ListView* list = dataOf<ListView>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					ListView* created = createData<ListView>(hwnd);
					created->_itemHeight = fontHeight(hwnd) + 6;
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_CREATE:
				{
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					list->_header = CreateWindowExW(0, WC_HEADERW, nullptr, WS_CHILD | HDS_HORZ | ((create->style & LVS_NOSORTHEADER) ? 0 : HDS_BUTTONS),
						0, 0, 0, 0, hwnd, nullptr, create->hInstance, nullptr);
					layoutHeader(hwnd, *list);
					return 0;
				}
				case WM_DESTROY:
				{
					NMLISTVIEW deleted{};
					deleted.iItem = -1;
					sendNotify(hwnd, parentOf(hwnd), LVN_DELETEALLITEMS, &deleted.hdr);
					return 0;
				}
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
				case WM_SIZE:
					layoutHeader(hwnd, *list);
					setTop(hwnd, *list, list->_top);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_SETFONT:
					DefWindowProcW(hwnd, message, wParam, lParam);
					if (list->_header)
						SendMessageW(list->_header, WM_SETFONT, wParam, lParam);
					list->_itemHeight = std::max(fontHeight(hwnd) + 6, smallImageSize(*list) + 2);
					layoutHeader(hwnd, *list);
					if (LOWORD(lParam))
						InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_STYLECHANGED:
					layoutHeader(hwnd, *list);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_ENABLE:
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_GETDLGCODE:
					return DLGC_WANTARROWS | DLGC_WANTCHARS;
				case WM_SETFOCUS:
				case WM_KILLFOCUS:
				{
					list->_hasFocus = message == WM_SETFOCUS;
					if (list->_hasFocus && list->_focused < 0 && itemCount(hwnd, *list) > 0)
						list->_focused = 0;
					InvalidateRect(hwnd, nullptr, FALSE);
					NMHDR header{};
					sendNotify(hwnd, parentOf(hwnd), list->_hasFocus ? NM_SETFOCUS : NM_KILLFOCUS, &header);
					return 0;
				}
				case WM_KEYDOWN:
					keyDown(hwnd, *list, wParam);
					return 0;
				case WM_CHAR:
					character(hwnd, *list, static_cast<char16_t>(wParam));
					return 0;
				case WM_MOUSEWHEEL:
					if (GetKeyState(VK_SHIFT) & 0x8000)
						setScrollX(hwnd, *list, list->_scrollX - GET_WHEEL_DELTA_WPARAM(wParam) * horizontalWheelStep / WHEEL_DELTA);
					else
						setTop(hwnd, *list, list->_top - GET_WHEEL_DELTA_WPARAM(wParam) * wheelRows / WHEEL_DELTA);
					return 0;
				case WM_MOUSEHWHEEL:
					setScrollX(hwnd, *list, list->_scrollX + GET_WHEEL_DELTA_WPARAM(wParam) * horizontalWheelStep / WHEEL_DELTA);
					return 0;
				case WM_LBUTTONDOWN:
				case WM_RBUTTONDOWN:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (GetFocus() != hwnd)
						SetFocus(hwnd);
					if (message == WM_LBUTTONDOWN && list->_scrollBar.contains(point) && !(styleOf(hwnd) & LVS_NOSCROLL))
					{
						setTop(hwnd, *list, list->_scrollBar.press(point));
						if (list->_scrollBar.isDragging())
							SetCapture(hwnd);
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					const Hit hit = hitTest(hwnd, *list, point);
					if (hit._item < 0)
					{
						// A click on no item clears the selection.
						if (message == WM_LBUTTONDOWN && !(wParam & (MK_SHIFT | MK_CONTROL)))
							changeAll(hwnd, *list, 0, LVIS_SELECTED);
						return 0;
					}
					if (message == WM_LBUTTONDOWN && hasCheckBoxes(*list) && hit._flags == LVHT_ONITEMSTATEICON)
					{
						toggleCheck(hwnd, *list, hit._item);
						return 0;
					}
					// A right click keeps a selection it falls in.
					if (message == WM_LBUTTONDOWN || !(stateOf(hwnd, *list, hit._item) & LVIS_SELECTED))
						userSelect(hwnd, *list, hit._item, (wParam & MK_SHIFT) != 0, (wParam & MK_CONTROL) != 0);
					if (message == WM_LBUTTONDOWN)
					{
						list->_pressed = hit._item;
						list->_pressPoint = point;
						SetCapture(hwnd);
					}
					return 0;
				}
				case WM_MOUSEMOVE:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (list->_scrollBar.isDragging())
					{
						setTop(hwnd, *list, list->_scrollBar.drag(point));
						return 0;
					}
					if (list->_pressed >= 0 && (std::abs(point.x - list->_pressPoint.x) > dragThreshold || std::abs(point.y - list->_pressPoint.y) > dragThreshold))
					{
						const int dragged = list->_pressed;
						list->_pressed = -1;
						ReleaseCapture();
						NMLISTVIEW drag{};
						drag.iItem = dragged;
						drag.ptAction = list->_pressPoint;
						drag.lParam = paramOf(hwnd, *list, dragged);
						sendNotify(hwnd, parentOf(hwnd), LVN_BEGINDRAG, &drag.hdr);
					}
					return 0;
				}
				case WM_LBUTTONUP:
				{
					if (list->_scrollBar.isDragging())
					{
						list->_scrollBar.release();
						ReleaseCapture();
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					const bool clicked = list->_pressed >= 0;
					list->_pressed = -1;
					if (GetCapture() == hwnd)
						ReleaseCapture();
					if (clicked)
					{
						const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
						notifyActivate(hwnd, NM_CLICK, hitTest(hwnd, *list, point), point);
					}
					return 0;
				}
				case WM_RBUTTONUP:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (!notifyActivate(hwnd, NM_RCLICK, hitTest(hwnd, *list, point), point))
					{
						POINT screen = point;
						ClientToScreen(hwnd, &screen);
						SendMessageW(parentOf(hwnd), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(screen.x, screen.y));
					}
					return 0;
				}
				case WM_CONTEXTMENU:
					return 0;
				case WM_CAPTURECHANGED:
					list->_pressed = -1;
					list->_scrollBar.release();
					return 0;
				case WM_LBUTTONDBLCLK:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const Hit hit = hitTest(hwnd, *list, point);
					if (hasCheckBoxes(*list) && hit._item >= 0 && hit._flags == LVHT_ONITEMSTATEICON)
					{
						toggleCheck(hwnd, *list, hit._item);
						return 0;
					}
					notifyActivate(hwnd, NM_DBLCLK, hit, point);
					if (hit._item >= 0)
						notifyActivate(hwnd, LVN_ITEMACTIVATE, hit, point);
					return 0;
				}
				case WM_NOTIFY:
				{
					// The header's notifications: column clicks become LVN_COLUMNCLICK, width changes
					// redraw; all of them also go on to the list view's parent, as on Windows.
					const auto* header = reinterpret_cast<const NMHDR*>(lParam);
					if (header->hwndFrom == list->_header)
					{
						if (header->code == HDN_ITEMCLICKW)
						{
							NMLISTVIEW click{};
							click.iItem = -1;
							click.iSubItem = reinterpret_cast<const NMHEADERW*>(lParam)->iItem;
							sendNotify(hwnd, parentOf(hwnd), LVN_COLUMNCLICK, &click.hdr);
							return 0;
						}
						if (header->code == HDN_ITEMCHANGEDW || header->code == HDN_ENDTRACKW)
							InvalidateRect(hwnd, nullptr, FALSE);
					}
					return SendMessageW(parentOf(hwnd), WM_NOTIFY, wParam, lParam);
				}
				case LVM_GETHEADER:
					return reinterpret_cast<LRESULT>(list->_header);
				case LVM_INSERTCOLUMNW:
				{
					const auto* column = reinterpret_cast<const LVCOLUMNW*>(lParam);
					HDITEMW item{};
					item.mask = HDI_FORMAT | HDI_LPARAM;
					item.fmt = HDF_LEFT;
					if (column->mask & LVCF_FMT)
						item.fmt = (column->fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT ? HDF_RIGHT : ((column->fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER ? HDF_CENTER : HDF_LEFT);
					if (column->mask & LVCF_WIDTH)
					{
						item.mask |= HDI_WIDTH;
						item.cxy = column->cx;
					}
					if (column->mask & LVCF_TEXT)
					{
						item.mask |= HDI_TEXT;
						item.pszText = column->pszText;
					}
					const LRESULT index = SendMessageW(list->_header, HDM_INSERTITEMW, wParam, reinterpret_cast<LPARAM>(&item));
					InvalidateRect(hwnd, nullptr, FALSE);
					return index;
				}
				case LVM_DELETECOLUMN:
				{
					const LRESULT result = SendMessageW(list->_header, HDM_DELETEITEM, wParam, 0);
					for (Item& item : list->_items)
					{
						if (wParam < item._texts.size())
							item._texts.erase(item._texts.begin() + static_cast<std::ptrdiff_t>(wParam));
					}
					InvalidateRect(hwnd, nullptr, FALSE);
					return result;
				}
				case LVM_SETCOLUMNW:
				{
					const auto* column = reinterpret_cast<const LVCOLUMNW*>(lParam);
					HDITEMW item{};
					if (column->mask & LVCF_FMT)
					{
						item.mask |= HDI_FORMAT;
						item.fmt = (column->fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT ? HDF_RIGHT : ((column->fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER ? HDF_CENTER : HDF_LEFT);
					}
					if (column->mask & LVCF_WIDTH)
					{
						item.mask |= HDI_WIDTH;
						item.cxy = column->cx;
					}
					if (column->mask & LVCF_TEXT)
					{
						item.mask |= HDI_TEXT;
						item.pszText = column->pszText;
					}
					const LRESULT result = SendMessageW(list->_header, HDM_SETITEMW, wParam, reinterpret_cast<LPARAM>(&item));
					InvalidateRect(hwnd, nullptr, FALSE);
					return result;
				}
				case LVM_GETCOLUMNW:
				{
					auto* column = reinterpret_cast<LVCOLUMNW*>(lParam);
					HDITEMW item{};
					item.mask = HDI_FORMAT | HDI_WIDTH | ((column->mask & LVCF_TEXT) ? HDI_TEXT : 0);
					item.pszText = column->pszText;
					item.cchTextMax = column->cchTextMax;
					if (!SendMessageW(list->_header, HDM_GETITEMW, wParam, reinterpret_cast<LPARAM>(&item)))
						return FALSE;
					if (column->mask & LVCF_WIDTH)
						column->cx = item.cxy;
					if (column->mask & LVCF_FMT)
						column->fmt = (item.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT ? LVCFMT_RIGHT : ((item.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? LVCFMT_CENTER : LVCFMT_LEFT);
					if (column->mask & LVCF_SUBITEM)
						column->iSubItem = static_cast<int>(wParam);
					return TRUE;
				}
				case LVM_SETCOLUMNWIDTH:
				{
					const int column = static_cast<int>(wParam);
					int width = static_cast<short>(LOWORD(lParam));
					if (width == LVSCW_AUTOSIZE || width == LVSCW_AUTOSIZE_USEHEADER)
					{
						// The widest text in the column (and its title), or the rest of the width for the
						// last column with LVSCW_AUTOSIZE_USEHEADER.
						width = 0;
						for (int i = 0; i < itemCount(hwnd, *list); ++i)
							width = std::max(width, textWidth(hwnd, textOf(hwnd, *list, i, column)));
						width += cellMargin + (column == 0 ? smallImageSize(*list) + (hasCheckBoxes(*list) ? checkBoxSize + imageGap : 0) + imageGap : 0);
						if (static_cast<short>(LOWORD(lParam)) == LVSCW_AUTOSIZE_USEHEADER)
						{
							char16_t title[256]{};
							HDITEMW item{};
							item.mask = HDI_TEXT;
							item.pszText = title;
							item.cchTextMax = 256;
							SendMessageW(list->_header, HDM_GETITEMW, wParam, reinterpret_cast<LPARAM>(&item));
							width = std::max(width, textWidth(hwnd, title) + 2 * cellMargin);
							if (column == columnCount(*list) - 1)
							{
								const RECT area = rowsArea(hwnd, *list);
								int used = 0;
								for (int i = 0; i < column; ++i)
									used += columnWidth(*list, i);
								width = std::max<int>(width, area.right - area.left - used);
							}
						}
					}
					HDITEMW item{};
					item.mask = HDI_WIDTH;
					item.cxy = width;
					const LRESULT result = SendMessageW(list->_header, HDM_SETITEMW, wParam, reinterpret_cast<LPARAM>(&item));
					InvalidateRect(hwnd, nullptr, FALSE);
					return result;
				}
				case LVM_GETCOLUMNWIDTH:
					return columnWidth(*list, static_cast<int>(wParam));
				case LVM_INSERTITEMW:
				{
					if (isVirtual(hwnd))
						return -1;
					const auto* source = reinterpret_cast<const LVITEMW*>(lParam);
					Item item;
					LVITEMW first = *source;
					first.iSubItem = 0;
					applyItem(hwnd, *list, item, first);
					if (source->mask & LVIF_STATE)
						item._state = source->state & source->stateMask & ~LVIS_FOCUSED;
					const int index = std::clamp(source->iItem, 0, static_cast<int>(list->_items.size()));
					list->_items.insert(list->_items.begin() + index, item);
					if (list->_focused >= index)
						++list->_focused;
					if (list->_selectionMark >= index)
						++list->_selectionMark;
					if ((source->mask & LVIF_STATE) && (source->state & source->stateMask & LVIS_FOCUSED))
						list->_focused = index;
					NMLISTVIEW inserted{};
					inserted.iItem = index;
					sendNotify(hwnd, parentOf(hwnd), LVN_INSERTITEM, &inserted.hdr);
					InvalidateRect(hwnd, nullptr, FALSE);
					return index;
				}
				case LVM_DELETEITEM:
				{
					const int index = static_cast<int>(wParam);
					if (isVirtual(hwnd) || !validIndex(hwnd, *list, index))
						return FALSE;
					NMLISTVIEW deleted{};
					deleted.iItem = index;
					deleted.lParam = list->_items[static_cast<size_t>(index)]._param;
					sendNotify(hwnd, parentOf(hwnd), LVN_DELETEITEM, &deleted.hdr);
					list->_items.erase(list->_items.begin() + index);
					if (list->_focused == index)
						list->_focused = -1;
					else if (list->_focused > index)
						--list->_focused;
					if (list->_selectionMark >= index)
						list->_selectionMark = list->_selectionMark == index ? -1 : list->_selectionMark - 1;
					setTop(hwnd, *list, list->_top);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case LVM_DELETEALLITEMS:
				{
					NMLISTVIEW deleted{};
					deleted.iItem = -1;
					// A non-zero answer means: no LVN_DELETEITEM for each item.
					if (!sendNotify(hwnd, parentOf(hwnd), LVN_DELETEALLITEMS, &deleted.hdr))
					{
						for (int i = 0; i < static_cast<int>(list->_items.size()); ++i)
						{
							NMLISTVIEW item{};
							item.iItem = i;
							item.lParam = list->_items[static_cast<size_t>(i)]._param;
							sendNotify(hwnd, parentOf(hwnd), LVN_DELETEITEM, &item.hdr);
						}
					}
					list->_items.clear();
					list->_virtualCount = 0;
					list->_virtualSelected.clear();
					list->_focused = list->_selectionMark = -1;
					list->_top = 0;
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case LVM_GETITEMW:
					return getItem(hwnd, *list, *reinterpret_cast<LVITEMW*>(lParam));
				case LVM_SETITEMW:
				{
					const auto* source = reinterpret_cast<const LVITEMW*>(lParam);
					if (!validIndex(hwnd, *list, source->iItem))
						return FALSE;
					if (!isVirtual(hwnd))
						applyItem(hwnd, *list, list->_items[static_cast<size_t>(source->iItem)], *source);
					if ((source->mask & LVIF_STATE) && source->iSubItem == 0)
						changeState(hwnd, *list, source->iItem, source->state, source->stateMask);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case LVM_GETITEMTEXTW:
				{
					auto* out = reinterpret_cast<LVITEMW*>(lParam);
					if (!validIndex(hwnd, *list, static_cast<int>(wParam)) || !out->pszText || out->cchTextMax <= 0)
						return 0;
					const std::u16string text = textOf(hwnd, *list, static_cast<int>(wParam), out->iSubItem);
					lstrcpynW(out->pszText, text.c_str(), out->cchTextMax);
					return static_cast<LRESULT>(std::min<size_t>(text.size(), static_cast<size_t>(out->cchTextMax - 1)));
				}
				case LVM_SETITEMTEXTW:
				{
					auto* source = reinterpret_cast<LVITEMW*>(lParam);
					if (isVirtual(hwnd) || !validIndex(hwnd, *list, static_cast<int>(wParam)))
						return FALSE;
					LVITEMW text = *source;
					text.mask = LVIF_TEXT;
					applyItem(hwnd, *list, list->_items[wParam], text);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case LVM_GETITEMCOUNT:
					return itemCount(hwnd, *list);
				case LVM_SETITEMCOUNT:
				{
					if (isVirtual(hwnd))
					{
						list->_virtualCount = static_cast<int>(wParam);
						list->_virtualSelected.resize(wParam, false);
						if (list->_focused >= list->_virtualCount)
							list->_focused = -1;
					}
					else
					{
						list->_items.reserve(wParam);
					}
					setTop(hwnd, *list, list->_top);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case LVM_GETNEXTITEM:
					return nextItem(hwnd, *list, static_cast<int>(wParam), static_cast<UINT>(lParam));
				case LVM_SETITEMSTATE:
				{
					const auto* source = reinterpret_cast<const LVITEMW*>(lParam);
					if (static_cast<int>(wParam) == -1)
					{
						changeAll(hwnd, *list, source->state, source->stateMask);
						return TRUE;
					}
					if (!validIndex(hwnd, *list, static_cast<int>(wParam)))
						return FALSE;
					// Focusing one item unfocuses the other.
					if ((source->stateMask & LVIS_FOCUSED) && (source->state & LVIS_FOCUSED) && list->_focused >= 0 && list->_focused != static_cast<int>(wParam))
						changeState(hwnd, *list, list->_focused, 0, LVIS_FOCUSED);
					changeState(hwnd, *list, static_cast<int>(wParam), source->state, source->stateMask);
					return TRUE;
				}
				case LVM_GETITEMSTATE:
					return stateOf(hwnd, *list, static_cast<int>(wParam)) & static_cast<UINT>(lParam);
				case LVM_GETSELECTEDCOUNT:
				{
					int count = 0;
					for (int i = 0; i < itemCount(hwnd, *list); ++i)
					{
						if (stateOf(hwnd, *list, i) & LVIS_SELECTED)
							++count;
					}
					return count;
				}
				case LVM_GETSELECTIONMARK:
					return list->_selectionMark;
				case LVM_SETSELECTIONMARK:
				{
					const int previous = list->_selectionMark;
					list->_selectionMark = static_cast<int>(lParam);
					return previous;
				}
				case LVM_ENSUREVISIBLE:
					ensureVisible(hwnd, *list, static_cast<int>(wParam));
					return TRUE;
				case LVM_REDRAWITEMS:
				case LVM_UPDATE:
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case LVM_GETITEMRECT:
				{
					auto* rect = reinterpret_cast<RECT*>(lParam);
					const int index = static_cast<int>(wParam);
					if (!validIndex(hwnd, *list, index))
						return FALSE;
					const int part = rect->left;
					RECT result = rowRect(hwnd, *list, index);
					if (part == LVIR_ICON || part == LVIR_LABEL || part == LVIR_SELECTBOUNDS)
					{
						const RECT first = cellRect(hwnd, *list, index, 0);
						int x = first.left + cellMargin / 2 + (hasCheckBoxes(*list) ? checkBoxSize + imageGap : 0);
						if (part == LVIR_ICON)
							result = {x, first.top, x + smallImageSize(*list), first.bottom};
						else if (part == LVIR_LABEL)
							result = {x + smallImageSize(*list) + (list->_smallImages ? imageGap : 0), first.top, first.right, first.bottom};
						else if (!(list->_extendedStyle & LVS_EX_FULLROWSELECT))
							result = first;
					}
					*rect = result;
					return TRUE;
				}
				case LVM_GETSUBITEMRECT:
				{
					auto* rect = reinterpret_cast<RECT*>(lParam);
					const int index = static_cast<int>(wParam);
					if (!validIndex(hwnd, *list, index))
						return FALSE;
					*rect = rect->top == 0 && rect->left == LVIR_BOUNDS ? rowRect(hwnd, *list, index) : cellRect(hwnd, *list, index, rect->top);
					return TRUE;
				}
				case LVM_HITTEST:
				case LVM_SUBITEMHITTEST:
				{
					auto* info = reinterpret_cast<LVHITTESTINFO*>(lParam);
					const Hit hit = hitTest(hwnd, *list, info->pt);
					info->iItem = hit._item;
					info->iSubItem = hit._subItem;
					info->flags = hit._flags;
					return hit._item;
				}
				case LVM_SETEXTENDEDLISTVIEWSTYLE:
				{
					const DWORD previous = list->_extendedStyle;
					const DWORD mask = wParam ? static_cast<DWORD>(wParam) : 0xFFFFFFFF;
					list->_extendedStyle = (list->_extendedStyle & ~mask) | (static_cast<DWORD>(lParam) & mask);
					InvalidateRect(hwnd, nullptr, FALSE);
					return previous;
				}
				case LVM_GETEXTENDEDLISTVIEWSTYLE:
					return list->_extendedStyle;
				case LVM_SETIMAGELIST:
				{
					HIMAGELIST& images = wParam == LVSIL_SMALL ? list->_smallImages : (wParam == LVSIL_STATE ? list->_stateImages : list->_normalImages);
					HIMAGELIST previous = images;
					images = reinterpret_cast<HIMAGELIST>(lParam);
					list->_itemHeight = std::max(fontHeight(hwnd) + 6, smallImageSize(*list) + 2);
					InvalidateRect(hwnd, nullptr, FALSE);
					return reinterpret_cast<LRESULT>(previous);
				}
				case LVM_GETIMAGELIST:
					return reinterpret_cast<LRESULT>(wParam == LVSIL_SMALL ? list->_smallImages : (wParam == LVSIL_STATE ? list->_stateImages : list->_normalImages));
				case LVM_SETBKCOLOR:
					list->_background = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case LVM_GETBKCOLOR:
					return static_cast<LRESULT>(list->_background == CLR_NONE ? GetSysColor(COLOR_WINDOW) : list->_background);
				case LVM_SETTEXTCOLOR:
					list->_textColor = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case LVM_GETTEXTCOLOR:
					return static_cast<LRESULT>(list->_textColor == CLR_NONE ? GetSysColor(COLOR_WINDOWTEXT) : list->_textColor);
				case LVM_SETTEXTBKCOLOR:
					list->_textBackground = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case LVM_GETTEXTBKCOLOR:
					return static_cast<LRESULT>(list->_textBackground);
				case LVM_GETSTRINGWIDTHW:
					return lParam ? textWidth(hwnd, reinterpret_cast<LPCWSTR>(lParam)) : 0;
				case LVM_SORTITEMS:
				case LVM_SORTITEMSEX:
				{
					if (isVirtual(hwnd))
						return FALSE;
					auto compare = reinterpret_cast<PFNLVCOMPARE>(lParam);
					const LPARAM data = static_cast<LPARAM>(wParam);
					// LVM_SORTITEMSEX compares indexes; the items are sorted on a copy, so the indexes are
					// those before the sort, as on Windows.
					std::vector<int> order(list->_items.size());
					for (size_t i = 0; i < order.size(); ++i)
						order[i] = static_cast<int>(i);
					std::stable_sort(order.begin(), order.end(), [&](int a, int b)
					{
						const LPARAM first = message == LVM_SORTITEMSEX ? a : list->_items[static_cast<size_t>(a)]._param;
						const LPARAM second = message == LVM_SORTITEMSEX ? b : list->_items[static_cast<size_t>(b)]._param;
						return compare(first, second, data) < 0;
					});
					std::vector<Item> sorted;
					sorted.reserve(order.size());
					int focused = -1;
					for (size_t i = 0; i < order.size(); ++i)
					{
						sorted.push_back(list->_items[static_cast<size_t>(order[i])]);
						if (order[i] == list->_focused)
							focused = static_cast<int>(i);
					}
					list->_items = std::move(sorted);
					list->_focused = focused;
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case LVM_GETTOPINDEX:
					return list->_top;
				case LVM_GETCOUNTPERPAGE:
					return visibleRows(hwnd, *list);
				case LVM_SCROLL:
					setScrollX(hwnd, *list, list->_scrollX + static_cast<int>(wParam));
					setTop(hwnd, *list, list->_top + static_cast<int>(lParam) / std::max(1, list->_itemHeight));
					return TRUE;
				case LVM_ISITEMVISIBLE:
				{
					const int index = static_cast<int>(wParam);
					return index >= list->_top && index < list->_top + visibleRows(hwnd, *list);
				}
				case LVM_FINDITEMW:
				{
					const auto* find = reinterpret_cast<const LVFINDINFOW*>(lParam);
					const int count = itemCount(hwnd, *list);
					const int start = static_cast<int>(wParam);
					for (int n = 1; n <= count; ++n)
					{
						const int index = (start + n + count) % count;
						if ((find->flags & LVFI_PARAM) && paramOf(hwnd, *list, index) == find->lParam)
							return index;
						if (find->flags & (LVFI_STRING | LVFI_PARTIAL))
						{
							const std::u16string text = textOf(hwnd, *list, index, 0);
							const std::u16string wanted = find->psz ? find->psz : u"";
							const bool found = (find->flags & LVFI_PARTIAL)
								? text.size() >= wanted.size() && CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, text.c_str(), static_cast<int>(wanted.size()), wanted.c_str(), static_cast<int>(wanted.size())) == CSTR_EQUAL
								: lstrcmpiW(text.c_str(), wanted.c_str()) == 0;
							if (found)
								return index;
						}
						if (!(find->flags & LVFI_WRAP) && index == count - 1)
							break;
					}
					return -1;
				}
				case LVM_SETCALLBACKMASK:
					list->_callbackMask = static_cast<UINT>(wParam);
					return TRUE;
				case LVM_GETCALLBACKMASK:
					return list->_callbackMask;
				case LVM_GETVIEW:
					return LV_VIEW_DETAILS;
				case LVM_SETVIEW:
					return 1;
				case LVM_APPROXIMATEVIEWRECT:
				{
					const int count = static_cast<int>(wParam) == -1 ? itemCount(hwnd, *list) : static_cast<int>(wParam);
					int width = 0;
					for (int column = 0; column < columnCount(*list); ++column)
						width += columnWidth(*list, column);
					return MAKELRESULT(width, count * list->_itemHeight + headerHeight(hwnd, *list));
				}
				case LVM_INSERTGROUP:
					return 0;
				case LVM_ENABLEGROUPVIEW:
				case LVM_REMOVEALLGROUPS:
				case LVM_SETGROUPINFO:
				case LVM_SETITEMPOSITION:
				case LVM_SETITEMPOSITION32:
				case LVM_SETICONSPACING:
				case LVM_SETWORKAREAS:
					return 0;
				case LVM_GETGROUPRECT:
				case LVM_ISGROUPVIEWENABLED:
				case LVM_HASGROUP:
					return FALSE;
				case LVM_GETTOOLTIPS:
				case LVM_SETTOOLTIPS:
				case LVM_EDITLABELW:
				case LVM_GETEDITCONTROL:
				case LVM_CANCELEDITLABEL:
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerListView()
	{
		registerHeader();
		registerControl(WC_LISTVIEWW, listViewProc);
	}
}
