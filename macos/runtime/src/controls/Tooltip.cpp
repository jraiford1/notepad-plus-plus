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

// The tooltip control (tooltips_class32): a popup that shows a tool's text after the mouse rests
// on the tool. Tools see the mouse through TTF_SUBCLASS or TTM_RELAYEVENT; their text can come from
// TTN_GETDISPINFO. Tracking tooltips (TTF_TRACK) are placed and shown by the program.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr UINT_PTR showTimer = 1;
		constexpr UINT_PTR hideTimer = 2;
		constexpr UINT_PTR subclassId = 0x7007;
		constexpr int defaultInitialDelay = 500;
		constexpr int defaultAutoPopDelay = 5000;
		constexpr int margin = 4;
		constexpr int cursorOffset = 20;

		struct Tool
		{
			UINT _flags = 0;
			HWND _hwnd = nullptr;
			UINT_PTR _id = 0;
			RECT _rect{};
			std::u16string _text;
			bool _textCallback = false;
			HINSTANCE _instance = nullptr;
			LPARAM _param = 0;
		};

		struct Tooltip
		{
			std::vector<Tool> _tools;
			int _maxWidth = -1;
			int _initialDelay = defaultInitialDelay;
			int _autoPopDelay = defaultAutoPopDelay;
			bool _active = true;
			int _hot = -1;          // the tool the mouse rests on
			int _shown = -1;        // the tool whose tip shows
			bool _tracking = false; // a TTF_TRACK tip is shown by the program
			POINT _trackPosition{};
			std::u16string _shownText;
			std::u16string _title;
			COLORREF _background = CLR_DEFAULT;
			COLORREF _textColor = CLR_DEFAULT;
			RECT _margins{};
		};

		// The window a tool watches: itself for TTF_IDISHWND, else its hwnd.
		HWND toolWindow(const Tool& tool)
		{
			return (tool._flags & TTF_IDISHWND) ? reinterpret_cast<HWND>(tool._id) : tool._hwnd;
		}

		bool sameTool(const Tool& tool, const TTTOOLINFOW& info)
		{
			return tool._hwnd == info.hwnd && tool._id == info.uId;
		}

		int findTool(const Tooltip& tooltip, const TTTOOLINFOW& info)
		{
			for (size_t i = 0; i < tooltip._tools.size(); ++i)
			{
				if (sameTool(tooltip._tools[i], info))
					return static_cast<int>(i);
			}
			return -1;
		}

		void setToolText(Tool& tool, const TTTOOLINFOW& info)
		{
			tool._textCallback = info.lpszText == LPSTR_TEXTCALLBACKW;
			tool._text.clear();
			if (tool._textCallback || !info.lpszText)
				return;
			if (IS_INTRESOURCE(info.lpszText))
			{
				char16_t buffer[1024]{};
				LoadStringW(info.hinst, static_cast<UINT>(reinterpret_cast<uintptr_t>(info.lpszText)), buffer, 1024);
				tool._text = buffer;
			}
			else
			{
				tool._text = info.lpszText;
			}
		}

		std::u16string textOf(HWND hwnd, const Tool& tool)
		{
			if (!tool._textCallback)
				return tool._text;
			// The text comes from the tool's window, asked with TTN_GETDISPINFO.
			NMTTDISPINFOW info{};
			info.hdr.hwndFrom = hwnd;
			info.hdr.idFrom = tool._id;
			info.hdr.code = TTN_GETDISPINFOW;
			info.lParam = tool._param;
			info.uFlags = tool._flags;
			SendMessageW(tool._hwnd, WM_NOTIFY, tool._id, reinterpret_cast<LPARAM>(&info));
			if (info.lpszText && IS_INTRESOURCE(info.lpszText))
			{
				char16_t buffer[1024]{};
				LoadStringW(info.hinst, static_cast<UINT>(reinterpret_cast<uintptr_t>(info.lpszText)), buffer, 1024);
				return buffer;
			}
			if (info.lpszText && info.lpszText != LPSTR_TEXTCALLBACKW)
				return info.lpszText;
			return info.szText;
		}

		UINT textFormat(HWND hwnd, const Tooltip& tooltip)
		{
			UINT format = DT_LEFT | DT_TOP | DT_EXPANDTABS;
			if (GetWindowLongW(hwnd, GWL_STYLE) & TTS_NOPREFIX)
				format |= DT_NOPREFIX;
			if (tooltip._maxWidth >= 0)
				format |= DT_WORDBREAK;
			else
				format |= DT_SINGLELINE;
			return format;
		}

		SIZE tipSize(HWND hwnd, const Tooltip& tooltip, const std::u16string& text)
		{
			HDC hdc = GetDC(hwnd);
			HGDIOBJ oldFont = SelectObject(hdc, fontOf(hwnd));
			RECT rect{0, 0, tooltip._maxWidth >= 0 ? tooltip._maxWidth : 2000, 0};
			DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect, textFormat(hwnd, tooltip) | DT_CALCRECT);
			SelectObject(hdc, oldFont);
			ReleaseDC(hwnd, hdc);
			return {rect.right - rect.left + 2 * margin + tooltip._margins.left + tooltip._margins.right,
				rect.bottom - rect.top + 2 * margin + tooltip._margins.top + tooltip._margins.bottom};
		}

		void hideTip(HWND hwnd, Tooltip& tooltip)
		{
			KillTimer(hwnd, hideTimer);
			if (tooltip._shown < 0 && !tooltip._tracking)
				return;
			const int shown = tooltip._shown;
			tooltip._shown = -1;
			ShowWindow(hwnd, SW_HIDE);
			if (shown >= 0 && shown < static_cast<int>(tooltip._tools.size()))
			{
				NMHDR header{hwnd, tooltip._tools[static_cast<size_t>(shown)]._id, TTN_POP};
				SendMessageW(tooltip._tools[static_cast<size_t>(shown)]._hwnd, WM_NOTIFY, header.idFrom, reinterpret_cast<LPARAM>(&header));
			}
		}

		// Shows a tool's tip at a screen point (keeping it on the screen).
		void showTip(HWND hwnd, Tooltip& tooltip, int index, POINT at, bool track)
		{
			const Tool& tool = tooltip._tools[static_cast<size_t>(index)];
			const std::u16string text = textOf(hwnd, tool);
			if (text.empty())
				return;
			tooltip._shownText = text;
			tooltip._shown = index;
			const SIZE size = tipSize(hwnd, tooltip, text);
			RECT work{};
			SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
			int x = at.x;
			int y = at.y;
			if (x + size.cx > work.right)
				x = std::max<int>(work.left, work.right - size.cx);
			if (y + size.cy > work.bottom)
				y = std::max<int>(work.top, at.y - size.cy - (track ? 0 : 2 * cursorOffset));
			NMHDR header{hwnd, tool._id, TTN_SHOW};
			SendMessageW(tool._hwnd, WM_NOTIFY, tool._id, reinterpret_cast<LPARAM>(&header));
			SetWindowPos(hwnd, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
			InvalidateRect(hwnd, nullptr, FALSE);
			if (!track)
				SetTimer(hwnd, hideTimer, static_cast<UINT>(tooltip._autoPopDelay), nullptr);
		}

		// The tool under a point given in the coordinates of a window.
		int toolAt(const Tooltip& tooltip, HWND window, POINT point)
		{
			for (size_t i = 0; i < tooltip._tools.size(); ++i)
			{
				const Tool& tool = tooltip._tools[i];
				if (tool._flags & TTF_TRACK)
					continue;
				if (toolWindow(tool) != window)
					continue;
				if (tool._flags & TTF_IDISHWND)
					return static_cast<int>(i);
				if (PtInRect(&tool._rect, point))
					return static_cast<int>(i);
			}
			return -1;
		}

		// What the mouse does over a tool's window, seen by subclassing or relayed by the program.
		void mouseEvent(HWND hwnd, Tooltip& tooltip, const MSG& msg)
		{
			if (!tooltip._active || tooltip._tracking)
				return;
			switch (msg.message)
			{
				case WM_MOUSEMOVE:
				{
					const POINT point{GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
					const int hot = toolAt(tooltip, msg.hwnd, point);
					if (hot == tooltip._hot)
						return;
					tooltip._hot = hot;
					if (tooltip._shown >= 0 && hot != tooltip._shown)
						hideTip(hwnd, tooltip);
					KillTimer(hwnd, showTimer);
					if (hot >= 0)
						SetTimer(hwnd, showTimer, static_cast<UINT>(tooltip._initialDelay), nullptr);
					return;
				}
				case WM_MOUSELEAVE:
				case WM_LBUTTONDOWN:
				case WM_RBUTTONDOWN:
				case WM_MBUTTONDOWN:
				case WM_MOUSEWHEEL:
					tooltip._hot = -1;
					KillTimer(hwnd, showTimer);
					hideTip(hwnd, tooltip);
					return;
				default:
					return;
			}
		}

		LRESULT CALLBACK toolSubclass(HWND tool, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
		{
			HWND hwnd = reinterpret_cast<HWND>(data);
			if (Tooltip* tooltip = dataOf<Tooltip>(hwnd))
			{
				if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || (message >= WM_LBUTTONDOWN && message <= WM_MBUTTONDBLCLK) || message == WM_MOUSEWHEEL)
				{
					if (message == WM_MOUSEMOVE)
					{
						TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, tool, 0};
						TrackMouseEvent(&track);
					}
					MSG msg{tool, message, wParam, lParam};
					mouseEvent(hwnd, *tooltip, msg);
				}
			}
			if (message == WM_NCDESTROY)
				RemoveWindowSubclass(tool, toolSubclass, subclassId);
			return DefSubclassProc(tool, message, wParam, lParam);
		}

		void paint(HWND hwnd, Tooltip& tooltip, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const bool dark = user::darkmode::controlsAreDark();
			const COLORREF background = tooltip._background != CLR_DEFAULT ? tooltip._background : (dark ? RGB(43, 43, 43) : GetSysColor(COLOR_INFOBK));
			const COLORREF text = tooltip._textColor != CLR_DEFAULT ? tooltip._textColor : (dark ? controlPalette()._text : GetSysColor(COLOR_INFOTEXT));
			HBRUSH fill = CreateSolidBrush(background);
			FillRect(hdc, &client, fill);
			DeleteObject(fill);
			HBRUSH frame = CreateSolidBrush(controlPalette()._frame);
			FrameRect(hdc, &client, frame);
			DeleteObject(frame);
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, text);
			RECT textRect{client.left + margin + tooltip._margins.left, client.top + margin + tooltip._margins.top,
				client.right - margin - tooltip._margins.right, client.bottom - margin - tooltip._margins.bottom};
			DrawTextW(hdc, tooltip._shownText.c_str(), static_cast<int>(tooltip._shownText.size()), &textRect, textFormat(hwnd, tooltip));
			RestoreDC(hdc, saved);
		}

		LRESULT CALLBACK tooltipProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Tooltip* tooltip = dataOf<Tooltip>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Tooltip>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_DESTROY:
					for (const Tool& tool : tooltip->_tools)
					{
						if (tool._flags & TTF_SUBCLASS)
							RemoveWindowSubclass(toolWindow(tool), toolSubclass, subclassId);
					}
					return 0;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *tooltip, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_ERASEBKGND:
					return 1;
				case WM_NCHITTEST:
					return HTTRANSPARENT;
				case WM_MOUSEACTIVATE:
					return MA_NOACTIVATE;
				case WM_TIMER:
					if (wParam == showTimer)
					{
						KillTimer(hwnd, showTimer);
						if (tooltip->_hot >= 0 && tooltip->_hot < static_cast<int>(tooltip->_tools.size()))
						{
							POINT cursor{};
							GetCursorPos(&cursor);
							showTip(hwnd, *tooltip, tooltip->_hot, {cursor.x, cursor.y + cursorOffset}, false);
						}
					}
					else if (wParam == hideTimer)
					{
						hideTip(hwnd, *tooltip);
					}
					return 0;
				case TTM_ADDTOOLW:
				{
					const auto* info = reinterpret_cast<const TTTOOLINFOW*>(lParam);
					Tool tool;
					tool._flags = info->uFlags;
					tool._hwnd = info->hwnd;
					tool._id = info->uId;
					tool._rect = info->rect;
					tool._instance = info->hinst;
					tool._param = info->cbSize >= TTTOOLINFOW_V2_SIZE ? info->lParam : 0;
					setToolText(tool, *info);
					const int existing = findTool(*tooltip, *info);
					if (existing >= 0)
						tooltip->_tools[static_cast<size_t>(existing)] = tool;
					else
						tooltip->_tools.push_back(tool);
					if ((tool._flags & TTF_SUBCLASS) && toolWindow(tool))
						SetWindowSubclass(toolWindow(tool), toolSubclass, subclassId, reinterpret_cast<DWORD_PTR>(hwnd));
					return TRUE;
				}
				case TTM_DELTOOLW:
				{
					const int index = findTool(*tooltip, *reinterpret_cast<const TTTOOLINFOW*>(lParam));
					if (index < 0)
						return 0;
					if (index == tooltip->_shown)
						hideTip(hwnd, *tooltip);
					const Tool removed = tooltip->_tools[static_cast<size_t>(index)];
					tooltip->_tools.erase(tooltip->_tools.begin() + index);
					tooltip->_hot = -1;
					tooltip->_shown = -1;
					// The subclass goes when no other tool of the window needs it.
					const bool stillUsed = std::any_of(tooltip->_tools.begin(), tooltip->_tools.end(), [&removed](const Tool& tool)
					{
						return (tool._flags & TTF_SUBCLASS) && toolWindow(tool) == toolWindow(removed);
					});
					if ((removed._flags & TTF_SUBCLASS) && !stillUsed)
						RemoveWindowSubclass(toolWindow(removed), toolSubclass, subclassId);
					return 0;
				}
				case TTM_GETTOOLCOUNT:
					return static_cast<LRESULT>(tooltip->_tools.size());
				case TTM_GETTOOLINFOW:
				case TTM_GETCURRENTTOOLW:
				{
					auto* info = reinterpret_cast<TTTOOLINFOW*>(lParam);
					const int index = message == TTM_GETCURRENTTOOLW ? tooltip->_shown : findTool(*tooltip, *info);
					if (index < 0)
						return FALSE;
					if (!info)
						return TRUE;
					const Tool& tool = tooltip->_tools[static_cast<size_t>(index)];
					info->uFlags = tool._flags;
					info->hwnd = tool._hwnd;
					info->uId = tool._id;
					info->rect = tool._rect;
					info->hinst = tool._instance;
					if (info->lpszText && info->lpszText != LPSTR_TEXTCALLBACKW && !tool._textCallback)
						lstrcpynW(info->lpszText, tool._text.c_str(), 80);
					if (info->cbSize >= TTTOOLINFOW_V2_SIZE)
						info->lParam = tool._param;
					return TRUE;
				}
				case TTM_UPDATETIPTEXTW:
				{
					const auto* info = reinterpret_cast<const TTTOOLINFOW*>(lParam);
					const int index = findTool(*tooltip, *info);
					if (index < 0)
						return 0;
					setToolText(tooltip->_tools[static_cast<size_t>(index)], *info);
					if (index == tooltip->_shown)
					{
						tooltip->_shownText = textOf(hwnd, tooltip->_tools[static_cast<size_t>(index)]);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				case TTM_GETTEXTW:
				{
					auto* info = reinterpret_cast<TTTOOLINFOW*>(lParam);
					const int index = findTool(*tooltip, *info);
					if (index >= 0 && info->lpszText)
						lstrcpynW(info->lpszText, textOf(hwnd, tooltip->_tools[static_cast<size_t>(index)]).c_str(), wParam ? static_cast<int>(wParam) : 80);
					return 0;
				}
				case TTM_NEWTOOLRECTW:
				{
					const auto* info = reinterpret_cast<const TTTOOLINFOW*>(lParam);
					const int index = findTool(*tooltip, *info);
					if (index >= 0)
						tooltip->_tools[static_cast<size_t>(index)]._rect = info->rect;
					return 0;
				}
				case TTM_SETMAXTIPWIDTH:
				{
					const int previous = tooltip->_maxWidth;
					tooltip->_maxWidth = static_cast<int>(lParam);
					return previous;
				}
				case TTM_GETMAXTIPWIDTH:
					return tooltip->_maxWidth;
				case TTM_SETDELAYTIME:
				{
					const int delay = static_cast<short>(LOWORD(lParam));
					if (wParam == TTDT_INITIAL)
						tooltip->_initialDelay = delay < 0 ? defaultInitialDelay : delay;
					else if (wParam == TTDT_AUTOPOP)
						tooltip->_autoPopDelay = delay < 0 ? defaultAutoPopDelay : delay;
					else if (wParam == TTDT_AUTOMATIC)
					{
						tooltip->_initialDelay = delay < 0 ? defaultInitialDelay : delay;
						tooltip->_autoPopDelay = delay < 0 ? defaultAutoPopDelay : delay * 10;
					}
					return 0;
				}
				case TTM_GETDELAYTIME:
					return wParam == TTDT_AUTOPOP ? tooltip->_autoPopDelay : tooltip->_initialDelay;
				case TTM_ACTIVATE:
					tooltip->_active = wParam != 0;
					if (!tooltip->_active)
						hideTip(hwnd, *tooltip);
					return 0;
				case TTM_RELAYEVENT:
				{
					const auto* msg = reinterpret_cast<const MSG*>(lParam);
					if (msg)
						mouseEvent(hwnd, *tooltip, *msg);
					return 0;
				}
				case TTM_TRACKACTIVATE:
				{
					const int index = findTool(*tooltip, *reinterpret_cast<const TTTOOLINFOW*>(lParam));
					if (wParam && index >= 0)
					{
						tooltip->_tracking = true;
						showTip(hwnd, *tooltip, index, tooltip->_trackPosition, true);
					}
					else
					{
						hideTip(hwnd, *tooltip);
						tooltip->_tracking = false;
					}
					return 0;
				}
				case TTM_TRACKPOSITION:
				{
					tooltip->_trackPosition = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (tooltip->_tracking && tooltip->_shown >= 0)
					{
						RECT window{};
						GetWindowRect(hwnd, &window);
						SetWindowPos(hwnd, nullptr, tooltip->_trackPosition.x, tooltip->_trackPosition.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
					}
					return 0;
				}
				case TTM_POPUP:
				{
					POINT cursor{};
					GetCursorPos(&cursor);
					const int index = tooltip->_hot >= 0 ? tooltip->_hot : (tooltip->_tools.empty() ? -1 : 0);
					if (index >= 0)
						showTip(hwnd, *tooltip, index, {cursor.x, cursor.y + cursorOffset}, false);
					return 0;
				}
				case TTM_POP:
					hideTip(hwnd, *tooltip);
					return 0;
				case TTM_UPDATE:
					if (tooltip->_shown >= 0)
					{
						tooltip->_shownText = textOf(hwnd, tooltip->_tools[static_cast<size_t>(tooltip->_shown)]);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				case TTM_SETTITLEW:
					tooltip->_title = lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"";
					return TRUE;
				case TTM_SETTIPBKCOLOR:
					tooltip->_background = static_cast<COLORREF>(wParam);
					return 0;
				case TTM_SETTIPTEXTCOLOR:
					tooltip->_textColor = static_cast<COLORREF>(wParam);
					return 0;
				case TTM_GETTIPBKCOLOR:
					return static_cast<LRESULT>(tooltip->_background == CLR_DEFAULT ? GetSysColor(COLOR_INFOBK) : tooltip->_background);
				case TTM_GETTIPTEXTCOLOR:
					return static_cast<LRESULT>(tooltip->_textColor == CLR_DEFAULT ? GetSysColor(COLOR_INFOTEXT) : tooltip->_textColor);
				case TTM_SETMARGIN:
					if (lParam)
						tooltip->_margins = *reinterpret_cast<const RECT*>(lParam);
					return 0;
				case TTM_GETMARGIN:
					if (lParam)
						*reinterpret_cast<RECT*>(lParam) = tooltip->_margins;
					return 0;
				case TTM_WINDOWFROMPOINT:
				{
					const POINT point = *reinterpret_cast<const POINT*>(lParam);
					return reinterpret_cast<LRESULT>(WindowFromPoint(point));
				}
				case TTM_ADJUSTRECT:
				{
					auto* rect = reinterpret_cast<RECT*>(lParam);
					if (rect)
						InflateRect(rect, wParam ? margin : -margin, wParam ? margin : -margin);
					return TRUE;
				}
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerTooltip()
	{
		registerControl(TOOLTIPS_CLASSW, tooltipProc, CS_SAVEBITS);
	}
}
