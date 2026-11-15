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

// GDI drawing: fills, lines, shapes, bitmap transfers, DIBs, pixels, icons; and the user32 drawing
// helpers built on them (FillRect, FrameRect, DrawEdge, DrawFocusRect, DrawIconEx...).
//
// User space of every context is "y down" (GDI's): images are drawn with a local flip.

#include "Gdi.h"

#include <algorithm>
#include <cmath>

namespace nppmac::gdi
{
	namespace
	{
		Pen* currentPen(DC* dc)
		{
			return objectAs<Pen>(dc->_state._pen, ObjectType::pen);
		}

		bool hasPen(DC* dc)
		{
			Pen* pen = currentPen(dc);
			return pen && pen->_style != PS_NULL;
		}

		bool hasBrush(DC* dc)
		{
			auto* brush = objectAs<Brush>(dc->_state._brush, ObjectType::brush);
			return brush && brush->_style != BS_NULL;
		}

		void applyPen(DC* dc, Canvas* canvas)
		{
			Pen* pen = currentPen(dc);
			canvas->setStrokeColor(penColor(dc, pen));
			canvas->setLineWidth(pen ? pen->_width : 1);
			canvas->setLineCap(LineCap::square);
			if (pen && (pen->_style == PS_DOT || pen->_style == PS_DASH || pen->_style == PS_DASHDOT || pen->_style == PS_DASHDOTDOT))
			{
				static const double dot[] = {1, 1};
				static const double dash[] = {6, 2};
				static const double dashDot[] = {6, 2, 1, 2};
				if (pen->_style == PS_DOT)
					canvas->setLineDash(dot);
				else if (pen->_style == PS_DASH)
					canvas->setLineDash(dash);
				else
					canvas->setLineDash(dashDot);
				canvas->setLineCap(LineCap::butt);
			}
			// R2_NOT and R2_XORPEN invert what is under the line (splitter and drag feedback).
			if (dc->_state._rop2 == R2_NOT || dc->_state._rop2 == R2_XORPEN || dc->_state._rop2 == R2_NOTXORPEN)
			{
				canvas->setBlendMode(BlendMode::difference);
				canvas->setStrokeColor(RGB(255, 255, 255));
			}
		}

		Bitmap* selectedBitmap(DC* dc)
		{
			return dc && dc->_kind == DCKind::memory ? objectAs<Bitmap>(dc->_bitmap, ObjectType::bitmap) : nullptr;
		}

		uint32_t toPixel(COLORREF color)
		{
			return 0xFF000000 | (static_cast<uint32_t>(GetRValue(color)) << 16) | (static_cast<uint32_t>(GetGValue(color)) << 8) | GetBValue(color);
		}

		COLORREF fromPixel(uint32_t pixel)
		{
			return RGB((pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF);
		}

		// Exact raster operations between two bitmaps (the common case of double buffering).
		bool rasterBitmaps(DC* destination, int x, int y, int width, int height, DC* source, int sourceX, int sourceY, DWORD rop)
		{
			Bitmap* dst = selectedBitmap(destination);
			Bitmap* src = source ? selectedBitmap(source) : nullptr;
			// Scaled bitmaps (double buffers at a screen's resolution) are copied by the canvas.
			if (!dst || dst->scaled() || (src && src->scaled()) || destination->_state._clipped || destination->_hasPaintClip)
				return false;
			const POINT dstOffset{destination->_state._viewportOrigin.x - destination->_state._windowOrigin.x, destination->_state._viewportOrigin.y - destination->_state._windowOrigin.y};
			x += dstOffset.x;
			y += dstOffset.y;
			if (source)
			{
				if (!src)
					return false;
				sourceX += source->_state._viewportOrigin.x - source->_state._windowOrigin.x;
				sourceY += source->_state._viewportOrigin.y - source->_state._windowOrigin.y;
			}
			const uint32_t pattern = toPixel(brushColor(destination, destination->_state._brush));
			for (int row = 0; row < height; ++row)
			{
				const int dy = y + row;
				const int sy = sourceY + row;
				if (dy < 0 || dy >= dst->_height || (src && (sy < 0 || sy >= src->_height)))
					continue;
				uint32_t* dstRow = dst->writeRow(dy);
				const uint32_t* srcRow = src ? src->readRow(sy) : nullptr;
				for (int column = 0; column < width; ++column)
				{
					const int dx = x + column;
					const int sx = sourceX + column;
					if (dx < 0 || dx >= dst->_width || (src && (sx < 0 || sx >= src->_width)))
						continue;
					const uint32_t s = srcRow ? srcRow[sx] : 0;
					uint32_t& d = dstRow[dx];
					switch (rop)
					{
						case SRCCOPY:
							d = s;
							break;
						case SRCPAINT:
							d = (d | s) | 0xFF000000;
							break;
						case SRCAND:
							d = (d & s) | 0xFF000000;
							break;
						case SRCINVERT:
							d = (d ^ s) | 0xFF000000;
							break;
						case NOTSRCCOPY:
							d = ~s | 0xFF000000;
							break;
						case DSTINVERT:
							d = ~d | 0xFF000000;
							break;
						case BLACKNESS:
							d = 0xFF000000;
							break;
						case WHITENESS:
							d = 0xFFFFFFFF;
							break;
						case PATCOPY:
							d = pattern;
							break;
						case PATINVERT:
							d = (d ^ pattern) | 0xFF000000;
							break;
						case MERGECOPY:
							d = (s & pattern) | 0xFF000000;
							break;
						default:
							d = s;
					}
				}
			}
			return true;
		}
	}

