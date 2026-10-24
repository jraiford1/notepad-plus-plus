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

// GDI device contexts: window DCs over view surfaces, memory DCs over bitmaps, attributes, clipping,
// coordinate origins, device capabilities; BeginPaint and EndPaint.

#include "Immortal.h"
#include "Gdi.h"
#include "user/Window.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>

namespace nppmac::gdi
{
	namespace
	{
		nppmac::Immortal<std::mutex> g_dcsMutexStorage;
		std::mutex& g_dcsMutex = g_dcsMutexStorage._value;

		std::unordered_set<const DC*>& liveDCs()
		{
			static auto* set = new std::unordered_set<const DC*>;
			return *set;
		}

		DC* createDC(DCKind kind)
		{
			auto* dc = new DC;
			dc->_kind = kind;
			dc->_state._pen = static_cast<HPEN>(GetStockObject(BLACK_PEN));
			dc->_state._brush = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
			dc->_state._font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
			for (HGDIOBJ object : {static_cast<HGDIOBJ>(dc->_state._pen), static_cast<HGDIOBJ>(dc->_state._brush), static_cast<HGDIOBJ>(dc->_state._font)})
				++toObject(object)->_selectedCount;
			std::lock_guard lock{g_dcsMutex};
			liveDCs().insert(dc);
			return dc;
		}

		void deselect(HGDIOBJ handle)
		{
			GdiObject* object = toObject(handle);
			if (!object || object->_selectedCount == 0)
				return;
			if (--object->_selectedCount == 0 && object->_deletePending)
			{
				object->_deletePending = false;
				DeleteObject(handle);
			}
		}

		void destroyDC(DC* dc)
		{
			if (dc->_gstateSaved)
				CGContextRestoreGState(dc->_context);
			auto release = [](const DCState& state)
			{
				deselect(state._pen);
				deselect(state._brush);
				deselect(state._font);
			};
			release(dc->_state);
			for (const DCState& state : dc->_saved)
				release(state);
			if (dc->_bitmap)
				deselect(dc->_bitmap);
			{
				std::lock_guard lock{g_dcsMutex};
				liveDCs().erase(dc);
			}
			delete dc;
		}

		void select(HGDIOBJ& slot, HGDIOBJ object)
		{
			++toObject(object)->_selectedCount;
			HGDIOBJ previous = slot;
			slot = object;
			deselect(previous);
		}

		POINT logicalToDeviceOffset(const DCState& state)
		{
			return {state._viewportOrigin.x - state._windowOrigin.x, state._viewportOrigin.y - state._windowOrigin.y};
		}
	}

	DC* toDC(HDC hdc)
	{
		auto* dc = reinterpret_cast<DC*>(hdc);
		std::lock_guard lock{g_dcsMutex};
		return liveDCs().count(dc) ? dc : nullptr;
	}

	HDC fromDC(DC* dc)
	{
		return reinterpret_cast<HDC>(dc);
	}

	CGContextRef DC::begin()
	{
		if (_kind == DCKind::memory && !_context)
		{
			auto* bitmap = objectAs<Bitmap>(_bitmap, ObjectType::bitmap);
			_context = bitmap ? bitmap->context() : nullptr;
		}
		if (!_context)
			return nullptr;
		CGContextSaveGState(_context);

		// Clip in device coordinates, before the logical offset.
		std::vector<CGRect> clip;
		const RECT bounds{0, 0, _width, _height};
		auto addClip = [&](const RECT& rect)
		{
			RECT area = rect;
			if (_hasPaintClip)
				IntersectRect(&area, &area, &_paintClip);
			if (_width > 0 && _height > 0)
				IntersectRect(&area, &area, &bounds);
			if (!IsRectEmpty(&area))
				clip.push_back(toCGRect(area));
		};
		if (_state._clipped)
		{
			for (const RECT& rect : _state._clip)
				addClip(rect);
			if (clip.empty())
				clip.push_back(CGRectZero);
		}
		else if (_hasPaintClip)
		{
			addClip(_paintClip);
			if (clip.empty())
				clip.push_back(CGRectZero);
		}
		if (!clip.empty())
			CGContextClipToRects(_context, clip.data(), clip.size());

		const POINT offset = logicalToDeviceOffset(_state);
		CGContextTranslateCTM(_context, offset.x, offset.y);
		CGContextSetShouldAntialias(_context, false);
		return _context;
	}

