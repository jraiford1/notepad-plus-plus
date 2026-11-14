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

// The "Scintilla" window class (Scintilla_RegisterClasses), over Scintilla's own Cocoa view.
// SCI_* messages go to Scintilla, as does the direct function upstream fetches with
// SCI_GETDIRECTFUNCTION; Scintilla's notifications reach the parent as WM_NOTIFY and WM_COMMAND.
//
// Both paths go through sendToScintilla, which converts font sizes: Notepad++ gives them in
// Windows points, which are 96/72 of a pixel, and the runtime's pixel is a macOS point, the unit
// Scintilla's Cocoa layer sizes fonts in. Without this, text would be a quarter smaller than on
// Windows.

#include "gdi/Gdi.h"
#include "user/Window.h"
#include "ScintillaBridge.h"

#include "Scintilla.h"

#include <richedit.h>

namespace
{
	using nppmac::user::Window;
	using nppmac::user::fromHandle;

	struct ScintillaData
	{
		void* _view = nullptr;
		SciFnDirect _direct = nullptr;  // Scintilla's own direct function and pointer
		sptr_t _directPointer = 0;
		bool _swallowCharacters = false; // the key's characters already went to Scintilla with the key
	};

	constexpr intptr_t windowsPointsPerInch = 72;
	constexpr intptr_t windowsPixelsPerInch = 96;

	intptr_t toScintillaFontSize(intptr_t size)
	{
		return size * windowsPixelsPerInch / windowsPointsPerInch;
	}

	intptr_t fromScintillaFontSize(intptr_t size)
	{
		return (size * windowsPointsPerInch + windowsPixelsPerInch / 2) / windowsPixelsPerInch;
	}

