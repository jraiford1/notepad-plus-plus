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

// user32 dialogs: templates (DLGTEMPLATE and DLGTEMPLATEEX, as llvm-rc writes them), dialog units,
// the dialog class and DefDlgProc, modal dialogs, keyboard navigation and the dialog-item helpers.

#include "Resources.h"
#include "Window.h"

#include <windowsx.h>

namespace nppmac::user
{
	namespace
	{
		// The window extra bytes of the dialog class: DWLP_MSGRESULT, DWLP_DLGPROC and DWLP_USER,
		// each pointer-sized in a 64-bit process.
		constexpr int dialogExtraBytes = std::max<int>(DLGWINDOWEXTRA, 3 * sizeof(LONG_PTR));

		struct DialogState
		{
			bool _ended = false;
			INT_PTR _result = 0;
			int _baseUnitX = 6;
			int _baseUnitY = 13;
			HFONT _font = nullptr;
			bool _ownsFont = false;
			UINT _defaultId = IDOK;
		};

		struct ItemTemplate
		{
			DWORD _helpId = 0;
			DWORD _exStyle = 0;
			DWORD _style = 0;
			short _x = 0;
			short _y = 0;
			short _cx = 0;
			short _cy = 0;
			DWORD _id = 0;
			std::u16string _className;
			std::u16string _title;
			WORD _titleOrdinal = 0;
			bool _titleIsOrdinal = false;
			const BYTE* _creationData = nullptr;
		};

		struct DialogTemplate
		{
			DWORD _style = 0;
			DWORD _exStyle = 0;
			short _x = 0;
			short _y = 0;
			short _cx = 0;
			short _cy = 0;
			std::u16string _menu;
			WORD _menuOrdinal = 0;
			std::u16string _className;
			std::u16string _title;
			WORD _pointSize = 0;
			WORD _weight = FW_NORMAL;
			BYTE _italic = 0;
			BYTE _charset = DEFAULT_CHARSET;
			std::u16string _typeface;
			std::vector<ItemTemplate> _items;
		};

		const BYTE* alignTo(const BYTE* base, const BYTE* p, size_t alignment)
		{
			const size_t offset = static_cast<size_t>(p - base);
			return base + ((offset + alignment - 1) & ~(alignment - 1));
		}

		WORD readWord(const BYTE*& p)
		{
			WORD value = 0;
			std::memcpy(&value, p, 2);
			p += 2;
			return value;
		}

		DWORD readDword(const BYTE*& p)
		{
			DWORD value = 0;
			std::memcpy(&value, p, 4);
			p += 4;
			return value;
		}

		std::u16string readString(const BYTE*& p)
		{
			std::u16string text;
			for (;;)
			{
				const WORD ch = readWord(p);
				if (ch == 0)
					break;
				text.push_back(static_cast<char16_t>(ch));
			}
			return text;
		}

		// A field that is empty, an ordinal (0xFFFF then a WORD), or a string.
		void readField(const BYTE*& p, std::u16string& text, WORD& ordinal, bool* isOrdinal = nullptr)
		{
			WORD first = 0;
			std::memcpy(&first, p, 2);
			if (first == 0xFFFF)
			{
				p += 2;
				ordinal = readWord(p);
				if (isOrdinal)
					*isOrdinal = true;
				return;
			}
			text = readString(p);
		}

		std::u16string predefinedClass(WORD ordinal)
		{
			switch (ordinal)
			{
				case 0x0080:
					return WC_BUTTONW;
				case 0x0081:
					return WC_EDITW;
				case 0x0082:
					return WC_STATICW;
				case 0x0083:
					return WC_LISTBOXW;
				case 0x0084:
					return WC_SCROLLBARW;
				case 0x0085:
					return WC_COMBOBOXW;
				default:
					return WC_STATICW;
			}
		}

