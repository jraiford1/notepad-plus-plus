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

// The combo box control (ComboBox), made as on Windows of an Edit child (CBS_DROPDOWN, CBS_SIMPLE)
// and a ComboLBox popup list, which GetComboBoxInfo hands out. The window is as high as its
// selection field; the height it is given is how far the list drops.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr int editId = 1001; // the id Windows gives the edit of a combo box
		constexpr int buttonWidth = 17;
		constexpr int fieldPadding = 3;
		constexpr UINT_PTR editSubclassId = 0xC0B0;

		struct ComboBox
		{
			HWND _edit = nullptr;
			HWND _list = nullptr;
			int _fieldHeight = 0;
			int _droppedHeight = 0;    // the height the program gave the whole control
			int _droppedWidth = 0;
			bool _dropped = false;
			bool _focused = false;
			int _selectionWhenDropped = CB_ERR;
		};

		LONG styleOf(HWND hwnd)
		{
			return GetWindowLongW(hwnd, GWL_STYLE);
		}

		UINT typeOf(HWND hwnd)
		{
			return static_cast<UINT>(styleOf(hwnd)) & 0x3;
		}

		bool isDropDownList(HWND hwnd)
		{
			return typeOf(hwnd) == CBS_DROPDOWNLIST;
		}

		HWND comboOf(HWND edit)
		{
			return GetParent(edit);
		}

		RECT buttonRect(HWND hwnd)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			return {client.right - buttonWidth, client.top, client.right, client.bottom};
		}

		RECT fieldRect(HWND hwnd)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			if (typeOf(hwnd) != CBS_SIMPLE)
				client.right -= buttonWidth;
			return client;
		}

		void notify(HWND hwnd, WORD code)
		{
			sendCommand(hwnd, code);
		}

		int currentSelection(const ComboBox& combo)
		{
			return static_cast<int>(SendMessageW(combo._list, LB_GETCURSEL, 0, 0));
		}

		std::u16string itemText(const ComboBox& combo, int index)
		{
			if (index < 0)
				return {};
			const LRESULT length = SendMessageW(combo._list, LB_GETTEXTLEN, static_cast<WPARAM>(index), 0);
			if (length == LB_ERR || length <= 0)
				return {};
			std::u16string text(static_cast<size_t>(length) + 1, u'\0');
			SendMessageW(combo._list, LB_GETTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data()));
			text.resize(static_cast<size_t>(length));
			return text;
		}

		// The edit shows the selected item.
		void showSelection(HWND hwnd, ComboBox& combo)
		{
			if (combo._edit)
			{
				const std::u16string text = itemText(combo, currentSelection(combo));
				SetWindowTextW(combo._edit, text.c_str());
				SendMessageW(combo._edit, EM_SETSEL, 0, -1);
			}
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		void layoutChildren(HWND hwnd, ComboBox& combo)
		{
			if (!combo._edit)
				return;
			RECT field = fieldRect(hwnd);
			InflateRect(&field, -fieldPadding, -fieldPadding);
			SetWindowPos(combo._edit, nullptr, field.left, field.top, field.right - field.left, field.bottom - field.top, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		int itemHeight(const ComboBox& combo)
		{
			return std::max(1, static_cast<int>(SendMessageW(combo._list, LB_GETITEMHEIGHT, 0, 0)));
		}

		void closeUp(HWND hwnd, ComboBox& combo, bool accept)
		{
			if (!combo._dropped)
				return;
			combo._dropped = false;
			if (GetCapture() == combo._list)
				ReleaseCapture();
			ShowWindow(combo._list, SW_HIDE);
			if (!accept && currentSelection(combo) != combo._selectionWhenDropped)
			{
				SendMessageW(combo._list, LB_SETCURSEL, static_cast<WPARAM>(combo._selectionWhenDropped), 0);
				showSelection(hwnd, combo);
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			notify(hwnd, CBN_CLOSEUP);
			notify(hwnd, accept ? CBN_SELENDOK : CBN_SELENDCANCEL);
		}

		void dropDown(HWND hwnd, ComboBox& combo)
		{
			if (combo._dropped || typeOf(hwnd) == CBS_SIMPLE)
				return;
			notify(hwnd, CBN_DROPDOWN);
			combo._selectionWhenDropped = currentSelection(combo);

			RECT window{};
			GetWindowRect(hwnd, &window);
			const int count = static_cast<int>(SendMessageW(combo._list, LB_GETCOUNT, 0, 0));
			const int wanted = std::max(1, count) * itemHeight(combo) + 2;
			const int room = std::max(itemHeight(combo) + 2, combo._droppedHeight - combo._fieldHeight);
			const int height = std::min(wanted, room);
			const int width = std::max<int>(window.right - window.left, combo._droppedWidth);

			// Below the field, or above it when the screen ends first.
			RECT work{};
			SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
			int top = window.bottom;
			if (top + height > work.bottom && window.top - height >= work.top)
				top = window.top - height;
			SetWindowPos(combo._list, HWND_TOPMOST, window.left, top, width, height, SWP_NOACTIVATE);
			SendMessageW(combo._list, LB_SETTOPINDEX, static_cast<WPARAM>(std::max(0, combo._selectionWhenDropped)), 0);
			ShowWindow(combo._list, SW_SHOWNA);
			combo._dropped = true;
			// The list takes the mouse until it closes; a click outside it closes it.
			SetCapture(combo._list);
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		// Moves the selection by the keyboard, as the field shows it.
		void selectByKey(HWND hwnd, ComboBox& combo, WPARAM key)
		{
			const int count = static_cast<int>(SendMessageW(combo._list, LB_GETCOUNT, 0, 0));
			if (count == 0)
				return;
			const int current = currentSelection(combo);
			int target = current;
			switch (key)
			{
				case VK_UP:
				case VK_LEFT:
					target = std::max(0, current - 1);
					break;
				case VK_DOWN:
				case VK_RIGHT:
					target = current < 0 ? 0 : std::min(count - 1, current + 1);
					break;
				case VK_HOME:
					target = 0;
					break;
				case VK_END:
					target = count - 1;
					break;
				case VK_PRIOR:
					target = std::max(0, current - 8);
					break;
				case VK_NEXT:
					target = std::min(count - 1, std::max(0, current) + 8);
					break;
				default:
					return;
			}
			if (target == current)
				return;
			SendMessageW(combo._list, LB_SETCURSEL, static_cast<WPARAM>(target), 0);
			showSelection(hwnd, combo);
			notify(hwnd, CBN_SELCHANGE);
		}

		// Keys shared by the combo box (drop-down lists) and its edit: returns whether one was handled.
		bool handleKey(HWND hwnd, ComboBox& combo, WPARAM key)
		{
			const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
			if (key == VK_F4 || (alt && (key == VK_DOWN || key == VK_UP)))
			{
				if (combo._dropped)
					closeUp(hwnd, combo, true);
				else
					dropDown(hwnd, combo);
				return true;
			}
			if (combo._dropped && (key == VK_RETURN || key == VK_ESCAPE))
			{
				closeUp(hwnd, combo, key == VK_RETURN);
				return true;
			}
			if (key == VK_UP || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT
				|| (isDropDownList(hwnd) && (key == VK_HOME || key == VK_END || key == VK_LEFT || key == VK_RIGHT)))
			{
				selectByKey(hwnd, combo, key);
				return true;
			}
			return false;
		}

		LRESULT CALLBACK editSubclass(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
		{
			HWND hwnd = comboOf(edit);
			ComboBox* combo = dataOf<ComboBox>(hwnd);
			if (!combo)
				return DefSubclassProc(edit, message, wParam, lParam);
			switch (message)
			{
				case WM_KEYDOWN:
				case WM_SYSKEYDOWN:
					if (handleKey(hwnd, *combo, wParam))
						return 0;
					break;
				case WM_GETDLGCODE:
				{
					// While the list is down, Enter and Escape close it instead of the dialog.
					LRESULT code = DefSubclassProc(edit, message, wParam, lParam);
					if (combo->_dropped)
						code |= DLGC_WANTALLKEYS;
					return code;
				}
				case WM_KILLFOCUS:
					closeUp(hwnd, *combo, true);
					break;
				default:
					break;
			}
			return DefSubclassProc(edit, message, wParam, lParam);
		}

		void paint(HWND hwnd, ComboBox& combo, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const bool enabled = IsWindowEnabled(hwnd) != FALSE;
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);

			if (isDropDownList(hwnd))
			{
				// A drop-down list looks like a button with the selected item on it.
				const bool hot = combo._dropped;
				HBRUSH fill = CreateSolidBrush(!enabled ? RGB(204, 204, 204) : (hot ? RGB(229, 241, 251) : RGB(225, 225, 225)));
				HBRUSH frame = CreateSolidBrush(!enabled ? RGB(191, 191, 191) : (hot || combo._focused ? RGB(0, 120, 215) : RGB(173, 173, 173)));
				FillRect(hdc, &client, fill);
				FrameRect(hdc, &client, frame);
				DeleteObject(fill);
				DeleteObject(frame);

				RECT field = fieldRect(hwnd);
				InflateRect(&field, -fieldPadding, -fieldPadding);
				const int selection = currentSelection(combo);
				if (styleOf(hwnd) & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE))
				{
					DRAWITEMSTRUCT draw{};
					draw.CtlType = ODT_COMBOBOX;
					draw.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
					draw.itemID = static_cast<UINT>(selection);
					draw.itemAction = ODA_DRAWENTIRE;
					draw.itemState = ODS_COMBOBOXEDIT | (combo._focused && !combo._dropped ? ODS_SELECTED | ODS_FOCUS : 0) | (enabled ? 0 : ODS_DISABLED);
					draw.hwndItem = hwnd;
					draw.hDC = hdc;
					draw.rcItem = field;
					draw.itemData = selection >= 0 ? static_cast<ULONG_PTR>(SendMessageW(combo._list, LB_GETITEMDATA, static_cast<WPARAM>(selection), 0)) : 0;
					SendMessageW(GetParent(hwnd), WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
				}
				else
				{
					if (combo._focused && !combo._dropped)
					{
						FillRect(hdc, &field, GetSysColorBrush(COLOR_HIGHLIGHT));
						SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
					}
					else
					{
						SetTextColor(hdc, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
					}
					const std::u16string text = itemText(combo, selection);
					RECT textRect{field.left + 2, field.top, field.right - 2, field.bottom};
					DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
				}
			}
			else
			{
				// An edit field with a button.
				HBRUSH background = controlColors(hwnd, hdc, WM_CTLCOLOREDIT);
				FillRect(hdc, &client, background);
				HBRUSH frame = CreateSolidBrush(combo._focused || combo._dropped ? RGB(0, 120, 215) : RGB(122, 122, 122));
				FrameRect(hdc, &client, frame);
				DeleteObject(frame);
				if (typeOf(hwnd) != CBS_SIMPLE)
				{
					RECT button = buttonRect(hwnd);
					InflateRect(&button, -1, -1);
					HBRUSH buttonBrush = CreateSolidBrush(combo._dropped ? RGB(204, 228, 247) : RGB(240, 240, 240));
					FillRect(hdc, &button, buttonBrush);
					DeleteObject(buttonBrush);
				}
			}

			if (typeOf(hwnd) != CBS_SIMPLE)
			{
				const RECT button = buttonRect(hwnd);
				const int x = (button.left + button.right) / 2;
				const int y = (button.top + button.bottom) / 2;
				HPEN pen = CreatePen(PS_SOLID, 1, enabled ? RGB(96, 96, 96) : RGB(160, 160, 160));
				HGDIOBJ oldPen = SelectObject(hdc, pen);
				const POINT arrow[] = {{x - 4, y - 2}, {x, y + 2}, {x + 4, y - 2}};
				Polyline(hdc, arrow, 3);
				SelectObject(hdc, oldPen);
				DeleteObject(pen);
			}
			RestoreDC(hdc, saved);
		}

		LRESULT forwardToList(const ComboBox& combo, UINT message, WPARAM wParam, LPARAM lParam)
		{
			const LRESULT result = SendMessageW(combo._list, message, wParam, lParam);
			return result == LB_ERR ? CB_ERR : (result == LB_ERRSPACE ? CB_ERRSPACE : result);
		}

		LRESULT CALLBACK comboProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			ComboBox* combo = dataOf<ComboBox>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					ComboBox* created = createData<ComboBox>(hwnd);
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					created->_droppedHeight = create->cy;
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_CREATE:
				{
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					const LONG style = create->style;
					HINSTANCE instance = create->hInstance;
					combo->_fieldHeight = fontHeight(hwnd) + 2 * fieldPadding + 2;

					DWORD listStyle = WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
					if (style & CBS_SORT)
						listStyle |= LBS_SORT;
					if (style & CBS_HASSTRINGS)
						listStyle |= LBS_HASSTRINGS;
					if (style & CBS_OWNERDRAWFIXED)
						listStyle |= LBS_OWNERDRAWFIXED;
					if (style & CBS_OWNERDRAWVARIABLE)
						listStyle |= LBS_OWNERDRAWVARIABLE;
					combo->_list = CreateWindowExW(WS_EX_TOOLWINDOW, u"ComboLBox", nullptr, listStyle, 0, 0, 0, 0, hwnd, nullptr, instance, hwnd);

					if ((style & 0x3) != CBS_DROPDOWNLIST)
					{
						DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_LEFT;
						if (style & CBS_AUTOHSCROLL)
							editStyle |= ES_AUTOHSCROLL;
						if (style & CBS_UPPERCASE)
							editStyle |= ES_UPPERCASE;
						if (style & CBS_LOWERCASE)
							editStyle |= ES_LOWERCASE;
						combo->_edit = CreateWindowExW(0, WC_EDITW, create->lpszName, editStyle, 0, 0, 0, 0, hwnd,
							reinterpret_cast<HMENU>(static_cast<INT_PTR>(editId)), instance, nullptr);
						SetWindowSubclass(combo->_edit, editSubclass, editSubclassId, 0);
					}
					// The window is as high as its field.
					RECT window{};
					GetWindowRect(hwnd, &window);
					SetWindowPos(hwnd, nullptr, 0, 0, window.right - window.left, combo->_fieldHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
					layoutChildren(hwnd, *combo);
					return 0;
				}
				case WM_DESTROY:
					if (combo->_list)
						DestroyWindow(combo->_list);
					combo->_list = nullptr;
					return 0;
				case WM_WINDOWPOSCHANGING:
				{
					// A height given to a combo box is how far its list drops; the field keeps its own.
					auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
					if (combo && combo->_fieldHeight > 0 && !(position->flags & SWP_NOSIZE) && typeOf(hwnd) != CBS_SIMPLE)
					{
						if (position->cy != combo->_fieldHeight)
							combo->_droppedHeight = position->cy;
						position->cy = combo->_fieldHeight;
					}
					return 0;
				}
				case WM_SIZE:
					layoutChildren(hwnd, *combo);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *combo, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *combo, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SETFONT:
				{
					DefWindowProcW(hwnd, message, wParam, lParam);
					if (combo->_edit)
						SendMessageW(combo->_edit, WM_SETFONT, wParam, lParam);
					SendMessageW(combo->_list, WM_SETFONT, wParam, lParam);
					combo->_fieldHeight = fontHeight(hwnd) + 2 * fieldPadding + 2;
					RECT window{};
					GetWindowRect(hwnd, &window);
					SetWindowPos(hwnd, nullptr, 0, 0, window.right - window.left, combo->_fieldHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
					layoutChildren(hwnd, *combo);
					return 0;
				}
				case WM_ENABLE:
					if (combo->_edit)
						EnableWindow(combo->_edit, wParam != 0);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_GETDLGCODE:
				{
					LRESULT code = DLGC_WANTARROWS | DLGC_WANTCHARS;
					if (combo->_dropped)
						code |= DLGC_WANTALLKEYS;
					return code;
				}
				case WM_SETFOCUS:
					if (combo->_edit)
					{
						// The edit takes the focus of a combo box that has one.
						SetFocus(combo->_edit);
						SendMessageW(combo->_edit, EM_SETSEL, 0, -1);
						return 0;
					}
					combo->_focused = true;
					InvalidateRect(hwnd, nullptr, FALSE);
					notify(hwnd, CBN_SETFOCUS);
					return 0;
				case WM_KILLFOCUS:
					if (combo->_edit)
						return 0;
					closeUp(hwnd, *combo, true);
					combo->_focused = false;
					InvalidateRect(hwnd, nullptr, FALSE);
					notify(hwnd, CBN_KILLFOCUS);
					return 0;
				case WM_COMMAND:
				{
					if (reinterpret_cast<HWND>(lParam) != combo->_edit || !combo->_edit)
						return 0;
					switch (HIWORD(wParam))
					{
						case EN_SETFOCUS:
							combo->_focused = true;
							InvalidateRect(hwnd, nullptr, FALSE);
							notify(hwnd, CBN_SETFOCUS);
							break;
						case EN_KILLFOCUS:
							combo->_focused = false;
							InvalidateRect(hwnd, nullptr, FALSE);
							notify(hwnd, CBN_KILLFOCUS);
							break;
						case EN_UPDATE:
							notify(hwnd, CBN_EDITUPDATE);
							break;
						case EN_CHANGE:
							notify(hwnd, CBN_EDITCHANGE);
							break;
						default:
							break;
					}
					return 0;
				}
				case WM_CTLCOLOREDIT:
				case WM_CTLCOLORLISTBOX:
				case WM_CTLCOLORSTATIC:
				{
					// The parts' colors are the combo box's parent's to choose.
					if (HWND parent = GetParent(hwnd))
					{
						if (const LRESULT brush = SendMessageW(parent, message, wParam, lParam))
							return brush;
					}
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_MEASUREITEM:
				case WM_DRAWITEM:
				case WM_DELETEITEM:
				{
					// Owner-drawn list items are the parent's to measure and draw, with the combo box's type and id.
					if (message == WM_MEASUREITEM)
					{
						auto* measured = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
						measured->CtlType = ODT_COMBOBOX;
					}
					else if (message == WM_DRAWITEM)
					{
						auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
						draw->CtlType = ODT_COMBOBOX;
					}
					else
					{
						auto* deleted = reinterpret_cast<DELETEITEMSTRUCT*>(lParam);
						deleted->CtlType = ODT_COMBOBOX;
						deleted->hwndItem = hwnd;
					}
					return SendMessageW(GetParent(hwnd), message, static_cast<WPARAM>(GetDlgCtrlID(hwnd)), lParam);
				}
				case WM_LBUTTONDOWN:
				case WM_LBUTTONDBLCLK:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					RECT button = buttonRect(hwnd);
					if (isDropDownList(hwnd) || PtInRect(&button, point))
					{
						if (GetFocus() != hwnd && GetFocus() != combo->_edit)
							SetFocus(combo->_edit ? combo->_edit : hwnd);
						if (combo->_dropped)
							closeUp(hwnd, *combo, true);
						else
							dropDown(hwnd, *combo);
					}
					return 0;
				}
				case WM_KEYDOWN:
				case WM_SYSKEYDOWN:
					if (handleKey(hwnd, *combo, wParam))
						return 0;
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_CHAR:
				{
					// A drop-down list selects the next item starting with the typed character.
					if (!isDropDownList(hwnd) || wParam < 0x20)
						return 0;
					const char16_t typed[2] = {static_cast<char16_t>(wParam), u'\0'};
					const LRESULT found = SendMessageW(combo->_list, LB_FINDSTRING, static_cast<WPARAM>(currentSelection(*combo)), reinterpret_cast<LPARAM>(typed));
					if (found != LB_ERR && found != currentSelection(*combo))
					{
						SendMessageW(combo->_list, LB_SETCURSEL, static_cast<WPARAM>(found), 0);
						showSelection(hwnd, *combo);
						notify(hwnd, CBN_SELCHANGE);
					}
					return 0;
				}
				case WM_MOUSEWHEEL:
					if (!combo->_dropped && (combo->_focused))
						selectByKey(hwnd, *combo, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? VK_UP : VK_DOWN);
					return 0;
				case comboListChosen:
				{
					const bool changed = static_cast<int>(wParam) != combo->_selectionWhenDropped;
					SendMessageW(combo->_list, LB_SETCURSEL, wParam, 0);
					showSelection(hwnd, *combo);
					if (changed)
						notify(hwnd, CBN_SELCHANGE);
					closeUp(hwnd, *combo, true);
					return 0;
				}
				case WM_SETTEXT:
				{
					if (combo->_edit)
						return SendMessageW(combo->_edit, WM_SETTEXT, wParam, lParam);
					// A drop-down list can only show one of its items.
					const LRESULT found = SendMessageW(combo->_list, LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), lParam);
					if (found == LB_ERR)
						return CB_ERR;
					SendMessageW(combo->_list, LB_SETCURSEL, static_cast<WPARAM>(found), 0);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case WM_GETTEXT:
				{
					if (combo->_edit)
						return SendMessageW(combo->_edit, WM_GETTEXT, wParam, lParam);
					const std::u16string text = itemText(*combo, currentSelection(*combo));
					if (wParam == 0 || !lParam)
						return 0;
					const size_t count = std::min<size_t>(text.size(), wParam - 1);
					auto* out = reinterpret_cast<char16_t*>(lParam);
					std::copy_n(text.c_str(), count, out);
					out[count] = u'\0';
					return static_cast<LRESULT>(count);
				}
				case WM_GETTEXTLENGTH:
					if (combo->_edit)
						return SendMessageW(combo->_edit, WM_GETTEXTLENGTH, 0, 0);
					return static_cast<LRESULT>(itemText(*combo, currentSelection(*combo)).size());
				case WM_CUT:
				case WM_COPY:
				case WM_PASTE:
				case WM_CLEAR:
				case WM_UNDO:
					return combo->_edit ? SendMessageW(combo->_edit, message, wParam, lParam) : 0;
				case CB_ADDSTRING:
					return forwardToList(*combo, LB_ADDSTRING, wParam, lParam);
				case CB_INSERTSTRING:
					return forwardToList(*combo, LB_INSERTSTRING, wParam, lParam);
				case CB_DELETESTRING:
					return forwardToList(*combo, LB_DELETESTRING, wParam, lParam);
				case CB_RESETCONTENT:
					SendMessageW(combo->_list, LB_RESETCONTENT, 0, 0);
					if (combo->_edit)
						SetWindowTextW(combo->_edit, u"");
					InvalidateRect(hwnd, nullptr, FALSE);
					return CB_OKAY;
				case CB_GETCOUNT:
					return forwardToList(*combo, LB_GETCOUNT, 0, 0);
				case CB_GETLBTEXT:
					return forwardToList(*combo, LB_GETTEXT, wParam, lParam);
				case CB_GETLBTEXTLEN:
					return forwardToList(*combo, LB_GETTEXTLEN, wParam, lParam);
				case CB_SETITEMDATA:
					return forwardToList(*combo, LB_SETITEMDATA, wParam, lParam);
				case CB_GETITEMDATA:
					return forwardToList(*combo, LB_GETITEMDATA, wParam, lParam);
				case CB_FINDSTRING:
					return forwardToList(*combo, LB_FINDSTRING, wParam, lParam);
				case CB_FINDSTRINGEXACT:
					return forwardToList(*combo, LB_FINDSTRINGEXACT, wParam, lParam);
				case CB_INITSTORAGE:
					return forwardToList(*combo, LB_INITSTORAGE, wParam, lParam);
				case CB_SETTOPINDEX:
					return forwardToList(*combo, LB_SETTOPINDEX, wParam, lParam);
				case CB_GETTOPINDEX:
					return forwardToList(*combo, LB_GETTOPINDEX, wParam, lParam);
				case CB_SETCURSEL:
				{
					const LRESULT result = SendMessageW(combo->_list, LB_SETCURSEL, wParam, 0);
					showSelection(hwnd, *combo);
					return result == LB_ERR ? CB_ERR : result;
				}
				case CB_GETCURSEL:
					return forwardToList(*combo, LB_GETCURSEL, 0, 0);
				case CB_SELECTSTRING:
				{
					const LRESULT found = SendMessageW(combo->_list, LB_SELECTSTRING, wParam, lParam);
					if (found != LB_ERR)
						showSelection(hwnd, *combo);
					return found == LB_ERR ? CB_ERR : found;
				}
				case CB_SETITEMHEIGHT:
				{
					// -1 is the selection field.
					if (static_cast<int>(wParam) == -1)
					{
						combo->_fieldHeight = LOWORD(lParam) + 2 * fieldPadding;
						RECT window{};
						GetWindowRect(hwnd, &window);
						SetWindowPos(hwnd, nullptr, 0, 0, window.right - window.left, combo->_fieldHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
						return CB_OKAY;
					}
					return forwardToList(*combo, LB_SETITEMHEIGHT, wParam, lParam);
				}
				case CB_GETITEMHEIGHT:
					if (static_cast<int>(wParam) == -1)
						return combo->_fieldHeight - 2 * fieldPadding;
					return forwardToList(*combo, LB_GETITEMHEIGHT, wParam, 0);
				case CB_SHOWDROPDOWN:
					if (wParam)
						dropDown(hwnd, *combo);
					else
						closeUp(hwnd, *combo, false);
					return TRUE;
				case CB_GETDROPPEDSTATE:
					return combo->_dropped;
				case CB_GETDROPPEDCONTROLRECT:
				{
					RECT window{};
					GetWindowRect(hwnd, &window);
					window.bottom = window.top + std::max(combo->_droppedHeight, combo->_fieldHeight);
					*reinterpret_cast<RECT*>(lParam) = window;
					return TRUE;
				}
				case CB_SETDROPPEDWIDTH:
					combo->_droppedWidth = static_cast<int>(wParam);
					return combo->_droppedWidth;
				case CB_GETDROPPEDWIDTH:
				{
					RECT window{};
					GetWindowRect(hwnd, &window);
					return std::max<int>(combo->_droppedWidth, window.right - window.left);
				}
				case CB_LIMITTEXT:
					return combo->_edit ? SendMessageW(combo->_edit, EM_LIMITTEXT, wParam, 0), TRUE : CB_ERR;
				case CB_GETEDITSEL:
					return combo->_edit ? SendMessageW(combo->_edit, EM_GETSEL, wParam, lParam) : CB_ERR;
				case CB_SETEDITSEL:
				{
					if (!combo->_edit)
						return CB_ERR;
					const short start = static_cast<short>(LOWORD(lParam));
					const short end = static_cast<short>(HIWORD(lParam));
					SendMessageW(combo->_edit, EM_SETSEL, static_cast<WPARAM>(static_cast<int>(start)), end);
					return TRUE;
				}
				case CB_SETCUEBANNER:
					return combo->_edit ? SendMessageW(combo->_edit, EM_SETCUEBANNER, 0, lParam) : FALSE;
				case CB_GETCOMBOBOXINFO:
				{
					auto* info = reinterpret_cast<COMBOBOXINFO*>(lParam);
					if (!info)
						return FALSE;
					info->rcItem = fieldRect(hwnd);
					InflateRect(&info->rcItem, -fieldPadding, -fieldPadding);
					info->rcButton = typeOf(hwnd) == CBS_SIMPLE ? RECT{} : buttonRect(hwnd);
					info->stateButton = combo->_dropped ? STATE_SYSTEM_PRESSED : 0;
					info->hwndCombo = hwnd;
					info->hwndItem = combo->_edit;
					info->hwndList = combo->_list;
					return TRUE;
				}
				case CB_SETEXTENDEDUI:
				case CB_SETLOCALE:
				case CB_SETMINVISIBLE:
					return CB_OKAY;
				case CB_GETEXTENDEDUI:
					return FALSE;
				case CB_GETMINVISIBLE:
					return 30;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerComboBox()
	{
		registerControl(WC_COMBOBOXW, comboProc);
	}
}

BOOL WINAPI GetComboBoxInfo(HWND hwnd, PCOMBOBOXINFO info)
{
	if (!info || info->cbSize != sizeof(COMBOBOXINFO))
		return FALSE;
	return static_cast<BOOL>(SendMessageW(hwnd, CB_GETCOMBOBOXINFO, 0, reinterpret_cast<LPARAM>(info)));
}
