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

// user32 messages: the message queue and loop, sent and posted messages (across threads too),
// DefWindowProc, timers, hooks, window subclassing (comctl32), painting entry points, and the
// conversion of native input to Win32 input messages.

#include "Window.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <unordered_map>

namespace nppmac::user
{
	namespace
	{
		using KeyboardState = std::array<BYTE, 256>;

		// What a keyboard or mouse message brings with it: the key state after the input, and a key
		// down's characters. They take effect when the message is retrieved, as on Windows, where
		// GetKeyState follows the messages the program has read and not the keyboard itself.
		struct InputState
		{
			KeyboardState _keys{};
			std::u16string _characters;
			bool _toFocus = false; // keyboard input: for the window that has the focus when it is read
		};

		struct QueuedMessage
		{
			MSG _msg{};
			void* _nativeEvent = nullptr; // retained native key event, released when processed
			std::shared_ptr<InputState> _input;
		};

		KeyboardState g_asyncKeyState{}; // the keyboard as it is now
		KeyboardState g_keyState{};      // as of the last input message retrieved
		unsigned g_modifiers = 0;
		std::unordered_map<BYTE, std::u16string> g_pendingCharacters; // characters of the key downs retrieved

		void applyInputState(const MSG& msg, const InputState& input)
		{
			g_keyState = input._keys;
			if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)
				g_pendingCharacters[static_cast<BYTE>(msg.wParam)] = input._characters;
		}

		// A message sent from another thread, waiting for the GUI thread to process it.
		struct CrossThreadSend
		{
			HWND _hwnd = nullptr;
			UINT _message = 0;
			WPARAM _wParam = 0;
			LPARAM _lParam = 0;
			LRESULT _result = 0;
			bool _done = false;
		};

		std::mutex g_queueMutex;
		std::condition_variable g_sendDone;

		std::deque<QueuedMessage>& queue()
		{
			static auto* messages = new std::deque<QueuedMessage>;
			return *messages;
		}

		std::deque<CrossThreadSend*>& crossThreadSends()
		{
			static auto* sends = new std::deque<CrossThreadSend*>;
			return *sends;
		}

		bool g_quitPosted = false;
		int g_quitCode = 0;
		pthread_t g_guiThread = pthread_self();
		void* g_currentNativeEvent = nullptr;
		DWORD g_messageTime = 0;
		POINT g_messagePosition{};

		bool isGuiThread()
		{
			return pthread_equal(pthread_self(), g_guiThread) != 0;
		}

		// --- Hooks --------------------------------------------------------------------------------

		struct Hook
		{
			int _type = 0;
			HOOKPROC _proc = nullptr;
		};

		std::vector<Hook*>& hooks()
		{
			static auto* list = new std::vector<Hook*>;
			return *list;
		}

		void callGetMessageHooks(MSG* msg, bool removed)
		{
			const std::vector<Hook*> list = hooks();
			for (Hook* hook : list)
			{
				if (hook->_type == WH_GETMESSAGE)
					hook->_proc(HC_ACTION, removed ? PM_REMOVE : PM_NOREMOVE, reinterpret_cast<LPARAM>(msg));
			}
		}

		// --- Timers -------------------------------------------------------------------------------

		struct Timer
		{
			HWND _hwnd = nullptr;
			UINT_PTR _id = 0;
			TIMERPROC _proc = nullptr;
			void* _native = nullptr;
			bool _pending = false; // a WM_TIMER for it is in the queue (Windows coalesces them)
		};

		std::vector<Timer*>& timers()
		{
			static auto* list = new std::vector<Timer*>;
			return *list;
		}

		UINT_PTR g_nextTimerId = 0x7FFF0000;

		void timerFired(void* context, uintptr_t)
		{
			auto* timer = static_cast<Timer*>(context);
			if (timer->_pending)
				return;
			timer->_pending = true;
			Window* window = fromHandle(timer->_hwnd);
			MSG msg{};
			msg.hwnd = timer->_hwnd;
			msg.message = WM_TIMER;
			msg.wParam = timer->_id;
			msg.lParam = reinterpret_cast<LPARAM>(timer->_proc);
			msg.time = GetTickCount();
			{
				std::lock_guard lock{g_queueMutex};
				queue().push_back({msg, nullptr});
			}
			(void)window;
		}

		Timer* findTimer(HWND hwnd, UINT_PTR id)
		{
			for (Timer* timer : timers())
			{
				if (timer->_hwnd == hwnd && timer->_id == id)
					return timer;
			}
			return nullptr;
		}

		// --- Registered messages ------------------------------------------------------------------

		std::unordered_map<std::u16string, UINT>& registeredMessages()
		{
			static auto* map = new std::unordered_map<std::u16string, UINT>;
			return *map;
		}

		// --- Message retrieval --------------------------------------------------------------------

		void processCrossThreadSends()
		{
			for (;;)
			{
				CrossThreadSend* send = nullptr;
				{
					std::lock_guard lock{g_queueMutex};
					if (crossThreadSends().empty())
						return;
					send = crossThreadSends().front();
					crossThreadSends().pop_front();
				}
				Window* window = fromHandle(send->_hwnd);
				const LRESULT result = window ? callWindowProc(window, send->_message, send->_wParam, send->_lParam) : 0;
				{
					std::lock_guard lock{g_queueMutex};
					send->_result = result;
					send->_done = true;
				}
				g_sendDone.notify_all();
			}
		}

		bool matches(const MSG& msg, HWND hwnd, UINT filterMin, UINT filterMax)
		{
			if (hwnd && msg.hwnd != hwnd && !IsChild(hwnd, msg.hwnd))
				return false;
			if (filterMin == 0 && filterMax == 0)
				return true;
			return msg.message >= filterMin && msg.message <= filterMax;
		}

		// Takes the first matching queued message; returns false if there is none.
		bool takeMessage(MSG* msg, HWND hwnd, UINT filterMin, UINT filterMax, bool remove, void** nativeEvent)
		{
			std::lock_guard lock{g_queueMutex};
			auto& messages = queue();
			for (auto it = messages.begin(); it != messages.end(); ++it)
			{
				// As on Windows, keyboard input goes to the window that has the focus when it is read,
				// so keys typed ahead of a dialog reach the dialog.
				if (it->_input && it->_input->_toFocus)
				{
					Window* focus = focusWindow() ? focusWindow() : activeWindow();
					it->_msg.hwnd = focus ? focus->_hwnd : nullptr;
				}
				if (!matches(it->_msg, hwnd, filterMin, filterMax))
					continue;
				*msg = it->_msg;
				if (nativeEvent)
					*nativeEvent = it->_nativeEvent;
				if (remove)
				{
					if (it->_input)
						applyInputState(it->_msg, *it->_input);
					if (it->_msg.message == WM_TIMER)
					{
						for (Timer* timer : timers())
						{
							if (timer->_hwnd == it->_msg.hwnd && timer->_id == it->_msg.wParam)
								timer->_pending = false;
						}
					}
					messages.erase(it);
				}
				return true;
			}
			return false;
		}

		void setCurrentNativeEvent(void* nativeEvent)
		{
			if (g_currentNativeEvent)
				ui::releaseNativeEvent(g_currentNativeEvent);
			g_currentNativeEvent = nativeEvent;
		}

