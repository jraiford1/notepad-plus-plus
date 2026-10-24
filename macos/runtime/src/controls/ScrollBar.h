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

// A vertical scroll bar that a control draws inside its own client area (the controls here have no
// non-client area). Positions are in the control's units: rows, lines or items.

#pragma once

#include "ControlSupport.h"

namespace nppmac::controls
{
	class VerticalScrollBar
	{
	public:
		static constexpr int width = 12;

		// total: how many units there are; page: how many fit; position: the first one shown.
		void update(const RECT& track, int total, int page, int position)
		{
			_track = track;
			_total = total;
			_page = std::max(1, page);
			_position = position;
		}

		int maximumPosition() const
		{
			return std::max(0, _total - _page);
		}

		bool isNeeded() const
		{
			return _total > _page;
		}

		RECT thumb() const
		{
			if (!isNeeded())
				return {};
			const int trackHeight = _track.bottom - _track.top;
			const int thumbHeight = std::max(minimumThumb, trackHeight * _page / std::max(1, _total));
			const int thumbTop = _track.top + (trackHeight - thumbHeight) * _position / std::max(1, maximumPosition());
			return {_track.left + 2, thumbTop, _track.right - 2, thumbTop + thumbHeight};
		}

		void paint(HDC hdc) const
		{
			HBRUSH trackBrush = CreateSolidBrush(RGB(240, 240, 240));
			FillRect(hdc, &_track, trackBrush);
			DeleteObject(trackBrush);
			const RECT thumbRect = thumb();
			if (IsRectEmpty(&thumbRect))
				return;
			HBRUSH thumbBrush = CreateSolidBrush(_dragging ? RGB(96, 96, 96) : RGB(194, 194, 194));
			FillRect(hdc, &thumbRect, thumbBrush);
			DeleteObject(thumbBrush);
		}

		bool contains(POINT point) const
		{
			return isNeeded() && PtInRect(&_track, point);
		}

		// A press on the track: grabs the thumb, or returns the position a page away.
		int press(POINT point)
		{
			const RECT thumbRect = thumb();
			if (PtInRect(&thumbRect, point))
			{
				_dragging = true;
				_grab = point.y - thumbRect.top;
				return _position;
			}
			const int page = std::max(1, _page - 1);
			return std::clamp(_position + (point.y < thumbRect.top ? -page : page), 0, maximumPosition());
		}

		bool isDragging() const
		{
			return _dragging;
		}

		// While the thumb is dragged: the position under the mouse.
		int drag(POINT point) const
		{
			const RECT thumbRect = thumb();
			const int free = (_track.bottom - _track.top) - (thumbRect.bottom - thumbRect.top);
			if (free <= 0)
				return _position;
			return std::clamp((point.y - _grab - _track.top) * maximumPosition() / free, 0, maximumPosition());
		}

		void release()
		{
			_dragging = false;
		}

	private:
		static constexpr int minimumThumb = 16;

		RECT _track{};
		int _total = 0;
		int _page = 1;
		int _position = 0;
		bool _dragging = false;
		int _grab = 0;
	};
}