		DialogTemplate parseTemplate(const BYTE* base)
		{
			DialogTemplate result;
			const BYTE* p = base;
			WORD version = 0;
			WORD signature = 0;
			std::memcpy(&version, p, 2);
			std::memcpy(&signature, p + 2, 2);
			const bool extended = version == 1 && signature == 0xFFFF;
			WORD count = 0;
			if (extended)
			{
				p += 4;
				readDword(p); // help id
				result._exStyle = readDword(p);
				result._style = readDword(p);
			}
			else
			{
				result._style = readDword(p);
				result._exStyle = readDword(p);
			}
			count = readWord(p);
			result._x = static_cast<short>(readWord(p));
			result._y = static_cast<short>(readWord(p));
			result._cx = static_cast<short>(readWord(p));
			result._cy = static_cast<short>(readWord(p));
			readField(p, result._menu, result._menuOrdinal);
			WORD classOrdinal = 0;
			readField(p, result._className, classOrdinal);
			result._title = readString(p);
			if (result._style & (DS_SETFONT | DS_SHELLFONT))
			{
				result._pointSize = readWord(p);
				if (extended)
				{
					result._weight = readWord(p);
					result._italic = *p++;
					result._charset = *p++;
				}
				result._typeface = readString(p);
			}

			for (WORD i = 0; i < count; ++i)
			{
				p = alignTo(base, p, 4);
				ItemTemplate item;
				if (extended)
				{
					item._helpId = readDword(p);
					item._exStyle = readDword(p);
					item._style = readDword(p);
				}
				else
				{
					item._style = readDword(p);
					item._exStyle = readDword(p);
				}
				item._x = static_cast<short>(readWord(p));
				item._y = static_cast<short>(readWord(p));
				item._cx = static_cast<short>(readWord(p));
				item._cy = static_cast<short>(readWord(p));
				item._id = extended ? readDword(p) : readWord(p);
				WORD itemClassOrdinal = 0;
				bool classIsOrdinal = false;
				readField(p, item._className, itemClassOrdinal, &classIsOrdinal);
				if (classIsOrdinal)
					item._className = predefinedClass(itemClassOrdinal);
				readField(p, item._title, item._titleOrdinal, &item._titleIsOrdinal);
				const WORD extraSize = readWord(p);
				if (extraSize)
				{
					item._creationData = p;
					p += extraSize;
				}
				result._items.push_back(item);
			}
			return result;
		}

		DialogState* stateOf(HWND hwnd)
		{
			Window* window = fromHandle(hwnd);
			return window ? static_cast<DialogState*>(window->_controlData.get()) : nullptr;
		}

		RECT toPixels(const DialogState& state, short x, short y, short cx, short cy)
		{
			return {MulDiv(x, state._baseUnitX, 4), MulDiv(y, state._baseUnitY, 8), MulDiv(x + cx, state._baseUnitX, 4), MulDiv(y + cy, state._baseUnitY, 8)};
		}

		// The dialog font and the base units it gives, as Windows computes them.
		void setUpFont(DialogState& state, const DialogTemplate& dialog)
		{
			if (dialog._style & (DS_SETFONT | DS_SHELLFONT))
			{
				LOGFONTW log{};
				log.lfHeight = -MulDiv(dialog._pointSize ? dialog._pointSize : 8, 96, 72);
				log.lfWeight = dialog._weight;
				log.lfItalic = dialog._italic;
				log.lfCharSet = dialog._charset;
				wcsncpy_s(log.lfFaceName, LF_FACESIZE, dialog._typeface.c_str(), _TRUNCATE);
				state._font = CreateFontIndirectW(&log);
				state._ownsFont = true;
			}
			else
			{
				state._font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			}
			HDC hdc = GetDC(nullptr);
			HGDIOBJ old = SelectObject(hdc, state._font);
			TEXTMETRICW metrics{};
			GetTextMetricsW(hdc, &metrics);
			SIZE size{};
			GetTextExtentPoint32W(hdc, u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size);
			SelectObject(hdc, old);
			DeleteDC(hdc);
			state._baseUnitX = std::max(1, static_cast<int>((size.cx / 26 + 1) / 2));
			state._baseUnitY = std::max(1, static_cast<int>(metrics.tmHeight));
		}

