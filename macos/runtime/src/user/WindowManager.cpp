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

// user32 windows: classes, the window tree, creation and destruction, geometry, visibility, text,
// window data, properties, focus and activation.

#include "Window.h"

#include <shlobj.h>

#include <algorithm>
#include <unordered_map>

namespace nppmac::user
{
	namespace
	{
		// HWNDs are small even numbers in a private range, never pointers.
		uintptr_t g_nextHandle = 0x10010;

		std::unordered_map<uintptr_t, Window*>& windows()
		{
			static auto* map = new std::unordered_map<uintptr_t, Window*>;
			return *map;
		}

		std::vector<Window*>& topLevels()
		{
			static auto* list = new std::vector<Window*>;
			return *list;
		}

		std::vector<std::unique_ptr<WindowClass>>& classes()
		{
			static auto* list = new std::vector<std::unique_ptr<WindowClass>>;
			return *list;
		}

		ATOM g_nextAtom = 0xC000;

		Window* g_focus = nullptr;
		Window* g_active = nullptr;

		bool isAtom(const char16_t* nameOrAtom)
		{
			return reinterpret_cast<uintptr_t>(nameOrAtom) <= 0xFFFF;
		}

		RECT toRect(const ui::Rect& rect)
		{
			return {rect._left, rect._top, rect._right, rect._bottom};
		}

		ui::Rect toUiRect(const RECT& rect)
		{
			return {rect.left, rect.top, rect.right, rect.bottom};
		}

		ui::TopLevelStyle topLevelStyle(DWORD style, DWORD exStyle)
		{
			ui::TopLevelStyle result;
			result._titled = (style & WS_CAPTION) == WS_CAPTION;
			result._closable = (style & WS_SYSMENU) != 0;
			result._resizable = (style & WS_THICKFRAME) != 0;
			result._minimizable = (style & WS_MINIMIZEBOX) != 0;
			result._toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
			result._topMost = (exStyle & WS_EX_TOPMOST) != 0;
			return result;
		}

		void removeFromParent(Window* window)
		{
			if (window->_parent)
			{
				auto& siblings = window->_parent->_children;
				siblings.erase(std::remove(siblings.begin(), siblings.end(), window), siblings.end());
			}
			else
			{
				auto& list = topLevels();
				list.erase(std::remove(list.begin(), list.end(), window), list.end());
			}
		}

		void sendSizeAndMove(Window* window)
		{
			WPARAM sizeType = SIZE_RESTORED;
			if (window->isTopLevel() && window->_view)
			{
				if (ui::isMinimized(window->_view))
					sizeType = SIZE_MINIMIZED;
				else if (ui::isMaximized(window->_view))
					sizeType = SIZE_MAXIMIZED;
			}
			window->send(WM_SIZE, sizeType, MAKELPARAM(window->_clientWidth, window->_clientHeight));
			const POINT origin = window->isTopLevel() ? clientOriginOnScreen(window) : POINT{window->_rect.left, window->_rect.top};
			window->send(WM_MOVE, 0, MAKELPARAM(origin.x, origin.y));
		}

		void destroyRecursively(Window* window)
		{
			window->_destroying = true;
			window->send(WM_DESTROY, 0, 0);
			// Children are destroyed after their parent's WM_DESTROY, top down.
			const std::vector<Window*> children = window->_children;
			for (Window* child : children)
				destroyRecursively(child);
			window->send(WM_NCDESTROY, 0, 0);
		}
	}

	// --- Window -----------------------------------------------------------------------------------

	LRESULT Window::send(UINT message, WPARAM wParam, LPARAM lParam)
	{
		return callWindowProc(this, message, wParam, lParam);
	}

	Window* fromHandle(HWND hwnd)
	{
		auto it = windows().find(reinterpret_cast<uintptr_t>(hwnd));
		return it == windows().end() ? nullptr : it->second;
	}

	std::vector<Window*> topLevelWindows()
	{
		return topLevels();
	}

	// --- Classes ----------------------------------------------------------------------------------

	WindowClass* findClass(const char16_t* nameOrAtom)
	{
		if (!nameOrAtom)
			return nullptr;
		// The dialog class is the system atom 0x8002 (WC_DIALOG), named "#32770".
		if (isAtom(nameOrAtom) && reinterpret_cast<uintptr_t>(nameOrAtom) == 0x8002)
			nameOrAtom = u"#32770";
		for (const auto& windowClass : classes())
		{
			if (isAtom(nameOrAtom) ? windowClass->_atom == static_cast<ATOM>(reinterpret_cast<uintptr_t>(nameOrAtom))
				: _wcsicmp(windowClass->_name.c_str(), nameOrAtom) == 0)
				return windowClass.get();
		}
		return nullptr;
	}

	void registerBuiltInClass(const std::u16string& name, WNDPROC proc, UINT style, int windowExtra, HCURSOR cursor,
		HBRUSH background, std::shared_ptr<BuiltInClass> builtIn)
	{
		if (findClass(name.c_str()))
			return;
		auto windowClass = std::make_unique<WindowClass>();
		windowClass->_name = name;
		windowClass->_windowProc = proc;
		windowClass->_style = style;
		windowClass->_windowExtra = windowExtra;
		windowClass->_cursor = cursor;
		windowClass->_background = background;
		windowClass->_builtIn = std::move(builtIn);
		windowClass->_atom = g_nextAtom++;
		classes().push_back(std::move(windowClass));
	}

	// --- Geometry ---------------------------------------------------------------------------------

	POINT clientOriginOnScreen(Window* window)
	{
		POINT origin{0, 0};
		for (Window* w = window; w; w = w->_parent)
		{
			if (w->isTopLevel())
			{
				const ui::Rect client = w->_view ? ui::clientRectOnScreen(w->_view) : toUiRect(w->_rect);
				origin.x += client._left;
				origin.y += client._top;
			}
			else
			{
				origin.x += w->_rect.left;
				origin.y += w->_rect.top;
			}
		}
		return origin;
	}

	void syncTopLevelGeometry(Window* window)
	{
		if (!window->_view)
			return;
		window->_rect = toRect(ui::windowRectOnScreen(window->_view));
		const ui::Rect client = ui::clientRectOnScreen(window->_view);
		window->_clientWidth = client.width();
		window->_clientHeight = client.height();
		fitClientToScrollBars(window);
	}

	void applyGeometry(Window* window, const RECT& rect)
	{
		const int oldWidth = window->_clientWidth;
		const int oldHeight = window->_clientHeight;
		window->_rect = rect;
		if (window->isTopLevel())
		{
			if (window->_view)
			{
				// AppKit reports the change back (windowDidResize:); that report is not a user move.
				window->_applyingGeometry = true;
				ui::setFrame(window->_view, toUiRect(rect));
				window->_applyingGeometry = false;
				syncTopLevelGeometry(window);
			}
			else
			{
				window->_clientWidth = rect.right - rect.left;
				window->_clientHeight = rect.bottom - rect.top;
				fitClientToScrollBars(window);
			}
		}
		else
		{
			window->_clientWidth = std::max<LONG>(0, rect.right - rect.left);
			window->_clientHeight = std::max<LONG>(0, rect.bottom - rect.top);
			fitClientToScrollBars(window);
			if (window->_view)
				ui::setFrame(window->_view, toUiRect(rect));
		}
		if ((oldWidth != window->_clientWidth || oldHeight != window->_clientHeight) && !window->_nativeControl)
			invalidate(window, nullptr, true);
	}

	// --- Focus ------------------------------------------------------------------------------------

	Window* focusWindow()
	{
		return g_focus;
	}

	void setSeeThrough(HWND hwnd, bool seeThrough)
	{
		Window* window = fromHandle(hwnd);
		if (!window || window->_nativeControl)
			return;
		window->_seeThrough = seeThrough;
		nppmac::ui::setSeeThrough(window->_view, seeThrough);
	}