	void fillRect(DC* dc, const RECT& rect, HBRUSH brushHandle)
	{
		auto* brush = objectAs<Brush>(brushHandle, ObjectType::brush);
		if (brush && brush->_style == BS_NULL)
			return;
		Canvas* canvas = dc->begin();
		if (!canvas)
			return;
		if (brush && brush->_style == BS_PATTERN && brush->_pattern)
		{
			// Tile the pattern bitmap.
			const std::shared_ptr<CanvasImage> image = brush->_pattern->image();
			canvas->clipToRect(toCanvasRect(rect));
			const int w = brush->_pattern->_width;
			const int h = brush->_pattern->_height;
			for (int y = rect.top - ((rect.top % h) + h) % h; image && y < rect.bottom; y += h)
			{
				for (int x = rect.left - ((rect.left % w) + w) % w; x < rect.right; x += w)
					canvas->drawImage(*image, {static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), static_cast<double>(h)});
			}
		}
		else
		{
			canvas->setFillColor(brushColor(dc, brushHandle));
			canvas->fillRect(toCanvasRect(rect));
		}
		dc->end();
	}
}

using namespace nppmac::gdi;

// --- Rectangles (user32) --------------------------------------------------------------------------

int WINAPI FillRect(HDC hdc, const RECT* rect, HBRUSH brush)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect)
		return 0;
	fillRect(dc, *rect, brush);
	return 1;
}

int WINAPI FrameRect(HDC hdc, const RECT* rect, HBRUSH brush)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect || IsRectEmpty(rect))
		return 0;
	fillRect(dc, {rect->left, rect->top, rect->right, rect->top + 1}, brush);
	fillRect(dc, {rect->left, rect->bottom - 1, rect->right, rect->bottom}, brush);
	fillRect(dc, {rect->left, rect->top, rect->left + 1, rect->bottom}, brush);
	fillRect(dc, {rect->right - 1, rect->top, rect->right, rect->bottom}, brush);
	return 1;
}

BOOL WINAPI InvertRect(HDC hdc, const RECT* rect)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect)
		return FALSE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	canvas->setBlendMode(BlendMode::difference);
	canvas->setFillColor(RGB(255, 255, 255));
	canvas->fillRect(toCanvasRect(*rect));
	dc->end();
	return TRUE;
}

BOOL WINAPI DrawFocusRect(HDC hdc, const RECT* rect)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect)
		return FALSE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	// A dotted frame, inverted, as on Windows.
	static const double dot[] = {1, 1};
	canvas->setBlendMode(BlendMode::difference);
	canvas->setStrokeColor(RGB(255, 255, 255));
	canvas->setLineWidth(1);
	canvas->setLineDash(dot);
	canvas->strokeRect({rect->left + 0.5, rect->top + 0.5, rect->right - rect->left - 1.0, rect->bottom - rect->top - 1.0});
	dc->end();
	return TRUE;
}

