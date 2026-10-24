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

// Standard scroll bars (WS_VSCROLL, WS_HSCROLL): their SCROLLINFO, the user32 functions over it,
// and WM_VSCROLL / WM_HSCROLL from the native scroll bars. A bar is visible when the window's style
// has its WS_ bit; SetScrollInfo shows and hides it as the range needs, as on Windows. The runtime's
// own controls draw their scroll bars themselves and only keep the state here.

#include "Window.h"

namespace nppmac::user
{
	namespace
	{
		constexpr int horizontal = SB_HORZ;
		constexpr int vertical = SB_VERT;

		DWORD styleBit(int bar)
		{
			return bar == vertical ? WS_VSCROLL : WS_HSCROLL;
		}

		bool isVisible(const Window& window, int bar)
		{
			return (window._style & styleBit(bar)) != 0;
		}

		// The largest position: the last page starts there.
		int maximumPosition(const SCROLLINFO& info)
		{
			return info.nPage > 0 ? std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1) : info.nMax;
		}

		ui::ScrollBarState nativeState(const Window& window, int bar)
		{
			const SCROLLINFO& info = window._scrollBars[bar];
			ui::ScrollBarState state;
			state._visible = isVisible(window, bar);
			state._enabled = !window._scrollBarsDisabled[bar];
			const int range = info.nMax - info.nMin + 1;
			const int travel = maximumPosition(info) - info.nMin;
			state._value = travel > 0 ? static_cast<double>(info.nPos - info.nMin) / travel : 0;
			state._proportion = range > 0 && info.nPage > 0 ? std::min(1.0, static_cast<double>(info.nPage) / range) : (travel > 0 ? 0.1 : 1);
			return state;
		}

		Window* standardBarWindow(HWND hwnd, int bar)
		{
			Window* window = fromHandle(hwnd);
			return window && (bar == horizontal || bar == vertical) ? window : nullptr;
		}

		void showBar(Window* window, int bar, bool show)
		{
			const DWORD bit = styleBit(bar);
			if (((window->_style & bit) != 0) == show)
				return;
			window->_style = show ? (window->_style | bit) : (window->_style & ~bit);
			updateScrollBars(window);
		}
	}

	void setClassDrawsOwnScrollBars(const char16_t* className)
	{
		if (WindowClass* windowClass = findClass(className))
			windowClass->_drawsOwnScrollBars = true;
	}

	void fitClientToScrollBars(Window* window)
	{
		if (!window || (window->_class && window->_class->_drawsOwnScrollBars) || window->_nativeControl)
			return;
		const int thickness = ui::scrollBarThickness();
		if (isVisible(*window, vertical))
			window->_clientWidth = std::max(0, window->_clientWidth - thickness);
		if (isVisible(*window, horizontal))
			window->_clientHeight = std::max(0, window->_clientHeight - thickness);
	}

	void updateScrollBars(Window* window)
	{
		if (!window || (window->_class && window->_class->_drawsOwnScrollBars) || window->_nativeControl)
			return;
		for (int bar : {horizontal, vertical})
			ui::setScrollBar(window->_view, bar == vertical, nativeState(*window, bar));
		// A bar that comes or goes changes the client area, as a non-client part does on Windows.
		const int oldWidth = window->_clientWidth;
		const int oldHeight = window->_clientHeight;
		applyGeometry(window, window->_rect);
		if (window->_created && (oldWidth != window->_clientWidth || oldHeight != window->_clientHeight))
			window->send(WM_SIZE, SIZE_RESTORED, MAKELPARAM(window->_clientWidth, window->_clientHeight));
	}

	void Window::nativeScroll(bool isVertical, ui::ScrollPart part, double value)
	{
		const int bar = isVertical ? vertical : horizontal;
		SCROLLINFO& info = _scrollBars[bar];
		int code = SB_ENDSCROLL;
		int position = 0;
		switch (part)
		{
			case ui::ScrollPart::lineUp:
				code = SB_LINEUP;
				break;
			case ui::ScrollPart::lineDown:
				code = SB_LINEDOWN;
				break;
			case ui::ScrollPart::pageUp:
				code = SB_PAGEUP;
				break;
			case ui::ScrollPart::pageDown:
				code = SB_PAGEDOWN;
				break;
			case ui::ScrollPart::thumbTrack:
			case ui::ScrollPart::thumbPosition:
				code = part == ui::ScrollPart::thumbTrack ? SB_THUMBTRACK : SB_THUMBPOSITION;
				position = info.nMin + static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * (maximumPosition(info) - info.nMin)));
				info.nTrackPos = position;
				break;
			case ui::ScrollPart::end:
				code = SB_ENDSCROLL;
				break;
		}
		// The 16-bit position in wParam is what old code reads; SIF_TRACKPOS has all of it.
		send(isVertical ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, static_cast<WORD>(position)), 0);
		if (fromHandle(_hwnd))
			ui::setScrollBar(_view, isVertical, nativeState(*this, bar));
	}
}

