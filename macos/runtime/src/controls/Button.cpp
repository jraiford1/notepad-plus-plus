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

// The button control (Button): push buttons, check boxes, radio buttons, group boxes and owner-drawn
// buttons, drawn as Windows 10 draws them. Colors come from the parent through WM_CTLCOLORBTN (push
// buttons) and WM_CTLCOLORSTATIC (the others), as on Windows, so dark mode works unchanged.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr int checkBoxSize = 13;
		constexpr int checkBoxTextGap = 4;
		constexpr int pushButtonTextMargin = 8;
		constexpr int splitWidth = 16;

		enum class ButtonKind
		{
			push,
			checkBox,
			radio,
			groupBox,
			ownerDraw,
		};

		struct Button
		{
			UINT _check = BST_UNCHECKED;
			bool _pushed = false;      // held down by the mouse or the space bar
			bool _hot = false;         // under the mouse
			bool _tracking = false;    // the mouse went down on it and is captured
			bool _focused = false;
			HANDLE _image = nullptr;   // BM_SETIMAGE
			UINT _imageType = IMAGE_BITMAP;
		};

		UINT typeOf(HWND hwnd)
		{
			return static_cast<UINT>(GetWindowLongW(hwnd, GWL_STYLE)) & BS_TYPEMASK;
		}

		ButtonKind kindOf(UINT type)
		{
			switch (type)
			{
				case BS_CHECKBOX:
				case BS_AUTOCHECKBOX:
				case BS_3STATE:
				case BS_AUTO3STATE:
					return ButtonKind::checkBox;
				case BS_RADIOBUTTON:
				case BS_AUTORADIOBUTTON:
					return ButtonKind::radio;
				case BS_GROUPBOX:
					return ButtonKind::groupBox;
				case BS_OWNERDRAW:
					return ButtonKind::ownerDraw;
				default:
					return ButtonKind::push;
			}
		}

		bool isPushLike(HWND hwnd)
		{
			return (GetWindowLongW(hwnd, GWL_STYLE) & BS_PUSHLIKE) != 0;
		}

		bool isDefault(UINT type)
		{
			return type == BS_DEFPUSHBUTTON || type == BS_DEFSPLITBUTTON;
		}

		bool isSplit(UINT type)
		{
			return type == BS_SPLITBUTTON || type == BS_DEFSPLITBUTTON;
		}

		HBRUSH solidBrush(COLORREF color)
		{
			return CreateSolidBrush(color);
		}

		void fillAndFrame(HDC hdc, const RECT& rect, COLORREF fill, COLORREF frame)
		{
			HBRUSH fillBrush = solidBrush(fill);
			HBRUSH frameBrush = solidBrush(frame);
			FillRect(hdc, &rect, fillBrush);
			FrameRect(hdc, &rect, frameBrush);
			DeleteObject(fillBrush);
			DeleteObject(frameBrush);
		}

		UINT textFormat(HWND hwnd, ButtonKind kind)
		{
			const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
			UINT format = (style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;
			const LONG horizontal = style & BS_CENTER;
			if (horizontal == BS_LEFT)
				format |= DT_LEFT;
			else if (horizontal == BS_RIGHT)
				format |= DT_RIGHT;
			else if (horizontal == BS_CENTER || kind == ButtonKind::push)
				format |= DT_CENTER;
			const LONG vertical = style & BS_VCENTER;
			if (vertical == BS_TOP)
				format |= DT_TOP;
			else if (vertical == BS_BOTTOM)
				format |= DT_BOTTOM;
			else if (!(style & BS_MULTILINE))
				format |= DT_VCENTER;
			return format;
		}

		// The text, or the image of a BS_ICON / BS_BITMAP button, centered in the rectangle.
		void drawContent(HWND hwnd, const Button& button, HDC hdc, RECT rect, ButtonKind kind, bool enabled)
		{
			const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
			if ((style & (BS_ICON | BS_BITMAP)) && button._image)
			{
				int width = 0;
				int height = 0;
				if (button._imageType == IMAGE_ICON)
				{
					ICONINFO info{};
					BITMAP bitmap{};
					if (GetIconInfo(static_cast<HICON>(button._image), &info) && GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap))
					{
						width = bitmap.bmWidth;
						height = bitmap.bmHeight;
					}
					if (info.hbmColor)
						DeleteObject(info.hbmColor);
					if (info.hbmMask)
						DeleteObject(info.hbmMask);
					DrawIconEx(hdc, (rect.left + rect.right - width) / 2, (rect.top + rect.bottom - height) / 2, static_cast<HICON>(button._image), width, height, 0, nullptr, DI_NORMAL);
				}
				else
				{
					BITMAP bitmap{};
					GetObjectW(button._image, sizeof(bitmap), &bitmap);
					HDC source = CreateCompatibleDC(hdc);
					HGDIOBJ old = SelectObject(source, button._image);
					BitBlt(hdc, (rect.left + rect.right - bitmap.bmWidth) / 2, (rect.top + rect.bottom - bitmap.bmHeight) / 2, bitmap.bmWidth, bitmap.bmHeight, source, 0, 0, SRCCOPY);
					SelectObject(source, old);
					DeleteDC(source);
				}
				return;
			}

			const std::u16string text = windowText(hwnd);
			if (text.empty())
				return;
			HGDIOBJ oldFont = SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			if (!enabled)
				SetTextColor(hdc, controlPalette()._textDisabled);
			UINT format = textFormat(hwnd, kind);
			if (format & DT_WORDBREAK)
			{
				// Multi-line text is centered vertically by measuring it first.
				RECT measured = rect;
				DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &measured, format | DT_CALCRECT);
				const int height = measured.bottom - measured.top;
				if (!(format & (DT_BOTTOM)) && height < rect.bottom - rect.top)
					rect.top += (rect.bottom - rect.top - height) / 2;
			}
			DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect, format);
			SelectObject(hdc, oldFont);
		}

		void drawPushButton(HWND hwnd, const Button& button, HDC hdc, const RECT& client, UINT type, bool enabled)
		{
			// The background around the button is the parent's.
			FillRect(hdc, &client, controlColors(hwnd, hdc, WM_CTLCOLORBTN));
			const bool down = button._pushed || (isPushLike(hwnd) && button._check == BST_CHECKED);
			const ControlPalette& palette = controlPalette();
			COLORREF fill = palette._face;
			COLORREF frame = palette._frame;
			if (!enabled)
			{
				fill = palette._faceDisabled;
				frame = palette._frameDisabled;
			}
			else if (down)
			{
				fill = palette._facePressed;
				frame = palette._accent;
			}
			else if (button._hot)
			{
				fill = palette._faceHot;
				frame = palette._frameHot;
			}
			else if (isDefault(type) || button._focused)
			{
				frame = palette._accent;
			}
			fillAndFrame(hdc, client, fill, frame);
			if (enabled && !down && (isDefault(type) || button._focused))
			{
				// The default button's frame is two pixels wide.
				RECT inner{client.left + 1, client.top + 1, client.right - 1, client.bottom - 1};
				HBRUSH frameBrush = solidBrush(frame);
				FrameRect(hdc, &inner, frameBrush);
				DeleteObject(frameBrush);
			}

			RECT content{client.left + pushButtonTextMargin / 2, client.top + 1, client.right - pushButtonTextMargin / 2, client.bottom - 1};
			if (isSplit(type))
			{
				content.right -= splitWidth;
				const int x = client.right - splitWidth / 2;
				const int y = (client.top + client.bottom) / 2;
				const POINT arrow[] = {{x - 3, y - 1}, {x + 4, y - 1}, {x, y + 3}};
				HBRUSH arrowBrush = solidBrush(enabled ? palette._text : palette._textDisabled);
				HGDIOBJ oldBrush = SelectObject(hdc, arrowBrush);
				HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
				Polygon(hdc, arrow, 3);
				SelectObject(hdc, oldPen);
				SelectObject(hdc, oldBrush);
				DeleteObject(arrowBrush);
				RECT separator{client.right - splitWidth, client.top + 4, client.right - splitWidth + 1, client.bottom - 4};
				FillRect(hdc, &separator, GetSysColorBrush(COLOR_BTNSHADOW));
			}
			if (down)
				OffsetRect(&content, 1, 1);
			// A push button's text has the theme's color, whatever WM_CTLCOLORBTN set.
			SetTextColor(hdc, palette._text);
			drawContent(hwnd, button, hdc, content, ButtonKind::push, enabled);
		}

		void drawCheckMark(HDC hdc, const RECT& box, COLORREF color)
		{
			HPEN pen = CreatePen(PS_SOLID, 2, color);
			HGDIOBJ oldPen = SelectObject(hdc, pen);
			const POINT mark[] = {{box.left + 3, box.top + 6}, {box.left + 5, box.top + 9}, {box.left + 10, box.top + 3}};
			Polyline(hdc, mark, 3);
			SelectObject(hdc, oldPen);
			DeleteObject(pen);
		}

		void drawCheckOrRadio(HWND hwnd, const Button& button, HDC hdc, const RECT& client, ButtonKind kind, bool enabled)
		{
			FillRect(hdc, &client, controlColors(hwnd, hdc, WM_CTLCOLORSTATIC));
			const bool textOnLeft = (GetWindowLongW(hwnd, GWL_STYLE) & (BS_LEFTTEXT | WS_EX_RIGHT)) != 0;
			const int top = (client.top + client.bottom - checkBoxSize) / 2;
			const int left = textOnLeft ? client.right - checkBoxSize : client.left;
			const RECT box{left, top, left + checkBoxSize, top + checkBoxSize};

			const ControlPalette& palette = controlPalette();
			COLORREF frame = enabled ? (button._hot || button._pushed ? palette._accent : palette._boxFrame) : palette._frameDisabled;
			COLORREF fill = button._pushed ? palette._facePressed : palette._boxFill;
			const COLORREF mark = enabled ? (button._hot || button._pushed ? palette._accent : palette._mark) : palette._textDisabled;
			if (kind == ButtonKind::radio)
			{
				HBRUSH fillBrush = solidBrush(fill);
				HPEN framePen = CreatePen(PS_SOLID, 1, frame);
				HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
				HGDIOBJ oldPen = SelectObject(hdc, framePen);
				Ellipse(hdc, box.left, box.top, box.right, box.bottom);
				if (button._check == BST_CHECKED)
				{
					HBRUSH dotBrush = solidBrush(mark);
					SelectObject(hdc, dotBrush);
					SelectObject(hdc, GetStockObject(NULL_PEN));
					Ellipse(hdc, box.left + 3, box.top + 3, box.right - 3, box.bottom - 3);
					SelectObject(hdc, fillBrush);
					DeleteObject(dotBrush);
				}
				SelectObject(hdc, oldPen);
				SelectObject(hdc, oldBrush);
				DeleteObject(framePen);
				DeleteObject(fillBrush);
			}
			else
			{
				fillAndFrame(hdc, box, fill, frame);
				if (button._check == BST_CHECKED)
				{
					drawCheckMark(hdc, box, mark);
				}
				else if (button._check == BST_INDETERMINATE)
				{
					RECT square{box.left + 3, box.top + 3, box.right - 3, box.bottom - 3};
					HBRUSH markBrush = solidBrush(mark);
					FillRect(hdc, &square, markBrush);
					DeleteObject(markBrush);
				}
			}

			RECT text = client;
			if (textOnLeft)
				text.right = box.left - checkBoxTextGap;
			else
				text.left = box.right + checkBoxTextGap;
			drawContent(hwnd, button, hdc, text, kind, enabled);
		}

		void drawGroupBox(HWND hwnd, const Button& button, HDC hdc, const RECT& client, bool enabled)
		{
			HBRUSH background = controlColors(hwnd, hdc, WM_CTLCOLORSTATIC);
			const std::u16string text = windowText(hwnd);
			HGDIOBJ oldFont = SelectObject(hdc, fontOf(hwnd));
			TEXTMETRICW metrics{};
			GetTextMetricsW(hdc, &metrics);
			const int frameTop = client.top + metrics.tmHeight / 2;

			RECT frame{client.left, frameTop, client.right, client.bottom};
			HBRUSH frameBrush = solidBrush(controlPalette()._divider);
			FrameRect(hdc, &frame, frameBrush);
			DeleteObject(frameBrush);

			if (!text.empty())
			{
				RECT label{client.left + 8, client.top, client.right - 8, client.top + metrics.tmHeight};
				DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &label, DT_SINGLELINE | DT_CALCRECT);
				label.left -= 2;
				label.right += 2;
				// Only the label's background is painted: the group box is see-through, as on Windows.
				FillRect(hdc, &label, background);
				label.left += 2;
				SetBkMode(hdc, TRANSPARENT);
				if (!enabled)
					SetTextColor(hdc, controlPalette()._textDisabled);
				DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &label, DT_SINGLELINE | DT_LEFT | DT_TOP);
			}
			SelectObject(hdc, oldFont);
			(void)button;
		}

		void paint(HWND hwnd, Button& button, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const UINT type = typeOf(hwnd);
			const ButtonKind kind = kindOf(type);
			const bool enabled = IsWindowEnabled(hwnd) != FALSE;

			if (kind == ButtonKind::ownerDraw)
			{
				DRAWITEMSTRUCT item{};
				item.CtlType = ODT_BUTTON;
				item.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
				item.itemAction = ODA_DRAWENTIRE;
				item.itemState = (button._pushed ? ODS_SELECTED : 0) | (button._focused ? ODS_FOCUS : 0) | (enabled ? 0 : ODS_DISABLED);
				item.hwndItem = hwnd;
				item.hDC = hdc;
				item.rcItem = client;
				SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
				return;
			}

			// Custom draw (comctl32 v6 buttons): the parent may draw everything or parts.
			NMCUSTOMDRAW custom{};
			custom.dwDrawStage = CDDS_PREPAINT;
			custom.hdc = hdc;
			custom.rc = client;
			custom.uItemState = (button._pushed ? CDIS_SELECTED : 0) | (button._hot ? CDIS_HOT : 0) | (button._focused ? CDIS_FOCUS : 0)
				| (enabled ? 0 : CDIS_DISABLED) | (button._check == BST_CHECKED ? CDIS_CHECKED : 0);
			const LRESULT prePaint = kind == ButtonKind::groupBox ? CDRF_DODEFAULT : sendNotify(hwnd, GetParent(hwnd), NM_CUSTOMDRAW, &custom.hdr);
			if (prePaint & CDRF_SKIPDEFAULT)
				return;

			const int saved = SaveDC(hdc);
			switch (kind)
			{
				case ButtonKind::checkBox:
				case ButtonKind::radio:
					if (isPushLike(hwnd))
						drawPushButton(hwnd, button, hdc, client, type, enabled);
					else
						drawCheckOrRadio(hwnd, button, hdc, client, kind, enabled);
					break;
				case ButtonKind::groupBox:
					drawGroupBox(hwnd, button, hdc, client, enabled);
					break;
				default:
					drawPushButton(hwnd, button, hdc, client, type, enabled);
					break;
			}
			RestoreDC(hdc, saved);

			if (prePaint & CDRF_NOTIFYPOSTPAINT)
			{
				custom.dwDrawStage = CDDS_POSTPAINT;
				sendNotify(hwnd, GetParent(hwnd), NM_CUSTOMDRAW, &custom.hdr);
			}
		}

		// BS_AUTORADIOBUTTON: checking one unchecks the other radio buttons of its group.
		void uncheckGroupSiblings(HWND hwnd)
		{
			HWND parent = GetParent(hwnd);
			if (!parent)
				return;
			// Round the group from a control the walk comes back to (as Windows does): the button itself
			// may be one it skips (hidden, disabled).
			const HWND start = GetNextDlgGroupItem(parent, hwnd, TRUE);
			HWND sibling = start;
			do
			{
				if (!sibling)
					break;
				char16_t className[16]{};
				GetClassNameW(sibling, className, 16);
				if (sibling != hwnd && _wcsicmp(className, WC_BUTTONW) == 0 && typeOf(sibling) == BS_AUTORADIOBUTTON)
					SendMessageW(sibling, BM_SETCHECK, BST_UNCHECKED, 0);
				sibling = GetNextDlgGroupItem(parent, sibling, FALSE);
			} while (sibling != start);
		}

		// A click: the automatic types change their state, then the parent hears BN_CLICKED.
		void click(HWND hwnd, Button& button)
		{
			switch (typeOf(hwnd))
			{
				case BS_AUTOCHECKBOX:
					button._check = button._check == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED;
					break;
				case BS_AUTO3STATE:
					button._check = button._check == BST_UNCHECKED ? BST_CHECKED : (button._check == BST_CHECKED ? BST_INDETERMINATE : BST_UNCHECKED);
					break;
				case BS_AUTORADIOBUTTON:
					button._check = BST_CHECKED;
					uncheckGroupSiblings(hwnd);
					break;
				default:
					break;
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			sendCommand(hwnd, BN_CLICKED);
		}

		bool inside(HWND hwnd, LPARAM lParam)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			return PtInRect(&client, point) != FALSE;
		}

		void setPushed(HWND hwnd, Button& button, bool pushed)
		{
			if (button._pushed == pushed)
				return;
			button._pushed = pushed;
			InvalidateRect(hwnd, nullptr, FALSE);
			if (GetWindowLongW(hwnd, GWL_STYLE) & BS_NOTIFY)
				sendCommand(hwnd, pushed ? BN_PUSHED : BN_UNPUSHED);
		}

		LRESULT CALLBACK buttonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Button* button = dataOf<Button>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Button>(hwnd);
					// A group box only draws its frame and label; the controls in it show through.
					if (kindOf(typeOf(hwnd)) == ButtonKind::groupBox)
						user::setSeeThrough(hwnd, true);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *button, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *button, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_GETDLGCODE:
				{
					switch (kindOf(typeOf(hwnd)))
					{
						case ButtonKind::radio:
							return DLGC_BUTTON | DLGC_RADIOBUTTON;
						case ButtonKind::groupBox:
							return DLGC_STATIC;
						case ButtonKind::checkBox:
							return DLGC_BUTTON;
						default:
							return DLGC_BUTTON | (isDefault(typeOf(hwnd)) ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
					}
				}
				case WM_LBUTTONDOWN:
				case WM_LBUTTONDBLCLK:
				{
					const UINT type = typeOf(hwnd);
					if (kindOf(type) == ButtonKind::groupBox)
						return 0;
					if (message == WM_LBUTTONDBLCLK && (GetWindowLongW(hwnd, GWL_STYLE) & BS_NOTIFY))
					{
						sendCommand(hwnd, BN_DOUBLECLICKED);
						return 0;
					}
					if (isSplit(type) && GET_X_LPARAM(lParam) >= [&] { RECT r{}; GetClientRect(hwnd, &r); return r.right - splitWidth; }())
					{
						// The arrow of a split button drops its menu (BCN_DROPDOWN).
						NMBCDROPDOWN dropDown{};
						GetClientRect(hwnd, &dropDown.rcButton);
						sendNotify(hwnd, GetParent(hwnd), BCN_DROPDOWN, &dropDown.hdr);
						return 0;
					}
					SetCapture(hwnd);
					button->_tracking = true;
					if (GetFocus() != hwnd)
						SetFocus(hwnd);
					setPushed(hwnd, *button, true);
					return 0;
				}
				case WM_MOUSEMOVE:
				{
					if (!button->_hot)
					{
						button->_hot = true;
						TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
						TrackMouseEvent(&track);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					if (button->_tracking)
						setPushed(hwnd, *button, inside(hwnd, lParam));
					return 0;
				}
				case WM_MOUSELEAVE:
					button->_hot = false;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_LBUTTONUP:
				{
					if (!button->_tracking)
						return 0;
					button->_tracking = false;
					const bool clicked = button->_pushed && inside(hwnd, lParam);
					setPushed(hwnd, *button, false);
					ReleaseCapture();
					if (clicked)
						click(hwnd, *button);
					return 0;
				}
				case WM_CAPTURECHANGED:
					if (button->_tracking)
					{
						button->_tracking = false;
						setPushed(hwnd, *button, false);
					}
					return 0;
				case WM_KEYDOWN:
					if (wParam == VK_SPACE)
						setPushed(hwnd, *button, true);
					return 0;
				case WM_KEYUP:
					if (wParam == VK_SPACE && button->_pushed)
					{
						setPushed(hwnd, *button, false);
						click(hwnd, *button);
					}
					return 0;
				case WM_CHAR:
					return 0;
				case WM_SETFOCUS:
					button->_focused = true;
					InvalidateRect(hwnd, nullptr, FALSE);
					if (GetWindowLongW(hwnd, GWL_STYLE) & BS_NOTIFY)
						sendCommand(hwnd, BN_SETFOCUS);
					// A radio button that gets the focus by the keyboard is also chosen.
					if (typeOf(hwnd) == BS_AUTORADIOBUTTON && button->_check != BST_CHECKED && !button->_tracking && !(GetKeyState(VK_LBUTTON) & 0x8000))
						click(hwnd, *button);
					return 0;
				case WM_KILLFOCUS:
					button->_focused = false;
					if (button->_pushed && !button->_tracking)
						setPushed(hwnd, *button, false);
					InvalidateRect(hwnd, nullptr, FALSE);
					if (GetWindowLongW(hwnd, GWL_STYLE) & BS_NOTIFY)
						sendCommand(hwnd, BN_KILLFOCUS);
					return 0;
				case WM_ENABLE:
				case WM_SETTEXT:
				case WM_SETFONT:
				case WM_STYLECHANGED:
				case WM_UPDATEUISTATE:
				{
					const LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return message == WM_SETTEXT ? TRUE : result;
				}
				case BM_GETCHECK:
					return button->_check;
				case BM_SETCHECK:
				{
					UINT check = static_cast<UINT>(wParam);
					const UINT type = typeOf(hwnd);
					if (check == BST_INDETERMINATE && type != BS_3STATE && type != BS_AUTO3STATE)
						check = BST_CHECKED;
					if (button->_check != check)
					{
						button->_check = check;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				case BM_GETSTATE:
					return button->_check | (button->_pushed ? BST_PUSHED : 0) | (button->_focused ? BST_FOCUS : 0) | (button->_hot ? BST_HOT : 0);
				case BM_SETSTATE:
					setPushed(hwnd, *button, wParam != 0);
					return 0;
				case BM_SETSTYLE:
				{
					const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
					SetWindowLongW(hwnd, GWL_STYLE, (style & ~static_cast<LONG>(BS_TYPEMASK)) | (static_cast<LONG>(wParam) & BS_TYPEMASK));
					if (LOWORD(lParam))
						InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case BM_CLICK:
					if (IsWindowEnabled(hwnd))
						click(hwnd, *button);
					return 0;
				case BM_SETIMAGE:
				{
					HANDLE previous = button->_image;
					button->_image = reinterpret_cast<HANDLE>(lParam);
					button->_imageType = static_cast<UINT>(wParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return reinterpret_cast<LRESULT>(previous);
				}
				case BM_GETIMAGE:
					return reinterpret_cast<LRESULT>(button->_image);
				case BM_SETDONTCLICK:
					return 0;
				case BCM_GETIDEALSIZE:
				{
					auto* size = reinterpret_cast<SIZE*>(lParam);
					if (!size)
						return FALSE;
					const ButtonKind kind = kindOf(typeOf(hwnd));
					const int text = textWidth(hwnd, windowText(hwnd));
					const int height = fontHeight(hwnd);
					if (kind == ButtonKind::checkBox || kind == ButtonKind::radio)
						*size = {checkBoxSize + checkBoxTextGap + text, std::max(checkBoxSize, height)};
					else
						*size = {text + 2 * pushButtonTextMargin + (isSplit(typeOf(hwnd)) ? splitWidth : 0), height + 8};
					return TRUE;
				}
				case BCM_SETSPLITINFO:
				case BCM_GETSPLITINFO:
				case BCM_SETDROPDOWNSTATE:
					return TRUE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerButton()
	{
		registerControl(WC_BUTTONW, buttonProc);
	}
}