BOOL WINAPI DrawEdge(HDC hdc, LPRECT rect, UINT edge, UINT flags)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect)
		return FALSE;
	// Light and dark sides, outer then inner, in the system 3D colors.
	auto side = [&](COLORREF topLeft, COLORREF bottomRight)
	{
		HBRUSH light = CreateSolidBrush(topLeft);
		HBRUSH dark = CreateSolidBrush(bottomRight);
		if (flags & BF_TOP)
			fillRect(dc, {rect->left, rect->top, rect->right, rect->top + 1}, light);
		if (flags & BF_LEFT)
			fillRect(dc, {rect->left, rect->top, rect->left + 1, rect->bottom}, light);
		if (flags & BF_BOTTOM)
			fillRect(dc, {rect->left, rect->bottom - 1, rect->right, rect->bottom}, dark);
		if (flags & BF_RIGHT)
			fillRect(dc, {rect->right - 1, rect->top, rect->right, rect->bottom}, dark);
		DeleteObject(light);
		DeleteObject(dark);
		// Each side moves the rectangle in, so the inner edge draws inside the outer one.
		{
			if (flags & BF_TOP)
				++rect->top;
			if (flags & BF_LEFT)
				++rect->left;
			if (flags & BF_BOTTOM)
				--rect->bottom;
			if (flags & BF_RIGHT)
				--rect->right;
		}
	};
	const RECT original = *rect;
	const bool flat = (flags & BF_FLAT) != 0;
	if (edge & (BDR_RAISEDOUTER | BDR_SUNKENOUTER))
	{
		const bool raised = (edge & BDR_RAISEDOUTER) != 0;
		side(flat ? GetSysColor(COLOR_BTNSHADOW) : (raised ? GetSysColor(COLOR_3DLIGHT) : GetSysColor(COLOR_BTNSHADOW)),
			flat ? GetSysColor(COLOR_BTNSHADOW) : (raised ? GetSysColor(COLOR_3DDKSHADOW) : GetSysColor(COLOR_BTNHIGHLIGHT)));
	}
	if (edge & (BDR_RAISEDINNER | BDR_SUNKENINNER))
	{
		const bool raised = (edge & BDR_RAISEDINNER) != 0;
		side(raised ? GetSysColor(COLOR_BTNHIGHLIGHT) : GetSysColor(COLOR_3DDKSHADOW),
			raised ? GetSysColor(COLOR_BTNSHADOW) : GetSysColor(COLOR_3DLIGHT));
	}
	if (flags & BF_MIDDLE)
		fillRect(dc, *rect, GetSysColorBrush(COLOR_BTNFACE));
	if (!(flags & BF_ADJUST))
		*rect = original;
	return TRUE;
}

BOOL WINAPI DrawFrameControl(HDC hdc, LPRECT rect, UINT type, UINT state)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect)
		return FALSE;
	fillRect(dc, *rect, GetSysColorBrush(COLOR_BTNFACE));
	DrawEdge(hdc, rect, (state & DFCS_PUSHED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
	(void)type;
	return TRUE;
}

// --- Lines and shapes -----------------------------------------------------------------------------

BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT previous)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (previous)
		*previous = dc->_state._position;
	dc->_state._position = {x, y};
	return TRUE;
}

BOOL WINAPI LineTo(HDC hdc, int x, int y)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const POINT from = dc->_state._position;
	dc->_state._position = {x, y};
	if (!hasPen(dc))
		return TRUE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	applyPen(dc, canvas);
	Pen* pen = currentPen(dc);
	if (pen && pen->_width <= 1)
	{
		// The end point is not drawn: shorten the line by one pixel along its direction.
		const int dx = (x > from.x) - (x < from.x);
		const int dy = (y > from.y) - (y < from.y);
		canvas->setLineCap(LineCap::butt);
		const double startX = dx > 0 ? from.x : (dx < 0 ? from.x + 1 : from.x + 0.5);
		const double startY = dy > 0 ? from.y : (dy < 0 ? from.y + 1 : from.y + 0.5);
		const double endX = dx > 0 ? x : (dx < 0 ? x + 1 : x + 0.5);
		const double endY = dy > 0 ? y : (dy < 0 ? y + 1 : y + 0.5);
		canvas->strokeLine({startX, startY}, {endX, endY});
	}
	else
		canvas->strokeLine({static_cast<double>(from.x), static_cast<double>(from.y)}, {static_cast<double>(x), static_cast<double>(y)});
	dc->end();
	return TRUE;
}

BOOL WINAPI Polyline(HDC hdc, const POINT* points, int count)
{
	DC* dc = toDC(hdc);
	if (!dc || count < 2)
		return FALSE;
	const POINT saved = dc->_state._position;
	MoveToEx(hdc, points[0].x, points[0].y, nullptr);
	for (int i = 1; i < count; ++i)
		LineTo(hdc, points[i].x, points[i].y);
	dc->_state._position = saved;
	return TRUE;
}

BOOL WINAPI PolylineTo(HDC hdc, const POINT* points, DWORD count)
{
	for (DWORD i = 0; i < count; ++i)
		LineTo(hdc, points[i].x, points[i].y);
	return TRUE;
}

BOOL WINAPI Polygon(HDC hdc, const POINT* points, int count)
{
	DC* dc = toDC(hdc);
	if (!dc || count < 2)
		return FALSE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	std::vector<CanvasPoint> corners;
	for (int i = 0; i < count; ++i)
		corners.push_back({points[i].x + 0.5, points[i].y + 0.5});
	canvas->setAntialias(true);
	if (hasBrush(dc))
	{
		canvas->setFillColor(brushColor(dc, dc->_state._brush));
		if (hasPen(dc))
			applyPen(dc, canvas);
		canvas->drawPolygon(corners, true, hasPen(dc));
	}
	else if (hasPen(dc))
	{
		applyPen(dc, canvas);
		canvas->drawPolygon(corners, false, true);
	}
	dc->end();
	return TRUE;
}