	void DC::end()
	{
		if (_context)
			CGContextRestoreGState(_context);
	}

	HDC createWindowDC(user::Window* window)
	{
		DC* dc = createDC(DCKind::window);
		dc->_window = window;
		dc->_context = window && window->_view && !window->_nativeControl ? static_cast<CGContextRef>(ui::surfaceContext(window->_view)) : nullptr;
		dc->_width = window ? window->_clientWidth : 0;
		dc->_height = window ? window->_clientHeight : 0;
		if (window && window->_font)
			SelectObject(fromDC(dc), window->_font);
		return fromDC(dc);
	}

	void releaseWindowDC(HDC hdc)
	{
		DC* dc = toDC(hdc);
		if (!dc)
			return;
		// What was drawn outside WM_PAINT reaches the screen with the next display pass.
		if (dc->_kind == DCKind::window && dc->_window && user::fromHandle(dc->_window->_hwnd) && dc->_window->_view && !dc->_window->_painting)
			ui::invalidate(dc->_window->_view, nullptr);
		destroyDC(dc);
	}
}

using namespace nppmac::gdi;
using nppmac::user::Window;

// --- Window DCs and painting ----------------------------------------------------------------------

HDC WINAPI GetDC(HWND hwnd)
{
	if (!hwnd)
	{
		// The screen DC: measurement only.
		return CreateCompatibleDC(nullptr);
	}
	Window* window = nppmac::user::fromHandle(hwnd);
	return window ? createWindowDC(window) : nullptr;
}

HDC WINAPI GetDCEx(HWND hwnd, HRGN clip, DWORD flags)
{
	HDC hdc = GetDC(hwnd);
	if (hdc && clip && (flags & DCX_INTERSECTRGN))
		SelectClipRgn(hdc, clip);
	return hdc;
}

HDC WINAPI GetWindowDC(HWND hwnd)
{
	return GetDC(hwnd);
}

int WINAPI ReleaseDC(HWND, HDC hdc)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return 0;
	if (dc->_kind == DCKind::window)
		releaseWindowDC(hdc);
	else
		DeleteDC(hdc);
	return 1;
}

HDC WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT paint)
{
	Window* window = nppmac::user::fromHandle(hwnd);
	if (!window)
		return nullptr;
	std::memset(paint, 0, sizeof(*paint));
	HDC hdc = createWindowDC(window);
	DC* dc = toDC(hdc);
	window->_painting = true;
	// The update region never extends past the client area (it may have shrunk since it was invalidated).
	RECT update = window->_invalid;
	const RECT client{0, 0, window->_clientWidth, window->_clientHeight};
	if (!IntersectRect(&update, &update, &client))
		update = {0, 0, 0, 0};
	dc->_paintClip = update;
	dc->_hasPaintClip = true;
	paint->hdc = hdc;
	paint->rcPaint = update;
	const bool erase = window->_eraseBackground;
	nppmac::user::validate(window, nullptr);
	paint->fErase = TRUE;
	if (erase && !IsRectEmpty(&update))
		paint->fErase = window->send(WM_ERASEBKGND, reinterpret_cast<WPARAM>(hdc), 0) ? FALSE : TRUE;
	return hdc;
}

BOOL WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT* paint)
{
	Window* window = nppmac::user::fromHandle(hwnd);
	if (window)
		window->_painting = false;
	DC* dc = toDC(paint->hdc);
	if (dc)
		destroyDC(dc);
	return TRUE;
}

HDC nppmac::gdi::createPrinterDC(const platform::PrintSetup& setup)
{
	DC* dc = createDC(DCKind::printer);
	dc->_print = std::make_shared<PrintDocument>();
	dc->_print->_setup = setup;
	dc->_width = static_cast<int>(std::lround(setup._printableWidth * printerUnitsPerPoint));
	dc->_height = static_cast<int>(std::lround(setup._printableHeight * printerUnitsPerPoint));
	return fromDC(dc);
}

// --- Memory DCs -----------------------------------------------------------------------------------

HDC WINAPI CreateCompatibleDC(HDC)
{
	DC* dc = createDC(DCKind::memory);
	dc->_bitmap = static_cast<HBITMAP>(GetStockObject(STOCK_LAST));
	++toObject(dc->_bitmap)->_selectedCount;
	dc->_width = 1;
	dc->_height = 1;
	return fromDC(dc);
}