	void setFocusWindow(Window* window)
	{
		g_focus = window;
	}

	Window* activeWindow()
	{
		return g_active;
	}

	void setActiveWindow(Window* window)
	{
		if (window == g_active)
			return;
		Window* previous = g_active;
		g_active = window;
		if (previous && fromHandle(previous->_hwnd))
		{
			previous->send(WM_NCACTIVATE, FALSE, 0);
			previous->send(WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(window ? window->_hwnd : nullptr));
		}
		if (window)
		{
			window->send(WM_NCACTIVATE, TRUE, 0);
			window->send(WM_ACTIVATE, WA_ACTIVE, reinterpret_cast<LPARAM>(previous ? previous->_hwnd : nullptr));
			// A window activated without taking focus itself gives it to the window (DefWindowProc).
			if (!g_focus || GetAncestor(g_focus->_hwnd, GA_ROOT) != window->_hwnd)
				SetFocus(window->_hwnd);
		}
	}

	void forgetWindow(Window* window)
	{
		if (g_focus == window)
			g_focus = nullptr;
		if (g_active == window)
			g_active = nullptr;
	}

	// --- Native callbacks -------------------------------------------------------------------------

	void Window::nativeFrameChanged(const ui::Rect&, const ui::Rect&)
	{
		if (_applyingGeometry || !_created)
			return;
		const int oldWidth = _clientWidth;
		const int oldHeight = _clientHeight;
		const RECT oldRect = _rect;
		syncTopLevelGeometry(this);
		if (oldWidth == _clientWidth && oldHeight == _clientHeight && std::memcmp(&oldRect, &_rect, sizeof(RECT)) == 0)
			return;

		WINDOWPOS position{};
		position.hwnd = _hwnd;
		position.x = _rect.left;
		position.y = _rect.top;
		position.cx = _rect.right - _rect.left;
		position.cy = _rect.bottom - _rect.top;
		position.flags = SWP_NOZORDER | SWP_NOACTIVATE;
		send(WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&position));
		if (oldWidth != _clientWidth || oldHeight != _clientHeight)
			invalidate(this, nullptr, true);
	}

	void Window::nativeCloseRequested()
	{
		send(WM_SYSCOMMAND, SC_CLOSE, 0);
	}

	void Window::nativeActivated(bool active)
	{
		if (active)
			setActiveWindow(this);
		else if (g_active == this)
			setActiveWindow(nullptr);
	}

	void Window::nativeMinimized(bool)
	{
		syncTopLevelGeometry(this);
		sendSizeAndMove(this);
	}

	void Window::nativeFocusChanged(bool)
	{
	}

	void Window::nativeComposition(const std::u16string& text, size_t caret)
	{
		send(imeComposition, caret, reinterpret_cast<LPARAM>(&text));
	}

	void Window::nativeInsertText(const std::u16string& text)
	{
		ui::windowManagerInsertText(text);
	}

	ui::Rect Window::nativeCaretRect()
	{
		RECT caret{};
		if (!send(imeCaretRect, 0, reinterpret_cast<LPARAM>(&caret)))
		{
			// Below the window's client area, where a candidate window does not hide it.
			GetClientRect(_hwnd, &caret);
			caret.top = caret.bottom;
		}
		return ui::Rect{caret.left, caret.top, caret.right, caret.bottom};
	}

	void Window::nativeFilesDropped(const std::vector<std::string>& paths, ui::Point position)
	{
		// An HDROP as DragQueryFileW reads it: DROPFILES, then the paths, doubly terminated.
		std::u16string list;
		for (const std::string& path : paths)
		{
			list += toUtf16(path);
			list += u'\0';
		}
		list += u'\0';
		const size_t size = sizeof(DROPFILES) + list.size() * sizeof(char16_t);
		HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
		auto* header = static_cast<DROPFILES*>(GlobalLock(drop));
		header->pFiles = sizeof(DROPFILES);
		header->pt = {position._x, position._y};
		header->fNC = FALSE;
		header->fWide = TRUE;
		std::memcpy(reinterpret_cast<BYTE*>(header) + sizeof(DROPFILES), list.data(), list.size() * sizeof(char16_t));
		GlobalUnlock(drop);

		// The message goes to the nearest window that accepts files (WS_EX_ACCEPTFILES).
		Window* target = this;
		while (target && !target->_acceptsFiles && !(target->_exStyle & WS_EX_ACCEPTFILES))
			target = target->_parent;
		postMessage(target ? target : this, WM_DROPFILES, reinterpret_cast<WPARAM>(drop), 0);
	}

	bool Window::nativeAcceptsFiles() const
	{
		for (const Window* w = this; w; w = w->_parent)
		{
			if (w->_acceptsFiles || (w->_exStyle & WS_EX_ACCEPTFILES))
				return true;
		}
		return false;
	}
}

