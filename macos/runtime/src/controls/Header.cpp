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

// The header control (SysHeader32): the column titles of a list view. Items can be clicked
// (HDN_ITEMCLICK) and resized by dragging the divider after them (HDN_BEGINTRACK, HDN_TRACK,
// HDN_ENDTRACK, HDN_ITEMCHANGING / HDN_ITEMCHANGED; a double click is HDN_DIVIDERDBLCLICK).

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr int dividerGrip = 4;   // how near a divider the mouse resizes
		constexpr int textMargin = 6;
		constexpr int minimumWidth = 0;

		struct HeaderItem
		{
			std::u16string _text;
			int _width = 50;
			int _format = HDF_LEFT;
			LPARAM _param = 0;
			int _image = I_IMAGENONE;
			int _order = 0;
		};

		struct Header
		{
			std::vector<HeaderItem> _items;
			HIMAGELIST _images = nullptr;
			int _hot = -1;
			int _pressed = -1;
			int _tracking = -1;          // the item whose divider is dragged
			int _trackStartX = 0;
			int _trackStartWidth = 0;
			int _scrollX = 0;            // the list view's horizontal scroll
		};

		RECT itemRect(HWND hwnd, const Header& header, int index)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			int x = -header._scrollX;
			for (int i = 0; i < index; ++i)
				x += header._items[static_cast<size_t>(i)]._width;
			return {x, client.top, x + header._items[static_cast<size_t>(index)]._width, client.bottom};
		}

		// The item at a point, and whether the point is on the divider after an item.
		int itemAt(HWND hwnd, const Header& header, int x, bool& onDivider)
		{
			onDivider = false;
			for (int i = 0; i < static_cast<int>(header._items.size()); ++i)
			{
				const RECT rect = itemRect(hwnd, header, i);
				if (std::abs(x - rect.right) <= dividerGrip)
				{
					onDivider = true;
					return i;
				}
				if (x >= rect.left && x < rect.right)
					return i;
			}
			return -1;
		}

		LRESULT notifyItem(HWND hwnd, const Header& header, UINT code, int index, int button = 0, HDITEMW* item = nullptr)
		{
			NMHEADERW notification{};
			notification.iItem = index;
			notification.iButton = button;
			HDITEMW described{};
			if (!item && index >= 0 && index < static_cast<int>(header._items.size()))
			{
				described.mask = HDI_WIDTH | HDI_LPARAM | HDI_FORMAT;
				described.cxy = header._items[static_cast<size_t>(index)]._width;
				described.lParam = header._items[static_cast<size_t>(index)]._param;
				described.fmt = header._items[static_cast<size_t>(index)]._format;
				item = &described;
			}
			notification.pitem = item;
			return sendNotify(hwnd, GetParent(hwnd), code, &notification.hdr);
		}

		void paint(HWND hwnd, Header& header, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);

			NMCUSTOMDRAW custom{};
			custom.dwDrawStage = CDDS_PREPAINT;
			custom.hdc = hdc;
			custom.rc = client;
			const LRESULT prePaint = sendNotify(hwnd, GetParent(hwnd), NM_CUSTOMDRAW, &custom.hdr);
			if (!(prePaint & CDRF_SKIPDEFAULT))
			{
				HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
				FillRect(hdc, &client, background);
				DeleteObject(background);
			}

			const COLORREF text = GetSysColor(IsWindowEnabled(hwnd) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
			for (int i = 0; i < static_cast<int>(header._items.size()); ++i)
			{
				const HeaderItem& item = header._items[static_cast<size_t>(i)];
				const RECT rect = itemRect(hwnd, header, i);
				if (rect.right <= client.left || rect.left >= client.right)
					continue;
				if (prePaint & CDRF_NOTIFYITEMDRAW)
				{
					custom.dwDrawStage = CDDS_ITEMPREPAINT;
					custom.rc = rect;
					custom.dwItemSpec = static_cast<DWORD_PTR>(i);
					custom.uItemState = (i == header._hot ? CDIS_HOT : 0) | (i == header._pressed ? CDIS_SELECTED : 0);
					custom.lItemlParam = item._param;
					if (sendNotify(hwnd, GetParent(hwnd), NM_CUSTOMDRAW, &custom.hdr) & CDRF_SKIPDEFAULT)
						continue;
				}
				if (i == header._hot || i == header._pressed)
				{
					HBRUSH hot = CreateSolidBrush(i == header._pressed ? RGB(204, 228, 247) : RGB(217, 235, 249));
					FillRect(hdc, &rect, hot);
					DeleteObject(hot);
				}
				// The divider after the item.
				RECT divider{rect.right - 1, rect.top + 3, rect.right, rect.bottom - 3};
				HBRUSH dividerBrush = CreateSolidBrush(RGB(229, 229, 229));
				FillRect(hdc, &divider, dividerBrush);
				DeleteObject(dividerBrush);

				RECT textRect{rect.left + textMargin, rect.top, rect.right - textMargin, rect.bottom};
				if (header._images && item._image != I_IMAGENONE && (item._format & HDF_IMAGE))
				{
					int width = 0;
					int height = 0;
					ImageList_GetIconSize(header._images, &width, &height);
					ImageList_Draw(header._images, item._image, hdc, textRect.left, (rect.top + rect.bottom - height) / 2, ILD_TRANSPARENT);
					textRect.left += width + 3;
				}
				UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
				const int alignment = item._format & HDF_JUSTIFYMASK;
				format |= alignment == HDF_RIGHT ? DT_RIGHT : (alignment == HDF_CENTER ? DT_CENTER : DT_LEFT);
				SetTextColor(hdc, text);
				DrawTextW(hdc, item._text.c_str(), static_cast<int>(item._text.size()), &textRect, format);

				// Sort arrows.
				if (item._format & (HDF_SORTUP | HDF_SORTDOWN))
				{
					const int x = (rect.left + rect.right) / 2;
					const int y = rect.top + 3;
					HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
					HGDIOBJ oldPen = SelectObject(hdc, pen);
					const POINT up[] = {{x - 3, y + 3}, {x, y}, {x + 3, y + 3}};
					const POINT down[] = {{x - 3, y}, {x, y + 3}, {x + 3, y}};
					Polyline(hdc, (item._format & HDF_SORTUP) ? up : down, 3);
					SelectObject(hdc, oldPen);
					DeleteObject(pen);
				}
			}
			RECT bottomLine{client.left, client.bottom - 1, client.right, client.bottom};
			HBRUSH line = CreateSolidBrush(RGB(229, 229, 229));
			FillRect(hdc, &bottomLine, line);
			DeleteObject(line);
			if (prePaint & CDRF_NOTIFYPOSTPAINT)
			{
				custom.dwDrawStage = CDDS_POSTPAINT;
				custom.rc = client;
				sendNotify(hwnd, GetParent(hwnd), NM_CUSTOMDRAW, &custom.hdr);
			}
			RestoreDC(hdc, saved);
		}

		void applyItem(HeaderItem& item, const HDITEMW& source)
		{
			if (source.mask & HDI_TEXT)
				item._text = source.pszText && source.pszText != LPSTR_TEXTCALLBACKW ? source.pszText : u"";
			if (source.mask & HDI_WIDTH)
				item._width = std::max(minimumWidth, source.cxy);
			if (source.mask & HDI_FORMAT)
				item._format = source.fmt;
			if (source.mask & HDI_LPARAM)
				item._param = source.lParam;
			if (source.mask & HDI_IMAGE)
				item._image = source.iImage;
			if (source.mask & HDI_ORDER)
				item._order = source.iOrder;
		}

		LRESULT CALLBACK headerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Header* header = dataOf<Header>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Header>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *header, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *header, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SETCURSOR:
				{
					POINT point{};
					GetCursorPos(&point);
					ScreenToClient(hwnd, &point);
					bool onDivider = false;
					itemAt(hwnd, *header, point.x, onDivider);
					if (onDivider || header->_tracking >= 0)
					{
						SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
						return TRUE;
					}
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_MOUSEMOVE:
				{
					const int x = GET_X_LPARAM(lParam);
					if (header->_tracking >= 0)
					{
						HDITEMW changed{};
						changed.mask = HDI_WIDTH;
						changed.cxy = std::max(minimumWidth, header->_trackStartWidth + x - header->_trackStartX);
						if (!notifyItem(hwnd, *header, HDN_TRACKW, header->_tracking, 0, &changed)
							&& !notifyItem(hwnd, *header, HDN_ITEMCHANGINGW, header->_tracking, 0, &changed))
						{
							header->_items[static_cast<size_t>(header->_tracking)]._width = changed.cxy;
							InvalidateRect(hwnd, nullptr, FALSE);
							notifyItem(hwnd, *header, HDN_ITEMCHANGEDW, header->_tracking, 0, &changed);
						}
						return 0;
					}
					bool onDivider = false;
					const int hot = (GetWindowLongW(hwnd, GWL_STYLE) & HDS_BUTTONS) ? itemAt(hwnd, *header, x, onDivider) : -1;
					if (hot != header->_hot)
					{
						header->_hot = onDivider ? -1 : hot;
						TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
						TrackMouseEvent(&track);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				case WM_MOUSELEAVE:
					header->_hot = -1;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_LBUTTONDOWN:
				{
					bool onDivider = false;
					const int index = itemAt(hwnd, *header, GET_X_LPARAM(lParam), onDivider);
					if (index < 0)
						return 0;
					SetCapture(hwnd);
					if (onDivider && !(GetWindowLongW(hwnd, GWL_STYLE) & HDS_NOSIZING))
					{
						if (notifyItem(hwnd, *header, HDN_BEGINTRACKW, index))
						{
							ReleaseCapture();
							return 0;
						}
						header->_tracking = index;
						header->_trackStartX = GET_X_LPARAM(lParam);
						header->_trackStartWidth = header->_items[static_cast<size_t>(index)]._width;
						return 0;
					}
					if (GetWindowLongW(hwnd, GWL_STYLE) & HDS_BUTTONS)
					{
						header->_pressed = index;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				case WM_LBUTTONUP:
				{
					if (GetCapture() == hwnd)
						ReleaseCapture();
					if (header->_tracking >= 0)
					{
						const int index = header->_tracking;
						header->_tracking = -1;
						notifyItem(hwnd, *header, HDN_ENDTRACKW, index);
						return 0;
					}
					if (header->_pressed >= 0)
					{
						const int index = header->_pressed;
						header->_pressed = -1;
						InvalidateRect(hwnd, nullptr, FALSE);
						bool onDivider = false;
						if (itemAt(hwnd, *header, GET_X_LPARAM(lParam), onDivider) == index)
							notifyItem(hwnd, *header, HDN_ITEMCLICKW, index);
					}
					return 0;
				}
				case WM_LBUTTONDBLCLK:
				{
					bool onDivider = false;
					const int index = itemAt(hwnd, *header, GET_X_LPARAM(lParam), onDivider);
					if (index >= 0 && onDivider)
						notifyItem(hwnd, *header, HDN_DIVIDERDBLCLICKW, index);
					else if (index >= 0)
						notifyItem(hwnd, *header, HDN_ITEMDBLCLICKW, index);
					return 0;
				}
				case WM_CAPTURECHANGED:
					header->_tracking = -1;
					header->_pressed = -1;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_RBUTTONUP:
				{
					NMHDR notification{};
					sendNotify(hwnd, GetParent(hwnd), NM_RCLICK, &notification);
					return 0;
				}
				case HDM_GETITEMCOUNT:
					return static_cast<LRESULT>(header->_items.size());
				case HDM_INSERTITEMW:
				{
					const auto* source = reinterpret_cast<const HDITEMW*>(lParam);
					HeaderItem item;
					applyItem(item, *source);
					const size_t index = std::min<size_t>(wParam, header->_items.size());
					header->_items.insert(header->_items.begin() + static_cast<std::ptrdiff_t>(index), item);
					InvalidateRect(hwnd, nullptr, FALSE);
					return static_cast<LRESULT>(index);
				}
				case HDM_DELETEITEM:
					if (wParam >= header->_items.size())
						return FALSE;
					header->_items.erase(header->_items.begin() + static_cast<std::ptrdiff_t>(wParam));
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case HDM_GETITEMW:
				{
					if (wParam >= header->_items.size())
						return FALSE;
					auto* out = reinterpret_cast<HDITEMW*>(lParam);
					const HeaderItem& item = header->_items[wParam];
					if ((out->mask & HDI_TEXT) && out->pszText && out->cchTextMax > 0)
						lstrcpynW(out->pszText, item._text.c_str(), out->cchTextMax);
					if (out->mask & HDI_WIDTH)
						out->cxy = item._width;
					if (out->mask & HDI_FORMAT)
						out->fmt = item._format;
					if (out->mask & HDI_LPARAM)
						out->lParam = item._param;
					if (out->mask & HDI_IMAGE)
						out->iImage = item._image;
					if (out->mask & HDI_ORDER)
						out->iOrder = static_cast<int>(wParam);
					return TRUE;
				}
				case HDM_SETITEMW:
				{
					if (wParam >= header->_items.size())
						return FALSE;
					applyItem(header->_items[wParam], *reinterpret_cast<const HDITEMW*>(lParam));
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case HDM_GETITEMRECT:
					if (wParam >= header->_items.size())
						return FALSE;
					*reinterpret_cast<RECT*>(lParam) = itemRect(hwnd, *header, static_cast<int>(wParam));
					return TRUE;
				case HDM_LAYOUT:
				{
					// The header sits on top of the rectangle given, which shrinks by its height.
					auto* layout = reinterpret_cast<HDLAYOUT*>(lParam);
					const int height = fontHeight(hwnd) + 10;
					layout->pwpos->hwnd = hwnd;
					layout->pwpos->hwndInsertAfter = nullptr;
					layout->pwpos->x = layout->prc->left;
					layout->pwpos->y = layout->prc->top;
					layout->pwpos->cx = layout->prc->right - layout->prc->left;
					layout->pwpos->cy = height;
					layout->pwpos->flags = SWP_NOZORDER | SWP_NOACTIVATE;
					layout->prc->top += height;
					return TRUE;
				}
				case HDM_HITTEST:
				{
					auto* info = reinterpret_cast<HDHITTESTINFO*>(lParam);
					bool onDivider = false;
					info->iItem = itemAt(hwnd, *header, info->pt.x, onDivider);
					info->flags = info->iItem < 0 ? HHT_NOWHERE : (onDivider ? HHT_ONDIVIDER : HHT_ONHEADER);
					return info->iItem;
				}
				case HDM_SETIMAGELIST:
				{
					HIMAGELIST previous = header->_images;
					header->_images = reinterpret_cast<HIMAGELIST>(lParam);
					return reinterpret_cast<LRESULT>(previous);
				}
				case HDM_GETIMAGELIST:
					return reinterpret_cast<LRESULT>(header->_images);
				case HDM_ORDERTOINDEX:
					return static_cast<LRESULT>(wParam);
				case HDM_GETORDERARRAY:
				{
					auto* order = reinterpret_cast<int*>(lParam);
					for (size_t i = 0; i < std::min<size_t>(wParam, header->_items.size()); ++i)
						order[i] = static_cast<int>(i);
					return TRUE;
				}
				case HDM_SETORDERARRAY:
					return TRUE;
				case headerScrolled:
					header->_scrollX = static_cast<int>(wParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerHeader()
	{
		registerControl(WC_HEADERW, headerProc);
	}
}