		HWND createDialog(HINSTANCE instance, const BYTE* data, HWND parent, DLGPROC proc, LPARAM parameter, bool modal)
		{
			ensureBuiltInClasses();
			const DialogTemplate dialog = parseTemplate(data);
			auto state = std::make_shared<DialogState>();
			setUpFont(*state, dialog);

			// The template's size is the client area.
			RECT rect = toPixels(*state, dialog._x, dialog._y, dialog._cx, dialog._cy);
			DWORD style = dialog._style & ~(DS_SETFONT | DS_SHELLFONT | DS_MODALFRAME | DS_3DLOOK | DS_FIXEDSYS | DS_CONTROL | DS_CENTER);
			const bool child = (dialog._style & WS_CHILD) != 0;
			if (modal)
				style &= ~WS_CHILD;
			if (!child)
			{
				// Top-level dialogs start centered on their owner (or the screen); Notepad++ usually
				// moves them itself afterwards.
				RECT reference{};
				if (parent)
					GetWindowRect(GetAncestor(parent, GA_ROOT), &reference);
				else
					SystemParametersInfoW(SPI_GETWORKAREA, 0, &reference, 0);
				const int width = rect.right - rect.left;
				const int height = rect.bottom - rect.top;
				const int left = reference.left + ((reference.right - reference.left) - width) / 2;
				const int top = reference.top + ((reference.bottom - reference.top) - height) / 3;
				rect = {left, top, left + width, top + height};
				AdjustWindowRectEx(&rect, style, FALSE, dialog._exStyle);
			}

			const char16_t* className = dialog._className.empty() ? u"#32770" : dialog._className.c_str();
			HWND hwnd = CreateWindowExW(dialog._exStyle | (child ? 0 : WS_EX_DLGMODALFRAME), className, dialog._title.c_str(),
				style & ~WS_VISIBLE, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, parent, nullptr, instance, nullptr);
			if (!hwnd)
				return nullptr;
			Window* window = fromHandle(hwnd);
			window->_controlData = state;
			window->_font = state->_font;
			SetWindowLongPtrW(hwnd, DWLP_DLGPROC, reinterpret_cast<LONG_PTR>(proc));
			if (!dialog._menu.empty() || dialog._menuOrdinal)
				SetMenu(hwnd, LoadMenuW(instance, dialog._menu.empty() ? MAKEINTRESOURCEW(dialog._menuOrdinal) : dialog._menu.c_str()));

			for (const ItemTemplate& item : dialog._items)
			{
				const RECT itemRect = toPixels(*state, item._x, item._y, item._cx, item._cy);
				std::u16string title = item._title;
				HWND control = CreateWindowExW(item._exStyle | WS_EX_NOPARENTNOTIFY, item._className.c_str(),
					item._titleIsOrdinal ? nullptr : title.c_str(), (item._style | WS_CHILD) & ~WS_POPUP,
					itemRect.left, itemRect.top, itemRect.right - itemRect.left, itemRect.bottom - itemRect.top,
					hwnd, reinterpret_cast<HMENU>(static_cast<uintptr_t>(item._id)), instance,
					const_cast<BYTE*>(item._creationData));
				if (!control)
					continue;
				SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->_font), FALSE);
				// A static control with an ordinal title shows an icon resource.
				if (item._titleIsOrdinal)
				{
					HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(item._titleOrdinal));
					SendMessageW(control, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
				}
				if ((item._style & 0xF) == BS_DEFPUSHBUTTON && _wcsicmp(item._className.c_str(), WC_BUTTONW) == 0)
					state->_defaultId = item._id;
			}

			// The first control with WS_TABSTOP gets the focus unless WM_INITDIALOG says otherwise.
			HWND firstTab = GetNextDlgTabItem(hwnd, nullptr, FALSE);
			if (SendMessageW(hwnd, WM_INITDIALOG, reinterpret_cast<WPARAM>(firstTab), parameter) && firstTab && !child)
				SetFocus(firstTab);
			if (!IsWindow(hwnd))
				return nullptr;
			if ((dialog._style & WS_VISIBLE) && !modal)
				ShowWindow(hwnd, child ? SW_SHOWNA : SW_SHOW);
			return hwnd;
		}

		bool isButtonClass(HWND hwnd, UINT* buttonType = nullptr)
		{
			char16_t name[32]{};
			GetClassNameW(hwnd, name, 32);
			if (_wcsicmp(name, WC_BUTTONW) != 0)
				return false;
			if (buttonType)
				*buttonType = static_cast<UINT>(GetWindowLongW(hwnd, GWL_STYLE)) & BS_TYPEMASK;
			return true;
		}
	}

	void registerDialogClass()
	{
		registerBuiltInClass(u"#32770", DefDlgProcW, CS_DBLCLKS | CS_SAVEBITS, dialogExtraBytes, LoadCursorW(nullptr, IDC_ARROW),
			reinterpret_cast<HBRUSH>(static_cast<uintptr_t>(COLOR_BTNFACE + 1)), nullptr);
	}
}