using namespace nppmac::user;

int WINAPI SetScrollInfo(HWND hwnd, int bar, LPCSCROLLINFO source, BOOL redraw)
{
	if (bar == SB_CTL)
		return static_cast<int>(SendMessageW(hwnd, SBM_SETSCROLLINFO, redraw, reinterpret_cast<LPARAM>(source)));
	Window* window = standardBarWindow(hwnd, bar);
	if (!window || !source)
		return 0;
	SCROLLINFO& info = window->_scrollBars[bar];
	if (source->fMask & SIF_RANGE)
	{
		info.nMin = source->nMin;
		info.nMax = std::max(source->nMin, source->nMax);
	}
	if (source->fMask & SIF_PAGE)
		info.nPage = std::min<UINT>(source->nPage, static_cast<UINT>(info.nMax - info.nMin + 1));
	if (source->fMask & SIF_POS)
		info.nPos = source->nPos;
	info.nPos = std::clamp(info.nPos, info.nMin, maximumPosition(info));

	if (source->fMask & (SIF_RANGE | SIF_PAGE))
	{
		// Not needed: hidden, or disabled with SIF_DISABLENOSCROLL.
		const bool needed = info.nMax > info.nMin && (info.nPage == 0 || static_cast<int>(info.nPage) <= info.nMax - info.nMin);
		if (source->fMask & SIF_DISABLENOSCROLL)
		{
			window->_scrollBarsDisabled[bar] = !needed;
			showBar(window, bar, true);
		}
		else
		{
			window->_scrollBarsDisabled[bar] = false;
			showBar(window, bar, needed);
		}
	}
	if (fromHandle(hwnd))
		nppmac::ui::setScrollBar(window->_view, bar == SB_VERT, nativeState(*window, bar));
	return info.nPos;
}

BOOL WINAPI GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO out)
{
	if (bar == SB_CTL)
		return static_cast<BOOL>(SendMessageW(hwnd, SBM_GETSCROLLINFO, 0, reinterpret_cast<LPARAM>(out)));
	Window* window = standardBarWindow(hwnd, bar);
	if (!window || !out)
		return FALSE;
	const SCROLLINFO& info = window->_scrollBars[bar];
	if (out->fMask & SIF_RANGE)
	{
		out->nMin = info.nMin;
		out->nMax = info.nMax;
	}
	if (out->fMask & SIF_PAGE)
		out->nPage = info.nPage;
	if (out->fMask & SIF_POS)
		out->nPos = info.nPos;
	if (out->fMask & SIF_TRACKPOS)
		out->nTrackPos = info.nTrackPos;
	return TRUE;
}

int WINAPI SetScrollPos(HWND hwnd, int bar, int position, BOOL redraw)
{
	if (bar == SB_CTL)
		return static_cast<int>(SendMessageW(hwnd, SBM_SETPOS, static_cast<WPARAM>(position), redraw));
	Window* window = standardBarWindow(hwnd, bar);
	if (!window)
		return 0;
	const int previous = window->_scrollBars[bar].nPos;
	SCROLLINFO info{sizeof(info), SIF_POS};
	info.nPos = position;
	SetScrollInfo(hwnd, bar, &info, redraw);
	return previous;
}

int WINAPI GetScrollPos(HWND hwnd, int bar)
{
	if (bar == SB_CTL)
		return static_cast<int>(SendMessageW(hwnd, SBM_GETPOS, 0, 0));
	Window* window = standardBarWindow(hwnd, bar);
	return window ? window->_scrollBars[bar].nPos : 0;
}