		bool quitPending(MSG* msg)
		{
			if (!g_quitPosted)
				return false;
			// WM_QUIT is only returned once the queue holds nothing else for the caller.
			std::lock_guard lock{g_queueMutex};
			if (!queue().empty())
				return false;
			msg->hwnd = nullptr;
			msg->message = WM_QUIT;
			msg->wParam = static_cast<WPARAM>(g_quitCode);
			msg->lParam = 0;
			return true;
		}

		bool peek(MSG* msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT flags)
		{
			processCrossThreadSends();
			void* nativeEvent = nullptr;
			const bool remove = (flags & PM_REMOVE) != 0;
			if (takeMessage(msg, hwnd, filterMin, filterMax, remove, &nativeEvent))
			{
				if (remove)
					setCurrentNativeEvent(nativeEvent);
				g_messageTime = msg->time;
				g_messagePosition = msg->pt;
				callGetMessageHooks(msg, remove);
				return true;
			}
			if ((filterMin == 0 && filterMax == 0) || (WM_QUIT >= filterMin && WM_QUIT <= filterMax))
			{
				if (quitPending(msg))
				{
					if (remove)
						g_quitPosted = false;
					return true;
				}
			}
			return false;
		}

		// --- Keyboard mapping ---------------------------------------------------------------------