using namespace nppmac::user;

// --- Creation -------------------------------------------------------------------------------------

HWND WINAPI CreateDialogIndirectParamW(HINSTANCE instance, LPCDLGTEMPLATEW dialogTemplate, HWND parent, DLGPROC proc, LPARAM parameter)
{
	return createDialog(instance, reinterpret_cast<const BYTE*>(dialogTemplate), parent, proc, parameter, false);
}

HWND WINAPI CreateDialogParamW(HINSTANCE instance, LPCWSTR name, HWND parent, DLGPROC proc, LPARAM parameter)
{
	std::span<const BYTE> data = resourceBytes(name, RT_DIALOG);
	if (data.empty())
	{
		SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
		return nullptr;
	}
	return createDialog(instance, data.data(), parent, proc, parameter, false);
}

INT_PTR WINAPI DialogBoxIndirectParamW(HINSTANCE instance, LPCDLGTEMPLATEW dialogTemplate, HWND parent, DLGPROC proc, LPARAM parameter)
{
	HWND owner = parent ? GetAncestor(parent, GA_ROOT) : nullptr;
	HWND hwnd = createDialog(instance, reinterpret_cast<const BYTE*>(dialogTemplate), owner, proc, parameter, true);
	if (!hwnd)
		return -1;
	DialogState* state = stateOf(hwnd);
	if (state->_ended)
	{
		const INT_PTR result = state->_result;
		DestroyWindow(hwnd);
		return result;
	}

	// Modal: the owner is disabled while the dialog runs its own message loop.
	const bool ownerWasEnabled = owner && IsWindowEnabled(owner);
	if (ownerWasEnabled)
		EnableWindow(owner, FALSE);
	ShowWindow(hwnd, SW_SHOW);
	MSG msg{};
	while (IsWindow(hwnd) && !stateOf(hwnd)->_ended)
	{
		if (!GetMessageW(&msg, nullptr, 0, 0))
		{
			PostQuitMessage(static_cast<int>(msg.wParam));
			break;
		}
		if (!IsDialogMessageW(hwnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	const INT_PTR result = IsWindow(hwnd) ? stateOf(hwnd)->_result : -1;
	if (ownerWasEnabled)
		EnableWindow(owner, TRUE);
	if (IsWindow(hwnd))
		DestroyWindow(hwnd);
	if (owner)
		SetActiveWindow(owner);
	return result;
}

INT_PTR WINAPI DialogBoxParamW(HINSTANCE instance, LPCWSTR name, HWND parent, DLGPROC proc, LPARAM parameter)
{
	std::span<const BYTE> data = resourceBytes(name, RT_DIALOG);
	if (data.empty())
	{
		SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
		return -1;
	}
	return DialogBoxIndirectParamW(instance, reinterpret_cast<LPCDLGTEMPLATEW>(data.data()), parent, proc, parameter);
}

BOOL WINAPI EndDialog(HWND hwnd, INT_PTR result)
{
	DialogState* state = stateOf(hwnd);
	if (!state)
		return FALSE;
	state->_ended = true;
	state->_result = result;
	// A modal dialog's loop notices; the window hides now, as on Windows.
	ShowWindow(hwnd, SW_HIDE);
	PostMessageW(hwnd, WM_NULL, 0, 0);
	return TRUE;
}

// --- DefDlgProc -----------------------------------------------------------------------------------

LRESULT WINAPI DefDlgProcW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto proc = reinterpret_cast<DLGPROC>(GetWindowLongPtrW(hwnd, DWLP_DLGPROC));
	SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, 0);
	if (proc)
	{
		const INT_PTR handled = proc(hwnd, message, wParam, lParam);
		if (!IsWindow(hwnd))
			return 0;
		if (handled)
		{
			// These messages return what the dialog procedure returned, not DWLP_MSGRESULT.
			switch (message)
			{
				case WM_COMPAREITEM:
				case WM_VKEYTOITEM:
				case WM_CHARTOITEM:
				case WM_QUERYDRAGICON:
				case WM_INITDIALOG:
				case WM_CTLCOLORMSGBOX:
				case WM_CTLCOLOREDIT:
				case WM_CTLCOLORLISTBOX:
				case WM_CTLCOLORBTN:
				case WM_CTLCOLORDLG:
				case WM_CTLCOLORSCROLLBAR:
				case WM_CTLCOLORSTATIC:
					return handled;
				default:
					return GetWindowLongPtrW(hwnd, DWLP_MSGRESULT);
			}
		}
	}

	DialogState* state = stateOf(hwnd);
	switch (message)
	{
		case WM_INITDIALOG:
			return TRUE;
		case WM_CLOSE:
			PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, IDCANCEL)));
			return 0;
		case WM_NCDESTROY:
			if (state && state->_ownsFont)
				DeleteObject(state->_font);
			return DefWindowProcW(hwnd, message, wParam, lParam);
		case DM_GETDEFID:
			return state ? MAKELONG(state->_defaultId, DC_HASDEFID) : 0;
		case DM_SETDEFID:
			if (state)
				state->_defaultId = static_cast<UINT>(wParam);
			return TRUE;
		case WM_NEXTDLGCTL:
		{
			HWND next = LOWORD(lParam) ? reinterpret_cast<HWND>(wParam) : GetNextDlgTabItem(hwnd, GetFocus(), wParam != 0);
			if (next)
				SetFocus(next);
			return 0;
		}
		case WM_GETFONT:
			return reinterpret_cast<LRESULT>(state ? state->_font : nullptr);
		case WM_ERASEBKGND:
		{
			HBRUSH brush = reinterpret_cast<HBRUSH>(SendMessageW(hwnd, WM_CTLCOLORDLG, wParam, reinterpret_cast<LPARAM>(hwnd)));
			RECT client{};
			GetClientRect(hwnd, &client);
			FillRect(reinterpret_cast<HDC>(wParam), &client, brush ? brush : GetSysColorBrush(COLOR_BTNFACE));
			return 1;
		}
		default:
			return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