BOOL WINAPI SetScrollRange(HWND hwnd, int bar, int minimum, int maximum, BOOL redraw)
{
	if (bar == SB_CTL)
		return static_cast<BOOL>(SendMessageW(hwnd, SBM_SETRANGE, static_cast<WPARAM>(minimum), maximum));
	SCROLLINFO info{sizeof(info), SIF_RANGE};
	info.nMin = minimum;
	info.nMax = maximum;
	SetScrollInfo(hwnd, bar, &info, redraw);
	return standardBarWindow(hwnd, bar) != nullptr;
}

BOOL WINAPI GetScrollRange(HWND hwnd, int bar, LPINT minimum, LPINT maximum)
{
	if (bar == SB_CTL)
		return static_cast<BOOL>(SendMessageW(hwnd, SBM_GETRANGE, reinterpret_cast<WPARAM>(minimum), reinterpret_cast<LPARAM>(maximum)));
	Window* window = standardBarWindow(hwnd, bar);
	if (!window)
		return FALSE;
	*minimum = window->_scrollBars[bar].nMin;
	*maximum = window->_scrollBars[bar].nMax;
	return TRUE;
}

BOOL WINAPI ShowScrollBar(HWND hwnd, int bar, BOOL show)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	if (bar == SB_CTL)
		return ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE), TRUE;
	if (bar == SB_HORZ || bar == SB_BOTH)
		showBar(window, SB_HORZ, show != FALSE);
	if (bar == SB_VERT || bar == SB_BOTH)
		showBar(window, SB_VERT, show != FALSE);
	return TRUE;
}

BOOL WINAPI EnableScrollBar(HWND hwnd, UINT bar, UINT arrows)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return FALSE;
	if (bar == SB_CTL)
		return EnableWindow(hwnd, arrows != ESB_DISABLE_BOTH), TRUE;
	for (int which : {SB_HORZ, SB_VERT})
	{
		if (bar == static_cast<UINT>(which) || bar == SB_BOTH)
		{
			window->_scrollBarsDisabled[which] = arrows == ESB_DISABLE_BOTH;
			nppmac::ui::setScrollBar(window->_view, which == SB_VERT, nativeState(*window, which));
		}
	}
	return TRUE;
}

BOOL WINAPI GetScrollBarInfo(HWND hwnd, LONG object, PSCROLLBARINFO info)
{
	Window* window = fromHandle(hwnd);
	if (!window || !info || (object != OBJID_VSCROLL && object != OBJID_HSCROLL && object != OBJID_CLIENT))
		return FALSE;
	const int bar = object == OBJID_HSCROLL ? SB_HORZ : SB_VERT;
	const int thickness = nppmac::ui::scrollBarThickness();
	RECT rect{};
	if (bar == SB_VERT)
		rect = {window->_clientWidth, 0, window->_clientWidth + thickness, window->_clientHeight};
	else
		rect = {0, window->_clientHeight, window->_clientWidth, window->_clientHeight + thickness};
	MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&rect), 2);
	info->rcScrollBar = rect;
	info->dxyLineButton = 0;
	const SCROLLINFO& scroll = window->_scrollBars[bar];
	const int length = bar == SB_VERT ? window->_clientHeight : window->_clientWidth;
	const int range = std::max(1, scroll.nMax - scroll.nMin + 1);
	const int thumb = scroll.nPage > 0 ? std::max(8, static_cast<int>(length * static_cast<int>(scroll.nPage) / range)) : 8;
	const int travel = std::max(1, maximumPosition(scroll) - scroll.nMin);
	info->xyThumbTop = (length - thumb) * (scroll.nPos - scroll.nMin) / travel;
	info->xyThumbBottom = info->xyThumbTop + thumb;
	std::fill(std::begin(info->rgstate), std::end(info->rgstate), 0);
	if (!isVisible(*window, bar))
		info->rgstate[0] = STATE_SYSTEM_INVISIBLE;
	else if (window->_scrollBarsDisabled[bar])
		info->rgstate[0] = STATE_SYSTEM_UNAVAILABLE;
	return TRUE;
}
