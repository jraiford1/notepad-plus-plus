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

// The small comctl32 controls: trackbar (msctls_trackbar32), progress bar (msctls_progress32),
// up-down (msctls_updown32) and link (SysLink).

#include "ControlSupport.h"

#include <cstdlib>

namespace nppmac::controls
{
	namespace
	{
		// --- Trackbar ------------------------------------------------------------------------------

		constexpr int thumbLength = 10;
		constexpr int thumbThickness = 18;
		constexpr int channelThickness = 4;

		struct Trackbar
		{
			int _minimum = 0;
			int _maximum = 100;
			int _position = 0;
			int _lineSize = 1;
			int _pageSize = 20;
			bool _dragging = false;
			bool _focused = false;
			HWND _buddies[2] = {};
		};

		bool isVertical(HWND hwnd)
		{
			return (GetWindowLongW(hwnd, GWL_STYLE) & TBS_VERT) != 0;
		}

		// The channel the thumb moves along, in the direction of the trackbar.
		RECT channelRect(HWND hwnd)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			if (isVertical(hwnd))
			{
				const int x = (client.left + client.right - channelThickness) / 2;
				return {x, client.top + thumbLength / 2 + 2, x + channelThickness, client.bottom - thumbLength / 2 - 2};
			}
			const int y = (client.top + client.bottom - channelThickness) / 2;
			return {client.left + thumbLength / 2 + 2, y, client.right - thumbLength / 2 - 2, y + channelThickness};
		}

		RECT thumbRect(HWND hwnd, const Trackbar& bar)
		{
			const RECT channel = channelRect(hwnd);
			const int range = std::max(1, bar._maximum - bar._minimum);
			if (isVertical(hwnd))
			{
				const int y = channel.top + (channel.bottom - channel.top) * (bar._position - bar._minimum) / range;
				const int x = (channel.left + channel.right) / 2;
				return {x - thumbThickness / 2, y - thumbLength / 2, x + thumbThickness / 2, y + thumbLength / 2};
			}
			const int x = channel.left + (channel.right - channel.left) * (bar._position - bar._minimum) / range;
			const int y = (channel.top + channel.bottom) / 2;
			return {x - thumbLength / 2, y - thumbThickness / 2, x + thumbLength / 2, y + thumbThickness / 2};
		}

		int positionAt(HWND hwnd, const Trackbar& bar, POINT point)
		{
			const RECT channel = channelRect(hwnd);
			const int length = std::max(1, isVertical(hwnd) ? static_cast<int>(channel.bottom - channel.top) : static_cast<int>(channel.right - channel.left));
			const int offset = isVertical(hwnd) ? point.y - channel.top : point.x - channel.left;
			const int range = bar._maximum - bar._minimum;
			return std::clamp(bar._minimum + (offset * range + length / 2) / length, bar._minimum, bar._maximum);
		}

		// Trackbars report to their parent with WM_HSCROLL or WM_VSCROLL.
		void notifyScroll(HWND hwnd, const Trackbar& bar, int code)
		{
			const UINT message = isVertical(hwnd) ? WM_VSCROLL : WM_HSCROLL;
			SendMessageW(GetParent(hwnd), message, MAKEWPARAM(code, code == TB_THUMBTRACK || code == TB_THUMBPOSITION ? bar._position : 0), reinterpret_cast<LPARAM>(hwnd));
		}