void nppmac::ui::windowManagerTestCommand(int id)
{
	using namespace nppmac::user;
	Window* target = activeWindow();
	if (!target && !topLevels().empty())
		target = topLevels().front();
	if (target)
		PostMessageW(target->_hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

void nppmac::ui::windowManagerApplicationActivated(bool active)
{
	using namespace nppmac::user;
	// To every top level, as Windows sends it; Notepad++ checks its files for outside changes then.
	const std::vector<Window*> windows = topLevelWindows();
	for (Window* window : windows)
	{
		if (fromHandle(window->_hwnd))
			window->send(WM_ACTIVATEAPP, active ? TRUE : FALSE, 0);
	}
}

bool nppmac::ui::windowManagerOpenFiles(const std::vector<std::string>& paths)
{
	using namespace nppmac::user;
	// The program's main window: the first visible top level that accepts dropped files.
	for (Window* window : topLevelWindows())
	{
		if (!window->_visible || !window->nativeAcceptsFiles())
			continue;
		// At the middle of the client area, which is the editor: the drop goes to the current view.
		window->nativeFilesDropped(paths, Point{window->_clientWidth / 2, window->_clientHeight / 2});
		return true;
	}
	return false;
}

nppmac::ui::NativeView* nppmac::ui::windowManagerSnapshotView()
{
	using namespace nppmac::user;
	NativeView* chosen = nullptr;
	for (Window* window : topLevelWindows())
	{
		if (window->_visible && !(window->_style & WS_DISABLED) && !(window->_style & WS_CHILD) && window->_view
			&& window->_clientWidth > 0 && window->_clientHeight > 0 && window->_class && window->_class->_name != u"tooltips_class32")
			chosen = window->_view;
	}
	return chosen;
}

nppmac::ui::NativeView* nppmac::ui::windowManagerFocusView()
{
	nppmac::user::Window* focus = nppmac::user::focusWindow();
	return focus ? focus->_view : nullptr;
}

void nppmac::ui::windowManagerAppearanceChanged()
{
	using namespace nppmac::user;
	// Sent to a copy of the list: a window may go away while handling it.
	const std::vector<Window*> windows = topLevels();
	for (Window* window : windows)
	{
		if (fromHandle(window->_hwnd))
			window->send(WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(u"ImmersiveColorSet"));
	}
}

void nppmac::ui::windowManagerDumpTree()
{
	std::function<void(nppmac::user::Window*, int)> dump = [&dump](nppmac::user::Window* window, int depth)
	{
		// The text as the window reports it (an edit control keeps its own).
		char16_t text[201]{};
		GetWindowTextW(window->_hwnd, text, 201);
		std::fprintf(stderr, "%*s%p %s id=%lu rect=(%d,%d,%d,%d) client=%dx%d %s%s text=\"%s\"\n", depth * 2, "", static_cast<void*>(window->_hwnd),
			nppmac::toUtf8(window->_class->_name).c_str(), static_cast<unsigned long>(window->_id), window->_rect.left, window->_rect.top,
			window->_rect.right, window->_rect.bottom, window->_clientWidth, window->_clientHeight, window->_visible ? "visible" : "hidden",
			window->_nativeControl ? " native" : "", nppmac::toUtf8(text).c_str());
		for (nppmac::user::Window* child : window->_children)
			dump(child, depth + 1);
	};
	for (nppmac::user::Window* window : nppmac::user::topLevelWindows())
		dump(window, 0);
}

using namespace nppmac::user;
using nppmac::to_u16string;
using nppmac::toUtf16;
using nppmac::toUtf8;

// --- Classes --------------------------------------------------------------------------------------

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW* info)
{
	ensureBuiltInClasses();
	if (!info || !info->lpszClassName)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	if (!reinterpret_cast<uintptr_t>(info->lpszClassName) || findClass(info->lpszClassName))
	{
		SetLastError(ERROR_CLASS_ALREADY_EXISTS);
		return 0;
	}
	auto windowClass = std::make_unique<WindowClass>();
	windowClass->_name = info->lpszClassName;
	windowClass->_style = info->style;
	windowClass->_windowProc = info->lpfnWndProc;
	windowClass->_classExtra = info->cbClsExtra;
	windowClass->_windowExtra = info->cbWndExtra;
	windowClass->_instance = info->hInstance;
	windowClass->_icon = info->hIcon;
	windowClass->_smallIcon = info->hIconSm;
	windowClass->_cursor = info->hCursor;
	windowClass->_background = info->hbrBackground;
	if (info->lpszMenuName && !isAtom(info->lpszMenuName))
		windowClass->_menuName = info->lpszMenuName;
	else if (info->lpszMenuName)
		windowClass->_menuOrdinal = static_cast<WORD>(reinterpret_cast<uintptr_t>(info->lpszMenuName));
	windowClass->_extra.resize(static_cast<size_t>(std::max(0, info->cbClsExtra)));
	windowClass->_atom = g_nextAtom++;
	const ATOM atom = windowClass->_atom;
	classes().push_back(std::move(windowClass));
	return atom;
}

ATOM WINAPI RegisterClassW(const WNDCLASSW* info)
{
	WNDCLASSEXW extended{};
	extended.cbSize = sizeof(extended);
	extended.style = info->style;
	extended.lpfnWndProc = info->lpfnWndProc;
	extended.cbClsExtra = info->cbClsExtra;
	extended.cbWndExtra = info->cbWndExtra;
	extended.hInstance = info->hInstance;
	extended.hIcon = info->hIcon;
	extended.hCursor = info->hCursor;
	extended.hbrBackground = info->hbrBackground;
	extended.lpszMenuName = info->lpszMenuName;
	extended.lpszClassName = info->lpszClassName;
	return RegisterClassExW(&extended);
}

BOOL WINAPI UnregisterClassW(LPCWSTR className, HINSTANCE)
{
	auto& list = classes();
	auto it = std::find_if(list.begin(), list.end(), [className](const auto& windowClass)
	{
		return isAtom(className) ? windowClass->_atom == static_cast<ATOM>(reinterpret_cast<uintptr_t>(className))
			: _wcsicmp(windowClass->_name.c_str(), className) == 0;
	});
	if (it == list.end() || (*it)->_builtIn)
	{
		SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
		return FALSE;
	}
	// Windows of the class keep a pointer to it, so a class in use stays registered.
	for (const auto& [handle, window] : windows())
	{
		if (window->_class == it->get())
		{
			SetLastError(ERROR_CLASS_HAS_WINDOWS);
			return FALSE;
		}
	}
	list.erase(it);
	return TRUE;
}

BOOL WINAPI GetClassInfoExW(HINSTANCE, LPCWSTR className, LPWNDCLASSEXW info)
{
	ensureBuiltInClasses();
	WindowClass* windowClass = findClass(className);
	if (!windowClass)
	{
		SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
		return FALSE;
	}
	info->style = windowClass->_style;
	info->lpfnWndProc = windowClass->_windowProc;
	info->cbClsExtra = windowClass->_classExtra;
	info->cbWndExtra = windowClass->_windowExtra;
	info->hInstance = windowClass->_instance;
	info->hIcon = windowClass->_icon;
	info->hIconSm = windowClass->_smallIcon;
	info->hCursor = windowClass->_cursor;
	info->hbrBackground = windowClass->_background;
	if (windowClass->_menuOrdinal)
		info->lpszMenuName = MAKEINTRESOURCEW(windowClass->_menuOrdinal);
	else
		info->lpszMenuName = windowClass->_menuName.empty() ? nullptr : windowClass->_menuName.c_str();
	info->lpszClassName = windowClass->_name.c_str();
	return windowClass->_atom;
}

BOOL WINAPI GetClassInfoW(HINSTANCE instance, LPCWSTR className, LPWNDCLASSW info)
{
	WNDCLASSEXW extended{};
	extended.cbSize = sizeof(extended);
	if (!GetClassInfoExW(instance, className, &extended))
		return FALSE;
	info->style = extended.style;
	info->lpfnWndProc = extended.lpfnWndProc;
	info->cbClsExtra = extended.cbClsExtra;
	info->cbWndExtra = extended.cbWndExtra;
	info->hInstance = extended.hInstance;
	info->hIcon = extended.hIcon;
	info->hCursor = extended.hCursor;
	info->hbrBackground = extended.hbrBackground;
	info->lpszMenuName = extended.lpszMenuName;
	info->lpszClassName = extended.lpszClassName;
	return TRUE;
}

int WINAPI GetClassNameW(HWND hwnd, LPWSTR name, int size)
{
	Window* window = fromHandle(hwnd);
	if (!window || size <= 0)
		return 0;
	lstrcpynW(name, window->_class->_name.c_str(), size);
	return lstrlenW(name);
}

int WINAPI GetClassNameA(HWND hwnd, LPSTR name, int size)
{
	Window* window = fromHandle(hwnd);
	if (!window || size <= 0)
		return 0;
	lstrcpynA(name, toUtf8(window->_class->_name).c_str(), size);
	return lstrlenA(name);
}

ULONG_PTR WINAPI GetClassLongPtrW(HWND hwnd, int index)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return 0;
	WindowClass* windowClass = window->_class;
	switch (index)
	{
		case GCLP_HBRBACKGROUND:
			return reinterpret_cast<ULONG_PTR>(windowClass->_background);
		case GCLP_HCURSOR:
			return reinterpret_cast<ULONG_PTR>(windowClass->_cursor);
		case GCLP_HICON:
			return reinterpret_cast<ULONG_PTR>(windowClass->_icon);
		case GCLP_HICONSM:
			return reinterpret_cast<ULONG_PTR>(windowClass->_smallIcon);
		case GCLP_WNDPROC:
			return reinterpret_cast<ULONG_PTR>(windowClass->_windowProc);
		case GCL_STYLE:
			return windowClass->_style;
		case GCW_ATOM:
			return windowClass->_atom;
		default:
			if (index >= 0 && static_cast<size_t>(index) + sizeof(ULONG_PTR) <= windowClass->_extra.size())
			{
				ULONG_PTR value = 0;
				std::memcpy(&value, windowClass->_extra.data() + index, sizeof(value));
				return value;
			}
			return 0;
	}
}

ULONG_PTR WINAPI SetClassLongPtrW(HWND hwnd, int index, LONG_PTR value)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return 0;
	const ULONG_PTR previous = GetClassLongPtrW(hwnd, index);
	WindowClass* windowClass = window->_class;
	switch (index)
	{
		case GCLP_HBRBACKGROUND:
			windowClass->_background = reinterpret_cast<HBRUSH>(value);
			break;
		case GCLP_HCURSOR:
			windowClass->_cursor = reinterpret_cast<HCURSOR>(value);
			break;
		case GCLP_HICON:
			windowClass->_icon = reinterpret_cast<HICON>(value);
			break;
		case GCLP_HICONSM:
			windowClass->_smallIcon = reinterpret_cast<HICON>(value);
			break;
		case GCL_STYLE:
			windowClass->_style = static_cast<UINT>(value);
			break;
		default:
			if (index >= 0 && static_cast<size_t>(index) + sizeof(LONG_PTR) <= windowClass->_extra.size())
				std::memcpy(windowClass->_extra.data() + index, &value, sizeof(value));
	}
	return previous;
}