HDC WINAPI CreateDCW(LPCWSTR, LPCWSTR, LPCWSTR, const DEVMODEW*)
{
	return CreateCompatibleDC(nullptr);
}

HDC WINAPI CreateICW(LPCWSTR driver, LPCWSTR device, LPCWSTR output, const DEVMODEW* mode)
{
	return CreateDCW(driver, device, output, mode);
}

BOOL WINAPI DeleteDC(HDC hdc)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	destroyDC(dc);
	return TRUE;
}

// --- Selection ------------------------------------------------------------------------------------

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ handle)
{
	DC* dc = toDC(hdc);
	GdiObject* object = toObject(handle);
	if (!dc || !object)
		return nullptr;
	HGDIOBJ previous = nullptr;
	switch (object->_type)
	{
		case ObjectType::pen:
			previous = dc->_state._pen;
			++object->_selectedCount;
			dc->_state._pen = static_cast<HPEN>(handle);
			break;
		case ObjectType::brush:
			previous = dc->_state._brush;
			++object->_selectedCount;
			dc->_state._brush = static_cast<HBRUSH>(handle);
			break;
		case ObjectType::font:
			previous = dc->_state._font;
			++object->_selectedCount;
			dc->_state._font = static_cast<HFONT>(handle);
			break;
		case ObjectType::bitmap:
		{
			if (dc->_kind != DCKind::memory)
				return nullptr;
			previous = dc->_bitmap;
			++object->_selectedCount;
			dc->_bitmap = static_cast<HBITMAP>(handle);
			auto* bitmap = static_cast<Bitmap*>(object);
			dc->_context = nullptr; // taken from the bitmap at the next drawing
			dc->_width = bitmap->_width;
			dc->_height = bitmap->_height;
			break;
		}
		case ObjectType::region:
		{
			SelectClipRgn(hdc, static_cast<HRGN>(handle));
			return reinterpret_cast<HGDIOBJ>(static_cast<uintptr_t>(SIMPLEREGION));
		}
		default:
			return nullptr;
	}
	// The previous object stays alive for the caller to select back or delete.
	GdiObject* previousObject = toObject(previous);
	if (previousObject && previousObject->_selectedCount > 0)
	{
		--previousObject->_selectedCount;
		if (previousObject->_selectedCount == 0 && previousObject->_deletePending)
		{
			previousObject->_deletePending = false;
			DeleteObject(previous);
			return nullptr;
		}
	}
	return previous;
}

HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return nullptr;
	switch (type)
	{
		case OBJ_PEN:
			return dc->_state._pen;
		case OBJ_BRUSH:
			return dc->_state._brush;
		case OBJ_FONT:
			return dc->_state._font;
		case OBJ_BITMAP:
			return dc->_bitmap;
		default:
			return nullptr;
	}
}

// --- Attributes -----------------------------------------------------------------------------------

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return CLR_INVALID;
	const COLORREF previous = dc->_state._textColor;
	dc->_state._textColor = color & 0xFFFFFF;
	return previous;
}

COLORREF WINAPI GetTextColor(HDC hdc)
{
	DC* dc = toDC(hdc);
	return dc ? dc->_state._textColor : CLR_INVALID;
}

COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return CLR_INVALID;
	const COLORREF previous = dc->_state._backgroundColor;
	dc->_state._backgroundColor = color & 0xFFFFFF;
	return previous;
}

COLORREF WINAPI GetBkColor(HDC hdc)
{
	DC* dc = toDC(hdc);
	return dc ? dc->_state._backgroundColor : CLR_INVALID;
}

int WINAPI SetBkMode(HDC hdc, int mode)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return 0;
	const int previous = dc->_state._backgroundMode;
	dc->_state._backgroundMode = mode;
	return previous;
}

int WINAPI GetBkMode(HDC hdc)
{
	DC* dc = toDC(hdc);
	return dc ? dc->_state._backgroundMode : 0;
}

COLORREF WINAPI SetDCBrushColor(HDC hdc, COLORREF color)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return CLR_INVALID;
	const COLORREF previous = dc->_state._dcBrushColor;
	dc->_state._dcBrushColor = color & 0xFFFFFF;
	return previous;
}

COLORREF WINAPI GetDCBrushColor(HDC hdc)
{
	DC* dc = toDC(hdc);
	return dc ? dc->_state._dcBrushColor : CLR_INVALID;
}