		// macOS virtual key code -> Windows virtual key, for keys that do not depend on the layout.
		BYTE virtualKeyFor(unsigned short keyCode, const std::u16string& charactersIgnoringModifiers)
		{
			switch (keyCode)
			{
				case 0x24: return VK_RETURN;
				case 0x4C: return VK_RETURN;   // keypad Enter
				case 0x30: return VK_TAB;
				case 0x31: return VK_SPACE;
				case 0x33: return VK_BACK;
				case 0x35: return VK_ESCAPE;
				case 0x75: return VK_DELETE;
				case 0x73: return VK_HOME;
				case 0x77: return VK_END;
				case 0x74: return VK_PRIOR;
				case 0x79: return VK_NEXT;
				case 0x7B: return VK_LEFT;
				case 0x7C: return VK_RIGHT;
				case 0x7D: return VK_DOWN;
				case 0x7E: return VK_UP;
				case 0x72: return VK_INSERT;   // Help key on old keyboards
				case 0x7A: return VK_F1;
				case 0x78: return VK_F2;
				case 0x63: return VK_F3;
				case 0x76: return VK_F4;
				case 0x60: return VK_F5;
				case 0x61: return VK_F6;
				case 0x62: return VK_F7;
				case 0x64: return VK_F8;
				case 0x65: return VK_F9;
				case 0x6D: return VK_F10;
				case 0x67: return VK_F11;
				case 0x6F: return VK_F12;
				case 0x69: return VK_F13;
				case 0x6B: return VK_F14;
				case 0x71: return VK_F15;
				case 0x6A: return VK_F16;
				case 0x40: return VK_F17;
				case 0x4F: return VK_F18;
				case 0x50: return VK_F19;
				case 0x5A: return VK_F20;
				case 0x52: return VK_NUMPAD0;
				case 0x53: return VK_NUMPAD1;
				case 0x54: return VK_NUMPAD2;
				case 0x55: return VK_NUMPAD3;
				case 0x56: return VK_NUMPAD4;
				case 0x57: return VK_NUMPAD5;
				case 0x58: return VK_NUMPAD6;
				case 0x59: return VK_NUMPAD7;
				case 0x5B: return VK_NUMPAD8;
				case 0x5C: return VK_NUMPAD9;
				case 0x41: return VK_DECIMAL;
				case 0x43: return VK_MULTIPLY;
				case 0x45: return VK_ADD;
				case 0x4E: return VK_SUBTRACT;
				case 0x4B: return VK_DIVIDE;
				case 0x47: return VK_CLEAR;
				case 0x38: return VK_SHIFT;
				case 0x3C: return VK_SHIFT;
				case 0x3B: return VK_CONTROL;
				case 0x3E: return VK_CONTROL;
				case 0x37: return VK_CONTROL;  // Command acts as Ctrl
				case 0x36: return VK_CONTROL;
				case 0x3A: return VK_MENU;     // Option acts as Alt
				case 0x3D: return VK_MENU;
				case 0x39: return VK_CAPITAL;
				default:
					break;
			}

			// Letters and digits follow the layout, as Windows virtual keys do.
			if (!charactersIgnoringModifiers.empty())
			{
				const char16_t ch = charactersIgnoringModifiers[0];
				if (ch >= u'a' && ch <= u'z')
					return static_cast<BYTE>('A' + (ch - u'a'));
				if (ch >= u'A' && ch <= u'Z')
					return static_cast<BYTE>(ch);
				if (ch >= u'0' && ch <= u'9')
					return static_cast<BYTE>(ch);
			}

			// Punctuation, by position (US layout), as the OEM keys are.
			switch (keyCode)
			{
				case 0x29: return VK_OEM_1;      // ;
				case 0x18: return VK_OEM_PLUS;   // =
				case 0x2B: return VK_OEM_COMMA;  // ,
				case 0x1B: return VK_OEM_MINUS;  // -
				case 0x2F: return VK_OEM_PERIOD; // .
				case 0x2C: return VK_OEM_2;      // /
				case 0x32: return VK_OEM_3;      // `
				case 0x21: return VK_OEM_4;      // [
				case 0x2A: return VK_OEM_5;      // backslash
				case 0x1E: return VK_OEM_6;      // ]
				case 0x27: return VK_OEM_7;      // '
				case 0x0A: return VK_OEM_102;    // section key on ISO keyboards
				default:
					break;
			}
			// Letters and digits by position, when the layout produced something else (Option held).
			static const std::pair<unsigned short, char> positions[] = {
				{0x00, 'A'}, {0x0B, 'B'}, {0x08, 'C'}, {0x02, 'D'}, {0x0E, 'E'}, {0x03, 'F'}, {0x05, 'G'}, {0x04, 'H'},
				{0x22, 'I'}, {0x26, 'J'}, {0x28, 'K'}, {0x25, 'L'}, {0x2E, 'M'}, {0x2D, 'N'}, {0x1F, 'O'}, {0x23, 'P'},
				{0x0C, 'Q'}, {0x0F, 'R'}, {0x01, 'S'}, {0x11, 'T'}, {0x20, 'U'}, {0x09, 'V'}, {0x0D, 'W'}, {0x07, 'X'},
				{0x10, 'Y'}, {0x06, 'Z'}, {0x1D, '0'}, {0x12, '1'}, {0x13, '2'}, {0x14, '3'}, {0x15, '4'}, {0x17, '5'},
				{0x16, '6'}, {0x1A, '7'}, {0x1C, '8'}, {0x19, '9'},
			};
			for (const auto& [code, key] : positions)
			{
				if (code == keyCode)
					return static_cast<BYTE>(key);
			}
			return 0;
		}

	}

	// --- Messages ---------------------------------------------------------------------------------

	LRESULT callSubclass(Window* window, size_t depth, UINT message, WPARAM wParam, LPARAM lParam);

	LRESULT callWindowProc(Window* window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!window)
			return 0;
		if (!window->_subclasses.empty())
			return callSubclass(window, 0, message, wParam, lParam);
		return window->_windowProc ? window->_windowProc(window->_hwnd, message, wParam, lParam) : 0;
	}

	// Keyboard input, addressed when it is read (see takeMessage).
	void postKeyboardInput(UINT message, WPARAM wParam, LPARAM lParam, void* nativeEvent = nullptr, const std::u16string& characters = {})
	{
		MSG msg{};
		msg.message = message;
		msg.wParam = wParam;
		msg.lParam = lParam;
		msg.time = GetTickCount();
		const ui::Point cursor = ui::cursorPosition();
		msg.pt = {cursor._x, cursor._y};
		auto input = std::make_shared<InputState>(InputState{g_asyncKeyState, characters, true});
		{
			std::lock_guard lock{g_queueMutex};
			queue().push_back({msg, nativeEvent, input});
		}
	}

	void postMessage(Window* window, UINT message, WPARAM wParam, LPARAM lParam, void* nativeEvent, const std::u16string& characters)
	{
		MSG msg{};
		msg.hwnd = window ? window->_hwnd : nullptr;
		msg.message = message;
		msg.wParam = wParam;
		msg.lParam = lParam;
		msg.time = GetTickCount();
		const ui::Point cursor = ui::cursorPosition();
		msg.pt = {cursor._x, cursor._y};
		std::shared_ptr<InputState> input;
		if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST))
			input = std::make_shared<InputState>(InputState{g_asyncKeyState, characters});
		{
			std::lock_guard lock{g_queueMutex};
			queue().push_back({msg, nativeEvent, input});
		}
		if (!isGuiThread())
			ui::wakeEventLoop();
	}

	void* currentNativeEvent()
	{
		return g_currentNativeEvent;
	}

	DWORD messageTime()
	{
		return g_messageTime;
	}

	POINT messagePosition()
	{
		return g_messagePosition;
	}

	void killWindowTimers(Window* window)
	{
		auto& list = timers();
		for (auto it = list.begin(); it != list.end();)
		{
			if ((*it)->_hwnd == window->_hwnd)
			{
				ui::stopTimer((*it)->_native);
				delete *it;
				it = list.erase(it);
			}
			else
			{
				++it;
			}
		}
		std::lock_guard lock{g_queueMutex};
		auto& messages = queue();
		messages.erase(std::remove_if(messages.begin(), messages.end(), [window](const QueuedMessage& queued)
		{
			if (queued._msg.hwnd == window->_hwnd && queued._nativeEvent)
				ui::releaseNativeEvent(queued._nativeEvent);
			return queued._msg.hwnd == window->_hwnd;
		}), messages.end());
	}

	// --- Painting ---------------------------------------------------------------------------------

	void invalidate(Window* window, const RECT* rect, bool erase)
	{
		if (!window || window->_nativeControl)
		{
			if (window && window->_view)
				ui::invalidate(window->_view, nullptr);
			return;
		}
		RECT area{0, 0, window->_clientWidth, window->_clientHeight};
		if (rect)
			IntersectRect(&area, &area, rect);
		if (IsRectEmpty(&area))
			return;
		UnionRect(&window->_invalid, &window->_invalid, &area);
		window->_eraseBackground = window->_eraseBackground || erase;
		const ui::Rect native{area.left, area.top, area.right, area.bottom};
		ui::invalidate(window->_view, &native);
	}

	void validate(Window* window, const RECT* rect)
	{
		if (!window)
			return;
		if (!rect)
		{
			SetRectEmpty(&window->_invalid);
			window->_eraseBackground = false;
			return;
		}
		// The update region is kept as a bounding box: validating part of it keeps the box unless
		// the part covers it.
		RECT remaining{};
		SubtractRect(&remaining, &window->_invalid, rect);
		window->_invalid = remaining;
		if (IsRectEmpty(&window->_invalid))
			window->_eraseBackground = false;
	}

	void Window::nativePaint(const ui::Rect& dirty)
	{
		if (_nativeControl || _painting)
			return;
		RECT area{dirty._left, dirty._top, dirty._right, dirty._bottom};
		// A part AppKit asks for that was not invalidated (exposure) only needs the surface, which
		// keeps its pixels; pending invalid areas are painted now.
		if (IsRectEmpty(&_invalid))
			return;
		(void)area;
		if (_seeThrough)
			ui::clearSurface(_view, {_invalid.left, _invalid.top, _invalid.right, _invalid.bottom});
		HWND hwnd = _hwnd;
		static const bool trace = std::getenv("NPPMAC_TRACE_PAINT") != nullptr;
		if (trace)
			std::fprintf(stderr, "nppmac: WM_PAINT %s (%ld,%ld,%ld,%ld)%s\n", toUtf8(_class->_name).c_str(), static_cast<long>(_invalid.left),
				static_cast<long>(_invalid.top), static_cast<long>(_invalid.right), static_cast<long>(_invalid.bottom), _eraseBackground ? " erase" : "");
		send(WM_PAINT, 0, 0);
		// A window procedure that does not paint (no BeginPaint) leaves the region invalid;
		// validate it so it is not asked again forever.
		if (Window* window = fromHandle(hwnd))
			validate(window, nullptr);
	}

	// --- Input state ------------------------------------------------------------------------------

	void setKeyState(BYTE virtualKey, bool down)
	{
		if (down)
		{
			if (!(g_asyncKeyState[virtualKey] & 0x80))
				g_asyncKeyState[virtualKey] ^= 0x01; // toggle state, as for Caps Lock
			g_asyncKeyState[virtualKey] |= 0x80;
		}
		else
		{
			g_asyncKeyState[virtualKey] &= static_cast<BYTE>(~0x80);
		}
	}

	void setModifierState(unsigned modifiers)
	{
		g_modifiers = modifiers;
		auto set = [](BYTE key, BYTE left, BYTE right, bool down)
		{
			setKeyState(key, down);
			setKeyState(left, down);
			setKeyState(right, down);
		};
		set(VK_SHIFT, VK_LSHIFT, VK_RSHIFT, (modifiers & ui::modifierShift) != 0);
		set(VK_CONTROL, VK_LCONTROL, VK_RCONTROL, (modifiers & ui::modifierControl) != 0);
		set(VK_MENU, VK_LMENU, VK_RMENU, (modifiers & ui::modifierAlt) != 0);
	}

	unsigned modifierFlagsForMessage()
	{
		unsigned flags = 0;
		if (g_modifiers & ui::modifierShift)
			flags |= MK_SHIFT;
		if (g_modifiers & ui::modifierControl)
			flags |= MK_CONTROL;
		if (g_asyncKeyState[VK_LBUTTON] & 0x80)
			flags |= MK_LBUTTON;
		if (g_asyncKeyState[VK_RBUTTON] & 0x80)
			flags |= MK_RBUTTON;
		if (g_asyncKeyState[VK_MBUTTON] & 0x80)
			flags |= MK_MBUTTON;
		return flags;
	}

	void Window::nativeMouse(ui::MouseAction action, ui::MouseButton button, ui::Point position, unsigned modifiers, int delta)
	{
		setModifierState(modifiers);
		const BYTE buttonKey = button == ui::MouseButton::left ? VK_LBUTTON : (button == ui::MouseButton::right ? VK_RBUTTON : VK_MBUTTON);
		if (action == ui::MouseAction::down || action == ui::MouseAction::doubleClick)
			setKeyState(buttonKey, true);
		else if (action == ui::MouseAction::up)
			setKeyState(buttonKey, false);

		// With a capture, mouse input goes to the capturing window in its own coordinates.
		Window* target = this;
		if (HWND capture = GetCapture())
		{
			if (capture != _hwnd && fromHandle(capture))
			{
				POINT p{position._x, position._y};
				ClientToScreen(_hwnd, &p);
				ScreenToClient(capture, &p);
				position = {p.x, p.y};
				target = fromHandle(capture);
			}
		}
		else
		{
			// A window that answers WM_NCHITTEST with HTTRANSPARENT (a plain label) passes the mouse
			// on to its parent.
			while (target->isChild() && target->_parent)
			{
				POINT screen{position._x, position._y};
				ClientToScreen(target->_hwnd, &screen);
				if (target->send(WM_NCHITTEST, 0, MAKELPARAM(static_cast<short>(screen.x), static_cast<short>(screen.y))) != HTTRANSPARENT)
					break;
				Window* parent = target->_parent;
				ScreenToClient(parent->_hwnd, &screen);
				position = {screen.x, screen.y};
				target = parent;
			}
		}

		const LPARAM coordinates = MAKELPARAM(static_cast<short>(position._x), static_cast<short>(position._y));
		const WPARAM flags = modifierFlagsForMessage();
		switch (action)
		{
			case ui::MouseAction::move:
				postMessage(target, WM_MOUSEMOVE, flags, coordinates);
				break;
			case ui::MouseAction::down:
				postMessage(target, button == ui::MouseButton::left ? WM_LBUTTONDOWN : (button == ui::MouseButton::right ? WM_RBUTTONDOWN : WM_MBUTTONDOWN), flags, coordinates);
				break;
			case ui::MouseAction::doubleClick:
			{
				// Only classes with CS_DBLCLKS get double clicks; the others get a second down.
				const bool doubleClicks = (target->_class->_style & CS_DBLCLKS) != 0;
				const UINT downMessage = button == ui::MouseButton::left ? WM_LBUTTONDOWN : (button == ui::MouseButton::right ? WM_RBUTTONDOWN : WM_MBUTTONDOWN);
				const UINT doubleMessage = button == ui::MouseButton::left ? WM_LBUTTONDBLCLK : (button == ui::MouseButton::right ? WM_RBUTTONDBLCLK : WM_MBUTTONDBLCLK);
				postMessage(target, doubleClicks ? doubleMessage : downMessage, flags, coordinates);
				break;
			}
			case ui::MouseAction::up:
			{
				postMessage(target, button == ui::MouseButton::left ? WM_LBUTTONUP : (button == ui::MouseButton::right ? WM_RBUTTONUP : WM_MBUTTONUP), flags, coordinates);
				// A right click also asks for a context menu, after the button comes up.
				if (button == ui::MouseButton::right)
				{
					POINT screen{position._x, position._y};
					ClientToScreen(target->_hwnd, &screen);
					postMessage(target, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(target->_hwnd), MAKELPARAM(screen.x, screen.y));
				}
				break;
			}
			case ui::MouseAction::wheel:
			case ui::MouseAction::horizontalWheel:
			{
				// Wheel messages carry screen coordinates and go to the focus window, as on Windows,
				// unless it is in another top level.
				POINT screen{position._x, position._y};
				ClientToScreen(target->_hwnd, &screen);
				Window* wheelTarget = target;
				if (Window* focus = focusWindow(); focus && GetAncestor(focus->_hwnd, GA_ROOT) == GetAncestor(target->_hwnd, GA_ROOT))
					wheelTarget = target;
				postMessage(wheelTarget, action == ui::MouseAction::wheel ? WM_MOUSEWHEEL : WM_MOUSEHWHEEL,
					MAKEWPARAM(flags, static_cast<short>(delta)), MAKELPARAM(screen.x, screen.y));
				break;
			}
			case ui::MouseAction::leave:
				postMessage(target, WM_MOUSELEAVE, 0, 0);
				break;
		}
	}

	// --- Keyboard ---------------------------------------------------------------------------------

	namespace
	{
		LPARAM keyLParam(BYTE virtualKey, bool down, bool repeat)
		{
			const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
			LPARAM lParam = 1 | (static_cast<LPARAM>(scanCode & 0xFF) << 16);
			if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP || virtualKey == VK_DOWN || virtualKey == VK_PRIOR
				|| virtualKey == VK_NEXT || virtualKey == VK_HOME || virtualKey == VK_END || virtualKey == VK_INSERT || virtualKey == VK_DELETE)
				lParam |= 1 << 24; // extended key
			if (g_modifiers & ui::modifierAlt)
				lParam |= 1 << 29;
			if (repeat || !down)
				lParam |= 1 << 30;
			if (!down)
				lParam |= static_cast<LPARAM>(1) << 31;
			return lParam;
		}
	}
}

