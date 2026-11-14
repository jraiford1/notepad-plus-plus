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

// The static control (Static): text, icons, bitmaps, frames and rectangles, and owner-drawn statics.
// Colors come from the parent through WM_CTLCOLORSTATIC.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		struct Static
		{
			HANDLE _image = nullptr; // STM_SETIMAGE / STM_SETICON
		};

		UINT typeOf(HWND hwnd)
		{
			return static_cast<UINT>(GetWindowLongW(hwnd, GWL_STYLE)) & SS_TYPEMASK;
		}

		SIZE imageSize(HANDLE image, UINT type)
		{
			SIZE size{};
			BITMAP bitmap{};
			if (type == SS_ICON)
			{
				ICONINFO info{};
				if (GetIconInfo(static_cast<HICON>(image), &info))
				{
					if (GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap))
						size = {bitmap.bmWidth, info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2};
					if (info.hbmColor)
						DeleteObject(info.hbmColor);
					if (info.hbmMask)
						DeleteObject(info.hbmMask);
				}
			}
			else if (GetObjectW(image, sizeof(bitmap), &bitmap))
			{
				size = {bitmap.bmWidth, bitmap.bmHeight};
			}
			return size;
		}

		void drawImage(HWND hwnd, const Static& control, HDC hdc, const RECT& client, UINT type)
		{
			if (!control._image)
				return;
			const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
			SIZE size = imageSize(control._image, type);
			if (style & SS_REALSIZEIMAGE)
				size = imageSize(control._image, type);
			else if (type == SS_ICON && !(style & SS_CENTERIMAGE))
				size = {client.right - client.left, client.bottom - client.top};
			int x = client.left;
			int y = client.top;
			if (style & SS_CENTERIMAGE)
			{
				x = (client.left + client.right - size.cx) / 2;
				y = (client.top + client.bottom - size.cy) / 2;
			}
			if (type == SS_ICON)
			{
				DrawIconEx(hdc, x, y, static_cast<HICON>(control._image), size.cx, size.cy, 0, nullptr, DI_NORMAL);
				return;
			}
			HDC source = CreateCompatibleDC(hdc);
			HGDIOBJ old = SelectObject(source, control._image);
			BitBlt(hdc, x, y, size.cx, size.cy, source, 0, 0, SRCCOPY);
			SelectObject(source, old);
			DeleteDC(source);
		}

		void drawText(HWND hwnd, HDC hdc, const RECT& client, UINT type)
		{
			const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
			const std::u16string text = windowText(hwnd);
			UINT format = DT_EXPANDTABS;
			switch (type)
			{
				case SS_CENTER:
					format |= DT_CENTER | DT_WORDBREAK;
					break;
				case SS_RIGHT:
					format |= DT_RIGHT | DT_WORDBREAK;
					break;
				case SS_SIMPLE:
				case SS_LEFTNOWORDWRAP:
					format |= DT_LEFT | DT_SINGLELINE;
					break;
				default:
					format |= DT_LEFT | DT_WORDBREAK;
					break;
			}
			if (style & SS_NOPREFIX)
				format |= DT_NOPREFIX;
			if (style & SS_CENTERIMAGE)
				format = (format & ~static_cast<UINT>(DT_WORDBREAK)) | DT_SINGLELINE | DT_VCENTER;
			switch (style & SS_ELLIPSISMASK)
			{
				case SS_ENDELLIPSIS:
					format |= DT_END_ELLIPSIS | DT_SINGLELINE;
					break;
				case SS_PATHELLIPSIS:
					format |= DT_PATH_ELLIPSIS | DT_SINGLELINE;
					break;
				case SS_WORDELLIPSIS:
					format |= DT_WORD_ELLIPSIS | DT_SINGLELINE;
					break;
				default:
					break;
			}
			if (format & DT_SINGLELINE)
				format &= ~static_cast<UINT>(DT_WORDBREAK);
			HGDIOBJ oldFont = SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			if (!IsWindowEnabled(hwnd))
				SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
			RECT rect = client;
			DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect, format);
			SelectObject(hdc, oldFont);
		}

		void paint(HWND hwnd, Static& control, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const UINT type = typeOf(hwnd);
			if (type == SS_OWNERDRAW)
			{
				DRAWITEMSTRUCT item{};
				item.CtlType = ODT_STATIC;
				item.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
				item.itemAction = ODA_DRAWENTIRE;
				item.itemState = IsWindowEnabled(hwnd) ? 0 : ODS_DISABLED;
				item.hwndItem = hwnd;
				item.hDC = hdc;
				item.rcItem = client;
				SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
				return;
			}

			const int saved = SaveDC(hdc);
			switch (type)
			{
				case SS_BLACKRECT:
				case SS_GRAYRECT:
				case SS_WHITERECT:
				{
					const int color = type == SS_BLACKRECT ? COLOR_WINDOWFRAME : (type == SS_GRAYRECT ? COLOR_BTNSHADOW : COLOR_WINDOW);
					FillRect(hdc, &client, GetSysColorBrush(color));
					break;
				}
				case SS_BLACKFRAME:
				case SS_GRAYFRAME:
				case SS_WHITEFRAME:
				{
					const int color = type == SS_BLACKFRAME ? COLOR_WINDOWFRAME : (type == SS_GRAYFRAME ? COLOR_BTNSHADOW : COLOR_WINDOW);
					FrameRect(hdc, &client, GetSysColorBrush(color));
					break;
				}
				case SS_ETCHEDHORZ:
				{
					RECT line{client.left, client.top, client.right, client.top + 2};
					DrawEdge(hdc, &line, EDGE_ETCHED, BF_TOP);
					break;
				}
				case SS_ETCHEDVERT:
				{
					RECT line{client.left, client.top, client.left + 2, client.bottom};
					DrawEdge(hdc, &line, EDGE_ETCHED, BF_LEFT);
					break;
				}
				case SS_ETCHEDFRAME:
				{
					RECT frame = client;
					DrawEdge(hdc, &frame, EDGE_ETCHED, BF_RECT);
					break;
				}
				case SS_ICON:
				case SS_BITMAP:
				case SS_ENHMETAFILE:
				{
					FillRect(hdc, &client, controlColors(hwnd, hdc, WM_CTLCOLORSTATIC));
					drawImage(hwnd, control, hdc, client, type);
					break;
				}
				default:
				{
					FillRect(hdc, &client, controlColors(hwnd, hdc, WM_CTLCOLORSTATIC));
					drawText(hwnd, hdc, client, type);
					break;
				}
			}
			RestoreDC(hdc, saved);
			if (GetWindowLongW(hwnd, GWL_STYLE) & SS_SUNKEN)
			{
				RECT frame = client;
				DrawEdge(hdc, &frame, BDR_SUNKENOUTER, BF_RECT);
			}
		}

		// Static icons and bitmaps named in a dialog template ("#123" or a name) are loaded on creation.
		void loadTemplateImage(HWND hwnd, Static& control, const CREATESTRUCTW& create)
		{
			const UINT type = typeOf(hwnd);
			if ((type != SS_ICON && type != SS_BITMAP) || !create.lpszName)
				return;
			LPCWSTR name = create.lpszName;
			if (!IS_INTRESOURCE(name) && name[0] == 0xFFFF)
				name = MAKEINTRESOURCEW(name[1]);
			else if (!IS_INTRESOURCE(name) && !*name)
				return;
			if (type == SS_ICON)
				control._image = LoadImageW(create.hInstance, name, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			else
				control._image = LoadImageW(create.hInstance, name, IMAGE_BITMAP, 0, 0, 0);
		}

		LRESULT CALLBACK staticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Static* control = dataOf<Static>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					Static* created = createData<Static>(hwnd);
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					loadTemplateImage(hwnd, *created, *create);
					// An owner-drawn static paints only what its owner draws; what is under it shows.
					if (typeOf(hwnd) == SS_OWNERDRAW)
						user::setSeeThrough(hwnd, true);
					// Image statics keep their name out of the window text.
					const UINT type = typeOf(hwnd);
					if (type == SS_ICON || type == SS_BITMAP)
						return TRUE;
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_NCHITTEST:
					// Only SS_NOTIFY statics take the mouse; the others let it through to their parent.
					return (GetWindowLongW(hwnd, GWL_STYLE) & SS_NOTIFY) ? HTCLIENT : HTTRANSPARENT;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *control, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *control, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_LBUTTONDOWN:
				case WM_LBUTTONDBLCLK:
					if (GetWindowLongW(hwnd, GWL_STYLE) & SS_NOTIFY)
						sendCommand(hwnd, message == WM_LBUTTONDOWN ? STN_CLICKED : STN_DBLCLK);
					return 0;
				case WM_ENABLE:
					if (GetWindowLongW(hwnd, GWL_STYLE) & SS_NOTIFY)
						sendCommand(hwnd, wParam ? STN_ENABLE : STN_DISABLE);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_SETTEXT:
				case WM_SETFONT:
				case WM_STYLECHANGED:
				{
					const LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return message == WM_SETTEXT ? TRUE : result;
				}
				case STM_SETICON:
				case STM_SETIMAGE:
				{
					HANDLE previous = control->_image;
					control->_image = reinterpret_cast<HANDLE>(message == STM_SETICON ? wParam : lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return reinterpret_cast<LRESULT>(previous);
				}
				case STM_GETICON:
				case STM_GETIMAGE:
					return reinterpret_cast<LRESULT>(control->_image);
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerStatic()
	{
		registerControl(WC_STATICW, staticProc);
	}
}