COLORREF WINAPI SetDCPenColor(HDC hdc, COLORREF color)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return CLR_INVALID;
	const COLORREF previous = dc->_state._dcPenColor;
	dc->_state._dcPenColor = color & 0xFFFFFF;
	return previous;
}

UINT WINAPI SetTextAlign(HDC hdc, UINT align)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return GDI_ERROR;
	const UINT previous = dc->_state._textAlign;
	dc->_state._textAlign = align;
	return previous;
}

UINT WINAPI GetTextAlign(HDC hdc)
{
	DC* dc = toDC(hdc);
	return dc ? dc->_state._textAlign : GDI_ERROR;
}

int WINAPI SetROP2(HDC hdc, int mode)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return 0;
	const int previous = dc->_state._rop2;
	dc->_state._rop2 = mode;
	return previous;
}

int WINAPI GetROP2(HDC hdc)
{
	DC* dc = toDC(hdc);
	return dc ? dc->_state._rop2 : 0;
}

int WINAPI SetStretchBltMode(HDC hdc, int mode)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return 0;
	const int previous = dc->_state._stretchMode;
	dc->_state._stretchMode = mode;
	return previous;
}

int WINAPI SetMapMode(HDC, int)
{
	return MM_TEXT;
}

int WINAPI GetMapMode(HDC)
{
	return MM_TEXT;
}

BOOL WINAPI SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT previous)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (previous)
		*previous = dc->_state._brushOrigin;
	dc->_state._brushOrigin = {x, y};
	return TRUE;
}

int WINAPI SetGraphicsMode(HDC, int)
{
	return GM_COMPATIBLE;
}

int WINAPI SetPolyFillMode(HDC, int)
{
	return ALTERNATE;
}

UINT WINAPI SetBoundsRect(HDC, const RECT*, UINT)
{
	return DCB_DISABLE;
}

// --- Coordinates ----------------------------------------------------------------------------------

BOOL WINAPI SetWindowOrgEx(HDC hdc, int x, int y, LPPOINT previous)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (previous)
		*previous = dc->_state._windowOrigin;
	dc->_state._windowOrigin = {x, y};
	return TRUE;
}

BOOL WINAPI OffsetWindowOrgEx(HDC hdc, int dx, int dy, LPPOINT previous)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (previous)
		*previous = dc->_state._windowOrigin;
	dc->_state._windowOrigin.x += dx;
	dc->_state._windowOrigin.y += dy;
	return TRUE;
}

BOOL WINAPI GetWindowOrgEx(HDC hdc, LPPOINT origin)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	*origin = dc->_state._windowOrigin;
	return TRUE;
}

BOOL WINAPI SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT previous)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (previous)
		*previous = dc->_state._viewportOrigin;
	dc->_state._viewportOrigin = {x, y};
	return TRUE;
}

BOOL WINAPI OffsetViewportOrgEx(HDC hdc, int dx, int dy, LPPOINT previous)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (previous)
		*previous = dc->_state._viewportOrigin;
	dc->_state._viewportOrigin.x += dx;
	dc->_state._viewportOrigin.y += dy;
	return TRUE;
}

BOOL WINAPI GetViewportOrgEx(HDC hdc, LPPOINT origin)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	*origin = dc->_state._viewportOrigin;
	return TRUE;
}

BOOL WINAPI DPtoLP(HDC hdc, LPPOINT points, int count)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const POINT offset{dc->_state._viewportOrigin.x - dc->_state._windowOrigin.x, dc->_state._viewportOrigin.y - dc->_state._windowOrigin.y};
	for (int i = 0; i < count; ++i)
	{
		points[i].x -= offset.x;
		points[i].y -= offset.y;
	}
	return TRUE;
}

BOOL WINAPI LPtoDP(HDC hdc, LPPOINT points, int count)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const POINT offset{dc->_state._viewportOrigin.x - dc->_state._windowOrigin.x, dc->_state._viewportOrigin.y - dc->_state._windowOrigin.y};
	for (int i = 0; i < count; ++i)
	{
		points[i].x += offset.x;
		points[i].y += offset.y;
	}
	return TRUE;
}

BOOL WINAPI GetCurrentPositionEx(HDC hdc, LPPOINT point)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	*point = dc->_state._position;
	return TRUE;
}

// --- Saving and restoring -------------------------------------------------------------------------