namespace nppmac::ui
{
	void windowManagerKeyEvent(const KeyEvent& event)
	{
		using namespace nppmac::user;
		setModifierState(event._modifiers);
		const BYTE virtualKey = virtualKeyFor(event._keyCode, event._charactersIgnoringModifiers);
		if (virtualKey == 0)
		{
			// A key Windows has no virtual key for: deliver its text only.
			if (event._down && !event._characters.empty())
				windowManagerInsertText(event._characters);
			if (event._nsEvent)
				releaseNativeEvent(event._nsEvent);
			return;
		}
		setKeyState(virtualKey, event._down);

		Window* target = focusWindow();
		if (!target)
			target = activeWindow();
		if (!target)
		{
			if (event._nsEvent)
				releaseNativeEvent(event._nsEvent);
			return;
		}

		// Alt (Option) without Ctrl makes system keys, as on Windows.
		const bool system = (event._modifiers & modifierAlt) && !(event._modifiers & modifierControl);
		const UINT message = event._down ? (system ? WM_SYSKEYDOWN : WM_KEYDOWN) : (system ? WM_SYSKEYUP : WM_KEYUP);
		postKeyboardInput(message, virtualKey, keyLParam(virtualKey, event._down, event._repeat), event._nsEvent, event._down ? event._characters : std::u16string{});
	}

	void windowManagerModifiersChanged(unsigned modifiers, unsigned short keyCode, bool down)
	{
		using namespace nppmac::user;
		setModifierState(modifiers);
		const BYTE virtualKey = virtualKeyFor(keyCode, {});
		Window* target = focusWindow() ? focusWindow() : activeWindow();
		if (!target || virtualKey == 0 || virtualKey == VK_CAPITAL)
			return;
		const bool system = virtualKey == VK_MENU || ((modifiers & modifierAlt) && !(modifiers & modifierControl));
		const UINT message = down ? (system ? WM_SYSKEYDOWN : WM_KEYDOWN) : (system ? WM_SYSKEYUP : WM_KEYUP);
		postKeyboardInput(message, virtualKey, keyLParam(virtualKey, down, false));
	}

	void windowManagerInsertText(const std::u16string& text)
	{
		using namespace nppmac::user;
		Window* target = focusWindow() ? focusWindow() : activeWindow();
		if (!target)
			return;
		for (const char16_t ch : text)
			postKeyboardInput(WM_CHAR, ch, 1);
	}
}

using namespace nppmac::user;

// --- Sending and posting --------------------------------------------------------------------------

LRESULT WINAPI SendMessageW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (hwnd == HWND_BROADCAST)
	{
		for (Window* window : topLevelWindows())
			callWindowProc(window, message, wParam, lParam);
		return 0;
	}
	Window* window = fromHandle(hwnd);
	if (!window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return 0;
	}
	if (isGuiThread())
		return callWindowProc(window, message, wParam, lParam);

	// From another thread: the GUI thread processes it while it waits for or retrieves messages.
	CrossThreadSend send{hwnd, message, wParam, lParam, 0, false};
	{
		std::lock_guard lock{g_queueMutex};
		crossThreadSends().push_back(&send);
	}
	nppmac::ui::wakeEventLoop();
	std::unique_lock lock{g_queueMutex};
	g_sendDone.wait(lock, [&send] { return send._done; });
	return send._result;
}