// --- Creation and destruction ---------------------------------------------------------------------

HWND WINAPI CreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y, int width, int height,
	HWND parentHandle, HMENU menu, HINSTANCE instance, LPVOID parameter)
{
	ensureBuiltInClasses();
	WindowClass* windowClass = findClass(className);
	if (!windowClass)
	{
		if (std::getenv("NPPMAC_TRACE_WINDOWS"))
			std::fprintf(stderr, "nppmac: CreateWindowEx: unknown class %s\n", isAtom(className) ? "(atom)" : toUtf8(className).c_str());
		SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
		return nullptr;
	}

	Window* parent = fromHandle(parentHandle);
	if ((style & WS_CHILD) && !parent)
	{
		SetLastError(ERROR_TLW_WITH_WSCHILD);
		return nullptr;
	}

	auto* window = new Window;
	window->_hwnd = reinterpret_cast<HWND>(g_nextHandle);
	g_nextHandle += 4;
	window->_class = windowClass;
	window->_windowProc = windowClass->_windowProc;
	window->_style = style;
	window->_exStyle = exStyle;
	window->_instance = instance;
	window->_extra.resize(static_cast<size_t>(std::max(0, windowClass->_windowExtra)));
	if (style & WS_CHILD)
	{
		window->_parent = parent;
		window->_id = reinterpret_cast<UINT_PTR>(menu);
		// A new child goes to the bottom of its siblings' z-order (which makes creation order the
		// dialog tab order).
		parent->_children.push_back(window);
	}
	else
	{
		window->_owner = parent ? fromHandle(GetAncestor(parent->_hwnd, GA_ROOT)) : nullptr;
		topLevels().insert(topLevels().begin(), window);
		// A top-level window without a menu of its own gets its class's.
		if (!menu && windowClass->_menuOrdinal)
			menu = LoadMenuW(windowClass->_instance, MAKEINTRESOURCEW(windowClass->_menuOrdinal));
		else if (!menu && !windowClass->_menuName.empty())
			menu = LoadMenuW(windowClass->_instance, windowClass->_menuName.c_str());
	}
	windows()[reinterpret_cast<uintptr_t>(window->_hwnd)] = window;

	// Default position and size for top levels.
	if (!(style & WS_CHILD))
	{
		const nppmac::ui::Rect work = nppmac::ui::mainScreenWorkArea();
		if (x == CW_USEDEFAULT || x == static_cast<int>(0x80000000))
		{
			x = work._left + 60;
			y = work._top + 40;
		}
		if (width == CW_USEDEFAULT || width == static_cast<int>(0x80000000))
		{
			width = std::min(1200, work.width() - 120);
			height = std::min(800, work.height() - 80);
		}
	}
	else
	{
		if (x == CW_USEDEFAULT)
			x = y = 0;
		if (width == CW_USEDEFAULT)
			width = height = 0;
	}

	CREATESTRUCTW create{};
	create.lpCreateParams = parameter;
	create.hInstance = instance;
	create.hMenu = menu;
	create.hwndParent = parentHandle;
	create.cy = height;
	create.cx = width;
	create.y = y;
	create.x = x;
	create.style = static_cast<LONG>(style);
	create.lpszName = windowName;
	create.lpszClass = className;
	create.dwExStyle = exStyle;

	// The native side: a built-in control's own view, or a custom-drawn view.
	void* nativeControl = windowClass->_builtIn && windowClass->_builtIn->_createNativeView ? windowClass->_builtIn->_createNativeView(*window, create) : nullptr;
	const RECT rect{x, y, x + width, y + height};
	if (nativeControl)
	{
		window->_nativeControl = true;
		window->_view = nppmac::ui::adoptView(window, parent ? parent->_view : nullptr, nativeControl, {rect.left, rect.top, rect.right, rect.bottom});
	}
	else
	{
		const nppmac::ui::ViewKind kind = (style & WS_CHILD) ? nppmac::ui::ViewKind::child
			: ((style & WS_POPUP) && (style & WS_CAPTION) != WS_CAPTION ? nppmac::ui::ViewKind::popup : nppmac::ui::ViewKind::topLevel);
		window->_view = nppmac::ui::createView(window, kind, parent && (style & WS_CHILD) ? parent->_view : nullptr,
			{rect.left, rect.top, rect.right, rect.bottom}, topLevelStyle(style, exStyle));
	}
	applyGeometry(window, rect);
	// The menu is there before WM_CREATE, which may read it with GetMenu.
	if (!(style & WS_CHILD) && menu)
		SetMenu(window->_hwnd, menu);
	if (exStyle & WS_EX_ACCEPTFILES)
		nppmac::ui::setAcceptsFiles(window->_view, true);
	if (exStyle & WS_EX_TRANSPARENT)
		setSeeThrough(window->_hwnd, true);
	// Standard scroll bars start with Windows' default range, 0 to 100.
	for (SCROLLINFO& scrollBar : window->_scrollBars)
		scrollBar = {sizeof(SCROLLINFO), 0, 0, 100, 0, 0, 0};
	if (style & (WS_VSCROLL | WS_HSCROLL))
		updateScrollBars(window);
	// Dialog controls do not clip their siblings, so where they overlap, the one Windows paints last,
	// the later one, is what shows: it is stacked on top of the others on screen (z-order is unchanged).
	if ((style & WS_CHILD) && !(style & WS_CLIPSIBLINGS) && parent && parent->_class && parent->_class->_name == u"#32770")
		nppmac::ui::orderView(window->_view, true);

	if (!window->send(WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create)))
	{
		DestroyWindow(window->_hwnd);
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	if (!fromHandle(window->_hwnd))
		return nullptr;
	RECT calc = rect;
	window->send(WM_NCCALCSIZE, FALSE, reinterpret_cast<LPARAM>(&calc));
	if (window->send(WM_CREATE, 0, reinterpret_cast<LPARAM>(&create)) == -1)
	{
		DestroyWindow(window->_hwnd);
		return nullptr;
	}
	if (!fromHandle(window->_hwnd))
		return nullptr;
	window->_created = true;
	HWND hwnd = window->_hwnd;
	sendSizeAndMove(window);
	if (!fromHandle(hwnd))
		return nullptr;

	if ((style & WS_CHILD) && !(exStyle & WS_EX_NOPARENTNOTIFY))
		parent->send(WM_PARENTNOTIFY, MAKEWPARAM(WM_CREATE, window->_id), reinterpret_cast<LPARAM>(hwnd));
	if (!fromHandle(hwnd))
		return nullptr;

	if (style & WS_VISIBLE)
	{
		window->_style &= ~WS_VISIBLE;
		ShowWindow(hwnd, (style & WS_CHILD) ? SW_SHOWNA : SW_SHOW);
	}

	return hwnd;
}

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width, int height,
	HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
{
	const std::u16string wideClass = reinterpret_cast<uintptr_t>(className) > 0xFFFF ? toUtf16(className) : std::u16string{};
	const std::u16string wideName = windowName ? toUtf16(windowName) : std::u16string{};
	return CreateWindowExW(exStyle, wideClass.empty() ? reinterpret_cast<LPCWSTR>(className) : wideClass.c_str(),
		windowName ? wideName.c_str() : nullptr, style, x, y, width, height, parent, menu, instance, parameter);
}