int WINAPI SaveDC(HDC hdc)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return 0;
	for (HGDIOBJ object : {static_cast<HGDIOBJ>(dc->_state._pen), static_cast<HGDIOBJ>(dc->_state._brush), static_cast<HGDIOBJ>(dc->_state._font)})
		++toObject(object)->_selectedCount;
	dc->_saved.push_back(dc->_state);
	return static_cast<int>(dc->_saved.size());
}

BOOL WINAPI RestoreDC(HDC hdc, int level)
{
	DC* dc = toDC(hdc);
	if (!dc || dc->_saved.empty())
		return FALSE;
	size_t index = level < 0 ? dc->_saved.size() + static_cast<size_t>(level) : static_cast<size_t>(level) - 1;
	if (index >= dc->_saved.size())
		return FALSE;
	auto releaseState = [](const DCState& state)
	{
		for (HGDIOBJ object : {static_cast<HGDIOBJ>(state._pen), static_cast<HGDIOBJ>(state._brush), static_cast<HGDIOBJ>(state._font)})
		{
			GdiObject* o = toObject(object);
			if (o && o->_selectedCount > 0 && --o->_selectedCount == 0 && o->_deletePending)
			{
				o->_deletePending = false;
				DeleteObject(object);
			}
		}
	};
	releaseState(dc->_state);
	dc->_state = dc->_saved[index];
	for (size_t i = index + 1; i < dc->_saved.size(); ++i)
		releaseState(dc->_saved[i]);
	dc->_saved.resize(index);
	return TRUE;
}

// --- Clipping (device coordinates) -----------------------------------------------------------------

int WINAPI SelectClipRgn(HDC hdc, HRGN handle)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return ERROR;
	auto* region = objectAs<Region>(handle, ObjectType::region);
	if (!region)
	{
		dc->_state._clip.clear();
		dc->_state._clipped = false;
		return SIMPLEREGION;
	}
	dc->_state._clip = region->_rects;
	dc->_state._clipped = true;
	return region->_rects.empty() ? NULLREGION : (region->_rects.size() == 1 ? SIMPLEREGION : COMPLEXREGION);
}

int WINAPI ExtSelectClipRgn(HDC hdc, HRGN region, int mode)
{
	if (mode == RGN_COPY)
		return SelectClipRgn(hdc, region);
	DC* dc = toDC(hdc);
	if (!dc)
		return ERROR;
	RECT box{};
	if (region)
		GetRgnBox(region, &box);
	if (mode == RGN_AND)
		return IntersectClipRect(hdc, box.left, box.top, box.right, box.bottom);
	if (mode == RGN_DIFF)
		return ExcludeClipRect(hdc, box.left, box.top, box.right, box.bottom);
	return SIMPLEREGION;
}

int WINAPI IntersectClipRect(HDC hdc, int left, int top, int right, int bottom)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return ERROR;
	const POINT offset{dc->_state._viewportOrigin.x - dc->_state._windowOrigin.x, dc->_state._viewportOrigin.y - dc->_state._windowOrigin.y};
	RECT rect{left + offset.x, top + offset.y, right + offset.x, bottom + offset.y};
	if (!dc->_state._clipped)
	{
		dc->_state._clip = {rect};
		dc->_state._clipped = true;
	}
	else
	{
		std::vector<RECT> result;
		for (const RECT& clip : dc->_state._clip)
		{
			RECT intersection{};
			if (IntersectRect(&intersection, &clip, &rect))
				result.push_back(intersection);
		}
		dc->_state._clip = std::move(result);
	}
	return dc->_state._clip.empty() ? NULLREGION : SIMPLEREGION;
}

int WINAPI ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return ERROR;
	const POINT offset{dc->_state._viewportOrigin.x - dc->_state._windowOrigin.x, dc->_state._viewportOrigin.y - dc->_state._windowOrigin.y};
	const RECT cut{left + offset.x, top + offset.y, right + offset.x, bottom + offset.y};
	std::vector<RECT> clip = dc->_state._clipped ? dc->_state._clip : std::vector<RECT>{RECT{0, 0, std::max(dc->_width, 32767), std::max(dc->_height, 32767)}};
	std::vector<RECT> result;
	for (const RECT& r : clip)
	{
		RECT overlap{};
		if (!IntersectRect(&overlap, &r, &cut))
		{
			result.push_back(r);
			continue;
		}
		if (r.top < overlap.top)
			result.push_back({r.left, r.top, r.right, overlap.top});
		if (overlap.bottom < r.bottom)
			result.push_back({r.left, overlap.bottom, r.right, r.bottom});
		if (r.left < overlap.left)
			result.push_back({r.left, overlap.top, overlap.left, overlap.bottom});
		if (overlap.right < r.right)
			result.push_back({overlap.right, overlap.top, r.right, overlap.bottom});
	}
	dc->_state._clip = std::move(result);
	dc->_state._clipped = true;
	return dc->_state._clip.empty() ? NULLREGION : COMPLEXREGION;
}