BOOL WINAPI Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (hasBrush(dc))
	{
		const int inset = hasPen(dc) ? 1 : 0;
		fillRect(dc, {left + inset, top + inset, right - inset, bottom - inset}, dc->_state._brush);
	}
	if (hasPen(dc))
	{
		Canvas* canvas = dc->begin();
		if (!canvas)
			return FALSE;
		applyPen(dc, canvas);
		const int width = currentPen(dc)->_width;
		canvas->strokeRect({left + width / 2.0, top + width / 2.0, static_cast<double>(right - left - width), static_cast<double>(bottom - top - width)});
		dc->end();
	}
	return TRUE;
}

BOOL WINAPI RoundRect(HDC hdc, int left, int top, int right, int bottom, int ellipseWidth, int ellipseHeight)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	canvas->setAntialias(true);
	const CanvasRect rect{left + 0.5, top + 0.5, right - left - 1.0, bottom - top - 1.0};
	const double radiusX = std::min(ellipseWidth / 2.0, rect._width / 2);
	const double radiusY = std::min(ellipseHeight / 2.0, rect._height / 2);
	if (hasBrush(dc))
	{
		canvas->setFillColor(brushColor(dc, dc->_state._brush));
		canvas->drawRoundedRect(rect, radiusX, radiusY, true, false);
	}
	if (hasPen(dc))
	{
		applyPen(dc, canvas);
		canvas->drawRoundedRect(rect, radiusX, radiusY, false, true);
	}
	dc->end();
	return TRUE;
}

BOOL WINAPI Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	canvas->setAntialias(true);
	const CanvasRect rect{left + 0.5, top + 0.5, right - left - 1.0, bottom - top - 1.0};
	if (hasBrush(dc))
	{
		canvas->setFillColor(brushColor(dc, dc->_state._brush));
		canvas->drawEllipse(rect, true, false);
	}
	if (hasPen(dc))
	{
		applyPen(dc, canvas);
		canvas->drawEllipse(rect, false, true);
	}
	dc->end();
	return TRUE;
}

BOOL WINAPI FillRgn(HDC hdc, HRGN regionHandle, HBRUSH brush)
{
	DC* dc = toDC(hdc);
	auto* region = objectAs<Region>(regionHandle, ObjectType::region);
	if (!dc || !region)
		return FALSE;
	for (const RECT& rect : region->_rects)
		fillRect(dc, rect, brush);
	return TRUE;
}

BOOL WINAPI PaintRgn(HDC hdc, HRGN region)
{
	DC* dc = toDC(hdc);
	return dc ? FillRgn(hdc, region, dc->_state._brush) : FALSE;
}

BOOL WINAPI FrameRgn(HDC hdc, HRGN regionHandle, HBRUSH brush, int, int)
{
	auto* region = objectAs<Region>(regionHandle, ObjectType::region);
	if (!region)
		return FALSE;
	const RECT box = region->box();
	return FrameRect(hdc, &box, brush);
}

// --- Bitmap transfers -----------------------------------------------------------------------------

BOOL WINAPI PatBlt(HDC hdc, int x, int y, int width, int height, DWORD rop)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	if (width < 0)
	{
		x += width;
		width = -width;
	}
	if (height < 0)
	{
		y += height;
		height = -height;
	}
	if (rasterBitmaps(dc, x, y, width, height, nullptr, 0, 0, rop))
		return TRUE;
	const RECT rect{x, y, x + width, y + height};
	switch (rop)
	{
		case BLACKNESS:
		{
			HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
			fillRect(dc, rect, black);
			return TRUE;
		}
		case WHITENESS:
			fillRect(dc, rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
			return TRUE;
		case DSTINVERT:
		case PATINVERT:
		{
			Canvas* canvas = dc->begin();
			if (!canvas)
				return FALSE;
			canvas->setBlendMode(BlendMode::difference);
			canvas->setFillColor(rop == DSTINVERT ? RGB(255, 255, 255) : brushColor(dc, dc->_state._brush));
			canvas->fillRect(toCanvasRect(rect));
			dc->end();
			return TRUE;
		}
		default:
			fillRect(dc, rect, dc->_state._brush);
			return TRUE;
	}
}

