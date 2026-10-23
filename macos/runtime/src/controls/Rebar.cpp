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

// The rebar control (ReBarWindow32): bands laid out in one row, each hosting a child window.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		constexpr int bandGap = 4;
		constexpr int gripperWidth = 8;

		struct Band
		{
			UINT _style = 0;
			HWND _child = nullptr;
			UINT _childMinWidth = 0;
			UINT _childMinHeight = 0;
			UINT _childHeight = 0;
			UINT _width = 0;
			UINT _idealWidth = 0;
			UINT _id = 0;
			LPARAM _data = 0;
			std::u16string _text;
			COLORREF _foreground = CLR_DEFAULT;
			COLORREF _background = CLR_DEFAULT;
		};

		struct Rebar
		{
			std::vector<Band> _bands;
			int _height = 0;
		};

		bool visible(const Band& band)
		{
			return !(band._style & RBBS_HIDDEN);
		}

		int rowHeight(const Rebar& rebar)
		{
			int height = 0;
			for (const Band& band : rebar._bands)
			{
				if (visible(band))
					height = std::max<int>(height, static_cast<int>(std::max(band._childMinHeight, band._childHeight)));
			}
			return height > 0 ? height + 4 : 0;
		}

		// Places the bands left to right; the last one takes what is left of the row.
		void layout(HWND hwnd, Rebar& rebar)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const int height = rowHeight(rebar);
			int x = 0;
			std::vector<size_t> shown;
			for (size_t i = 0; i < rebar._bands.size(); ++i)
			{
				if (visible(rebar._bands[i]))
					shown.push_back(i);
				else if (rebar._bands[i]._child)
					ShowWindow(rebar._bands[i]._child, SW_HIDE);
			}
			for (size_t n = 0; n < shown.size(); ++n)
			{
				Band& band = rebar._bands[shown[n]];
				const int gripper = (band._style & RBBS_NOGRIPPER) ? 0 : gripperWidth;
				int width = static_cast<int>(std::max({band._width, band._childMinWidth, band._idealWidth})) + gripper;
				if (n + 1 == shown.size())
					width = std::max(width, static_cast<int>(client.right) - x);
				if (band._child)
				{
					const int childHeight = static_cast<int>(std::max(band._childMinHeight, band._childHeight));
					SetWindowPos(band._child, nullptr, x + gripper, (height - childHeight) / 2, std::max(0, width - gripper), childHeight,
						SWP_NOZORDER | SWP_NOACTIVATE);
					ShowWindow(band._child, SW_SHOWNA);
				}
				x += width + bandGap;
			}

			// The rebar docks at the top of its parent and is as high as its row.
			if (height != rebar._height)
			{
				rebar._height = height;
				const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
				if (!(style & CCS_NORESIZE))
				{
					RECT window{};
					GetWindowRect(hwnd, &window);
					SetWindowPos(hwnd, nullptr, 0, 0, window.right - window.left, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
				}
				NMHDR header{};
				sendNotify(hwnd, GetParent(hwnd), RBN_HEIGHTCHANGE, &header);
			}
		}

		void applyInfo(HWND hwnd, Band& band, const REBARBANDINFOW& info)
		{
			if (info.fMask & RBBIM_STYLE)
				band._style = info.fStyle;
			if (info.fMask & RBBIM_CHILD)
			{
				band._child = info.hwndChild;
				// The child becomes the rebar's; the toolbar keeps notifying its original parent.
				if (band._child)
				{
					char16_t className[64]{};
					GetClassNameW(band._child, className, 64);
					if (_wcsicmp(className, TOOLBARCLASSNAMEW) == 0)
						SendMessageW(band._child, TB_SETPARENT, reinterpret_cast<WPARAM>(GetParent(band._child)), 0);
					SetParent(band._child, hwnd);
				}
			}
			if (info.fMask & RBBIM_CHILDSIZE)
			{
				band._childMinWidth = info.cxMinChild;
				band._childMinHeight = info.cyMinChild;
				if (info.cbSize >= REBARBANDINFOW_V6_SIZE)
					band._childHeight = info.cyChild;
			}
			if (info.fMask & RBBIM_SIZE)
				band._width = info.cx;
			if (info.fMask & RBBIM_IDEALSIZE)
				band._idealWidth = info.cxIdeal;
			if (info.fMask & RBBIM_ID)
				band._id = info.wID;
			if (info.fMask & RBBIM_LPARAM)
				band._data = info.lParam;
			if ((info.fMask & RBBIM_TEXT) && info.lpText)
				band._text = info.lpText;
			if (info.fMask & RBBIM_COLORS)
			{
				band._foreground = info.clrFore;
				band._background = info.clrBack;
			}
		}

		void readInfo(const Band& band, REBARBANDINFOW& info)
		{
			if (info.fMask & RBBIM_STYLE)
				info.fStyle = band._style;
			if (info.fMask & RBBIM_CHILD)
				info.hwndChild = band._child;
			if (info.fMask & RBBIM_CHILDSIZE)
			{
				info.cxMinChild = band._childMinWidth;
				info.cyMinChild = band._childMinHeight;
				if (info.cbSize >= REBARBANDINFOW_V6_SIZE)
					info.cyChild = band._childHeight;
			}
			if (info.fMask & RBBIM_SIZE)
				info.cx = band._width;
			if (info.fMask & RBBIM_IDEALSIZE)
				info.cxIdeal = band._idealWidth;
			if (info.fMask & RBBIM_ID)
				info.wID = band._id;
			if (info.fMask & RBBIM_LPARAM)
				info.lParam = band._data;
			if ((info.fMask & RBBIM_TEXT) && info.lpText && info.cch > 0)
				lstrcpynW(info.lpText, band._text.c_str(), static_cast<int>(info.cch));
			if (info.fMask & RBBIM_COLORS)
			{
				info.clrFore = band._foreground;
				info.clrBack = band._background;
			}
		}

		LRESULT CALLBACK rebarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Rebar* rebar = dataOf<Rebar>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Rebar>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_SIZE:
					if (rebar)
						layout(hwnd, *rebar);
					return 0;
				case WM_WINDOWPOSCHANGING:
				{
					// A rebar is always as high as its bands, whatever height it is moved with.
					auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
					if (rebar && !(position->flags & SWP_NOSIZE) && !(GetWindowLongW(hwnd, GWL_STYLE) & CCS_NORESIZE))
						position->cy = rowHeight(*rebar);
					return 0;
				}
				case WM_PRINTCLIENT:
				{
					RECT client{};
					GetClientRect(hwnd, &client);
					FillRect(reinterpret_cast<HDC>(wParam), &client, GetSysColorBrush(COLOR_BTNFACE));
					return 0;
				}
				case WM_PAINT:
				{
					PAINTSTRUCT paint{};
					HDC hdc = BeginPaint(hwnd, &paint);
					RECT client{};
					GetClientRect(hwnd, &client);
					// The parent may paint the background (Notepad++'s dark mode does, through WM_ERASEBKGND).
					FillRect(hdc, &client, GetSysColorBrush(COLOR_BTNFACE));
					EndPaint(hwnd, &paint);
					return 0;
				}
				case WM_NOTIFY:
				case WM_COMMAND:
					// Children's notifications go on to the rebar's parent.
					return SendMessageW(GetParent(hwnd), message, wParam, lParam);
				case RB_SETBARINFO:
					return TRUE;
				case RB_GETBARINFO:
					return TRUE;
				case RB_INSERTBANDW:
				{
					const auto* info = reinterpret_cast<const REBARBANDINFOW*>(lParam);
					Band band;
					applyInfo(hwnd, band, *info);
					const size_t index = static_cast<int>(wParam) < 0 ? rebar->_bands.size() : std::min<size_t>(wParam, rebar->_bands.size());
					rebar->_bands.insert(rebar->_bands.begin() + static_cast<std::ptrdiff_t>(index), band);
					layout(hwnd, *rebar);
					return TRUE;
				}
				case RB_SETBANDINFOW:
				{
					if (wParam >= rebar->_bands.size())
						return FALSE;
					applyInfo(hwnd, rebar->_bands[wParam], *reinterpret_cast<const REBARBANDINFOW*>(lParam));
					layout(hwnd, *rebar);
					return TRUE;
				}
				case RB_GETBANDINFOW:
				{
					if (wParam >= rebar->_bands.size())
						return FALSE;
					readInfo(rebar->_bands[wParam], *reinterpret_cast<REBARBANDINFOW*>(lParam));
					return TRUE;
				}
				case RB_DELETEBAND:
				{
					if (wParam >= rebar->_bands.size())
						return FALSE;
					if (HWND child = rebar->_bands[wParam]._child)
						ShowWindow(child, SW_HIDE);
					rebar->_bands.erase(rebar->_bands.begin() + static_cast<std::ptrdiff_t>(wParam));
					layout(hwnd, *rebar);
					return TRUE;
				}
				case RB_SHOWBAND:
				{
					if (wParam >= rebar->_bands.size())
						return FALSE;
					if (lParam)
						rebar->_bands[wParam]._style &= ~RBBS_HIDDEN;
					else
						rebar->_bands[wParam]._style |= RBBS_HIDDEN;
					layout(hwnd, *rebar);
					return TRUE;
				}
				case RB_IDTOINDEX:
				{
					for (size_t i = 0; i < rebar->_bands.size(); ++i)
					{
						if (rebar->_bands[i]._id == wParam)
							return static_cast<LRESULT>(i);
					}
					return -1;
				}
				case RB_GETBANDCOUNT:
					return static_cast<LRESULT>(rebar->_bands.size());
				case RB_GETROWCOUNT:
					return rebar->_bands.empty() ? 0 : 1;
				case RB_GETBARHEIGHT:
				case RB_GETROWHEIGHT:
					return rowHeight(*rebar);
				case RB_GETRECT:
				{
					if (wParam >= rebar->_bands.size())
						return FALSE;
					RECT rect{};
					if (HWND child = rebar->_bands[wParam]._child)
					{
						GetWindowRect(child, &rect);
						MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rect), 2);
					}
					*reinterpret_cast<RECT*>(lParam) = rect;
					return TRUE;
				}
				case RB_MAXIMIZEBAND:
				case RB_MINIMIZEBAND:
					layout(hwnd, *rebar);
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerRebar()
	{
		registerControl(REBARCLASSNAMEW, rebarProc);
	}
}