BOOL WINAPI DestroyWindow(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return FALSE;
	}
	if (window->_destroying)
		return TRUE;

	// Owned top-level windows go first.
	if (window->isTopLevel())
	{
		for (Window* other : topLevelWindows())
		{
			if (other->_owner == window)
				DestroyWindow(other->_hwnd);
		}
	}

	if (window->_visible)
		ShowWindow(hwnd, SW_HIDE);
	if (window->_parent && !(window->_exStyle & WS_EX_NOPARENTNOTIFY))
		window->_parent->send(WM_PARENTNOTIFY, MAKEWPARAM(WM_DESTROY, window->_id), reinterpret_cast<LPARAM>(hwnd));

	destroyRecursively(window);

	// Free the windows bottom up, after all messages.
	std::function<void(Window*)> release = [&release](Window* w)
	{
		const std::vector<Window*> children = w->_children;
		for (Window* child : children)
			release(child);
		killWindowTimers(w);
		forgetWindow(w);
		if (GetCapture() == w->_hwnd)
			ReleaseCapture();
		removeFromParent(w);
		windows().erase(reinterpret_cast<uintptr_t>(w->_hwnd));
		if (w->_view)
			nppmac::ui::destroyView(w->_view);
		w->_view = nullptr;
		delete w;
	};
	detachMenu(hwnd);
	release(window);
	return TRUE;
}

BOOL WINAPI IsWindow(HWND hwnd)
{
	return fromHandle(hwnd) ? TRUE : FALSE;
}

// Every window takes its messages' text as UTF-16.
BOOL WINAPI IsWindowUnicode(HWND hwnd)
{
	return IsWindow(hwnd);
}

// --- Tree -----------------------------------------------------------------------------------------

HWND WINAPI GetParent(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return nullptr;
	// GetParent returns the owner of a popup, the parent of a child, nothing for an overlapped window.
	if (window->isChild())
		return window->_parent ? window->_parent->_hwnd : nullptr;
	if ((window->_style & WS_POPUP) && window->_owner)
		return window->_owner->_hwnd;
	return nullptr;
}

HWND WINAPI SetParent(HWND hwnd, HWND newParentHandle)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return nullptr;
	Window* newParent = fromHandle(newParentHandle);
	HWND previous = window->_parent ? window->_parent->_hwnd : nullptr;
	if (newParent == window->_parent)
		return previous;

	removeFromParent(window);
	window->_parent = newParent;
	if (newParent)
	{
		newParent->_children.insert(newParent->_children.begin(), window);
		window->_style = (window->_style & ~WS_POPUP) | WS_CHILD;
	}
	else
	{
		topLevels().insert(topLevels().begin(), window);
	}
	if (window->_view)
	{
		nppmac::ui::setParent(window->_view, newParent ? newParent->_view : nullptr, {window->_rect.left, window->_rect.top, window->_rect.right, window->_rect.bottom},
			topLevelStyle(window->_style, window->_exStyle), window->_visible);
	}
	applyGeometry(window, window->_rect);
	return previous;
}

HWND WINAPI GetAncestor(HWND hwnd, UINT flags)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return nullptr;
	switch (flags)
	{
		case GA_PARENT:
			return window->_parent ? window->_parent->_hwnd : nullptr;
		case GA_ROOT:
		{
			while (window->_parent)
				window = window->_parent;
			return window->_hwnd;
		}
		case GA_ROOTOWNER:
		default:
		{
			while (window->_parent)
				window = window->_parent;
			while (window->_owner)
				window = window->_owner;
			return window->_hwnd;
		}
	}
}

BOOL WINAPI IsChild(HWND parentHandle, HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	for (Window* w = window ? window->_parent : nullptr; w; w = w->_parent)
	{
		if (w->_hwnd == parentHandle)
			return TRUE;
	}
	return FALSE;
}

HWND WINAPI GetWindow(HWND hwnd, UINT command)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return nullptr;
	const std::vector<Window*>& siblings = window->_parent ? window->_parent->_children : topLevels();
	auto position = std::find(siblings.begin(), siblings.end(), window);
	switch (command)
	{
		case GW_CHILD:
			return window->_children.empty() ? nullptr : window->_children.front()->_hwnd;
		case GW_HWNDFIRST:
			return siblings.empty() ? nullptr : siblings.front()->_hwnd;
		case GW_HWNDLAST:
			return siblings.empty() ? nullptr : siblings.back()->_hwnd;
		case GW_HWNDNEXT:
			return position == siblings.end() || position + 1 == siblings.end() ? nullptr : (*(position + 1))->_hwnd;
		case GW_HWNDPREV:
			return position == siblings.end() || position == siblings.begin() ? nullptr : (*(position - 1))->_hwnd;
		case GW_OWNER:
			return window->_owner ? window->_owner->_hwnd : nullptr;
		case GW_ENABLEDPOPUP:
			return nullptr;
		default:
			return nullptr;
	}
}

BOOL WINAPI EnumChildWindows(HWND parentHandle, WNDENUMPROC callback, LPARAM parameter)
{
	Window* parent = fromHandle(parentHandle);
	if (!parent)
		return FALSE;
	// All descendants, depth first, as Windows enumerates them.
	std::vector<HWND> handles;
	std::function<void(Window*)> collect = [&](Window* w)
	{
		for (Window* child : w->_children)
		{
			handles.push_back(child->_hwnd);
			collect(child);
		}
	};
	collect(parent);
	for (HWND handle : handles)
	{
		if (fromHandle(handle) && !callback(handle, parameter))
			return FALSE;
	}
	return TRUE;
}

BOOL WINAPI EnumThreadWindows(DWORD, WNDENUMPROC callback, LPARAM parameter)
{
	std::vector<HWND> handles;
	for (Window* window : topLevels())
		handles.push_back(window->_hwnd);
	for (HWND handle : handles)
	{
		if (fromHandle(handle) && !callback(handle, parameter))
			return FALSE;
	}
	return TRUE;
}

BOOL WINAPI EnumWindows(WNDENUMPROC callback, LPARAM parameter)
{
	return EnumThreadWindows(0, callback, parameter);
}

HWND WINAPI FindWindowExW(HWND parentHandle, HWND childAfter, LPCWSTR className, LPCWSTR windowName)
{
	Window* parent = fromHandle(parentHandle);
	const std::vector<Window*>& list = parent ? parent->_children : topLevels();
	bool started = childAfter == nullptr;
	for (Window* window : list)
	{
		if (!started)
		{
			started = window->_hwnd == childAfter;
			continue;
		}
		if (className)
		{
			const bool classMatches = isAtom(className) ? window->_class->_atom == static_cast<ATOM>(reinterpret_cast<uintptr_t>(className))
				: _wcsicmp(window->_class->_name.c_str(), className) == 0;
			if (!classMatches)
				continue;
		}
		if (windowName && window->_text != windowName)
			continue;
		return window->_hwnd;
	}
	return nullptr;
}

HWND WINAPI FindWindowW(LPCWSTR className, LPCWSTR windowName)
{
	return FindWindowExW(nullptr, nullptr, className, windowName);
}

HWND WINAPI GetDlgItem(HWND dialog, int id)
{
	Window* window = fromHandle(dialog);
	if (!window)
		return nullptr;
	for (Window* child : window->_children)
	{
		if (static_cast<int>(child->_id) == id)
			return child->_hwnd;
	}
	return nullptr;
}

int WINAPI GetDlgCtrlID(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	return window && window->isChild() ? static_cast<int>(window->_id) : 0;
}

DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD processId)
{
	if (processId)
		*processId = GetCurrentProcessId();
	return fromHandle(hwnd) ? GetCurrentThreadId() : 0;
}

// --- Window data ----------------------------------------------------------------------------------