BOOL WINAPI StretchBlt(HDC destinationHandle, int x, int y, int width, int height, HDC sourceHandle, int sourceX, int sourceY,
	int sourceWidth, int sourceHeight, DWORD rop)
{
	DC* destination = toDC(destinationHandle);
	DC* source = toDC(sourceHandle);
	if (!destination)
		return FALSE;
	if (!source || rop == PATCOPY || rop == BLACKNESS || rop == WHITENESS || rop == DSTINVERT || rop == PATINVERT)
		return PatBlt(destinationHandle, x, y, width, height, rop);
	if (width == sourceWidth && height == sourceHeight && rasterBitmaps(destination, x, y, width, height, source, sourceX, sourceY, rop))
		return TRUE;

	// Through the canvas: the source as an image (its bitmap, or the window surface).
	std::shared_ptr<CanvasImage> image;
	CanvasRect target{static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)};
	bool clipToTarget = false;
	if (Bitmap* bitmap = selectedBitmap(source))
	{
		const POINT offset{source->_state._viewportOrigin.x - source->_state._windowOrigin.x, source->_state._viewportOrigin.y - source->_state._windowOrigin.y};
		const CanvasRect area{static_cast<double>(sourceX + offset.x), static_cast<double>(sourceY + offset.y), static_cast<double>(sourceWidth),
			static_cast<double>(sourceHeight)};
		CanvasRect covered{};
		image = bitmap->imagePart(area, &covered);
		if (bitmap->scaled() && width == sourceWidth && height == sourceHeight)
		{
			// Whole pixels at their size, so a double buffer reaches a window at its resolution pixel for
			// pixel; they may reach a little past the rectangle asked for, so the copy is clipped to it.
			target = {x + covered._x - area._x, y + covered._y - area._y, covered._width, covered._height};
			clipToTarget = true;
		}
	}
	else if (source->_canvas)
	{
		if (const std::shared_ptr<CanvasImage> whole = source->_canvas->snapshot())
		{
			// Surfaces are scaled for the display.
			const double scale = static_cast<double>(whole->width()) / std::max(1, source->_width);
			image = whole->cropped({sourceX * scale, sourceY * scale, sourceWidth * scale, sourceHeight * scale});
		}
	}
	if (!image)
		return FALSE;

	Canvas* canvas = destination->begin();
	if (!canvas)
		return FALSE;
	switch (rop)
	{
		case SRCPAINT:
			canvas->setBlendMode(BlendMode::lighten);
			break;
		case SRCAND:
			canvas->setBlendMode(BlendMode::multiply);
			break;
		case SRCINVERT:
			canvas->setBlendMode(BlendMode::difference);
			break;
		default:
			canvas->setBlendMode(BlendMode::copy);
	}
	canvas->setInterpolation(destination->_state._stretchMode == HALFTONE ? Interpolation::high : Interpolation::none);
	if (clipToTarget)
		canvas->clipToRect({static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)});
	canvas->drawImage(*image, target);
	destination->end();
	return TRUE;
}

BOOL WINAPI BitBlt(HDC destination, int x, int y, int width, int height, HDC source, int sourceX, int sourceY, DWORD rop)
{
	return StretchBlt(destination, x, y, width, height, source, sourceX, sourceY, width, height, rop);
}

BOOL WINAPI AlphaBlend(HDC destinationHandle, int x, int y, int width, int height, HDC sourceHandle, int sourceX, int sourceY,
	int sourceWidth, int sourceHeight, BLENDFUNCTION blend)
{
	DC* destination = toDC(destinationHandle);
	DC* source = toDC(sourceHandle);
	Bitmap* bitmap = source ? selectedBitmap(source) : nullptr;
	if (!destination || !bitmap)
		return FALSE;
	const std::shared_ptr<CanvasImage> image = bitmap->imagePart({static_cast<double>(sourceX), static_cast<double>(sourceY),
		static_cast<double>(sourceWidth), static_cast<double>(sourceHeight)});
	Canvas* canvas = destination->begin();
	if (canvas && image)
	{
		canvas->setAlpha(blend.SourceConstantAlpha / 255.0);
		canvas->setInterpolation(Interpolation::high);
		canvas->drawImage(*image, {static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)});
	}
	if (canvas)
		destination->end();
	return canvas ? TRUE : FALSE;
}

BOOL WINAPI TransparentBlt(HDC destinationHandle, int x, int y, int width, int height, HDC sourceHandle, int sourceX, int sourceY,
	int sourceWidth, int sourceHeight, UINT transparent)
{
	DC* source = toDC(sourceHandle);
	Bitmap* bitmap = source ? selectedBitmap(source) : nullptr;
	if (!bitmap)
		return FALSE;
	// A copy of the source in which the transparent color is clear.
	HBITMAP copy = CreateBitmap(sourceWidth, sourceHeight, 1, 32, nullptr);
	auto* target = objectAs<Bitmap>(copy, ObjectType::bitmap);
	const uint32_t key = toPixel(transparent) & 0xFFFFFF;
	for (int row = 0; row < sourceHeight; ++row)
	{
		for (int column = 0; column < sourceWidth; ++column)
		{
			const int sx = sourceX + column;
			const int sy = sourceY + row;
			uint32_t pixel = (sx >= 0 && sy >= 0 && sx < bitmap->_width && sy < bitmap->_height) ? bitmap->readRow(sy)[sx] : 0;
			target->writeRow(row)[column] = (pixel & 0xFFFFFF) == key ? 0 : (pixel | 0xFF000000);
		}
	}
	HDC copyDC = CreateCompatibleDC(nullptr);
	HGDIOBJ old = SelectObject(copyDC, copy);
	BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	const BOOL result = AlphaBlend(destinationHandle, x, y, width, height, copyDC, 0, 0, sourceWidth, sourceHeight, blend);
	SelectObject(copyDC, old);
	DeleteDC(copyDC);
	DeleteObject(copy);
	return result;
}

