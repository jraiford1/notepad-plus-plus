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

// The window manager's model of windows, shared by the user32 implementation files.

#pragma once

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Backend.h"

namespace nppmac::user
{
	class Window;

	// Built-in window classes (controls) can back a window with a native view and handle messages
	// themselves before the class window procedure would.
	struct BuiltInClass
	{
		// Creates the native view for a new window of the class, or returns nullptr for a
		// custom-drawn view. Called before WM_NCCREATE.
		std::function<void*(Window& window, const CREATESTRUCTW& create)> _createNativeView;
	};

	struct WindowClass
	{
		std::u16string _name;
		UINT _style = 0;
		WNDPROC _windowProc = nullptr;
		int _classExtra = 0;
		int _windowExtra = 0;
		HINSTANCE _instance = nullptr;
		HICON _icon = nullptr;
		HICON _smallIcon = nullptr;
		HCURSOR _cursor = nullptr;
		HBRUSH _background = nullptr;
		std::u16string _menuName;
		WORD _menuOrdinal = 0; // a menu named by MAKEINTRESOURCE
		std::vector<BYTE> _extra;
		std::shared_ptr<BuiltInClass> _builtIn;
		bool _drawsOwnScrollBars = false; // the runtime's controls draw WS_VSCROLL / WS_HSCROLL themselves
		bool _editsText = false;          // keys go through the macOS text input system (input methods)
		ATOM _atom = 0;
	};

	struct Subclass
	{
		SUBCLASSPROC _proc = nullptr;
		UINT_PTR _id = 0;
		DWORD_PTR _refData = 0;
	};

	class Window : public ui::ViewOwner
	{
	public:
		HWND _hwnd = nullptr;
		WindowClass* _class = nullptr;
		WNDPROC _windowProc = nullptr;
		std::vector<Subclass> _subclasses;   // the last one added is called first
		size_t _subclassDepth = 0;           // which subclass DefSubclassProc goes to next

		Window* _parent = nullptr;          // child windows: the parent; top levels: nullptr
		Window* _owner = nullptr;           // top levels: the owner window
		std::vector<Window*> _children;     // in z-order, topmost first

		DWORD _style = 0;
		DWORD _exStyle = 0;
		std::u16string _text;
		UINT_PTR _id = 0;                   // child windows: control id; top levels: HMENU
		HINSTANCE _instance = nullptr;
		LONG_PTR _userData = 0;
		std::vector<BYTE> _extra;
		std::map<std::u16string, HANDLE> _properties;
		HFONT _font = nullptr;              // remembered for WM_GETFONT by built-in classes

		// Child: position in the parent's client area. Top level: window rectangle on screen.
		RECT _rect{};
		// Client area size; for children it is the whole window.
		int _clientWidth = 0;
		int _clientHeight = 0;

		ui::NativeView* _view = nullptr;
		bool _nativeControl = false;        // backed by a native control (no surface, no WM_PAINT)
		bool _seeThrough = false;           // see nppmac::ui::setSeeThrough
		SCROLLINFO _scrollBars[2]{};        // SB_HORZ, SB_VERT; visible when the style has WS_HSCROLL / WS_VSCROLL
		bool _scrollBarsDisabled[2]{};
		bool _visible = false;
		bool _created = false;              // WM_CREATE has been sent
		bool _applyingGeometry = false;     // the window manager is moving the native window itself
		bool _destroying = false;
		bool _acceptsFiles = false;
		bool _painting = false;             // inside BeginPaint/EndPaint
		RECT _invalid{};                    // pending update region (bounding box), client coordinates
		bool _eraseBackground = false;

		// State for the WINDOWPLACEMENT of top levels.
		RECT _normalRect{};

		// Built-in class data.
		std::shared_ptr<void> _controlData;

		bool isTopLevel() const {
			return _parent == nullptr;
		}

		bool isChild() const {
			return (_style & WS_CHILD) != 0;
		}

		LRESULT send(UINT message, WPARAM wParam, LPARAM lParam);

