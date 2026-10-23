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

// The tooltip control (tooltips_class32). Tools are recorded and every request succeeds; the tips
// themselves are not shown yet.

#include "ControlSupport.h"

namespace nppmac::controls
{
	namespace
	{
		struct Tooltip
		{
			std::vector<TTTOOLINFOW> _tools;
			int _maxWidth = -1;
		};

		LRESULT CALLBACK tooltipProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Tooltip* tooltip = dataOf<Tooltip>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
					createData<Tooltip>(hwnd);
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case TTM_ADDTOOLW:
				{
					TTTOOLINFOW tool = *reinterpret_cast<const TTTOOLINFOW*>(lParam);
					tool.lpszText = nullptr;
					tooltip->_tools.push_back(tool);
					return TRUE;
				}
				case TTM_DELTOOLW:
				{
					const auto* info = reinterpret_cast<const TTTOOLINFOW*>(lParam);
					auto& tools = tooltip->_tools;
					tools.erase(std::remove_if(tools.begin(), tools.end(), [info](const TTTOOLINFOW& tool)
					{
						return tool.hwnd == info->hwnd && tool.uId == info->uId;
					}), tools.end());
					return 0;
				}
				case TTM_GETTOOLCOUNT:
					return static_cast<LRESULT>(tooltip->_tools.size());
				case TTM_SETMAXTIPWIDTH:
				{
					const int previous = tooltip->_maxWidth;
					tooltip->_maxWidth = static_cast<int>(lParam);
					return previous;
				}
				case TTM_GETMAXTIPWIDTH:
					return tooltip->_maxWidth;
				case TTM_SETDELAYTIME:
				case TTM_ACTIVATE:
				case TTM_TRACKACTIVATE:
				case TTM_TRACKPOSITION:
				case TTM_POPUP:
				case TTM_POP:
				case TTM_RELAYEVENT:
				case TTM_UPDATETIPTEXTW:
				case TTM_NEWTOOLRECTW:
				case TTM_SETTITLEW:
				case TTM_SETTIPBKCOLOR:
				case TTM_SETTIPTEXTCOLOR:
				case TTM_SETMARGIN:
				case TTM_UPDATE:
					return 0;
				case TTM_GETTOOLINFOW:
				case TTM_GETCURRENTTOOLW:
					return FALSE;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerTooltip()
	{
		registerControl(TOOLTIPS_CLASSW, tooltipProc, 0);
	}
}