LRESULT WINAPI SendMessageTimeoutW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT, UINT, PDWORD_PTR result)
{
	const LRESULT value = SendMessageW(hwnd, message, wParam, lParam);
	if (result)
		*result = static_cast<DWORD_PTR>(value);
	return TRUE;
}

BOOL WINAPI SendNotifyMessageW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (isGuiThread())
	{
		SendMessageW(hwnd, message, wParam, lParam);
		return TRUE;
	}
	return PostMessageW(hwnd, message, wParam, lParam);
}

LRESULT WINAPI SendDlgItemMessageW(HWND dialog, int id, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND item = GetDlgItem(dialog, id);
	return item ? SendMessageW(item, message, wParam, lParam) : 0;
}

BOOL WINAPI PostMessageW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (hwnd == HWND_BROADCAST)
	{
		for (Window* window : topLevelWindows())
			postMessage(window, message, wParam, lParam);
		return TRUE;
	}
	Window* window = fromHandle(hwnd);
	if (hwnd && !window)
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return FALSE;
	}
	postMessage(window, message, wParam, lParam);
	return TRUE;
}

BOOL WINAPI PostThreadMessageW(DWORD, UINT message, WPARAM wParam, LPARAM lParam)
{
	postMessage(nullptr, message, wParam, lParam);
	return TRUE;
}

void WINAPI PostQuitMessage(int exitCode)
{
	g_quitPosted = true;
	g_quitCode = exitCode;
	nppmac::ui::wakeEventLoop();
}

LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return proc ? proc(hwnd, message, wParam, lParam) : 0;
}

UINT WINAPI RegisterWindowMessageW(LPCWSTR name)
{
	auto& map = registeredMessages();
	auto it = map.find(name);
	if (it != map.end())
		return it->second;
	const UINT id = 0xC000 + static_cast<UINT>(map.size()) + 0x100;
	map.emplace(name, id);
	return id;
}

// --- The message loop -----------------------------------------------------------------------------

BOOL WINAPI PeekMessageW(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT flags)
{
	if (peek(msg, hwnd, filterMin, filterMax, flags))
		return TRUE;
	// Let pending native events arrive (and post their messages), then look again.
	nppmac::ui::runEventLoopOnce(0);
	return peek(msg, hwnd, filterMin, filterMax, flags) ? TRUE : FALSE;
}

BOOL WINAPI GetMessageW(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax)
{
	for (;;)
	{
		if (peek(msg, hwnd, filterMin, filterMax, PM_REMOVE))
			return msg->message == WM_QUIT ? FALSE : TRUE;
		nppmac::ui::runEventLoopOnce(-1);
	}
}

BOOL WINAPI WaitMessage()
{
	nppmac::ui::runEventLoopOnce(-1);
	return TRUE;
}

DWORD WINAPI MsgWaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds, DWORD)
{
	// Waits for the objects while letting native events arrive.
	const ULONGLONG deadline = GetTickCount64() + milliseconds;
	for (;;)
	{
		const DWORD result = count ? WaitForMultipleObjects(count, handles, waitAll, 0) : WAIT_TIMEOUT;
		if (result != WAIT_TIMEOUT)
			return result;
		{
			std::lock_guard lock{g_queueMutex};
			if (!queue().empty())
				return WAIT_OBJECT_0 + count;
		}
		if (milliseconds != INFINITE && GetTickCount64() >= deadline)
			return WAIT_TIMEOUT;
		nppmac::ui::runEventLoopOnce(10);
	}
}

DWORD WINAPI MsgWaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, DWORD milliseconds, DWORD wakeMask, DWORD flags)
{
	return MsgWaitForMultipleObjects(count, handles, (flags & MWMO_WAITALL) != 0, milliseconds, wakeMask);
}

LRESULT WINAPI DispatchMessageW(const MSG* msg)
{
	if (msg->message == WM_TIMER && msg->lParam)
	{
		auto proc = reinterpret_cast<TIMERPROC>(msg->lParam);
		proc(msg->hwnd, WM_TIMER, msg->wParam, GetTickCount());
		return 0;
	}
	Window* window = fromHandle(msg->hwnd);
	if (!window)
		return 0;

	// Mouse input sets the cursor first (WM_SETCURSOR), as the system does on Windows.
	if (msg->message == WM_MOUSEMOVE || (msg->message >= WM_LBUTTONDOWN && msg->message <= WM_MBUTTONDBLCLK))
	{
		if (!GetCapture())
			window->send(WM_SETCURSOR, reinterpret_cast<WPARAM>(msg->hwnd), MAKELPARAM(HTCLIENT, msg->message));
		if (!fromHandle(msg->hwnd))
			return 0;
	}
	// A click activates the window's top level and focuses windows that take focus on click.
	if (msg->message == WM_LBUTTONDOWN || msg->message == WM_RBUTTONDOWN || msg->message == WM_MBUTTONDOWN)
	{
		Window* root = fromHandle(GetAncestor(msg->hwnd, GA_ROOT));
		if (root && root != activeWindow() && root->_visible)
			SetActiveWindow(root->_hwnd);
	}
	return callWindowProc(window, msg->message, msg->wParam, msg->lParam);
}

BOOL WINAPI TranslateMessage(const MSG* msg)
{
	if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
		return FALSE;
	const BYTE virtualKey = static_cast<BYTE>(msg->wParam);
	auto it = g_pendingCharacters.find(virtualKey);
	if (it == g_pendingCharacters.end())
		return FALSE;
	std::u16string characters = it->second;
	g_pendingCharacters.erase(it);
	Window* window = fromHandle(msg->hwnd);
	if (!window || characters.empty())
		return FALSE;

	// Ctrl+letter makes the control character on Windows (Ctrl+A is 1), Command does too here.
	const bool control = (g_keyState[VK_CONTROL] & 0x80) != 0;
	if (control && virtualKey >= 'A' && virtualKey <= 'Z')
		characters = std::u16string(1, static_cast<char16_t>(virtualKey - 'A' + 1));
	else if (control)
		return FALSE;
	// Keys whose "characters" are private-use function key codes produce no WM_CHAR.
	if (characters[0] >= 0xF700 && characters[0] <= 0xF8FF)
		return FALSE;
	if (virtualKey == VK_RETURN)
		characters = u"\r";
	else if (virtualKey == VK_BACK)
		characters = u"\b";
	else if (virtualKey == VK_ESCAPE)
		characters = u"\x1b";
	else if (virtualKey == VK_DELETE)
		return FALSE;

	const UINT charMessage = msg->message == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR;
	// The characters go to the front of the queue, before any later input.
	std::lock_guard lock{g_queueMutex};
	auto& messages = queue();
	for (auto rit = characters.rbegin(); rit != characters.rend(); ++rit)
	{
		MSG charMsg = *msg;
		charMsg.message = charMessage;
		charMsg.wParam = *rit;
		messages.push_front({charMsg, nullptr});
	}
	return TRUE;
}

LONG WINAPI GetMessageTime()
{
	return static_cast<LONG>(messageTime());
}

DWORD WINAPI GetMessagePos()
{
	const POINT p = messagePosition();
	return MAKELONG(static_cast<WORD>(p.x), static_cast<WORD>(p.y));
}