		void setPosition(HWND hwnd, Trackbar& bar, int position)
		{
			position = std::clamp(position, bar._minimum, bar._maximum);
			if (position == bar._position)
				return;
			bar._position = position;
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		void paintTrackbar(HWND hwnd, Trackbar& bar, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			FillRect(hdc, &client, controlColors(hwnd, hdc, WM_CTLCOLORSTATIC));
			const bool enabled = IsWindowEnabled(hwnd) != FALSE;
			RECT channel = channelRect(hwnd);
			HBRUSH channelFill = CreateSolidBrush(controlPalette()._track);
			HBRUSH channelFrame = CreateSolidBrush(controlPalette()._divider);
			FillRect(hdc, &channel, channelFill);
			FrameRect(hdc, &channel, channelFrame);
			DeleteObject(channelFill);
			DeleteObject(channelFrame);

			if (!(GetWindowLongW(hwnd, GWL_STYLE) & TBS_NOTHUMB))
			{
				const RECT thumb = thumbRect(hwnd, bar);
				HBRUSH thumbFill = CreateSolidBrush(!enabled ? controlPalette()._faceDisabled : (bar._dragging ? controlPalette()._thumbActive : controlPalette()._accent));
				FillRect(hdc, &thumb, thumbFill);
				DeleteObject(thumbFill);
			}
			if (bar._focused)
			{
				RECT focus = client;
				DrawFocusRect(hdc, &focus);
			}
		}

		LRESULT CALLBACK trackbarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Trackbar* bar = dataOf<Trackbar>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Trackbar>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paintTrackbar(hwnd, *bar, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paintTrackbar(hwnd, *bar, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_GETDLGCODE:
					return DLGC_WANTARROWS;
				case WM_SETFOCUS:
				case WM_KILLFOCUS:
					bar->_focused = message == WM_SETFOCUS;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_ENABLE:
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_LBUTTONDOWN:
				{
					SetFocus(hwnd);
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const RECT thumb = thumbRect(hwnd, *bar);
					if (PtInRect(&thumb, point))
					{
						bar->_dragging = true;
						SetCapture(hwnd);
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					// A click beside the thumb moves it by a page towards the click.
					const bool before = isVertical(hwnd) ? point.y < thumb.top : point.x < thumb.left;
					setPosition(hwnd, *bar, bar->_position + (before ? -bar->_pageSize : bar->_pageSize));
					notifyScroll(hwnd, *bar, before ? TB_PAGEUP : TB_PAGEDOWN);
					notifyScroll(hwnd, *bar, TB_ENDTRACK);
					return 0;
				}
				case WM_MOUSEMOVE:
					if (bar->_dragging)
					{
						const int previous = bar->_position;
						setPosition(hwnd, *bar, positionAt(hwnd, *bar, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
						if (bar->_position != previous)
							notifyScroll(hwnd, *bar, TB_THUMBTRACK);
					}
					return 0;
				case WM_LBUTTONUP:
				case WM_CAPTURECHANGED:
					if (bar->_dragging)
					{
						bar->_dragging = false;
						if (message == WM_LBUTTONUP)
							ReleaseCapture();
						InvalidateRect(hwnd, nullptr, FALSE);
						notifyScroll(hwnd, *bar, TB_THUMBPOSITION);
						notifyScroll(hwnd, *bar, TB_ENDTRACK);
					}
					return 0;
				case WM_KEYDOWN:
				{
					int position = bar->_position;
					int code = -1;
					switch (wParam)
					{
						case VK_LEFT:
						case VK_UP:
							position -= bar->_lineSize;
							code = TB_LINEUP;
							break;
						case VK_RIGHT:
						case VK_DOWN:
							position += bar->_lineSize;
							code = TB_LINEDOWN;
							break;
						case VK_PRIOR:
							position -= bar->_pageSize;
							code = TB_PAGEUP;
							break;
						case VK_NEXT:
							position += bar->_pageSize;
							code = TB_PAGEDOWN;
							break;
						case VK_HOME:
							position = bar->_minimum;
							code = TB_TOP;
							break;
						case VK_END:
							position = bar->_maximum;
							code = TB_BOTTOM;
							break;
						default:
							return 0;
					}
					setPosition(hwnd, *bar, position);
					notifyScroll(hwnd, *bar, code);
					notifyScroll(hwnd, *bar, TB_ENDTRACK);
					return 0;
				}
				case TBM_GETPOS:
					return bar->_position;
				case TBM_SETPOS:
				case TBM_SETPOSNOTIFY:
					setPosition(hwnd, *bar, static_cast<int>(lParam));
					if (message == TBM_SETPOSNOTIFY)
						notifyScroll(hwnd, *bar, TB_THUMBPOSITION);
					return 0;
				case TBM_GETRANGEMIN:
					return bar->_minimum;
				case TBM_GETRANGEMAX:
					return bar->_maximum;
				case TBM_SETRANGE:
					bar->_minimum = static_cast<short>(LOWORD(lParam));
					bar->_maximum = static_cast<short>(HIWORD(lParam));
					setPosition(hwnd, *bar, bar->_position);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case TBM_SETRANGEMIN:
					bar->_minimum = static_cast<int>(lParam);
					setPosition(hwnd, *bar, bar->_position);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case TBM_SETRANGEMAX:
					bar->_maximum = static_cast<int>(lParam);
					setPosition(hwnd, *bar, bar->_position);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case TBM_SETLINESIZE:
				{
					const int previous = bar->_lineSize;
					bar->_lineSize = static_cast<int>(lParam);
					return previous;
				}
				case TBM_GETLINESIZE:
					return bar->_lineSize;
				case TBM_SETPAGESIZE:
				{
					const int previous = bar->_pageSize;
					bar->_pageSize = static_cast<int>(lParam);
					return previous;
				}
				case TBM_GETPAGESIZE:
					return bar->_pageSize;
				case TBM_GETTHUMBRECT:
					*reinterpret_cast<RECT*>(lParam) = thumbRect(hwnd, *bar);
					return 0;
				case TBM_GETCHANNELRECT:
					*reinterpret_cast<RECT*>(lParam) = channelRect(hwnd);
					return 0;
				case TBM_SETBUDDY:
				{
					HWND& buddy = bar->_buddies[wParam ? 0 : 1];
					HWND previous = buddy;
					buddy = reinterpret_cast<HWND>(lParam);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TBM_GETBUDDY:
					return reinterpret_cast<LRESULT>(bar->_buddies[wParam ? 0 : 1]);
				case TBM_SETTICFREQ:
				case TBM_SETTIC:
				case TBM_CLEARTICS:
				case TBM_SETTOOLTIPS:
				case TBM_SETTHUMBLENGTH:
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}

		// --- Progress bar --------------------------------------------------------------------------

		struct Progress
		{
			int _minimum = 0;
			int _maximum = 100;
			int _position = 0;
			int _step = 10;
			COLORREF _barColor = CLR_DEFAULT;
			COLORREF _backgroundColor = CLR_DEFAULT;
			int _state = PBST_NORMAL;
		};

		void paintProgress(HWND hwnd, const Progress& progress, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			HBRUSH background = CreateSolidBrush(progress._backgroundColor == CLR_DEFAULT ? controlPalette()._track : progress._backgroundColor);
			FillRect(hdc, &client, background);
			DeleteObject(background);
			HBRUSH frame = CreateSolidBrush(controlPalette()._frame);
			FrameRect(hdc, &client, frame);
			DeleteObject(frame);

			const int range = std::max(1, progress._maximum - progress._minimum);
			RECT bar = client;
			InflateRect(&bar, -1, -1);
			if (GetWindowLongW(hwnd, GWL_STYLE) & PBS_VERTICAL)
				bar.top = bar.bottom - (bar.bottom - bar.top) * (progress._position - progress._minimum) / range;
			else
				bar.right = bar.left + (bar.right - bar.left) * (progress._position - progress._minimum) / range;
			COLORREF color = progress._barColor == CLR_DEFAULT ? RGB(6, 176, 37) : progress._barColor;
			if (progress._state == PBST_ERROR)
				color = RGB(218, 38, 38);
			else if (progress._state == PBST_PAUSED)
				color = RGB(218, 203, 38);
			HBRUSH fill = CreateSolidBrush(color);
			FillRect(hdc, &bar, fill);
			DeleteObject(fill);
		}

		LRESULT setProgressPosition(HWND hwnd, Progress& progress, int position)
		{
			const int previous = progress._position;
			progress._position = std::clamp(position, progress._minimum, progress._maximum);
			InvalidateRect(hwnd, nullptr, FALSE);
			return previous;
		}

		LRESULT CALLBACK progressProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Progress* progress = dataOf<Progress>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Progress>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paintProgress(hwnd, *progress, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paintProgress(hwnd, *progress, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case PBM_SETRANGE:
				{
					const LRESULT previous = MAKELRESULT(progress->_minimum, progress->_maximum);
					progress->_minimum = LOWORD(lParam);
					progress->_maximum = HIWORD(lParam);
					setProgressPosition(hwnd, *progress, progress->_position);
					return previous;
				}
				case PBM_SETRANGE32:
				{
					const LRESULT previous = MAKELRESULT(progress->_minimum, progress->_maximum);
					progress->_minimum = static_cast<int>(wParam);
					progress->_maximum = static_cast<int>(lParam);
					setProgressPosition(hwnd, *progress, progress->_position);
					return previous;
				}
				case PBM_GETRANGE:
				{
					if (auto* range = reinterpret_cast<PBRANGE*>(lParam))
					{
						range->iLow = progress->_minimum;
						range->iHigh = progress->_maximum;
					}
					return wParam ? progress->_minimum : progress->_maximum;
				}
				case PBM_SETPOS:
					return setProgressPosition(hwnd, *progress, static_cast<int>(wParam));
				case PBM_GETPOS:
					return progress->_position;
				case PBM_DELTAPOS:
					return setProgressPosition(hwnd, *progress, progress->_position + static_cast<int>(wParam));
				case PBM_SETSTEP:
				{
					const int previous = progress->_step;
					progress->_step = static_cast<int>(wParam);
					return previous;
				}
				case PBM_STEPIT:
				{
					// Stepping past the end wraps to the start, as on Windows.
					int position = progress->_position + progress->_step;
					if (position > progress->_maximum)
						position = progress->_minimum + (position - progress->_maximum);
					return setProgressPosition(hwnd, *progress, position);
				}
				case PBM_SETBARCOLOR:
				{
					const COLORREF previous = progress->_barColor;
					progress->_barColor = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return static_cast<LRESULT>(previous);
				}
				case PBM_SETBKCOLOR:
				{
					const COLORREF previous = progress->_backgroundColor;
					progress->_backgroundColor = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return static_cast<LRESULT>(previous);
				}
				case PBM_SETSTATE:
				{
					const int previous = progress->_state;
					progress->_state = static_cast<int>(wParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return previous;
				}
				case PBM_GETSTATE:
					return progress->_state;
				case PBM_SETMARQUEE:
					return TRUE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}

		// --- Up-down -------------------------------------------------------------------------------

		struct UpDown
		{
			int _minimum = 100; // Windows' default range runs backwards
			int _maximum = 0;
			int _position = 0;
			HWND _buddy = nullptr;
			int _pressed = 0; // -1 down arrow, 1 up arrow
		};

		void updateBuddy(HWND hwnd, const UpDown& upDown)
		{
			if (upDown._buddy && (GetWindowLongW(hwnd, GWL_STYLE) & UDS_SETBUDDYINT))
				SetWindowTextW(upDown._buddy, nppmac::to_u16string(upDown._position).c_str());
		}

		// Docks the up-down beside its buddy (UDS_ALIGNRIGHT / UDS_ALIGNLEFT).
		void alignToBuddy(HWND hwnd, const UpDown& upDown)
		{
			const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
			if (!upDown._buddy || !(style & (UDS_ALIGNRIGHT | UDS_ALIGNLEFT)))
				return;
			RECT buddy{};
			GetWindowRect(upDown._buddy, &buddy);
			MapWindowPoints(nullptr, GetParent(hwnd), reinterpret_cast<POINT*>(&buddy), 2);
			RECT own{};
			GetWindowRect(hwnd, &own);
			const int width = own.right - own.left > 0 ? own.right - own.left : 16;
			const int x = (style & UDS_ALIGNRIGHT) ? buddy.right : buddy.left - width;
			SetWindowPos(hwnd, nullptr, x, buddy.top, width, buddy.bottom - buddy.top, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		void step(HWND hwnd, UpDown& upDown, int delta)
		{
			// The up arrow moves towards the maximum, whichever way the range runs.
			const int direction = upDown._maximum >= upDown._minimum ? 1 : -1;
			NMUPDOWN change{};
			change.iPos = upDown._position;
			change.iDelta = delta * direction;
			if (sendNotify(hwnd, GetParent(hwnd), UDN_DELTAPOS, &change.hdr))
				return;
			const int low = std::min(upDown._minimum, upDown._maximum);
			const int high = std::max(upDown._minimum, upDown._maximum);
			int position = upDown._position + change.iDelta;
			if (position < low || position > high)
				position = (GetWindowLongW(hwnd, GWL_STYLE) & UDS_WRAP) ? (position < low ? high : low) : std::clamp(position, low, high);
			upDown._position = position;
			updateBuddy(hwnd, upDown);
			const UINT message = (GetWindowLongW(hwnd, GWL_STYLE) & UDS_HORZ) ? WM_HSCROLL : WM_VSCROLL;
			SendMessageW(GetParent(hwnd), message, MAKEWPARAM(SB_THUMBPOSITION, position), reinterpret_cast<LPARAM>(hwnd));
			SendMessageW(GetParent(hwnd), message, MAKEWPARAM(SB_ENDSCROLL, position), reinterpret_cast<LPARAM>(hwnd));
		}

		void paintUpDown(HWND hwnd, const UpDown& upDown, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const int middle = (client.top + client.bottom) / 2;
			const RECT halves[2] = {{client.left, client.top, client.right, middle}, {client.left, middle, client.right, client.bottom}};
			for (int i = 0; i < 2; ++i)
			{
				const bool pressed = upDown._pressed == (i == 0 ? 1 : -1);
				HBRUSH fill = CreateSolidBrush(pressed ? controlPalette()._facePressed : controlPalette()._face);
				HBRUSH frame = CreateSolidBrush(controlPalette()._frame);
				FillRect(hdc, &halves[i], fill);
				FrameRect(hdc, &halves[i], frame);
				DeleteObject(fill);
				DeleteObject(frame);
				const int x = (halves[i].left + halves[i].right) / 2;
				const int y = (halves[i].top + halves[i].bottom) / 2;
				const POINT up[] = {{x - 3, y + 1}, {x + 4, y + 1}, {x, y - 3}};
				const POINT down[] = {{x - 3, y - 1}, {x + 4, y - 1}, {x, y + 3}};
				HBRUSH arrowBrush = CreateSolidBrush(controlPalette()._text);
				HGDIOBJ oldBrush = SelectObject(hdc, arrowBrush);
				HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
				Polygon(hdc, i == 0 ? up : down, 3);
				SelectObject(hdc, oldPen);
				SelectObject(hdc, oldBrush);
				DeleteObject(arrowBrush);
			}
		}

		LRESULT CALLBACK upDownProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			UpDown* upDown = dataOf<UpDown>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<UpDown>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_CREATE:
				{
					// UDS_AUTOBUDDY: the previous window in the z-order is the buddy.
					if (GetWindowLongW(hwnd, GWL_STYLE) & UDS_AUTOBUDDY)
					{
						upDown->_buddy = GetWindow(hwnd, GW_HWNDPREV);
						alignToBuddy(hwnd, *upDown);
					}
					return 0;
				}
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paintUpDown(hwnd, *upDown, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_ERASEBKGND:
					return 1;
				case WM_LBUTTONDOWN:
				{
					RECT client{};
					GetClientRect(hwnd, &client);
					upDown->_pressed = GET_Y_LPARAM(lParam) < (client.top + client.bottom) / 2 ? 1 : -1;
					SetCapture(hwnd);
					InvalidateRect(hwnd, nullptr, FALSE);
					step(hwnd, *upDown, upDown->_pressed);
					return 0;
				}
				case WM_LBUTTONUP:
				case WM_CAPTURECHANGED:
					if (upDown->_pressed)
					{
						upDown->_pressed = 0;
						if (message == WM_LBUTTONUP)
							ReleaseCapture();
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				case UDM_SETRANGE:
					upDown->_minimum = static_cast<short>(HIWORD(lParam));
					upDown->_maximum = static_cast<short>(LOWORD(lParam));
					return 0;
				case UDM_SETRANGE32:
					upDown->_minimum = static_cast<int>(wParam);
					upDown->_maximum = static_cast<int>(lParam);
					return 0;
				case UDM_GETRANGE:
					return MAKELRESULT(upDown->_maximum, upDown->_minimum);
				case UDM_GETRANGE32:
					if (wParam)
						*reinterpret_cast<int*>(wParam) = upDown->_minimum;
					if (lParam)
						*reinterpret_cast<int*>(lParam) = upDown->_maximum;
					return 0;
				case UDM_SETPOS:
				case UDM_SETPOS32:
				{
					const int previous = upDown->_position;
					upDown->_position = message == UDM_SETPOS ? static_cast<short>(LOWORD(lParam)) : static_cast<int>(lParam);
					updateBuddy(hwnd, *upDown);
					return previous;
				}
				case UDM_GETPOS:
				case UDM_GETPOS32:
				{
					// With a buddy, the position is the number the buddy shows.
					if (upDown->_buddy && (GetWindowLongW(hwnd, GWL_STYLE) & UDS_SETBUDDYINT))
					{
						const std::u16string text = windowText(upDown->_buddy);
						if (!text.empty())
							upDown->_position = static_cast<int>(std::strtol(nppmac::toUtf8(text).c_str(), nullptr, 10));
					}
					if (message == UDM_GETPOS32)
					{
						if (lParam)
							*reinterpret_cast<BOOL*>(lParam) = FALSE;
						return upDown->_position;
					}
					return MAKELRESULT(upDown->_position, 0);
				}
				case UDM_SETBUDDY:
				{
					HWND previous = upDown->_buddy;
					upDown->_buddy = reinterpret_cast<HWND>(wParam);
					alignToBuddy(hwnd, *upDown);
					return reinterpret_cast<LRESULT>(previous);
				}
				case UDM_GETBUDDY:
					return reinterpret_cast<LRESULT>(upDown->_buddy);
				case UDM_SETACCEL:
				case UDM_SETBASE:
					return TRUE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}

		// --- Link ----------------------------------------------------------------------------------

		// A SysLink's text marks links with <a href="...">...</a>; one link per control is what
		// Notepad++ uses, and what is supported.
		struct Link
		{
			std::u16string _shown;   // the text without the markup
			std::u16string _url;
			std::u16string _id;
			size_t _linkStart = 0;
			size_t _linkLength = 0;
			bool _focused = false;
		};

		void parseLink(Link& link, const std::u16string& markup)
		{
			link = Link{};
			const size_t open = markup.find(u"<a");
			const size_t openEnd = open == std::u16string::npos ? open : markup.find(u'>', open);
			const size_t close = openEnd == std::u16string::npos ? openEnd : markup.find(u"</a>", openEnd);
			if (close == std::u16string::npos)
			{
				link._shown = markup;
				return;
			}
			const std::u16string attributes = markup.substr(open + 2, openEnd - open - 2);
			auto attribute = [&attributes](std::u16string_view name) -> std::u16string
			{
				const size_t at = attributes.find(name);
				if (at == std::u16string::npos)
					return {};
				const size_t quote = attributes.find(u'"', at);
				const size_t end = quote == std::u16string::npos ? quote : attributes.find(u'"', quote + 1);
				return end == std::u16string::npos ? std::u16string{} : attributes.substr(quote + 1, end - quote - 1);
			};
			link._url = attribute(u"href=");
			link._id = attribute(u"id=");
			const std::u16string before = markup.substr(0, open);
			const std::u16string text = markup.substr(openEnd + 1, close - openEnd - 1);
			link._shown = before + text + markup.substr(close + 4);
			link._linkStart = before.size();
			link._linkLength = text.size();
			if (link._url.empty())
				link._url = text;
		}

		void paintLink(HWND hwnd, const Link& link, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			FillRect(hdc, &client, controlColors(hwnd, hdc, WM_CTLCOLORSTATIC));
			const int saved = SaveDC(hdc);
			SetBkMode(hdc, TRANSPARENT);
			// The link part is drawn underlined, in the link color.
			HFONT font = fontOf(hwnd);
			LOGFONTW logFont{};
			GetObjectW(font, sizeof(logFont), &logFont);
			logFont.lfUnderline = TRUE;
			HFONT underlined = CreateFontIndirectW(&logFont);
			int x = client.left;
			const auto drawPart = [&](size_t start, size_t length, bool isLink)
			{
				if (length == 0)
					return;
				SelectObject(hdc, isLink ? underlined : font);
				if (isLink)
					SetTextColor(hdc, IsWindowEnabled(hwnd) ? RGB(0, 102, 204) : GetSysColor(COLOR_GRAYTEXT));
				const std::u16string part = link._shown.substr(start, length);
				TextOutW(hdc, x, client.top, part.c_str(), static_cast<int>(part.size()));
				SIZE size{};
				GetTextExtentPoint32W(hdc, part.c_str(), static_cast<int>(part.size()), &size);
				x += size.cx;
			};
			const COLORREF textColor = GetTextColor(hdc);
			drawPart(0, link._linkStart, false);
			drawPart(link._linkStart, link._linkLength, true);
			SetTextColor(hdc, textColor);
			drawPart(link._linkStart + link._linkLength, link._shown.size() - link._linkStart - link._linkLength, false);
			RestoreDC(hdc, saved);
			DeleteObject(underlined);
		}

		void activateLink(HWND hwnd, const Link& link)
		{
			NMLINK notification{};
			notification.item.mask = LIF_ITEMINDEX | LIF_URL | LIF_ITEMID;
			lstrcpynW(notification.item.szUrl, link._url.c_str(), L_MAX_URL_LENGTH);
			lstrcpynW(notification.item.szID, link._id.c_str(), MAX_LINKID_TEXT);
			sendNotify(hwnd, GetParent(hwnd), NM_CLICK, &notification.hdr);
		}

		LRESULT CALLBACK linkProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Link* link = dataOf<Link>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					Link* created = createData<Link>(hwnd);
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					if (create->lpszName && !IS_INTRESOURCE(create->lpszName))
						parseLink(*created, create->lpszName);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_SETTEXT:
					DefWindowProcW(hwnd, message, wParam, lParam);
					parseLink(*link, lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"");
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paintLink(hwnd, *link, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paintLink(hwnd, *link, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SETCURSOR:
					SetCursor(LoadCursorW(nullptr, IDC_HAND));
					return TRUE;
				case WM_GETDLGCODE:
					return DLGC_WANTCHARS;
				case WM_LBUTTONUP:
					activateLink(hwnd, *link);
					return 0;
				case WM_KEYDOWN:
					if (wParam == VK_RETURN || wParam == VK_SPACE)
						activateLink(hwnd, *link);
					return 0;
				case LM_GETIDEALHEIGHT: // also LM_GETIDEALSIZE, which passes a SIZE to fill
				{
					auto* size = reinterpret_cast<SIZE*>(lParam);
					if (size)
						*size = {textWidth(hwnd, link->_shown), fontHeight(hwnd)};
					return fontHeight(hwnd);
				}
				case LM_GETITEM:
				case LM_SETITEM:
				case LM_HITTEST:
					return FALSE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerSmallControls()
	{
		registerControl(TRACKBAR_CLASSW, trackbarProc);
		registerControl(PROGRESS_CLASSW, progressProc);
		registerControl(UPDOWN_CLASSW, upDownProc);
		registerControl(WC_LINK, linkProc);
	}
}
