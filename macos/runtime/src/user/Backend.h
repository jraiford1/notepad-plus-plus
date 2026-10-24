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

// The native side of windows: what the window manager (Win32 side, <windows.h>) asks AppKit to do,
// and what AppKit tells the window manager. Plain C++ types only, so that both sides can include it.
//
// Coordinates are in points with the origin at the top left: of the parent's client area for child
// views, of the primary screen for top-level windows (as Windows screen coordinates).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nppmac::ui
{
	struct Rect
	{
		int _left = 0;
		int _top = 0;
		int _right = 0;
		int _bottom = 0;

		int width() const {
			return _right - _left;
		}

		int height() const {
			return _bottom - _top;
		}
	};

	struct Point
	{
		int _x = 0;
		int _y = 0;
	};

	enum Modifier : unsigned
	{
		modifierShift = 1,
		modifierControl = 2, // the Command key and the Control key both act as Ctrl
		modifierAlt = 4,     // the Option key
		modifierCommand = 8, // set in addition to modifierControl when it came from Command
	};

	enum class MouseButton
	{
		none,
		left,
		right,
		middle,
	};

	enum class MouseAction
	{
		move,
		down,
		up,
		doubleClick,
		wheel,
		horizontalWheel,
		leave,
	};

	// Implemented by the window manager for each window; called by the native side.
	// What the user did on a standard scroll bar.
	enum class ScrollPart
	{
		lineUp,
		lineDown,
		pageUp,
		pageDown,
		thumbTrack,
		thumbPosition,
		end,
	};

	// A standard scroll bar (WS_VSCROLL / WS_HSCROLL): value is where the thumb is (0 to 1) and
	// proportion how much of the content shows (0 to 1).
	struct ScrollBarState
	{
		bool _visible = false;
		bool _enabled = true;
		double _value = 0;
		double _proportion = 1;
	};

	class ViewOwner
	{
	public:
		virtual ~ViewOwner() = default;

		// The view needs its pixels for the given rectangle (client coordinates). For a custom-drawn
		// view the owner paints into the view's surface before this returns.
		virtual void nativePaint(const Rect& dirty) = 0;

		// A top-level window was moved or resized by the user or the system (screen coordinates).
		virtual void nativeFrameChanged(const Rect& windowRect, const Rect& clientRectOnScreen) = 0;

		virtual void nativeCloseRequested() = 0;
		virtual void nativeActivated(bool active) = 0;
		virtual void nativeMinimized(bool minimized) = 0;
		virtual void nativeFocusChanged(bool focused) = 0;

		// Mouse input in client coordinates; delta is the wheel movement in Windows units (120 per notch).
		virtual void nativeMouse(MouseAction action, MouseButton button, Point position, unsigned modifiers, int delta) = 0;

		// Files dropped on the window (client coordinates).
		virtual void nativeFilesDropped(const std::vector<std::string>& paths, Point position) = 0;

		// The view accepts dropped files.
		virtual bool nativeAcceptsFiles() const = 0;

		// The user worked a standard scroll bar; value is the thumb's place (0 to 1) when dragging.
		virtual void nativeScroll(bool vertical, ScrollPart part, double value) = 0;
	};

	// Creates the application object (an NSApplication subclass that routes keys to the window
	// manager) and its delegate. Called once at start-up, before anything else uses AppKit.
	void createApplication();

	// A native view (child) or window (top level), owned by the window manager's Window.
	class NativeView;

	enum class ViewKind
	{
		topLevel,     // an NSWindow; its content view is the client area
		popup,        // a borderless top-level NSWindow (tooltips, drop-downs, popup windows)
		child,        // a custom-drawn NSView with its own surface
	};

	struct TopLevelStyle
	{
		bool _titled = true;
		bool _closable = true;
		bool _resizable = true;
		bool _minimizable = true;
		bool _toolWindow = false;
		bool _topMost = false;
	};

	NativeView* createView(ViewOwner* owner, ViewKind kind, NativeView* parent, const Rect& frame, const TopLevelStyle& style);
	void destroyView(NativeView* view);

	// Child views: frame in the parent's client coordinates. Top levels: window frame on screen.
	void setFrame(NativeView* view, const Rect& frame);
	void setVisible(NativeView* view, bool visible, bool activate);
	void setTitle(NativeView* view, const std::u16string& title);
	void setEnabled(NativeView* view, bool enabled);
	// Moves a view under a new parent. A top-level window becoming a child gives up its NSWindow; a
	// child becoming top-level (no parent) gets one, with the frame (screen) and style given.
	void setParent(NativeView* view, NativeView* newParent, const Rect& frame, const TopLevelStyle& style, bool visible);
	// Moves the view above (front == true) or below its siblings, or right above another sibling.
	void orderView(NativeView* view, bool front, NativeView* above = nullptr);

	// Client area of a top-level window, in screen coordinates.
	Rect clientRectOnScreen(NativeView* view);
	// Window frame of a top-level window, in screen coordinates.
	Rect windowRectOnScreen(NativeView* view);
	// The window frame that gives a top-level window this client area.
	Rect windowRectForClient(const Rect& clientOnScreen, const TopLevelStyle& style);

	void invalidate(NativeView* view, const Rect* rect);
	void displayNow(NativeView* view);
	void makeKeyAndFocus(NativeView* view);
	bool isMinimized(NativeView* view);
	bool isMaximized(NativeView* view);
	void minimize(NativeView* view);
	void maximize(NativeView* view);
	void restore(NativeView* view);

	// The surface a custom-drawn view paints into: a CGContextRef with a top-left origin in points,
	// already scaled for the display. Valid until the view is resized or destroyed.
	void* surfaceContext(NativeView* view);

	// A see-through view (a group box, WS_EX_TRANSPARENT): what it does not paint shows the views
	// under it, and the mouse goes through it to them, as HTTRANSPARENT does on Windows. Its surface
	// is cleared before each paint.
	void setSeeThrough(NativeView* view, bool seeThrough);

	// Standard scroll bars, shown at the right and bottom edges of the view, over its content: the
	// window manager makes the client area smaller by their thickness, as Windows' non-client area.
	void setScrollBar(NativeView* view, bool vertical, const ScrollBarState& state);

	// Light or dark: the application's (menus, panels, alerts and every window that does not choose)
	// and a top-level window's (its title bar, scroll bars and native parts).
	enum class Appearance
	{
		system,
		light,
		dark,
	};
	void setApplicationAppearance(Appearance appearance);
	void setWindowAppearance(NativeView* view, Appearance appearance);
	int scrollBarThickness();
	void clearSurface(NativeView* view, const Rect& rect);

	// For built-in classes backed by a native control (Scintilla, buttons, edits...): the window
	// manager hands over an NSView created elsewhere (as an opaque pointer) to place in the hierarchy.
	NativeView* adoptView(ViewOwner* owner, NativeView* parent, void* nsView, const Rect& frame);
	void* nsView(NativeView* view);

	// Screen geometry, in Windows screen coordinates (origin at the top left of the main screen).
	Rect mainScreenRect();
	Rect mainScreenWorkArea();
	Point cursorPosition();
	double backingScale();

	// Drag and drop of files onto windows.
	void setAcceptsFiles(NativeView* view, bool accepts);

	// Mouse capture: while set, mouse events outside the view still go to it.
	void setCapture(NativeView* view);
	void releaseCapture();

	enum class CursorShape
	{
		arrow,
		iBeam,
		wait,
		cross,
		upArrow,
		sizeNWSE,
		sizeNESW,
		sizeWE,
		sizeNS,
		sizeAll,
		no,
		hand,
		appStarting,
		help,
	};
	void setCursor(CursorShape shape);
	void hideCursor(bool hide);

	// The event loop. waitForEvent blocks until a native event arrives or the timeout (ms, negative
	// for none) expires, and dispatches it; native events post Win32 messages through the owners.
	void runEventLoopOnce(int timeoutMilliseconds);
	// Wakes a thread blocked in runEventLoopOnce (a message was posted from another thread).
	void wakeEventLoop();

	// Keyboard input arrives at the application level, before any view: the window manager posts it
	// to the focus window's queue, so TranslateAccelerator sees every key as on Windows. The native
	// event is kept with the message so that a native control can still receive it (forwardKey).
	struct KeyEvent
	{
		bool _down = true;
		bool _repeat = false;
		unsigned short _keyCode = 0;   // macOS virtual key code
		unsigned _modifiers = 0;       // Modifier flags
		std::u16string _characters;    // text the key produces (with modifiers)
		std::u16string _charactersIgnoringModifiers;
		void* _nsEvent = nullptr;      // retained; released with releaseNativeEvent
	};

	// Implemented by the window manager.
	void windowManagerKeyEvent(const KeyEvent& event);
	// Debugging aid: prints the window tree (class, id, rectangle, visibility) to stderr.
	void windowManagerDumpTree();
	// Test aid: sends WM_COMMAND with the id to the active top-level window.
	void windowManagerTestCommand(int id);
	void windowManagerModifiersChanged(unsigned modifiers, unsigned short keyCode, bool down);
	// Text committed by an input method or a character picker, outside key events.
	void windowManagerInsertText(const std::u16string& text);

	void retainNativeEvent(void* nsEvent);
	void releaseNativeEvent(void* nsEvent);
	// Hands a key event to a native control's view (keyDown: / keyUp:).
	void forwardKey(NativeView* view, void* nsEvent);
	// Gives keyboard focus to a native view (first responder) without activating its window.
	void focusNativeView(NativeView* view);

	// Timers fire on the main run loop, also during AppKit's own modal loops (menus, live resize).
	using TimerCallback = void (*)(void* context, uintptr_t id);
	void* startTimer(unsigned milliseconds, TimerCallback callback, void* context, uintptr_t id);
	void stopTimer(void* timer);
}