LONG_PTR WINAPI GetWindowLongPtrW(HWND hwnd, int index)
{
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return 0;
	}
	switch (index)
	{
		case GWLP_WNDPROC:
			return reinterpret_cast<LONG_PTR>(window->_windowProc);
		case GWLP_HINSTANCE:
			return reinterpret_cast<LONG_PTR>(window->_instance);
		case GWLP_HWNDPARENT:
			return reinterpret_cast<LONG_PTR>(GetParent(hwnd));
		case GWLP_ID:
			return static_cast<LONG_PTR>(window->_id);
		case GWLP_USERDATA:
			return window->_userData;
		case GWL_STYLE:
			return static_cast<LONG_PTR>(window->_style);
		case GWL_EXSTYLE:
			return static_cast<LONG_PTR>(window->_exStyle);
		default:
			if (index >= 0 && static_cast<size_t>(index) + sizeof(LONG_PTR) <= window->_extra.size())
			{
				LONG_PTR value = 0;
				std::memcpy(&value, window->_extra.data() + index, sizeof(value));
				return value;
			}
			if (index >= 0 && static_cast<size_t>(index) + sizeof(LONG) <= window->_extra.size())
			{
				LONG value = 0;
				std::memcpy(&value, window->_extra.data() + index, sizeof(value));
				return value;
			}
			SetLastError(ERROR_INVALID_INDEX);
			return 0;
	}
}

LONG WINAPI GetWindowLongW(HWND hwnd, int index)
{
	return static_cast<LONG>(GetWindowLongPtrW(hwnd, index));
}

// Window procedures here all take UTF-16 messages: the ANSI forms are the same calls.
LONG_PTR WINAPI GetWindowLongPtrA(HWND hwnd, int index)
{
	return GetWindowLongPtrW(hwnd, index);
}

LONG_PTR WINAPI SetWindowLongPtrA(HWND hwnd, int index, LONG_PTR value)
{
	return SetWindowLongPtrW(hwnd, index, value);
}

LONG_PTR WINAPI SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value)
{
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return 0;
	}
	const LONG_PTR previous = GetWindowLongPtrW(hwnd, index);
	SetLastError(ERROR_SUCCESS);
	switch (index)
	{
		case GWLP_WNDPROC:
			window->_windowProc = reinterpret_cast<WNDPROC>(value);
			break;
		case GWLP_HINSTANCE:
			window->_instance = reinterpret_cast<HINSTANCE>(value);
			break;
		case GWLP_ID:
			window->_id = static_cast<UINT_PTR>(value);
			break;
		case GWLP_USERDATA:
			window->_userData = value;
			break;
		case GWLP_HWNDPARENT:
			if (window->isTopLevel())
				window->_owner = fromHandle(reinterpret_cast<HWND>(value));
			break;
		case GWL_STYLE:
		{
			STYLESTRUCT styles{window->_style, static_cast<DWORD>(value)};
			window->send(WM_STYLECHANGING, GWL_STYLE, reinterpret_cast<LPARAM>(&styles));
			const bool visibilityChanged = ((window->_style ^ styles.styleNew) & WS_VISIBLE) != 0;
			window->_style = styles.styleNew;
			if (visibilityChanged)
			{
				window->_visible = (window->_style & WS_VISIBLE) != 0;
				nppmac::ui::setVisible(window->_view, window->_visible, false);
			}
			nppmac::ui::setEnabled(window->_view, !(window->_style & WS_DISABLED));
			if ((styles.styleOld ^ styles.styleNew) & (WS_VSCROLL | WS_HSCROLL))
				updateScrollBars(window);
			window->send(WM_STYLECHANGED, GWL_STYLE, reinterpret_cast<LPARAM>(&styles));
			break;
		}
		case GWL_EXSTYLE:
		{
			STYLESTRUCT styles{window->_exStyle, static_cast<DWORD>(value)};
			window->send(WM_STYLECHANGING, GWL_EXSTYLE, reinterpret_cast<LPARAM>(&styles));
			window->_exStyle = styles.styleNew;
			nppmac::ui::setAcceptsFiles(window->_view, (window->_exStyle & WS_EX_ACCEPTFILES) != 0 || window->_acceptsFiles);
			window->send(WM_STYLECHANGED, GWL_EXSTYLE, reinterpret_cast<LPARAM>(&styles));
			break;
		}
		default:
			if (index >= 0 && static_cast<size_t>(index) + sizeof(LONG_PTR) <= window->_extra.size())
			{
				std::memcpy(window->_extra.data() + index, &value, sizeof(value));
			}
			else if (index >= 0 && static_cast<size_t>(index) + sizeof(LONG) <= window->_extra.size())
			{
				const LONG narrow = static_cast<LONG>(value);
				std::memcpy(window->_extra.data() + index, &narrow, sizeof(narrow));
			}
			else
			{
				SetLastError(ERROR_INVALID_INDEX);
				return 0;
			}
	}
	return previous;
}

LONG WINAPI SetWindowLongW(HWND hwnd, int index, LONG value)
{
	return static_cast<LONG>(SetWindowLongPtrW(hwnd, index, value));
}

BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	const std::u16string key = isAtom(name) ? u"#" + to_u16string(reinterpret_cast<uintptr_t>(name)) : std::u16string{name};
	window->_properties[key] = data;
	return TRUE;
}

HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return nullptr;
	const std::u16string key = isAtom(name) ? u"#" + to_u16string(reinterpret_cast<uintptr_t>(name)) : std::u16string{name};
	auto it = window->_properties.find(key);
	return it == window->_properties.end() ? nullptr : it->second;
}

HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return nullptr;
	const std::u16string key = isAtom(name) ? u"#" + to_u16string(reinterpret_cast<uintptr_t>(name)) : std::u16string{name};
	auto it = window->_properties.find(key);
	if (it == window->_properties.end())
		return nullptr;
	HANDLE data = it->second;
	window->_properties.erase(it);
	return data;
}

// --- Text -----------------------------------------------------------------------------------------

BOOL WINAPI SetWindowTextW(HWND hwnd, LPCWSTR text)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	return window->send(WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text ? text : u"")) ? TRUE : FALSE;
}

BOOL WINAPI SetWindowTextA(HWND hwnd, LPCSTR text)
{
	return SetWindowTextW(hwnd, toUtf16(text ? text : "").c_str());
}

int WINAPI GetWindowTextW(HWND hwnd, LPWSTR text, int size)
{
	Window* window = fromHandle(hwnd);
	if (!window || size <= 0)
		return 0;
	return static_cast<int>(window->send(WM_GETTEXT, static_cast<WPARAM>(size), reinterpret_cast<LPARAM>(text)));
}

int WINAPI GetWindowTextA(HWND hwnd, LPSTR text, int size)
{
	const int length = GetWindowTextLengthW(hwnd);
	std::u16string wide(static_cast<size_t>(length) + 1, u'\0');
	GetWindowTextW(hwnd, wide.data(), length + 1);
	wide.resize(static_cast<size_t>(length));
	if (size <= 0)
		return 0;
	lstrcpynA(text, toUtf8(wide).c_str(), size);
	return lstrlenA(text);
}

int WINAPI GetWindowTextLengthW(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	return window ? static_cast<int>(window->send(WM_GETTEXTLENGTH, 0, 0)) : 0;
}

// --- Visibility and state -------------------------------------------------------------------------