LPARAM WINAPI GetMessageExtraInfo()
{
	return 0;
}

BOOL WINAPI InSendMessage()
{
	return FALSE;
}

BOOL WINAPI ReplyMessage(LRESULT)
{
	return FALSE;
}

// --- Timers ---------------------------------------------------------------------------------------

UINT_PTR WINAPI SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc)
{
	if (hwnd && !fromHandle(hwnd))
	{
		SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return 0;
	}
	if (!hwnd)
		id = g_nextTimerId++;
	Timer* timer = findTimer(hwnd, id);
	if (timer)
	{
		nppmac::ui::stopTimer(timer->_native);
	}
	else
	{
		timer = new Timer;
		timer->_hwnd = hwnd;
		timer->_id = id;
		timers().push_back(timer);
	}
	timer->_proc = proc;
	timer->_pending = false;
	timer->_native = nppmac::ui::startTimer(std::max<UINT>(elapse, USER_TIMER_MINIMUM), timerFired, timer, id);
	return hwnd ? (id ? id : 1) : id;
}

BOOL WINAPI KillTimer(HWND hwnd, UINT_PTR id)
{
	auto& list = timers();
	for (auto it = list.begin(); it != list.end(); ++it)
	{
		if ((*it)->_hwnd == hwnd && (*it)->_id == id)
		{
			nppmac::ui::stopTimer((*it)->_native);
			delete *it;
			list.erase(it);
			std::lock_guard lock{g_queueMutex};
			auto& messages = queue();
			messages.erase(std::remove_if(messages.begin(), messages.end(), [hwnd, id](const QueuedMessage& queued)
			{
				return queued._msg.message == WM_TIMER && queued._msg.hwnd == hwnd && queued._msg.wParam == id;
			}), messages.end());
			return TRUE;
		}
	}
	return FALSE;
}

// --- Hooks ----------------------------------------------------------------------------------------

HHOOK WINAPI SetWindowsHookExW(int type, HOOKPROC proc, HINSTANCE, DWORD)
{
	auto* hook = new Hook{type, proc};
	hooks().push_back(hook);
	return reinterpret_cast<HHOOK>(hook);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK handle)
{
	auto& list = hooks();
	auto* hook = reinterpret_cast<Hook*>(handle);
	auto it = std::find(list.begin(), list.end(), hook);
	if (it == list.end())
		return FALSE;
	list.erase(it);
	delete hook;
	return TRUE;
}

LRESULT WINAPI CallNextHookEx(HHOOK, int, WPARAM, LPARAM)
{
	return 0;
}

// --- Painting -------------------------------------------------------------------------------------

BOOL WINAPI InvalidateRect(HWND hwnd, const RECT* rect, BOOL erase)
{
	if (!hwnd)
	{
		for (Window* window : topLevelWindows())
			invalidate(window, nullptr, erase != FALSE);
		return TRUE;
	}
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	invalidate(window, rect, erase != FALSE);
	return TRUE;
}

BOOL WINAPI InvalidateRgn(HWND hwnd, HRGN region, BOOL erase)
{
	RECT box{};
	if (region && GetRgnBox(region, &box) != NULLREGION)
		return InvalidateRect(hwnd, &box, erase);
	return InvalidateRect(hwnd, region ? &box : nullptr, erase);
}

BOOL WINAPI ValidateRect(HWND hwnd, const RECT* rect)
{
	validate(fromHandle(hwnd), rect);
	return TRUE;
}

BOOL WINAPI GetUpdateRect(HWND hwnd, LPRECT rect, BOOL)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	if (rect)
		*rect = window->_invalid;
	return IsRectEmpty(&window->_invalid) ? FALSE : TRUE;
}

BOOL WINAPI UpdateWindow(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	if (window->_view)
		nppmac::ui::displayNow(window->_view);
	return TRUE;
}

BOOL WINAPI RedrawWindow(HWND hwnd, const RECT* rect, HRGN region, UINT flags)
{
	Window* window = hwnd ? fromHandle(hwnd) : nullptr;
	if (hwnd && !window)
		return FALSE;
	std::vector<Window*> targets;
	if (window)
		targets.push_back(window);
	else
		targets = topLevelWindows();

	RECT box{};
	const RECT* area = rect;
	if (!area && region && GetRgnBox(region, &box) != NULLREGION)
		area = &box;

	std::function<void(Window*, const RECT*)> apply = [&](Window* w, const RECT* r)
	{
		if (flags & (RDW_INVALIDATE | RDW_ERASE | RDW_FRAME))
			invalidate(w, r, (flags & RDW_ERASE) != 0);
		if (flags & RDW_VALIDATE)
			validate(w, r);
		if (flags & RDW_ALLCHILDREN)
		{
			for (Window* child : w->_children)
				apply(child, nullptr);
		}
	};
	for (Window* target : targets)
	{
		apply(target, area);
		if ((flags & (RDW_UPDATENOW | RDW_ERASENOW)) && target->_view)
			nppmac::ui::displayNow(target->_view);
	}
	return TRUE;
}

BOOL WINAPI ScrollWindow(HWND hwnd, int, int, const RECT*, const RECT*)
{
	// The whole client area is repainted rather than moved: simpler, and fast enough.
	return InvalidateRect(hwnd, nullptr, TRUE);
}

int WINAPI ScrollWindowEx(HWND hwnd, int, int, const RECT*, const RECT*, HRGN, LPRECT update, UINT)
{
	if (update)
		GetClientRect(hwnd, update);
	InvalidateRect(hwnd, nullptr, TRUE);
	return SIMPLEREGION;
}

// --- DefWindowProc --------------------------------------------------------------------------------