BOOL WINAPI GradientFill(HDC hdc, PTRIVERTEX vertices, ULONG, PVOID mesh, ULONG count, ULONG mode)
{
	DC* dc = toDC(hdc);
	if (!dc || mode == GRADIENT_FILL_TRIANGLE)
		return FALSE;
	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	auto* rects = static_cast<GRADIENT_RECT*>(mesh);
	for (ULONG i = 0; i < count; ++i)
	{
		const TRIVERTEX& a = vertices[rects[i].UpperLeft];
		const TRIVERTEX& b = vertices[rects[i].LowerRight];
		const CanvasRGB from{a.Red / 65535.0, a.Green / 65535.0, a.Blue / 65535.0};
		const CanvasRGB to{b.Red / 65535.0, b.Green / 65535.0, b.Blue / 65535.0};
		canvas->save();
		canvas->clipToRect({static_cast<double>(a.x), static_cast<double>(a.y), static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y)});
		const CanvasPoint end = mode == GRADIENT_FILL_RECT_H ? CanvasPoint{static_cast<double>(b.x), static_cast<double>(a.y)}
		                                                     : CanvasPoint{static_cast<double>(a.x), static_cast<double>(b.y)};
		canvas->fillLinearGradient({static_cast<double>(a.x), static_cast<double>(a.y)}, from, end, to);
		canvas->restore();
	}
	dc->end();
	return TRUE;
}

// --- DIBs -----------------------------------------------------------------------------------------

namespace
{
	// Reads a DIB's pixels (1, 4, 8, 24 or 32 bits, top-down or bottom-up) as BGRA rows, top first.
	std::vector<uint32_t> readDib(const BITMAPINFO* info, const void* bits, int& width, int& height)
	{
		const BITMAPINFOHEADER& header = info->bmiHeader;
		width = header.biWidth;
		height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
		const bool bottomUp = header.biHeight > 0;
		const int bitCount = header.biBitCount;
		if (!isAllowedBitmapSize(width, height))
		{
			width = height = 0;
			return {};
		}
		const size_t stride = ((static_cast<size_t>(width) * static_cast<size_t>(bitCount) + 31) / 32) * 4;
		const RGBQUAD* palette = reinterpret_cast<const RGBQUAD*>(reinterpret_cast<const BYTE*>(info) + header.biSize);
		std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
		const auto* data = static_cast<const BYTE*>(bits);
		for (int y = 0; y < height; ++y)
		{
			const BYTE* row = data + stride * static_cast<size_t>(bottomUp ? height - 1 - y : y);
			uint32_t* out = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
			for (int x = 0; x < width; ++x)
			{
				uint32_t pixel = 0xFF000000;
				switch (bitCount)
				{
					case 32:
						std::memcpy(&pixel, row + x * 4, 4);
						break;
					case 24:
						pixel = 0xFF000000 | (static_cast<uint32_t>(row[x * 3 + 2]) << 16) | (static_cast<uint32_t>(row[x * 3 + 1]) << 8) | row[x * 3];
						break;
					case 8:
					case 4:
					case 1:
					{
						int index = 0;
						if (bitCount == 8)
							index = row[x];
						else if (bitCount == 4)
							index = (row[x / 2] >> ((x % 2) ? 0 : 4)) & 0x0F;
						else
							index = (row[x / 8] >> (7 - x % 8)) & 1;
						const RGBQUAD& c = palette[index];
						pixel = 0xFF000000 | (static_cast<uint32_t>(c.rgbRed) << 16) | (static_cast<uint32_t>(c.rgbGreen) << 8) | c.rgbBlue;
						break;
					}
					default:
						break;
				}
				out[x] = pixel;
			}
		}
		return pixels;
	}
}

int WINAPI StretchDIBits(HDC hdc, int x, int y, int width, int height, int sourceX, int sourceY, int sourceWidth, int sourceHeight,
	const void* bits, const BITMAPINFO* info, UINT, DWORD rop)
{
	int dibWidth = 0;
	int dibHeight = 0;
	std::vector<uint32_t> pixels = readDib(info, bits, dibWidth, dibHeight);
	if (pixels.empty())
		return 0;
	HBITMAP temporary = CreateBitmap(dibWidth, dibHeight, 1, 32, pixels.data());
	HDC source = CreateCompatibleDC(nullptr);
	HGDIOBJ old = SelectObject(source, temporary);
	// sourceY counts from the bottom of a bottom-up DIB.
	const int top = info->bmiHeader.biHeight > 0 ? dibHeight - sourceY - sourceHeight : sourceY;
	StretchBlt(hdc, x, y, width, height, source, sourceX, top, sourceWidth, sourceHeight, rop);
	SelectObject(source, old);
	DeleteDC(source);
	DeleteObject(temporary);
	return height;
}