BOOL WINAPI ShowWindow(HWND hwnd, int command)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	const bool wasVisible = window->_visible;

	switch (command)
	{
		case SW_HIDE:
		{
			if (!wasVisible)
				return FALSE;
			window->send(WM_SHOWWINDOW, FALSE, 0);
			window->_visible = false;
			window->_style &= ~WS_VISIBLE;
			nppmac::ui::setVisible(window->_view, false, false);
			if (window == focusWindow() || IsChild(hwnd, focusWindow() ? focusWindow()->_hwnd : nullptr))
			{
				if (window->_parent)
					SetFocus(GetAncestor(hwnd, GA_ROOT));
			}
			return TRUE;
		}
		case SW_MINIMIZE:
		case SW_SHOWMINIMIZED:
		case SW_SHOWMINNOACTIVE:
		case SW_FORCEMINIMIZE:
		{
			if (window->isTopLevel())
			{
				if (!wasVisible)
				{
					window->_visible = true;
					window->_style |= WS_VISIBLE;
					nppmac::ui::setVisible(window->_view, true, false);
				}
				nppmac::ui::minimize(window->_view);
				window->_style |= WS_MINIMIZE;
			}
			return wasVisible ? TRUE : FALSE;
		}
		case SW_MAXIMIZE:
		{
			if (window->isTopLevel())
			{
				window->_style |= WS_MAXIMIZE;
				window->_style &= ~WS_MINIMIZE;
				if (!wasVisible)
				{
					window->send(WM_SHOWWINDOW, TRUE, 0);
					window->_visible = true;
					window->_style |= WS_VISIBLE;
					nppmac::ui::setVisible(window->_view, true, true);
				}
				nppmac::ui::maximize(window->_view);
				syncTopLevelGeometry(window);
				sendSizeAndMove(window);
			}
			return wasVisible ? TRUE : FALSE;
		}
		case SW_RESTORE:
		{
			if (window->isTopLevel())
			{
				nppmac::ui::restore(window->_view);
				window->_style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
			}
			[[fallthrough]];
		}
		default:
		{
			// SW_SHOW, SW_SHOWNORMAL, SW_SHOWNA, SW_SHOWNOACTIVATE, SW_SHOWDEFAULT, SW_RESTORE.
			if (!wasVisible)
			{
				window->send(WM_SHOWWINDOW, TRUE, 0);
				if (!fromHandle(hwnd))
					return FALSE;
				window->_visible = true;
				window->_style |= WS_VISIBLE;
				const bool activate = window->isTopLevel() && command != SW_SHOWNA && command != SW_SHOWNOACTIVATE;
				nppmac::ui::setVisible(window->_view, true, activate);
				if (!window->_nativeControl)
					invalidate(window, nullptr, true);
				if (activate)
				{
					syncTopLevelGeometry(window);
					setActiveWindow(window);
				}
			}
			return wasVisible ? TRUE : FALSE;
		}
	}
}

BOOL WINAPI ShowWindowAsync(HWND hwnd, int command)
{
	return ShowWindow(hwnd, command);
}

BOOL WINAPI IsWindowVisible(HWND hwnd)
{
	for (Window* window = fromHandle(hwnd); window; window = window->_parent)
	{
		if (!window->_visible)
			return FALSE;
	}
	return fromHandle(hwnd) ? TRUE : FALSE;
}

BOOL WINAPI IsWindowEnabled(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	return window && !(window->_style & WS_DISABLED) ? TRUE : FALSE;
}

BOOL WINAPI EnableWindow(HWND hwnd, BOOL enable)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	const BOOL wasDisabled = (window->_style & WS_DISABLED) ? TRUE : FALSE;
	if (enable && wasDisabled)
	{
		window->_style &= ~WS_DISABLED;
		nppmac::ui::setEnabled(window->_view, true);
		window->send(WM_ENABLE, TRUE, 0);
	}
	else if (!enable && !wasDisabled)
	{
		window->send(WM_CANCELMODE, 0, 0);
		window->_style |= WS_DISABLED;
		nppmac::ui::setEnabled(window->_view, false);
		if (focusWindow() == window)
			SetFocus(nullptr);
		window->send(WM_ENABLE, FALSE, 0);
	}
	return wasDisabled;
}

BOOL WINAPI IsIconic(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	return window && window->isTopLevel() && window->_view && nppmac::ui::isMinimized(window->_view) ? TRUE : FALSE;
}

BOOL WINAPI IsZoomed(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	return window && window->isTopLevel() && window->_view && nppmac::ui::isMaximized(window->_view) ? TRUE : FALSE;
}

BOOL WINAPI GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT* placement)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	placement->length = sizeof(WINDOWPLACEMENT);
	placement->flags = 0;
	placement->showCmd = IsIconic(hwnd) ? SW_SHOWMINIMIZED : (IsZoomed(hwnd) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
	placement->ptMinPosition = {-1, -1};
	placement->ptMaxPosition = {-1, -1};
	placement->rcNormalPosition = IsZoomed(hwnd) && window->_normalRect.right > window->_normalRect.left ? window->_normalRect : window->_rect;
	return TRUE;
}

BOOL WINAPI SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT* placement)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	window->_normalRect = placement->rcNormalPosition;
	const RECT& r = placement->rcNormalPosition;
	const UINT command = placement->showCmd;
	const bool maximize = command == SW_SHOWMAXIMIZED || command == SW_MAXIMIZE;
	const bool minimize = command == SW_SHOWMINIMIZED || command == SW_MINIMIZE || command == SW_SHOWMINNOACTIVE;
	// As on Windows, the window is placed and shown (or hidden) in one step: it is already visible
	// when WM_SIZE arrives, so a layout that measures its visible children (Notepad++ counts its
	// toolbar and status bar only when visible) sees them.
	UINT flags = SWP_NOZORDER;
	if (command == SW_HIDE)
		flags |= SWP_HIDEWINDOW;
	else if (!maximize && !minimize)
		flags |= SWP_SHOWWINDOW;
	if (command == SW_SHOWNA || command == SW_SHOWNOACTIVATE || command == SW_HIDE)
		flags |= SWP_NOACTIVATE;
	SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
	if (maximize)
		ShowWindow(hwnd, SW_MAXIMIZE);
	else if (minimize)
		ShowWindow(hwnd, SW_MINIMIZE);
	return TRUE;
}

// --- Position and size ----------------------------------------------------------------------------

BOOL WINAPI SetWindowPos(HWND hwnd, HWND insertAfter, int x, int y, int width, int height, UINT flags)
{
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return FALSE;
	}

	WINDOWPOS position{};
	position.hwnd = hwnd;
	position.hwndInsertAfter = insertAfter;
	position.x = (flags & SWP_NOMOVE) ? window->_rect.left : x;
	position.y = (flags & SWP_NOMOVE) ? window->_rect.top : y;
	position.cx = (flags & SWP_NOSIZE) ? window->_rect.right - window->_rect.left : std::max(0, width);
	position.cy = (flags & SWP_NOSIZE) ? window->_rect.bottom - window->_rect.top : std::max(0, height);
	position.flags = flags;
	window->send(WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&position));
	if (!fromHandle(hwnd))
		return FALSE;

	const RECT oldRect = window->_rect;
	const int oldClientWidth = window->_clientWidth;
	const int oldClientHeight = window->_clientHeight;
	const RECT newRect{position.x, position.y, position.x + position.cx, position.y + position.cy};
	const bool moved = newRect.left != oldRect.left || newRect.top != oldRect.top;
	const bool resized = (newRect.right - newRect.left) != (oldRect.right - oldRect.left) || (newRect.bottom - newRect.top) != (oldRect.bottom - oldRect.top);
	if (moved || resized || (flags & SWP_FRAMECHANGED))
		applyGeometry(window, newRect);

	if (!(position.flags & SWP_NOZORDER) && window->_view)
	{
		std::vector<Window*>& siblings = window->_parent ? window->_parent->_children : topLevels();
		siblings.erase(std::remove(siblings.begin(), siblings.end(), window), siblings.end());
		if (insertAfter == HWND_BOTTOM)
		{
			siblings.push_back(window);
			nppmac::ui::orderView(window->_view, false);
		}
		else if (insertAfter == HWND_TOP || insertAfter == HWND_TOPMOST || insertAfter == HWND_NOTOPMOST || !fromHandle(insertAfter))
		{
			siblings.insert(siblings.begin(), window);
			nppmac::ui::orderView(window->_view, true);
		}
		else
		{
			Window* after = fromHandle(insertAfter);
			auto it = std::find(siblings.begin(), siblings.end(), after);
			siblings.insert(it == siblings.end() ? siblings.end() : it + 1, window);
			if (after->_view)
				nppmac::ui::orderView(window->_view, false, after->_view);
		}
	}

	if (flags & SWP_SHOWWINDOW)
		ShowWindow(hwnd, (flags & SWP_NOACTIVATE) ? SW_SHOWNA : SW_SHOW);
	else if (flags & SWP_HIDEWINDOW)
		ShowWindow(hwnd, SW_HIDE);
	if (!fromHandle(hwnd))
		return TRUE;

	position.x = window->_rect.left;
	position.y = window->_rect.top;
	position.cx = window->_rect.right - window->_rect.left;
	position.cy = window->_rect.bottom - window->_rect.top;
	if (!moved)
		position.flags |= SWP_NOMOVE;
	if (oldClientWidth == window->_clientWidth && oldClientHeight == window->_clientHeight)
		position.flags |= SWP_NOSIZE;
	if (moved || resized || !(flags & (SWP_NOMOVE | SWP_NOSIZE)) || (flags & SWP_FRAMECHANGED) || (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)))
		window->send(WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&position));
	return TRUE;
}