LRESULT WINAPI DefWindowProcW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return 0;

	switch (message)
	{
		case WM_NCCREATE:
		{
			const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			if (create->lpszName && reinterpret_cast<uintptr_t>(create->lpszName) > 0xFFFF)
			{
				window->_text = create->lpszName;
				if (window->isTopLevel() && window->_view)
					nppmac::ui::setTitle(window->_view, window->_text);
			}
			return TRUE;
		}
		case WM_NCCALCSIZE:
		case WM_NCPAINT:
		case WM_NCDESTROY:
			return 0;

		case WM_NCHITTEST:
			return HTCLIENT;

		case WM_NCACTIVATE:
			return TRUE;

		case WM_ACTIVATE:
		{
			if (LOWORD(wParam) != WA_INACTIVE && window->isTopLevel())
			{
				Window* focus = focusWindow();
				if (!focus || GetAncestor(focus->_hwnd, GA_ROOT) != hwnd)
					SetFocus(hwnd);
			}
			return 0;
		}

		case WM_SETTEXT:
		{
			window->_text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"";
			if (window->isTopLevel() && window->_view)
				nppmac::ui::setTitle(window->_view, window->_text);
			return TRUE;
		}
		case WM_GETTEXT:
		{
			if (wParam == 0)
				return 0;
			auto* buffer = reinterpret_cast<LPWSTR>(lParam);
			lstrcpynW(buffer, window->_text.c_str(), static_cast<int>(wParam));
			return lstrlenW(buffer);
		}
		case WM_GETTEXTLENGTH:
			return static_cast<LRESULT>(window->_text.size());

		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;

		case WM_SYSCOMMAND:
		{
			switch (wParam & 0xFFF0)
			{
				case SC_CLOSE:
					window->send(WM_CLOSE, 0, 0);
					return 0;
				case SC_MINIMIZE:
					ShowWindow(hwnd, SW_MINIMIZE);
					return 0;
				case SC_MAXIMIZE:
					ShowWindow(hwnd, SW_MAXIMIZE);
					return 0;
				case SC_RESTORE:
					ShowWindow(hwnd, SW_RESTORE);
					return 0;
				default:
					return 0;
			}
		}

		case WM_ERASEBKGND:
		{
			HBRUSH brush = window->_class->_background;
			if (!brush)
				return 0;
			// A background "brush" can be a system color index plus one.
			if (reinterpret_cast<uintptr_t>(brush) <= COLOR_MENUBAR + 1)
				brush = GetSysColorBrush(static_cast<int>(reinterpret_cast<uintptr_t>(brush)) - 1);
			RECT client{};
			GetClientRect(hwnd, &client);
			FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
			return 1;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT paint{};
			BeginPaint(hwnd, &paint);
			EndPaint(hwnd, &paint);
			return 0;
		}

		case WM_PRINT:
		{
			// Draws the window into another DC (double buffering): background, then client area.
			if (lParam & PRF_ERASEBKGND)
				window->send(WM_ERASEBKGND, wParam, 0);
			if (lParam & PRF_CLIENT)
				window->send(WM_PRINTCLIENT, wParam, lParam);
			return 0;
		}

		case WM_WINDOWPOSCHANGED:
		{
			const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
			if (!(position->flags & SWP_NOSIZE))
			{
				WPARAM sizeType = IsZoomed(hwnd) ? SIZE_MAXIMIZED : (IsIconic(hwnd) ? SIZE_MINIMIZED : SIZE_RESTORED);
				window->send(WM_SIZE, sizeType, MAKELPARAM(window->_clientWidth, window->_clientHeight));
			}
			if (!(position->flags & SWP_NOMOVE))
			{
				POINT origin = window->isTopLevel() ? clientOriginOnScreen(window) : POINT{window->_rect.left, window->_rect.top};
				window->send(WM_MOVE, 0, MAKELPARAM(origin.x, origin.y));
			}
			return 0;
		}

		case WM_GETMINMAXINFO:
			return 0;

		case WM_SETCURSOR:
		{
			// The parent may set the cursor for its children.
			if (window->_parent && window->_parent->send(WM_SETCURSOR, wParam, lParam))
				return TRUE;
			if (LOWORD(lParam) == HTCLIENT && window->_class->_cursor)
			{
				SetCursor(window->_class->_cursor);
				return TRUE;
			}
			return FALSE;
		}

		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		case WM_CONTEXTMENU:
		case WM_APPCOMMAND:
		{
			// Unhandled, they go to the parent.
			if (window->_parent && window->isChild())
				return window->_parent->send(message, wParam, lParam);
			return 0;
		}

		case WM_CTLCOLORMSGBOX:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSCROLLBAR:
		case WM_CTLCOLORSTATIC:
		{
			auto dc = reinterpret_cast<HDC>(wParam);
			SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor(dc, GetSysColor(message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ? COLOR_WINDOW : COLOR_BTNFACE));
			return reinterpret_cast<LRESULT>(GetSysColorBrush(message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ? COLOR_WINDOW : COLOR_BTNFACE));
		}

		case WM_NOTIFYFORMAT:
			return NFR_UNICODE;

		case WM_QUERYENDSESSION:
		case WM_QUERYOPEN:
			return TRUE;

		case WM_GETFONT:
			return reinterpret_cast<LRESULT>(window->_font);

		case WM_SETFONT:
			window->_font = reinterpret_cast<HFONT>(wParam);
			return 0;

		case WM_SHOWWINDOW:
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_CHAR:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_SYSCHAR:
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_TIMER:
		case WM_CREATE:
		case WM_DESTROY:
		default:
			return 0;
	}
}

// --- Subclassing (comctl32) -----------------------------------------------------------------------
// Subclass procedures run before the window procedure, newest first, each passing the message on
// with DefSubclassProc; the window procedure (GWLP_WNDPROC) is not replaced.

namespace nppmac::user
{
	LRESULT callSubclass(Window* window, size_t depth, UINT message, WPARAM wParam, LPARAM lParam)
	{
		HWND hwnd = window->_hwnd;
		if (depth >= window->_subclasses.size())
			return window->_windowProc ? window->_windowProc(hwnd, message, wParam, lParam) : 0;
		const Subclass subclass = window->_subclasses[window->_subclasses.size() - 1 - depth];
		const size_t savedDepth = window->_subclassDepth;
		window->_subclassDepth = depth + 1;
		const LRESULT result = subclass._proc(hwnd, message, wParam, lParam, subclass._id, subclass._refData);
		if (Window* still = fromHandle(hwnd))
			still->_subclassDepth = savedDepth;
		return result;
	}
}

BOOL WINAPI SetWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR refData)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	for (Subclass& subclass : window->_subclasses)
	{
		if (subclass._proc == proc && subclass._id == id)
		{
			subclass._refData = refData;
			return TRUE;
		}
	}
	window->_subclasses.push_back({proc, id, refData});
	return TRUE;
}

BOOL WINAPI GetWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR* refData)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	for (const Subclass& subclass : window->_subclasses)
	{
		if (subclass._proc == proc && subclass._id == id)
		{
			if (refData)
				*refData = subclass._refData;
			return TRUE;
		}
	}
	return FALSE;
}

BOOL WINAPI RemoveWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	auto& list = window->_subclasses;
	auto it = std::find_if(list.begin(), list.end(), [proc, id](const Subclass& s) { return s._proc == proc && s._id == id; });
	if (it == list.end())
		return FALSE;
	// A subclass removed while it runs: DefSubclassProc must still reach the next one.
	const size_t depthOfRemoved = static_cast<size_t>(list.end() - it) - 1;
	list.erase(it);
	if (window->_subclassDepth > depthOfRemoved)
		--window->_subclassDepth;
	return TRUE;
}

LRESULT WINAPI DefSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return 0;
	return callSubclass(window, window->_subclassDepth, message, wParam, lParam);
}

// --- Keyboard state -------------------------------------------------------------------------------

SHORT WINAPI GetKeyState(int virtualKey)
{
	const BYTE state = g_keyState[virtualKey & 0xFF];
	return static_cast<SHORT>(((state & 0x80) ? 0x8000 : 0) | (state & 0x01));
}

SHORT WINAPI GetAsyncKeyState(int virtualKey)
{
	return static_cast<SHORT>((g_asyncKeyState[virtualKey & 0xFF] & 0x80) ? 0x8000 : 0);
}

BOOL WINAPI GetKeyboardState(PBYTE state)
{
	std::copy(g_keyState.begin(), g_keyState.end(), state);
	return TRUE;
}

BOOL WINAPI SetKeyboardState(LPBYTE state)
{
	std::copy(state, state + g_keyState.size(), g_keyState.begin());
	return TRUE;
}

HKL WINAPI GetKeyboardLayout(DWORD)
{
	return reinterpret_cast<HKL>(static_cast<uintptr_t>(0x04090409)); // en-US
}