int WINAPI SetDIBitsToDevice(HDC hdc, int x, int y, DWORD width, DWORD height, int sourceX, int sourceY, UINT, UINT,
	const void* bits, const BITMAPINFO* info, UINT usage)
{
	return StretchDIBits(hdc, x, y, static_cast<int>(width), static_cast<int>(height), sourceX, sourceY, static_cast<int>(width),
		static_cast<int>(height), bits, info, usage, SRCCOPY);
}

int WINAPI SetDIBits(HDC, HBITMAP handle, UINT start, UINT lines, const void* bits, const BITMAPINFO* info, UINT)
{
	auto* bitmap = objectAs<Bitmap>(handle, ObjectType::bitmap);
	if (!bitmap)
		return 0;
	int width = 0;
	int height = 0;
	std::vector<uint32_t> pixels = readDib(info, bits, width, height);
	(void)start;
	const int rows = std::min({static_cast<int>(lines), height, bitmap->_height});
	for (int y = 0; y < rows; ++y)
		std::copy_n(pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width), std::min(width, bitmap->_width), bitmap->writeRow(y));
	return rows;
}

int WINAPI GetDIBits(HDC, HBITMAP handle, UINT start, UINT lines, LPVOID bits, LPBITMAPINFO info, UINT)
{
	auto* bitmap = objectAs<Bitmap>(handle, ObjectType::bitmap);
	if (!bitmap)
		return 0;
	BITMAPINFOHEADER& header = info->bmiHeader;
	if (!bits)
	{
		// Fill in the format.
		header.biWidth = bitmap->_width;
		header.biHeight = bitmap->_height;
		header.biPlanes = 1;
		header.biBitCount = 32;
		header.biCompression = BI_RGB;
		header.biSizeImage = static_cast<DWORD>(bitmap->_width * bitmap->_height * 4);
		return bitmap->_height;
	}
	const bool bottomUp = header.biHeight > 0;
	const int bitCount = header.biBitCount ? header.biBitCount : 32;
	const size_t stride = ((static_cast<size_t>(bitmap->_width) * static_cast<size_t>(bitCount) + 31) / 32) * 4;
	auto* out = static_cast<BYTE*>(bits);
	const UINT rows = std::min<UINT>(lines, static_cast<UINT>(bitmap->_height) - std::min<UINT>(start, static_cast<UINT>(bitmap->_height)));
	for (UINT i = 0; i < rows; ++i)
	{
		// Scan line "start + i" counts from the bottom for a bottom-up request.
		const int scanLine = static_cast<int>(start + i);
		const int sourceRow = bottomUp ? bitmap->_height - 1 - scanLine : scanLine;
		const uint32_t* source = bitmap->readRow(sourceRow);
		BYTE* destination = out + stride * i;
		for (int x = 0; x < bitmap->_width; ++x)
		{
			if (bitCount == 32)
				std::memcpy(destination + x * 4, &source[x], 4);
			else if (bitCount == 24)
			{
				destination[x * 3] = static_cast<BYTE>(source[x] & 0xFF);
				destination[x * 3 + 1] = static_cast<BYTE>((source[x] >> 8) & 0xFF);
				destination[x * 3 + 2] = static_cast<BYTE>((source[x] >> 16) & 0xFF);
			}
		}
	}
	return static_cast<int>(rows);
}

// --- Pixels ---------------------------------------------------------------------------------------

COLORREF WINAPI GetPixel(HDC hdc, int x, int y)
{
	DC* dc = toDC(hdc);
	Bitmap* bitmap = selectedBitmap(dc);
	if (!bitmap || x < 0 || y < 0 || x >= bitmap->_width || y >= bitmap->_height)
		return CLR_INVALID;
	return fromPixel(bitmap->readRow(y)[x]);
}

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF color)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return CLR_INVALID;
	if (Bitmap* bitmap = selectedBitmap(dc))
	{
		if (x >= 0 && y >= 0 && x < bitmap->_width && y < bitmap->_height)
			bitmap->writeRow(y)[x] = toPixel(color);
		return color;
	}
	HBRUSH brush = CreateSolidBrush(color);
	fillRect(dc, {x, y, x + 1, y + 1}, brush);
	DeleteObject(brush);
	return color;
}

BOOL WINAPI SetPixelV(HDC hdc, int x, int y, COLORREF color)
{
	return SetPixel(hdc, x, y, color) != CLR_INVALID ? TRUE : FALSE;
}

// --- Icons and cursors ----------------------------------------------------------------------------