// --- Navigation -----------------------------------------------------------------------------------

HWND WINAPI GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous)
{
	Window* window = fromHandle(dialog);
	if (!window)
		return nullptr;
	// The z-order (top first) is the creation order, which is the tab order.
	std::vector<Window*> candidates;
	for (Window* child : window->_children)
	{
		if ((child->_style & WS_TABSTOP) && child->_visible && !(child->_style & WS_DISABLED))
			candidates.push_back(child);
	}
	if (candidates.empty())
		return nullptr;
	auto it = std::find_if(candidates.begin(), candidates.end(), [control](Window* w) { return w->_hwnd == control; });
	if (it == candidates.end())
		return previous ? candidates.back()->_hwnd : candidates.front()->_hwnd;
	if (previous)
		return it == candidates.begin() ? candidates.back()->_hwnd : (*(it - 1))->_hwnd;
	return it + 1 == candidates.end() ? candidates.front()->_hwnd : (*(it + 1))->_hwnd;
}

HWND WINAPI GetNextDlgGroupItem(HWND dialog, HWND control, BOOL previous)
{
	return GetNextDlgTabItem(dialog, control, previous);
}

BOOL WINAPI IsDialogMessageW(HWND dialog, LPMSG msg)
{
	if (!dialog || (msg->hwnd != dialog && !IsChild(dialog, msg->hwnd)))
		return FALSE;
	if (msg->message == WM_KEYDOWN)
	{
		// Controls that want the key (edits, Scintilla) keep it.
		const LRESULT code = SendMessageW(msg->hwnd, WM_GETDLGCODE, msg->wParam, reinterpret_cast<LPARAM>(msg));
		if (code & DLGC_WANTALLKEYS)
			return FALSE;
		switch (msg->wParam)
		{
			case VK_TAB:
			{
				if (code & DLGC_WANTTAB)
					return FALSE;
				const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
				if (HWND next = GetNextDlgTabItem(dialog, GetFocus(), backwards))
					SetFocus(next);
				return TRUE;
			}
			case VK_RETURN:
			{
				const LRESULT defaultId = SendMessageW(dialog, DM_GETDEFID, 0, 0);
				UINT id = HIWORD(defaultId) == DC_HASDEFID ? LOWORD(defaultId) : IDOK;
				// A focused push button is pressed instead of the default one.
				UINT type = 0;
				HWND focus = GetFocus();
				if (focus && isButtonClass(focus, &type) && (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON))
					id = static_cast<UINT>(GetDlgCtrlID(focus));
				SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, static_cast<int>(id))));
				return TRUE;
			}
			case VK_ESCAPE:
				SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, IDCANCEL)));
				return TRUE;
			default:
				break;
		}
	}
	TranslateMessage(msg);
	DispatchMessageW(msg);
	return TRUE;
}