		// ui::ViewOwner
		void nativePaint(const ui::Rect& dirty) override;
		void nativePixelsLost() override;
		void nativeFrameChanged(const ui::Rect& windowRect, const ui::Rect& clientRectOnScreen) override;
		void nativeCloseRequested() override;
		void nativeActivated(bool active) override;
		void nativeMinimized(bool minimized) override;
		void nativeFocusChanged(bool focused) override;
		void nativeMouse(ui::MouseAction action, ui::MouseButton button, ui::Point position, unsigned modifiers, int delta) override;
		void nativeFilesDropped(const std::vector<std::string>& paths, ui::Point position) override;
		bool nativeAcceptsFiles() const override;
		void nativeScroll(bool vertical, ui::ScrollPart part, double value) override;
		void nativeComposition(const std::u16string& text, size_t caret) override;
		void nativeInsertText(const std::u16string& text) override;
		ui::Rect nativeCaretRect() override;
	};

	// --- Window table -----------------------------------------------------------------------------

	Window* fromHandle(HWND hwnd);
	std::vector<Window*> topLevelWindows();

	// --- Classes ----------------------------------------------------------------------------------

	WindowClass* findClass(const char16_t* nameOrAtom);
	// Registers a class implemented by the runtime (Scintilla, controls).
	void registerBuiltInClass(const std::u16string& name, WNDPROC proc, UINT style, int windowExtra, HCURSOR cursor,
		HBRUSH background, std::shared_ptr<BuiltInClass> builtIn);
	void ensureBuiltInClasses();

	// --- Geometry ---------------------------------------------------------------------------------

	// The client area's top-left corner in screen coordinates.
	POINT clientOriginOnScreen(Window* window);
	// Recomputes a top level's rectangles from its native window (after the user moved or resized it).
	void syncTopLevelGeometry(Window* window);

	// Applies a new position and size (the work of SetWindowPos without the messages).
	void applyGeometry(Window* window, const RECT& rect);

	// --- Painting ---------------------------------------------------------------------------------

	void invalidate(Window* window, const RECT* rect, bool erase);
	void validate(Window* window, const RECT* rect);

	// --- Focus and activation ---------------------------------------------------------------------

	Window* focusWindow();
	void setFocusWindow(Window* window);

	// Makes a window see-through (nppmac::ui::setSeeThrough): group boxes and WS_EX_TRANSPARENT.
	void setSeeThrough(HWND hwnd, bool seeThrough);

	// Standard scroll bars: shows or hides them as the style says, and makes the client area fit.
	void updateScrollBars(Window* window);
	void setClassDrawsOwnScrollBars(const char16_t* className);
	// The class edits text: its keys go through the macOS text input system first (input methods,
	// dead keys), as Windows' IME does. It gets the text being composed as imeComposition and is
	// asked where its caret is with imeCaretRect.
	void setClassEditsText(const char16_t* className);

	// The window the caret was made for (CreateCaret), and where SetCaretPos put it.
	HWND caretWindow(POINT& position);
	// wParam: the caret's place in the text being composed; lParam: const std::u16string* (empty when
	// composing ends).
	constexpr UINT imeComposition = WM_APP + 0x3F03;
	// lParam: RECT* to fill with the caret (client coordinates); returns TRUE if it did.
	constexpr UINT imeCaretRect = WM_APP + 0x3F04;
	// The client size once the visible standard scroll bars take their room.
	void fitClientToScrollBars(Window* window);
	Window* activeWindow();
	void setActiveWindow(Window* window);

	// --- Messages ---------------------------------------------------------------------------------

	LRESULT callWindowProc(Window* window, UINT message, WPARAM wParam, LPARAM lParam);
	// Keyboard and mouse messages carry the input state they were posted with (and a key down its
	// characters), which takes effect when they are retrieved.
	void postMessage(Window* window, UINT message, WPARAM wParam, LPARAM lParam, void* nativeEvent = nullptr, const std::u16string& characters = {});
	// The native key event that came with the message being processed (or nullptr).
	void* currentNativeEvent();
	DWORD messageTime();
	POINT messagePosition();

	// --- Input state ------------------------------------------------------------------------------

	void setKeyState(BYTE virtualKey, bool down);
	void setModifierState(unsigned modifiers);
	unsigned modifierFlagsForMessage(); // MK_* flags for mouse messages

	// --- Timers -----------------------------------------------------------------------------------

	void killWindowTimers(Window* window);

	// --- Destruction ------------------------------------------------------------------------------

	// Called by DestroyWindow for every window being destroyed, after WM_NCDESTROY.
	void forgetWindow(Window* window);
	// Removes a destroyed window's menu bar.
	void detachMenu(HWND hwnd);
}