HICON WINAPI CreateIconIndirect(PICONINFO info)
{
	auto* icon = new Icon;
	icon->_isIcon = info->fIcon != FALSE;
	icon->_hotspot = {static_cast<LONG>(info->xHotspot), static_cast<LONG>(info->yHotspot)};
	auto copy = [](HBITMAP handle) -> std::shared_ptr<Bitmap>
	{
		auto* source = objectAs<Bitmap>(handle, ObjectType::bitmap);
		if (!source)
			return nullptr;
		auto bitmap = std::make_shared<Bitmap>();
		bitmap->_width = source->_width;
		bitmap->_height = source->_height;
		bitmap->_bitsPerPixel = source->_bitsPerPixel;
		bitmap->_ownedPixels.resize(static_cast<size_t>(source->_width) * static_cast<size_t>(source->_height));
		bitmap->_pixels = bitmap->_ownedPixels.data();
		for (int y = 0; y < source->_height; ++y)
			std::copy_n(source->readRow(y), source->_width, bitmap->writeRow(y));
		return bitmap;
	};
	icon->_color = copy(info->hbmColor);
	icon->_mask = copy(info->hbmMask);
	// Without alpha in the color bitmap, the AND mask decides transparency.
	if (icon->_color)
	{
		bool hasAlpha = false;
		for (uint32_t pixel : icon->_color->_ownedPixels)
		{
			if ((pixel >> 24) != 0 && (pixel >> 24) != 0xFF)
			{
				hasAlpha = true;
				break;
			}
		}
		if (!hasAlpha && icon->_mask && icon->_mask->_height >= icon->_color->_height)
		{
			for (int y = 0; y < icon->_color->_height; ++y)
			{
				for (int x = 0; x < icon->_color->_width; ++x)
				{
					const bool transparent = (icon->_mask->readRow(y)[x] & 0xFFFFFF) != 0;
					uint32_t& pixel = icon->_color->writeRow(y)[x];
					pixel = transparent ? 0 : (pixel | 0xFF000000);
				}
			}
		}
		else if (hasAlpha)
		{
			// Premultiply, as the bitmaps are stored.
			for (uint32_t& pixel : icon->_color->_ownedPixels)
			{
				const uint32_t a = pixel >> 24;
				const uint32_t r = ((pixel >> 16) & 0xFF) * a / 255;
				const uint32_t g = ((pixel >> 8) & 0xFF) * a / 255;
				const uint32_t b = (pixel & 0xFF) * a / 255;
				pixel = (a << 24) | (r << 16) | (g << 8) | b;
			}
		}
	}
	return reinterpret_cast<HICON>(trackObject(icon));
}

BOOL WINAPI GetIconInfo(HICON handle, PICONINFO info)
{
	auto* icon = objectAs<Icon>(handle, ObjectType::icon);
	if (!icon)
		return FALSE;
	info->fIcon = icon->_isIcon;
	info->xHotspot = static_cast<DWORD>(icon->_hotspot.x);
	info->yHotspot = static_cast<DWORD>(icon->_hotspot.y);
	info->hbmColor = nullptr;
	info->hbmMask = nullptr;
	if (icon->_color)
	{
		info->hbmColor = CreateBitmap(icon->_color->_width, icon->_color->_height, 1, 32, icon->_color->_pixels);
		// Alpha is in the color bitmap; the mask is all opaque.
		info->hbmMask = CreateBitmap(icon->_color->_width, icon->_color->_height, 1, 1, nullptr);
	}
	return TRUE;
}

BOOL WINAPI DestroyIcon(HICON handle)
{
	auto* icon = objectAs<Icon>(handle, ObjectType::icon);
	if (!icon)
		return FALSE;
	if (!icon->_shared)
		DeleteObject(handle);
	return TRUE;
}

BOOL WINAPI DestroyCursor(HCURSOR handle)
{
	return DestroyIcon(handle);
}

HICON WINAPI CopyIcon(HICON handle)
{
	auto* icon = objectAs<Icon>(handle, ObjectType::icon);
	if (!icon)
		return nullptr;
	ICONINFO info{};
	GetIconInfo(handle, &info);
	HICON copy = CreateIconIndirect(&info);
	DeleteObject(info.hbmColor);
	DeleteObject(info.hbmMask);
	return copy;
}

BOOL WINAPI DrawIconEx(HDC hdc, int x, int y, HICON handle, int width, int height, UINT, HBRUSH background, UINT)
{
	DC* dc = toDC(hdc);
	auto* icon = objectAs<Icon>(handle, ObjectType::icon);
	if (!dc || !icon || !icon->_color)
		return FALSE;
	if (width == 0)
		width = icon->_color->_width;
	if (height == 0)
		height = icon->_color->_height;
	if (background)
		fillRect(dc, {x, y, x + width, y + height}, background);
	Canvas* canvas = dc->begin();
	if (canvas)
	{
		if (const std::shared_ptr<CanvasImage> image = icon->_color->imageFor(canvas))
		{
			canvas->setInterpolation(Interpolation::high);
			canvas->drawImage(*image, sharpDestination(*canvas, *image,
				{static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)}));
		}
		dc->end();
	}
	return TRUE;
}

BOOL WINAPI DrawIcon(HDC hdc, int x, int y, HICON icon)
{
	return DrawIconEx(hdc, x, y, icon, 0, 0, 0, nullptr, DI_NORMAL);
}