// --- Units ----------------------------------------------------------------------------------------

BOOL WINAPI MapDialogRect(HWND dialog, LPRECT rect)
{
	DialogState* state = stateOf(dialog);
	const int baseX = state ? state->_baseUnitX : 6;
	const int baseY = state ? state->_baseUnitY : 13;
	rect->left = MulDiv(rect->left, baseX, 4);
	rect->right = MulDiv(rect->right, baseX, 4);
	rect->top = MulDiv(rect->top, baseY, 8);
	rect->bottom = MulDiv(rect->bottom, baseY, 8);
	return TRUE;
}

DWORD WINAPI GetDialogBaseUnits()
{
	return MAKELONG(6, 13);
}

// --- Item helpers ---------------------------------------------------------------------------------

BOOL WINAPI SetDlgItemTextW(HWND dialog, int id, LPCWSTR text)
{
	HWND item = GetDlgItem(dialog, id);
	return item ? SetWindowTextW(item, text) : FALSE;
}

BOOL WINAPI SetDlgItemTextA(HWND dialog, int id, LPCSTR text)
{
	return SetDlgItemTextW(dialog, id, nppmac::toUtf16(text ? text : "").c_str());
}

UINT WINAPI GetDlgItemTextW(HWND dialog, int id, LPWSTR text, int size)
{
	HWND item = GetDlgItem(dialog, id);
	if (!item)
	{
		if (text && size > 0)
			text[0] = u'\0';
		return 0;
	}
	return static_cast<UINT>(GetWindowTextW(item, text, size));
}

UINT WINAPI GetDlgItemTextA(HWND dialog, int id, LPSTR text, int size)
{
	HWND item = GetDlgItem(dialog, id);
	if (!item)
	{
		if (text && size > 0)
			text[0] = '\0';
		return 0;
	}
	return static_cast<UINT>(GetWindowTextA(item, text, size));
}

BOOL WINAPI SetDlgItemInt(HWND dialog, int id, UINT value, BOOL isSigned)
{
	char16_t text[32]{};
	if (isSigned)
		swprintf(text, 32, u"%d", static_cast<int>(value));
	else
		swprintf(text, 32, u"%u", value);
	return SetDlgItemTextW(dialog, id, text);
}

UINT WINAPI GetDlgItemInt(HWND dialog, int id, BOOL* translated, BOOL isSigned)
{
	char16_t text[64]{};
	GetDlgItemTextW(dialog, id, text, 64);
	char16_t* end = nullptr;
	const long value = wcstol(text, &end, 10);
	const bool ok = end != text && (isSigned || value >= 0);
	if (translated)
		*translated = ok ? TRUE : FALSE;
	return ok ? static_cast<UINT>(value) : 0;
}

BOOL WINAPI CheckDlgButton(HWND dialog, int id, UINT check)
{
	SendDlgItemMessageW(dialog, id, BM_SETCHECK, check, 0);
	return TRUE;
}

UINT WINAPI IsDlgButtonChecked(HWND dialog, int id)
{
	return static_cast<UINT>(SendDlgItemMessageW(dialog, id, BM_GETCHECK, 0, 0));
}

BOOL WINAPI CheckRadioButton(HWND dialog, int first, int last, int check)
{
	for (int id = first; id <= last; ++id)
		SendDlgItemMessageW(dialog, id, BM_SETCHECK, id == check ? BST_CHECKED : BST_UNCHECKED, 0);
	return TRUE;
}