UINT WINAPI MapVirtualKeyW(UINT code, UINT mapType)
{
	// Scan codes of the US layout for the keys Notepad++ asks about.
	static const std::pair<BYTE, BYTE> scanCodes[] = {
		{VK_ESCAPE, 0x01}, {'1', 0x02}, {'2', 0x03}, {'3', 0x04}, {'4', 0x05}, {'5', 0x06}, {'6', 0x07}, {'7', 0x08}, {'8', 0x09},
		{'9', 0x0A}, {'0', 0x0B}, {VK_OEM_MINUS, 0x0C}, {VK_OEM_PLUS, 0x0D}, {VK_BACK, 0x0E}, {VK_TAB, 0x0F}, {'Q', 0x10},
		{'W', 0x11}, {'E', 0x12}, {'R', 0x13}, {'T', 0x14}, {'Y', 0x15}, {'U', 0x16}, {'I', 0x17}, {'O', 0x18}, {'P', 0x19},
		{VK_OEM_4, 0x1A}, {VK_OEM_6, 0x1B}, {VK_RETURN, 0x1C}, {VK_CONTROL, 0x1D}, {'A', 0x1E}, {'S', 0x1F}, {'D', 0x20},
		{'F', 0x21}, {'G', 0x22}, {'H', 0x23}, {'J', 0x24}, {'K', 0x25}, {'L', 0x26}, {VK_OEM_1, 0x27}, {VK_OEM_7, 0x28},
		{VK_OEM_3, 0x29}, {VK_SHIFT, 0x2A}, {VK_OEM_5, 0x2B}, {'Z', 0x2C}, {'X', 0x2D}, {'C', 0x2E}, {'V', 0x2F}, {'B', 0x30},
		{'N', 0x31}, {'M', 0x32}, {VK_OEM_COMMA, 0x33}, {VK_OEM_PERIOD, 0x34}, {VK_OEM_2, 0x35}, {VK_MULTIPLY, 0x37},
		{VK_MENU, 0x38}, {VK_SPACE, 0x39}, {VK_CAPITAL, 0x3A}, {VK_F1, 0x3B}, {VK_F2, 0x3C}, {VK_F3, 0x3D}, {VK_F4, 0x3E},
		{VK_F5, 0x3F}, {VK_F6, 0x40}, {VK_F7, 0x41}, {VK_F8, 0x42}, {VK_F9, 0x43}, {VK_F10, 0x44}, {VK_HOME, 0x47}, {VK_UP, 0x48},
		{VK_PRIOR, 0x49}, {VK_SUBTRACT, 0x4A}, {VK_LEFT, 0x4B}, {VK_RIGHT, 0x4D}, {VK_ADD, 0x4E}, {VK_END, 0x4F}, {VK_DOWN, 0x50},
		{VK_NEXT, 0x51}, {VK_INSERT, 0x52}, {VK_DELETE, 0x53}, {VK_OEM_102, 0x56}, {VK_F11, 0x57}, {VK_F12, 0x58},
	};
	switch (mapType)
	{
		case MAPVK_VK_TO_VSC:
		case MAPVK_VK_TO_VSC_EX:
			for (const auto& [vk, scan] : scanCodes)
			{
				if (vk == code)
					return scan;
			}
			return 0;
		case MAPVK_VSC_TO_VK:
		case MAPVK_VSC_TO_VK_EX:
			for (const auto& [vk, scan] : scanCodes)
			{
				if (scan == code)
					return vk;
			}
			return 0;
		case MAPVK_VK_TO_CHAR:
			if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9'))
				return code;
			switch (code)
			{
				case VK_OEM_1: return ';';
				case VK_OEM_PLUS: return '=';
				case VK_OEM_COMMA: return ',';
				case VK_OEM_MINUS: return '-';
				case VK_OEM_PERIOD: return '.';
				case VK_OEM_2: return '/';
				case VK_OEM_3: return '`';
				case VK_OEM_4: return '[';
				case VK_OEM_5: return '\\';
				case VK_OEM_6: return ']';
				case VK_OEM_7: return '\'';
				case VK_SPACE: return ' ';
				default: return 0;
			}
		default:
			return 0;
	}
}

UINT WINAPI MapVirtualKeyExW(UINT code, UINT mapType, HKL)
{
	return MapVirtualKeyW(code, mapType);
}

int WINAPI ToAscii(UINT virtualKey, UINT, const BYTE* keyState, LPWORD character, UINT)
{
	UINT ch = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_CHAR);
	if (ch == 0)
		return 0;
	const bool shift = keyState && (keyState[VK_SHIFT] & 0x80);
	if (ch >= 'A' && ch <= 'Z' && !shift)
		ch = ch - 'A' + 'a';
	*character = static_cast<WORD>(ch);
	return 1;
}

int WINAPI ToUnicode(UINT virtualKey, UINT scanCode, const BYTE* keyState, LPWSTR buffer, int size, UINT flags)
{
	WORD ch = 0;
	if (size <= 0 || !ToAscii(virtualKey, scanCode, keyState, &ch, flags))
		return 0;
	buffer[0] = static_cast<char16_t>(ch);
	return 1;
}

SHORT WINAPI VkKeyScanW(WCHAR ch)
{
	if (ch >= u'a' && ch <= u'z')
		return static_cast<SHORT>(ch - u'a' + 'A');
	if ((ch >= u'A' && ch <= u'Z'))
		return static_cast<SHORT>(ch | 0x100);
	if (ch >= u'0' && ch <= u'9')
		return static_cast<SHORT>(ch);
	return -1;
}

UINT WINAPI SendInput(UINT, LPINPUT, int)
{
	return 0;
}

void WINAPI mouse_event(DWORD, DWORD, DWORD, DWORD, ULONG_PTR)
{
}

void WINAPI keybd_event(BYTE, BYTE, DWORD, ULONG_PTR)
{
}

// --- Mouse tracking -------------------------------------------------------------------------------

BOOL WINAPI TrackMouseEvent(LPTRACKMOUSEEVENT track)
{
	// Views always report when the mouse leaves; WM_MOUSELEAVE is posted for every leave.
	if (track->dwFlags & TME_QUERY)
	{
		track->dwFlags = 0;
		return TRUE;
	}
	return TRUE;
}

BOOL WINAPI _TrackMouseEvent(LPTRACKMOUSEEVENT track)
{
	return TrackMouseEvent(track);
}

namespace
{
	HWND g_capture = nullptr;
}

HWND WINAPI GetCapture()
{
	if (g_capture && !fromHandle(g_capture))
		g_capture = nullptr;
	return g_capture;
}

HWND WINAPI SetCapture(HWND hwnd)
{
	HWND previous = GetCapture();
	Window* window = fromHandle(hwnd);
	g_capture = window ? hwnd : nullptr;
	if (window)
		nppmac::ui::setCapture(window->_view);
	if (previous && previous != hwnd)
		SendMessageW(previous, WM_CAPTURECHANGED, 0, reinterpret_cast<LPARAM>(hwnd));
	return previous;
}

BOOL WINAPI ReleaseCapture()
{
	HWND previous = GetCapture();
	g_capture = nullptr;
	nppmac::ui::releaseCapture();
	if (previous)
		SendMessageW(previous, WM_CAPTURECHANGED, 0, 0);
	return TRUE;
}

BOOL WINAPI GetCursorPos(LPPOINT point)
{
	const nppmac::ui::Point p = nppmac::ui::cursorPosition();
	point->x = p._x;
	point->y = p._y;
	return TRUE;
}

BOOL WINAPI SetCursorPos(int, int)
{
	return TRUE;
}

BOOL WINAPI GetCursorInfo(PCURSORINFO info)
{
	info->flags = CURSOR_SHOWING;
	info->hCursor = GetCursor();
	GetCursorPos(&info->ptScreenPos);
	return TRUE;
}
