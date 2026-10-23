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

// The toolbar control (ToolbarWindow32): one row of flat buttons with images.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr int separatorWidth = 8;
		constexpr int dropDownArrowWidth = 12;

		struct Button
		{
			int _image = I_IMAGENONE;
			int _command = 0;
			BYTE _state = TBSTATE_ENABLED;
			BYTE _style = BTNS_BUTTON;
			DWORD_PTR _data = 0;
			std::u16string _text;
			int _width = 0; // separators: their width
		};

		struct Toolbar
		{
			std::vector<Button> _buttons;
			HIMAGELIST _images = nullptr;
			HIMAGELIST _disabledImages = nullptr;
			HIMAGELIST _hotImages = nullptr;
			int _buttonWidth = 24;
			int _buttonHeight = 22;
			int _bitmapWidth = 16;
			int _bitmapHeight = 16;
			DWORD _extendedStyle = 0;
			int _paddingX = 7;
			int _paddingY = 6;
			int _hot = -1;
			int _pressed = -1;
			bool _buttonSizeSet = false;
			HWND _notify = nullptr;
			HIMAGELIST _addedBitmaps = nullptr; // the toolbar's own list, filled by TB_ADDBITMAP

			~Toolbar()
			{
				if (_addedBitmaps)
					ImageList_Destroy(_addedBitmaps);
			}
		};

		bool isSeparator(const Button& button)
		{
			return (button._style & BTNS_SEP) != 0;
		}

		void updateButtonSize(Toolbar& bar)
		{
			if (bar._buttonSizeSet)
				return;
			int width = 0;
			int height = 0;
			if (bar._images)
				ImageList_GetIconSize(bar._images, &width, &height);
			else
			{
				width = bar._bitmapWidth;
				height = bar._bitmapHeight;
			}
			bar._buttonWidth = width + bar._paddingX;
			bar._buttonHeight = height + bar._paddingY;
		}

		int buttonWidth(const Toolbar& bar, const Button& button)
		{
			if (isSeparator(button))
				return button._width > 0 ? button._width : separatorWidth;
			int width = bar._buttonWidth;
			if ((button._style & BTNS_DROPDOWN) && (bar._extendedStyle & TBSTYLE_EX_DRAWDDARROWS))
				width += dropDownArrowWidth;
			return width;
		}

		RECT itemRect(const Toolbar& bar, size_t index)
		{
			int x = 0;
			for (size_t i = 0; i < index; ++i)
			{
				if (!(bar._buttons[i]._state & TBSTATE_HIDDEN))
					x += buttonWidth(bar, bar._buttons[i]);
			}
			const Button& button = bar._buttons[index];
			if (button._state & TBSTATE_HIDDEN)
				return {x, 0, x, 0};
			return {x, 0, x + buttonWidth(bar, button), bar._buttonHeight};
		}

		int hitTest(const Toolbar& bar, POINT point)
		{
			for (size_t i = 0; i < bar._buttons.size(); ++i)
			{
				const RECT rect = itemRect(bar, i);
				if (PtInRect(&rect, point))
					return static_cast<int>(i);
			}
			return -1;
		}

		int indexOf(const Toolbar& bar, int command)
		{
			for (size_t i = 0; i < bar._buttons.size(); ++i)
			{
				if (bar._buttons[i]._command == command && !isSeparator(bar._buttons[i]))
					return static_cast<int>(i);
			}
			return -1;
		}

		SIZE idealSize(const Toolbar& bar)
		{
			int width = 0;
			for (const Button& button : bar._buttons)
			{
				if (!(button._state & TBSTATE_HIDDEN))
					width += buttonWidth(bar, button);
			}
			return {width, bar._buttonHeight};
		}

		Button fromTbButton(const TBBUTTON& source)
		{
			Button button;
			button._image = source.iBitmap;
			button._command = source.idCommand;
			button._state = source.fsState;
			button._style = source.fsStyle;
			button._data = source.dwData;
			if (source.iString && !IS_INTRESOURCE(source.iString))
				button._text = reinterpret_cast<LPCWSTR>(source.iString);
			if (isSeparator(button))
				button._width = source.iBitmap > 0 ? source.iBitmap : separatorWidth;
			return button;
		}

		void paint(HWND hwnd, Toolbar& bar, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			HWND notify = notifyWindow(hwnd, bar._notify);

			// Custom draw, as Notepad++'s dark mode uses it.
			NMTBCUSTOMDRAW custom{};
			custom.nmcd.dwDrawStage = CDDS_PREPAINT;
			custom.nmcd.hdc = hdc;
			custom.nmcd.rc = client;
			const LRESULT prePaint = sendNotify(hwnd, notify, NM_CUSTOMDRAW, &custom.nmcd.hdr);
			if (!(prePaint & CDRF_SKIPDEFAULT))
			{
				// Without TBSTYLE_TRANSPARENT the toolbar paints its own background.
				if (!(GetWindowLongW(hwnd, GWL_STYLE) & TBSTYLE_TRANSPARENT))
					FillRect(hdc, &client, GetSysColorBrush(COLOR_BTNFACE));
				else
					SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(hdc), 0);
			}

			for (size_t i = 0; i < bar._buttons.size(); ++i)
			{
				const Button& button = bar._buttons[i];
				if (button._state & TBSTATE_HIDDEN)
					continue;
				const RECT rect = itemRect(bar, i);
				if (isSeparator(button))
				{
					RECT line{(rect.left + rect.right) / 2, rect.top + 3, (rect.left + rect.right) / 2 + 1, rect.bottom - 3};
					FillRect(hdc, &line, GetSysColorBrush(COLOR_BTNSHADOW));
					continue;
				}
				const bool enabled = (button._state & TBSTATE_ENABLED) != 0;
				const bool checked = (button._state & TBSTATE_CHECKED) != 0;
				const bool hot = enabled && static_cast<int>(i) == bar._hot;
				const bool pressed = enabled && static_cast<int>(i) == bar._pressed && hot;

				LRESULT itemResult = CDRF_DODEFAULT;
				if (prePaint & CDRF_NOTIFYITEMDRAW)
				{
					custom.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
					custom.nmcd.rc = rect;
					custom.nmcd.dwItemSpec = static_cast<DWORD_PTR>(button._command);
					custom.nmcd.uItemState = (hot ? CDIS_HOT : 0) | (pressed ? CDIS_SELECTED : 0) | (checked ? CDIS_CHECKED : 0) | (enabled ? 0 : CDIS_DISABLED);
					custom.nmcd.lItemlParam = static_cast<LPARAM>(button._data);
					custom.clrBtnFace = GetSysColor(COLOR_BTNFACE);
					custom.clrBtnHighlight = GetSysColor(COLOR_BTNHIGHLIGHT);
					custom.clrHighlightHotTrack = GetSysColor(COLOR_HIGHLIGHT);
					custom.nStringBkMode = TRANSPARENT;
					custom.nHLStringBkMode = TRANSPARENT;
					itemResult = sendNotify(hwnd, notify, NM_CUSTOMDRAW, &custom.nmcd.hdr);
					if (itemResult & CDRF_SKIPDEFAULT)
						continue;
				}

				if (!(itemResult & TBCDRF_NOBACKGROUND) && (hot || pressed || checked))
				{
					// A Windows 10 flat toolbar: light blue when hot, a darker frame when pressed or checked.
					HBRUSH fill = CreateSolidBrush(pressed ? RGB(204, 228, 247) : (checked ? RGB(204, 232, 255) : RGB(229, 243, 255)));
					HBRUSH frame = CreateSolidBrush(pressed || checked ? RGB(153, 209, 255) : RGB(204, 232, 255));
					FillRect(hdc, &rect, fill);
					FrameRect(hdc, &rect, frame);
					DeleteObject(fill);
					DeleteObject(frame);
				}

				HIMAGELIST images = !enabled && bar._disabledImages ? bar._disabledImages : (hot && bar._hotImages ? bar._hotImages : bar._images);
				if (images && button._image >= 0)
				{
					int imageWidth = 0;
					int imageHeight = 0;
					ImageList_GetIconSize(images, &imageWidth, &imageHeight);
					const int contentWidth = buttonWidth(bar, button) - (((button._style & BTNS_DROPDOWN) && (bar._extendedStyle & TBSTYLE_EX_DRAWDDARROWS)) ? dropDownArrowWidth : 0);
					const int x = rect.left + (contentWidth - imageWidth) / 2 + (pressed ? 1 : 0);
					const int y = rect.top + (bar._buttonHeight - imageHeight) / 2 + (pressed ? 1 : 0);
					ImageList_Draw(images, button._image, hdc, x, y, (!enabled && !bar._disabledImages) ? ILD_BLEND50 : ILD_NORMAL);
				}
				if ((button._style & BTNS_DROPDOWN) && (bar._extendedStyle & TBSTYLE_EX_DRAWDDARROWS))
				{
					const int cx = rect.right - dropDownArrowWidth / 2;
					const int cy = (rect.top + rect.bottom) / 2;
					const POINT arrow[] = {{cx - 3, cy - 1}, {cx + 3, cy - 1}, {cx, cy + 2}};
					HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(BLACK_BRUSH));
					HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
					Polygon(hdc, arrow, 3);
					SelectObject(hdc, oldBrush);
					SelectObject(hdc, oldPen);
				}

				if (itemResult & CDRF_NOTIFYPOSTPAINT)
				{
					custom.nmcd.dwDrawStage = CDDS_ITEMPOSTPAINT;
					sendNotify(hwnd, notify, NM_CUSTOMDRAW, &custom.nmcd.hdr);
				}
			}
		}

		void setHot(HWND hwnd, Toolbar& bar, int index)
		{
			if (bar._hot == index)
				return;
			bar._hot = index;
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		LRESULT CALLBACK toolbarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Toolbar* bar = dataOf<Toolbar>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					createData<Toolbar>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
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
				case WM_MOUSEMOVE:
				{
					setHot(hwnd, *bar, hitTest(*bar, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
					return 0;
				}
				case WM_MOUSELEAVE:
					setHot(hwnd, *bar, -1);
					return 0;
				case WM_LBUTTONDOWN:
				case WM_LBUTTONDBLCLK:
				{
					const int index = hitTest(*bar, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
					if (index < 0 || isSeparator(bar->_buttons[static_cast<size_t>(index)]) || !(bar->_buttons[static_cast<size_t>(index)]._state & TBSTATE_ENABLED))
						return 0;
					const Button& button = bar->_buttons[static_cast<size_t>(index)];
					// The arrow part of a drop-down button asks the parent for its menu.
					const RECT rect = itemRect(*bar, static_cast<size_t>(index));
					const bool onArrow = (button._style & BTNS_WHOLEDROPDOWN) || ((button._style & BTNS_DROPDOWN)
						&& (!(bar->_extendedStyle & TBSTYLE_EX_DRAWDDARROWS) || GET_X_LPARAM(lParam) >= rect.right - dropDownArrowWidth));
					if (onArrow)
					{
						NMTOOLBARW dropDown{};
						dropDown.iItem = button._command;
						dropDown.rcButton = rect;
						sendNotify(hwnd, notifyWindow(hwnd, bar->_notify), TBN_DROPDOWN, &dropDown.hdr);
						return 0;
					}
					bar->_pressed = index;
					bar->_hot = index;
					SetCapture(hwnd);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case WM_LBUTTONUP:
				{
					const int pressed = bar->_pressed;
					bar->_pressed = -1;
					if (GetCapture() == hwnd)
						ReleaseCapture();
					InvalidateRect(hwnd, nullptr, FALSE);
					const int index = hitTest(*bar, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
					if (pressed >= 0 && index == pressed)
					{
						Button& button = bar->_buttons[static_cast<size_t>(index)];
						if (button._style & BTNS_CHECK)
							button._state ^= TBSTATE_CHECKED;
						PostMessageW(notifyWindow(hwnd, bar->_notify), WM_COMMAND, MAKEWPARAM(button._command, BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
					}
					return 0;
				}
				case WM_RBUTTONUP:
				{
					NMMOUSE click{};
					click.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const int index = hitTest(*bar, click.pt);
					click.dwItemSpec = index >= 0 ? static_cast<DWORD_PTR>(bar->_buttons[static_cast<size_t>(index)]._command) : static_cast<DWORD_PTR>(-1);
					sendNotify(hwnd, notifyWindow(hwnd, bar->_notify), NM_RCLICK, &click.hdr);
					return 0;
				}

				case TB_BUTTONSTRUCTSIZE:
				case TB_SETMAXTEXTROWS:
				case TB_SETDRAWTEXTFLAGS:
				case TB_SETCOLORSCHEME:
				case TB_SETINDENT:
					return 0;
				case TB_SETPARENT:
				{
					HWND previous = notifyWindow(hwnd, bar->_notify);
					bar->_notify = reinterpret_cast<HWND>(wParam);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TB_SETIMAGELIST:
				{
					HIMAGELIST previous = bar->_images;
					bar->_images = reinterpret_cast<HIMAGELIST>(lParam);
					updateButtonSize(*bar);
					InvalidateRect(hwnd, nullptr, TRUE);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TB_GETIMAGELIST:
					return reinterpret_cast<LRESULT>(bar->_images);
				case TB_SETDISABLEDIMAGELIST:
				{
					HIMAGELIST previous = bar->_disabledImages;
					bar->_disabledImages = reinterpret_cast<HIMAGELIST>(lParam);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TB_GETDISABLEDIMAGELIST:
					return reinterpret_cast<LRESULT>(bar->_disabledImages);
				case TB_SETHOTIMAGELIST:
				{
					HIMAGELIST previous = bar->_hotImages;
					bar->_hotImages = reinterpret_cast<HIMAGELIST>(lParam);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TB_GETHOTIMAGELIST:
					return reinterpret_cast<LRESULT>(bar->_hotImages);
				case TB_SETBITMAPSIZE:
					bar->_bitmapWidth = LOWORD(lParam);
					bar->_bitmapHeight = HIWORD(lParam);
					updateButtonSize(*bar);
					return TRUE;
				case TB_SETBUTTONSIZE:
				{
					// As on Windows, a button is never smaller than its image and the padding around it.
					int imageWidth = bar->_bitmapWidth;
					int imageHeight = bar->_bitmapHeight;
					if (bar->_images)
						ImageList_GetIconSize(bar->_images, &imageWidth, &imageHeight);
					bar->_buttonWidth = std::max<int>(LOWORD(lParam), imageWidth + bar->_paddingX);
					bar->_buttonHeight = std::max<int>(HIWORD(lParam), imageHeight + bar->_paddingY);
					bar->_buttonSizeSet = true;
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TB_GETBUTTONSIZE:
					return MAKELONG(bar->_buttonWidth, bar->_buttonHeight);
				case TB_SETPADDING:
				{
					const LRESULT previous = MAKELONG(bar->_paddingX, bar->_paddingY);
					bar->_paddingX = LOWORD(lParam);
					bar->_paddingY = HIWORD(lParam);
					updateButtonSize(*bar);
					return previous;
				}
				case TB_GETPADDING:
					return MAKELONG(bar->_paddingX, bar->_paddingY);
				case TB_SETEXTENDEDSTYLE:
				{
					const DWORD previous = bar->_extendedStyle;
					bar->_extendedStyle = wParam ? ((bar->_extendedStyle & ~static_cast<DWORD>(wParam)) | (static_cast<DWORD>(lParam) & static_cast<DWORD>(wParam))) : static_cast<DWORD>(lParam);
					return previous;
				}
				case TB_GETEXTENDEDSTYLE:
					return bar->_extendedStyle;
				case TB_ADDBITMAP:
				{
					// The bitmap goes into the toolbar's own image list, which is used unless one is set.
					const auto* added = reinterpret_cast<const TBADDBITMAP*>(lParam);
					if (!added || added->hInst == HINST_COMMCTRL)
						return -1;
					HBITMAP bitmap = added->hInst ? LoadBitmapW(added->hInst, MAKEINTRESOURCEW(added->nID)) : reinterpret_cast<HBITMAP>(added->nID);
					if (!bitmap)
						return -1;
					if (!bar->_addedBitmaps)
						bar->_addedBitmaps = ImageList_Create(bar->_bitmapWidth, bar->_bitmapHeight, ILC_COLOR32, 0, 0);
					if (!bar->_images)
						bar->_images = bar->_addedBitmaps;
					const int first = ImageList_Add(bar->_addedBitmaps, bitmap, nullptr);
					if (added->hInst)
						DeleteObject(bitmap);
					updateButtonSize(*bar);
					InvalidateRect(hwnd, nullptr, TRUE);
					return first;
				}
				case TB_ADDBUTTONSW:
				case TB_INSERTBUTTONW:
				{
					const auto* buttons = reinterpret_cast<const TBBUTTON*>(lParam);
					if (message == TB_ADDBUTTONSW)
					{
						for (WPARAM i = 0; i < wParam; ++i)
							bar->_buttons.push_back(fromTbButton(buttons[i]));
					}
					else
					{
						const size_t index = std::min<size_t>(wParam, bar->_buttons.size());
						bar->_buttons.insert(bar->_buttons.begin() + static_cast<std::ptrdiff_t>(index), fromTbButton(*buttons));
					}
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TB_DELETEBUTTON:
				{
					if (wParam >= bar->_buttons.size())
						return FALSE;
					bar->_buttons.erase(bar->_buttons.begin() + static_cast<std::ptrdiff_t>(wParam));
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TB_BUTTONCOUNT:
					return static_cast<LRESULT>(bar->_buttons.size());
				case TB_COMMANDTOINDEX:
					return indexOf(*bar, static_cast<int>(wParam));
				case TB_GETBUTTON:
				{
					if (wParam >= bar->_buttons.size())
						return FALSE;
					const Button& button = bar->_buttons[wParam];
					auto* out = reinterpret_cast<TBBUTTON*>(lParam);
					out->iBitmap = isSeparator(button) ? button._width : button._image;
					out->idCommand = button._command;
					out->fsState = button._state;
					out->fsStyle = button._style;
					out->dwData = button._data;
					out->iString = 0;
					return TRUE;
				}
				case TB_GETBUTTONINFOW:
				case TB_SETBUTTONINFOW:
				{
					auto* info = reinterpret_cast<TBBUTTONINFOW*>(lParam);
					const int index = (info->dwMask & TBIF_BYINDEX) ? static_cast<int>(wParam) : indexOf(*bar, static_cast<int>(wParam));
					if (index < 0 || static_cast<size_t>(index) >= bar->_buttons.size())
						return -1;
					Button& button = bar->_buttons[static_cast<size_t>(index)];
					if (message == TB_GETBUTTONINFOW)
					{
						if (info->dwMask & TBIF_COMMAND)
							info->idCommand = button._command;
						if (info->dwMask & TBIF_IMAGE)
							info->iImage = button._image;
						if (info->dwMask & TBIF_STATE)
							info->fsState = button._state;
						if (info->dwMask & TBIF_STYLE)
							info->fsStyle = button._style;
						if (info->dwMask & TBIF_LPARAM)
							info->lParam = static_cast<DWORD_PTR>(button._data);
						if (info->dwMask & TBIF_SIZE)
							info->cx = static_cast<WORD>(buttonWidth(*bar, button));
						if ((info->dwMask & TBIF_TEXT) && info->pszText && info->cchText > 0)
							lstrcpynW(info->pszText, button._text.c_str(), info->cchText);
					}
					else
					{
						if (info->dwMask & TBIF_COMMAND)
							button._command = info->idCommand;
						if (info->dwMask & TBIF_IMAGE)
							button._image = info->iImage;
						if (info->dwMask & TBIF_STATE)
							button._state = info->fsState;
						if (info->dwMask & TBIF_STYLE)
							button._style = info->fsStyle;
						if (info->dwMask & TBIF_LPARAM)
							button._data = static_cast<DWORD_PTR>(info->lParam);
						if ((info->dwMask & TBIF_TEXT) && info->pszText)
							button._text = info->pszText;
						InvalidateRect(hwnd, nullptr, TRUE);
					}
					return index;
				}
				case TB_ENABLEBUTTON:
				case TB_CHECKBUTTON:
				case TB_HIDEBUTTON:
				case TB_PRESSBUTTON:
				{
					const int index = indexOf(*bar, static_cast<int>(wParam));
					if (index < 0)
						return FALSE;
					BYTE& state = bar->_buttons[static_cast<size_t>(index)]._state;
					const BYTE flag = message == TB_ENABLEBUTTON ? TBSTATE_ENABLED : (message == TB_CHECKBUTTON ? TBSTATE_CHECKED
						: (message == TB_HIDEBUTTON ? TBSTATE_HIDDEN : TBSTATE_PRESSED));
					if (LOWORD(lParam))
						state |= flag;
					else
						state &= static_cast<BYTE>(~flag);
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TB_ISBUTTONENABLED:
				case TB_ISBUTTONCHECKED:
				case TB_ISBUTTONHIDDEN:
				case TB_ISBUTTONPRESSED:
				{
					const int index = indexOf(*bar, static_cast<int>(wParam));
					if (index < 0)
						return FALSE;
					const BYTE state = bar->_buttons[static_cast<size_t>(index)]._state;
					const BYTE flag = message == TB_ISBUTTONENABLED ? TBSTATE_ENABLED : (message == TB_ISBUTTONCHECKED ? TBSTATE_CHECKED
						: (message == TB_ISBUTTONHIDDEN ? TBSTATE_HIDDEN : TBSTATE_PRESSED));
					return (state & flag) ? TRUE : FALSE;
				}
				case TB_GETSTATE:
				{
					const int index = indexOf(*bar, static_cast<int>(wParam));
					return index < 0 ? -1 : bar->_buttons[static_cast<size_t>(index)]._state;
				}
				case TB_SETSTATE:
				{
					const int index = indexOf(*bar, static_cast<int>(wParam));
					if (index < 0)
						return FALSE;
					bar->_buttons[static_cast<size_t>(index)]._state = static_cast<BYTE>(LOWORD(lParam));
					InvalidateRect(hwnd, nullptr, TRUE);
					return TRUE;
				}
				case TB_GETITEMRECT:
				case TB_GETITEMDROPDOWNRECT:
				{
					if (wParam >= bar->_buttons.size())
						return FALSE;
					RECT rect = itemRect(*bar, wParam);
					if (message == TB_GETITEMDROPDOWNRECT)
						rect.left = rect.right - dropDownArrowWidth;
					*reinterpret_cast<RECT*>(lParam) = rect;
					return TRUE;
				}
				case TB_GETRECT:
				{
					const int index = indexOf(*bar, static_cast<int>(wParam));
					if (index < 0)
						return FALSE;
					*reinterpret_cast<RECT*>(lParam) = itemRect(*bar, static_cast<size_t>(index));
					return TRUE;
				}
				case TB_HITTEST:
				{
					const int index = hitTest(*bar, *reinterpret_cast<POINT*>(lParam));
					return index;
				}
				case TB_GETMAXSIZE:
				case TB_GETIDEALSIZE:
				{
					const SIZE size = idealSize(*bar);
					if (message == TB_GETIDEALSIZE)
					{
						auto* out = reinterpret_cast<SIZE*>(lParam);
						if (wParam)
							out->cy = size.cy;
						else
							out->cx = size.cx;
					}
					else
					{
						*reinterpret_cast<SIZE*>(lParam) = size;
					}
					return TRUE;
				}
				case TB_GETROWS:
					return 1;
				case TB_AUTOSIZE:
				{
					// Fits the control to its buttons, docked at the top unless told otherwise.
					const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
					const SIZE size = idealSize(*bar);
					if (!(style & CCS_NORESIZE))
					{
						RECT parent{};
						GetClientRect(GetParent(hwnd), &parent);
						if (style & CCS_NOPARENTALIGN)
							SetWindowPos(hwnd, nullptr, 0, 0, std::max<LONG>(size.cx, 0), size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
						else
							SetWindowPos(hwnd, nullptr, 0, 0, parent.right, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
					}
					return 0;
				}
				case TB_GETTOOLTIPS:
					return 0;
				case TB_SETTOOLTIPS:
					return 0;
				case TB_GETHOTITEM:
					return bar->_hot;
				case TB_SETHOTITEM:
				{
					const int previous = bar->_hot;
					setHot(hwnd, *bar, static_cast<int>(wParam));
					return previous;
				}
				case TB_CUSTOMIZE:
				case TB_SAVERESTOREW:
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerToolbar()
	{
		registerControl(TOOLBARCLASSNAMEW, toolbarProc);
	}
}