int WINAPI GetClipRgn(HDC hdc, HRGN region)
{
	DC* dc = toDC(hdc);
	auto* target = objectAs<Region>(region, ObjectType::region);
	if (!dc || !target)
		return -1;
	if (!dc->_state._clipped)
		return 0;
	target->_rects = dc->_state._clip;
	return 1;
}

int WINAPI GetClipBox(HDC hdc, LPRECT rect)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return ERROR;
	RECT box{0, 0, dc->_width, dc->_height};
	if (dc->_hasPaintClip)
		box = dc->_paintClip;
	if (dc->_state._clipped)
	{
		RECT clipBox{};
		for (const RECT& r : dc->_state._clip)
			UnionRect(&clipBox, &clipBox, &r);
		IntersectRect(&box, &box, &clipBox);
	}
	// Logical coordinates.
	OffsetRect(&box, dc->_state._windowOrigin.x - dc->_state._viewportOrigin.x, dc->_state._windowOrigin.y - dc->_state._viewportOrigin.y);
	*rect = box;
	return IsRectEmpty(&box) ? NULLREGION : SIMPLEREGION;
}

BOOL WINAPI RectVisible(HDC hdc, const RECT* rect)
{
	RECT box{};
	if (GetClipBox(hdc, &box) == ERROR)
		return FALSE;
	RECT intersection{};
	return IntersectRect(&intersection, &box, rect);
}

BOOL WINAPI PtVisible(HDC hdc, int x, int y)
{
	RECT box{};
	if (GetClipBox(hdc, &box) == ERROR)
		return FALSE;
	const POINT point{x, y};
	return PtInRect(&box, point);
}

// --- Device capabilities --------------------------------------------------------------------------

int WINAPI GetDeviceCaps(HDC hdc, int index)
{
	DC* dc = toDC(hdc);
	const nppmac::ui::Rect screen = nppmac::ui::mainScreenRect();
	switch (index)
	{
		case LOGPIXELSX:
		case LOGPIXELSY:
			return 96;
		case BITSPIXEL:
			return 32;
		case PLANES:
			return 1;
		case HORZRES:
		case DESKTOPHORZRES:
			return dc && dc->_kind != DCKind::info && dc->_width > 1 ? dc->_width : screen.width();
		case VERTRES:
		case DESKTOPVERTRES:
			return dc && dc->_kind != DCKind::info && dc->_height > 1 ? dc->_height : screen.height();
		case HORZSIZE:
			return screen.width() * 254 / 960;
		case VERTSIZE:
			return screen.height() * 254 / 960;
		case NUMCOLORS:
			return -1;
		case RASTERCAPS:
			return RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP | RC_DIBTODEV | RC_STRETCHDIB;
		case TECHNOLOGY:
			return dc && dc->_kind == DCKind::printer ? DT_RASPRINTER : DT_RASDISPLAY;
		case VREFRESH:
			return 60;
		case SHADEBLENDCAPS:
			return SB_CONST_ALPHA | SB_PIXEL_ALPHA;
		case PHYSICALWIDTH:
			return dc && dc->_print ? static_cast<int>(std::lround(dc->_print->_setup._paperWidth * printerUnitsPerPoint)) : 0;
		case PHYSICALHEIGHT:
			return dc && dc->_print ? static_cast<int>(std::lround(dc->_print->_setup._paperHeight * printerUnitsPerPoint)) : 0;
		case PHYSICALOFFSETX:
			return dc && dc->_print ? static_cast<int>(std::lround(dc->_print->_setup._printableLeft * printerUnitsPerPoint)) : 0;
		case PHYSICALOFFSETY:
			return dc && dc->_print ? static_cast<int>(std::lround(dc->_print->_setup._printableTop * printerUnitsPerPoint)) : 0;
		default:
			return 0;
	}
}

BOOL WINAPI GdiFlush()
{
	return TRUE;
}
