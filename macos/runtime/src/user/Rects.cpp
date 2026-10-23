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

// user32 rectangle and point helpers.

#include <windows.h>

#include <algorithm>

BOOL WINAPI SetRect(LPRECT rect, int left, int top, int right, int bottom)
{
	if (!rect)
		return FALSE;
	*rect = {left, top, right, bottom};
	return TRUE;
}

BOOL WINAPI SetRectEmpty(LPRECT rect)
{
	if (!rect)
		return FALSE;
	*rect = {0, 0, 0, 0};
	return TRUE;
}

BOOL WINAPI CopyRect(LPRECT destination, const RECT* source)
{
	if (!destination || !source)
		return FALSE;
	*destination = *source;
	return TRUE;
}

BOOL WINAPI IsRectEmpty(const RECT* rect)
{
	return !rect || rect->left >= rect->right || rect->top >= rect->bottom;
}

BOOL WINAPI EqualRect(const RECT* a, const RECT* b)
{
	return a && b && a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

BOOL WINAPI PtInRect(const RECT* rect, POINT point)
{
	return rect && point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom;
}

BOOL WINAPI OffsetRect(LPRECT rect, int dx, int dy)
{
	if (!rect)
		return FALSE;
	rect->left += dx;
	rect->right += dx;
	rect->top += dy;
	rect->bottom += dy;
	return TRUE;
}

BOOL WINAPI InflateRect(LPRECT rect, int dx, int dy)
{
	if (!rect)
		return FALSE;
	rect->left -= dx;
	rect->right += dx;
	rect->top -= dy;
	rect->bottom += dy;
	return TRUE;
}

BOOL WINAPI IntersectRect(LPRECT destination, const RECT* a, const RECT* b)
{
	if (!destination || !a || !b)
		return FALSE;
	if (IsRectEmpty(a) || IsRectEmpty(b) || a->left >= b->right || b->left >= a->right || a->top >= b->bottom || b->top >= a->bottom)
	{
		SetRectEmpty(destination);
		return FALSE;
	}
	*destination = {std::max(a->left, b->left), std::max(a->top, b->top), std::min(a->right, b->right), std::min(a->bottom, b->bottom)};
	return TRUE;
}

BOOL WINAPI UnionRect(LPRECT destination, const RECT* a, const RECT* b)
{
	if (!destination || !a || !b)
		return FALSE;
	if (IsRectEmpty(a))
	{
		if (IsRectEmpty(b))
		{
			SetRectEmpty(destination);
			return FALSE;
		}
		*destination = *b;
		return TRUE;
	}
	if (IsRectEmpty(b))
	{
		*destination = *a;
		return TRUE;
	}
	*destination = {std::min(a->left, b->left), std::min(a->top, b->top), std::max(a->right, b->right), std::max(a->bottom, b->bottom)};
	return TRUE;
}

BOOL WINAPI SubtractRect(LPRECT destination, const RECT* a, const RECT* b)
{
	// The result is a rectangle only when b covers a whole side of a; otherwise a is returned.
	if (!destination || !a || !b)
		return FALSE;
	RECT result = *a;
	RECT overlap{};
	if (IntersectRect(&overlap, a, b))
	{
		if (overlap.left == a->left && overlap.right == a->right)
		{
			if (overlap.top == a->top)
				result.top = overlap.bottom;
			else if (overlap.bottom == a->bottom)
				result.bottom = overlap.top;
		}
		else if (overlap.top == a->top && overlap.bottom == a->bottom)
		{
			if (overlap.left == a->left)
				result.left = overlap.right;
			else if (overlap.right == a->right)
				result.right = overlap.left;
		}
	}
	*destination = result;
	if (IsRectEmpty(&result))
	{
		SetRectEmpty(destination);
		return FALSE;
	}
	return TRUE;
}