	// Printing: Notepad++ gives SCI_FORMATRANGEFULL the printer's HDC, and Scintilla's Cocoa layer draws
	// into a CGContext: the DC's, ready for drawing. Pages it only measures (no page is open) get a
	// scratch context.
	template <typename Range>
	sptr_t formatRange(const ScintillaData& data, unsigned int message, uptr_t draw, sptr_t lParam)
	{
		if (!lParam)
			return data._direct(data._directPointer, message, draw, lParam);
		static CGContextRef scratch = []
		{
			CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
			CGContextRef context = CGBitmapContextCreate(nullptr, 1, 1, 8, 4, colorSpace, static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst));
			CGColorSpaceRelease(colorSpace);
			return context;
		}();
		Range range = *reinterpret_cast<const Range*>(lParam);
		nppmac::gdi::DC* dc = nppmac::gdi::toDC(static_cast<HDC>(range.hdc));
		CGContextRef context = dc ? dc->begin() : nullptr;
		range.hdc = range.hdcTarget = context ? context : scratch;
		const sptr_t result = data._direct(data._directPointer, message, context ? draw : 0, reinterpret_cast<sptr_t>(&range));
		if (context)
			dc->end();
		return result;
	}

	// The Windows Edit control's messages, which Scintilla's Windows layer translates (SciMessageFromEM
	// and EditMessage in ScintillaWin.cxx) and its Cocoa layer does not know. Notepad++ sends some:
	// Edit > Undo is WM_UNDO, Cut, Copy and Paste are WM_CUT, WM_COPY and WM_PASTE. Messages whose
	// structures differ from Scintilla's (EM_FINDTEXTEX, EM_FORMATRANGE, EM_GETTEXTRANGE) are left out:
	// Notepad++ uses the SCI_ ones.
	bool editMessage(const ScintillaData& data, unsigned int message, uptr_t wParam, sptr_t lParam, sptr_t& result)
	{
		auto sci = [&data](unsigned int sciMessage, uptr_t w = 0, sptr_t l = 0)
		{
			return data._direct(data._directPointer, sciMessage, w, l);
		};
		unsigned int simple = 0;
		switch (message)
		{
			case WM_UNDO: simple = SCI_UNDO; break;
			case WM_CUT: simple = SCI_CUT; break;
			case WM_COPY: simple = SCI_COPY; break;
			case WM_PASTE: simple = SCI_PASTE; break;
			case WM_CLEAR: simple = SCI_CLEAR; break;
			case EM_CANPASTE: simple = SCI_CANPASTE; break;
			case EM_CANUNDO: simple = SCI_CANUNDO; break;
			case EM_EMPTYUNDOBUFFER: simple = SCI_EMPTYUNDOBUFFER; break;
			case EM_GETFIRSTVISIBLELINE: simple = SCI_GETFIRSTVISIBLELINE; break;
			case EM_GETLINECOUNT: simple = SCI_GETLINECOUNT; break;
			case EM_GETSELTEXT: simple = SCI_GETSELTEXT; break;
			case EM_HIDESELECTION: simple = SCI_HIDESELECTION; break;
			case EM_LINEINDEX: simple = SCI_POSITIONFROMLINE; break;
			case EM_LINESCROLL: simple = SCI_LINESCROLL; break;
			case EM_REPLACESEL: simple = SCI_REPLACESEL; break;
			case EM_SCROLLCARET: simple = SCI_SCROLLCARET; break;
			case EM_SETREADONLY: simple = SCI_SETREADONLY; break;
			default: break;
		}
		if (simple != 0)
		{
			result = sci(simple, wParam, lParam);
			return true;
		}

		auto start = [&sci] { return sci(SCI_GETSELECTIONSTART); };
		auto end = [&sci] { return sci(SCI_GETSELECTIONEND); };
		switch (message)
		{
			case EM_LINEFROMCHAR:
				result = sci(SCI_LINEFROMPOSITION, static_cast<sptr_t>(wParam) < 0 ? static_cast<uptr_t>(start()) : wParam);
				return true;
			case EM_EXLINEFROMCHAR:
				result = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(lParam));
				return true;
			case EM_GETSEL:
			{
				const sptr_t first = start();
				const sptr_t last = end();
				if (wParam)
					*reinterpret_cast<DWORD*>(wParam) = static_cast<DWORD>(first);
				if (lParam)
					*reinterpret_cast<DWORD*>(lParam) = static_cast<DWORD>(last);
				result = MAKELRESULT(first, last);
				return true;
			}
			case EM_EXGETSEL:
				if (lParam)
				{
					auto* range = reinterpret_cast<CHARRANGE*>(lParam);
					range->cpMin = static_cast<LONG>(start());
					range->cpMax = static_cast<LONG>(end());
				}
				result = 0;
				return true;
			case EM_SETSEL:
			{
				sptr_t anchor = static_cast<sptr_t>(wParam);
				sptr_t caret = lParam;
				if (anchor == 0 && caret == -1)
					caret = sci(SCI_GETLENGTH);
				if (anchor == -1)
					anchor = caret; // removes the selection
				sci(SCI_SETSEL, static_cast<uptr_t>(anchor), caret);
				sci(SCI_SCROLLCARET);
				result = 0;
				return true;
			}
			case EM_EXSETSEL:
			{
				result = 0;
				if (!lParam)
					return true;
				const auto* range = reinterpret_cast<const CHARRANGE*>(lParam);
				sci(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
				if (range->cpMin == 0 && range->cpMax == -1)
					sci(SCI_SETSEL, 0, sci(SCI_GETLENGTH));
				else
					sci(SCI_SETSEL, static_cast<uptr_t>(range->cpMin), range->cpMax);
				sci(SCI_SCROLLCARET);
				result = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETSELECTIONSTART)));
				return true;
			}
			default:
				return false;
		}
	}

	sptr_t sendToScintilla(const ScintillaData& data, unsigned int message, uptr_t wParam, sptr_t lParam)
	{
		if (sptr_t result = 0; editMessage(data, message, wParam, lParam, result))
			return result;
		switch (message)
		{
			case SCI_ASSIGNCMDKEY:
			case SCI_CLEARCMDKEY:
			{
				// Notepad++ names letter keys by their Windows virtual-key code, the capital letter
				// ('Z' for Ctrl+Z). Scintilla's Cocoa layer looks keys up by the character typed,
				// which without Shift is the small letter: the key is assigned for that one too.
				const uptr_t key = wParam & 0xFFFF;
				const uptr_t modifiers = wParam >> 16;
				const sptr_t result = data._direct(data._directPointer, message, wParam, lParam);
				if (key >= 'A' && key <= 'Z' && !(modifiers & SCMOD_SHIFT))
					data._direct(data._directPointer, message, (key - 'A' + 'a') | (modifiers << 16), lParam);
				return result;
			}
			case SCI_FORMATRANGEFULL:
				return formatRange<Sci_RangeToFormatFull>(data, message, wParam, lParam);
			case SCI_STYLESETSIZE:
				return data._direct(data._directPointer, SCI_STYLESETSIZEFRACTIONAL, wParam, toScintillaFontSize(lParam * SC_FONT_SIZE_MULTIPLIER));
			case SCI_STYLESETSIZEFRACTIONAL:
				return data._direct(data._directPointer, message, wParam, toScintillaFontSize(lParam));
			case SCI_STYLEGETSIZE:
			{
				const intptr_t size = fromScintillaFontSize(data._direct(data._directPointer, SCI_STYLEGETSIZEFRACTIONAL, wParam, 0));
				return (size + SC_FONT_SIZE_MULTIPLIER / 2) / SC_FONT_SIZE_MULTIPLIER;
			}
			case SCI_STYLEGETSIZEFRACTIONAL:
				return fromScintillaFontSize(data._direct(data._directPointer, message, wParam, lParam));
			default:
				return data._direct(data._directPointer, message, wParam, lParam);
		}
	}

	// What SCI_GETDIRECTFUNCTION hands out; the pointer is the window's ScintillaData.
	sptr_t directFunction(sptr_t pointer, unsigned int message, uptr_t wParam, sptr_t lParam)
	{
		return sendToScintilla(*reinterpret_cast<const ScintillaData*>(pointer), message, wParam, lParam);
	}

	ScintillaData* dataOf(Window* window)
	{
		return window ? static_cast<ScintillaData*>(window->_controlData.get()) : nullptr;
	}

	void notify(intptr_t windowId, unsigned message, uintptr_t wParam, uintptr_t lParam)
	{
		HWND hwnd = reinterpret_cast<HWND>(windowId);
		Window* window = fromHandle(hwnd);
		if (!window || !window->_parent || window->_destroying)
			return;
		if (message == nppmac::scintilla::cocoaNotify)
		{
			auto* notification = reinterpret_cast<SCNotification*>(lParam);
			notification->nmhdr.hwndFrom = hwnd;
			notification->nmhdr.idFrom = window->_id;
			SendMessageW(window->_parent->_hwnd, WM_NOTIFY, window->_id, static_cast<LPARAM>(lParam));
		}
		else if (message == nppmac::scintilla::cocoaCommand)
		{
			SendMessageW(window->_parent->_hwnd, WM_COMMAND, wParam, reinterpret_cast<LPARAM>(hwnd));
		}
	}

	void contextMenu(intptr_t windowId, int screenX, int screenY)
	{
		HWND hwnd = reinterpret_cast<HWND>(windowId);
		if (Window* window = fromHandle(hwnd))
			nppmac::user::postMessage(window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(screenX, screenY));
	}

	LRESULT CALLBACK scintillaProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Window* window = fromHandle(hwnd);
		ScintillaData* data = dataOf(window);
		if (!data || !data->_view)
			return DefWindowProcW(hwnd, message, wParam, lParam);

		if (message == SCI_GETDIRECTFUNCTION)
			return reinterpret_cast<LRESULT>(&directFunction);
		if (message == SCI_GETDIRECTPOINTER)
			return reinterpret_cast<LRESULT>(data);
		if (message >= SCI_START && message < 5000)
			return sendToScintilla(*data, message, wParam, lParam);
		if (sptr_t result = 0; editMessage(*data, message, wParam, lParam, result))
			return result;

		switch (message)
		{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYUP:
			{
				if (void* event = nppmac::user::currentNativeEvent())
				{
					data->_swallowCharacters = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
					nppmac::ui::forwardKey(window->_view, event);
				}
				return 0;
			}
			case WM_CHAR:
			case WM_SYSCHAR:
			{
				if (data->_swallowCharacters)
					return 0;
				const char16_t ch = static_cast<char16_t>(wParam);
				if (ch >= 0x20 || ch == u'\t' || ch == u'\r')
					nppmac::scintilla::insertText(data->_view, std::u16string(1, ch == u'\r' ? u'\n' : ch));
				return 0;
			}
			case WM_SETFOCUS:
				nppmac::scintilla::message(data->_view, SCI_SETFOCUS, 1, 0);
				return 0;
			case WM_KILLFOCUS:
				nppmac::scintilla::message(data->_view, SCI_SETFOCUS, 0, 0);
				return 0;
			case WM_GETDLGCODE:
				return DLGC_HASSETSEL | DLGC_WANTALLKEYS;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
				ValidateRect(hwnd, nullptr);
				return 0;
			case WM_GETTEXTLENGTH:
				return nppmac::scintilla::message(data->_view, SCI_GETLENGTH, 0, 0);
			case WM_DESTROY:
				nppmac::scintilla::forgetView(data->_view);
				return 0;
			case WM_SETCURSOR:
				return TRUE; // Scintilla sets its own cursors
			default:
				return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}
}