BOOL WINAPI MoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint)
{
	const BOOL result = SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
	return result;
}

BOOL WINAPI BringWindowToTop(HWND hwnd)
{
	return SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

namespace
{
	struct DeferredPositions
	{
		std::vector<WINDOWPOS> _positions;
	};
}

HDWP WINAPI BeginDeferWindowPos(int)
{
	return reinterpret_cast<HDWP>(new DeferredPositions);
}

HDWP WINAPI DeferWindowPos(HDWP handle, HWND hwnd, HWND insertAfter, int x, int y, int width, int height, UINT flags)
{
	auto* deferred = reinterpret_cast<DeferredPositions*>(handle);
	if (!deferred)
		return nullptr;
	deferred->_positions.push_back({hwnd, insertAfter, x, y, width, height, flags});
	return handle;
}

BOOL WINAPI EndDeferWindowPos(HDWP handle)
{
	auto* deferred = reinterpret_cast<DeferredPositions*>(handle);
	if (!deferred)
		return FALSE;
	for (const WINDOWPOS& position : deferred->_positions)
		SetWindowPos(position.hwnd, position.hwndInsertAfter, position.x, position.y, position.cx, position.cy, position.flags);
	delete deferred;
	return TRUE;
}

BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT rect)
{
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return FALSE;
	}
	if (window->isTopLevel())
	{
		*rect = window->_rect;
	}
	else
	{
		const POINT origin = clientOriginOnScreen(window->_parent);
		*rect = window->_rect;
		OffsetRect(rect, origin.x, origin.y);
	}
	return TRUE;
}

BOOL WINAPI GetClientRect(HWND hwnd, LPRECT rect)
{
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return FALSE;
	}
	*rect = {0, 0, window->_clientWidth, window->_clientHeight};
	return TRUE;
}

BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT point)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	const POINT origin = clientOriginOnScreen(window);
	point->x += origin.x;
	point->y += origin.y;
	return TRUE;
}

BOOL WINAPI ScreenToClient(HWND hwnd, LPPOINT point)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	const POINT origin = clientOriginOnScreen(window);
	point->x -= origin.x;
	point->y -= origin.y;
	return TRUE;
}

int WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count)
{
	const POINT fromOrigin = from ? clientOriginOnScreen(fromHandle(from)) : POINT{0, 0};
	const POINT toOrigin = to ? clientOriginOnScreen(fromHandle(to)) : POINT{0, 0};
	const LONG dx = fromOrigin.x - toOrigin.x;
	const LONG dy = fromOrigin.y - toOrigin.y;
	for (UINT i = 0; i < count; ++i)
	{
		points[i].x += dx;
		points[i].y += dy;
	}
	return MAKELONG(static_cast<WORD>(dx), static_cast<WORD>(dy));
}

HWND WINAPI ChildWindowFromPointEx(HWND parentHandle, POINT point, UINT flags)
{
	Window* parent = fromHandle(parentHandle);
	if (!parent)
		return nullptr;
	if (point.x < 0 || point.y < 0 || point.x >= parent->_clientWidth || point.y >= parent->_clientHeight)
		return nullptr;
	for (Window* child : parent->_children)
	{
		if ((flags & CWP_SKIPINVISIBLE) && !child->_visible)
			continue;
		if ((flags & CWP_SKIPDISABLED) && (child->_style & WS_DISABLED))
			continue;
		if (PtInRect(&child->_rect, point))
			return child->_hwnd;
	}
	return parentHandle;
}

HWND WINAPI ChildWindowFromPoint(HWND parent, POINT point)
{
	return ChildWindowFromPointEx(parent, point, CWP_ALL);
}

HWND WINAPI WindowFromPoint(POINT point)
{
	// The deepest visible window under the point, starting from the frontmost top level.
	for (Window* top : topLevels())
	{
		if (!top->_visible || IsIconic(top->_hwnd) || !PtInRect(&top->_rect, point))
			continue;
		Window* found = top;
		for (;;)
		{
			POINT local = point;
			ScreenToClient(found->_hwnd, &local);
			HWND child = ChildWindowFromPointEx(found->_hwnd, local, CWP_SKIPINVISIBLE);
			if (!child || child == found->_hwnd)
				break;
			found = fromHandle(child);
		}
		return found->_hwnd;
	}
	return nullptr;
}

// --- Focus and activation -------------------------------------------------------------------------

HWND WINAPI GetFocus()
{
	return focusWindow() ? focusWindow()->_hwnd : nullptr;
}

HWND WINAPI SetFocus(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	Window* previous = focusWindow();
	HWND previousHandle = previous ? previous->_hwnd : nullptr;
	if (hwnd && !window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return nullptr;
	}
	if (window == previous)
		return previousHandle;

	// Focusing a window in another top level activates that top level first.
	if (window)
	{
		Window* root = fromHandle(GetAncestor(hwnd, GA_ROOT));
		if (root != activeWindow() && root->_visible)
		{
			nppmac::ui::makeKeyAndFocus(root->_view);
		}
	}

	if (previous)
		previous->send(WM_KILLFOCUS, reinterpret_cast<WPARAM>(hwnd), 0);
	// The focus may have moved during WM_KILLFOCUS.
	if (window && fromHandle(hwnd))
	{
		setFocusWindow(window);
		if (window->_view)
			nppmac::ui::focusNativeView(window->_view);
		window->send(WM_SETFOCUS, reinterpret_cast<WPARAM>(previousHandle), 0);
	}
	else
	{
		setFocusWindow(nullptr);
	}
	return previousHandle;
}

HWND WINAPI GetActiveWindow()
{
	return activeWindow() ? activeWindow()->_hwnd : nullptr;
}

HWND WINAPI SetActiveWindow(HWND hwnd)
{
	HWND previous = GetActiveWindow();
	Window* window = fromHandle(hwnd);
	if (window && window->isTopLevel())
	{
		nppmac::ui::makeKeyAndFocus(window->_view);
		setActiveWindow(window);
	}
	return previous;
}

HWND WINAPI GetForegroundWindow()
{
	return GetActiveWindow();
}

BOOL WINAPI SetForegroundWindow(HWND hwnd)
{
	Window* window = fromHandle(GetAncestor(hwnd, GA_ROOT));
	if (!window)
		return FALSE;
	SetActiveWindow(window->_hwnd);
	return TRUE;
}

HWND WINAPI GetLastActivePopup(HWND hwnd)
{
	return hwnd;
}

BOOL WINAPI FlashWindowEx(PFLASHWINFO)
{
	return TRUE;
}

BOOL WINAPI LockWindowUpdate(HWND)
{
	return TRUE;
}

void WINAPI DragAcceptFiles(HWND hwnd, BOOL accept)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return;
	window->_acceptsFiles = accept != FALSE;
	nppmac::ui::setAcceptsFiles(window->_view, window->_acceptsFiles);
}