namespace nppmac::user
{
	void registerScintillaClass()
	{
		auto builtIn = std::make_shared<BuiltInClass>();
		builtIn->_createNativeView = [](Window& window, const CREATESTRUCTW&) -> void*
		{
			auto data = std::make_shared<ScintillaData>();
			data->_view = nppmac::scintilla::createView(reinterpret_cast<intptr_t>(window._hwnd), notify, contextMenu);
			window._controlData = data;
			data->_direct = reinterpret_cast<SciFnDirect>(nppmac::scintilla::message(data->_view, SCI_GETDIRECTFUNCTION, 0, 0));
			data->_directPointer = nppmac::scintilla::message(data->_view, SCI_GETDIRECTPOINTER, 0, 0);
			nppmac::scintilla::message(data->_view, SCI_SETIDENTIFIER, window._id, 0);
			return data->_view;
		};
		registerBuiltInClass(u"Scintilla", scintillaProc, CS_DBLCLKS, sizeof(LONG_PTR), LoadCursorW(nullptr, IDC_IBEAM), nullptr, builtIn);
	}
}

int Scintilla_RegisterClasses(void*)
{
	nppmac::user::ensureBuiltInClasses();
	return 1;
}

int Scintilla_ReleaseResources(void)
{
	return 1;
}
