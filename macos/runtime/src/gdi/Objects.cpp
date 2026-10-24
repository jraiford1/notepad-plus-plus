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

// GDI objects: pens, brushes, fonts, bitmaps, regions; stock objects; selection and deletion.

#include "Immortal.h"
#include "Gdi.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>

namespace nppmac::gdi
{
	namespace
	{
		// Live objects, so that a stale handle is ignored instead of dereferenced.
		nppmac::Immortal<std::mutex> g_objectsMutexStorage;
		std::mutex& g_objectsMutex = g_objectsMutexStorage._value;

		std::unordered_set<const GdiObject*>& liveObjects()
		{
			static auto* set = new std::unordered_set<const GdiObject*>;
			return *set;
		}

		template <typename T>
		T* track(T* object)
		{
			std::lock_guard lock{g_objectsMutex};
			liveObjects().insert(object);
			return object;
		}

		void untrack(const GdiObject* object)
		{
			std::lock_guard lock{g_objectsMutex};
			liveObjects().erase(object);
		}

		template <typename T>
		T* stock(T* object)
		{
			object->_stock = true;
			return track(object);
		}

		Brush* solidStockBrush(COLORREF color)
		{
			auto* brush = new Brush;
			brush->_color = color;
			return stock(brush);
		}

		Pen* stockPen(int style, COLORREF color)
		{
			auto* pen = new Pen;
			pen->_style = style;
			pen->_color = color;
			return stock(pen);
		}

		Font* stockFont(const char16_t* face, LONG height, LONG weight)
		{
			auto* font = new Font;
			font->_log.lfHeight = height;
			font->_log.lfWeight = weight;
			font->_log.lfCharSet = DEFAULT_CHARSET;
			wcsncpy_s(font->_log.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
			return stock(font);
		}

		HGDIOBJ stockObject(int index)
		{
			static HGDIOBJ objects[STOCK_LAST + 1] = {};
			static std::once_flag once;
			std::call_once(once, []
			{
				objects[WHITE_BRUSH] = solidStockBrush(RGB(255, 255, 255));
				objects[LTGRAY_BRUSH] = solidStockBrush(RGB(192, 192, 192));
				objects[GRAY_BRUSH] = solidStockBrush(RGB(128, 128, 128));
				objects[DKGRAY_BRUSH] = solidStockBrush(RGB(64, 64, 64));
				objects[BLACK_BRUSH] = solidStockBrush(RGB(0, 0, 0));
				auto* hollow = new Brush;
				hollow->_style = BS_NULL;
				objects[NULL_BRUSH] = stock(hollow);
				objects[WHITE_PEN] = stockPen(PS_SOLID, RGB(255, 255, 255));
				objects[BLACK_PEN] = stockPen(PS_SOLID, RGB(0, 0, 0));
				objects[NULL_PEN] = stockPen(PS_NULL, 0);
				// The GUI font is the macOS system font, at the size of Windows' (9 pt at 96 DPI).
				objects[OEM_FIXED_FONT] = stockFont(u"Menlo", -12, FW_NORMAL);
				objects[ANSI_FIXED_FONT] = stockFont(u"Menlo", -12, FW_NORMAL);
				objects[ANSI_VAR_FONT] = stockFont(u"MS Shell Dlg", -12, FW_NORMAL);
				objects[SYSTEM_FONT] = stockFont(u"MS Shell Dlg", -13, FW_NORMAL);
				objects[DEVICE_DEFAULT_FONT] = stockFont(u"MS Shell Dlg", -13, FW_NORMAL);
				objects[SYSTEM_FIXED_FONT] = stockFont(u"Menlo", -12, FW_NORMAL);
				objects[DEFAULT_GUI_FONT] = stockFont(u"MS Shell Dlg", -12, FW_NORMAL);
				auto* dcBrush = new Brush;
				dcBrush->_dcBrush = true;
				objects[DC_BRUSH] = stock(dcBrush);
				auto* dcPen = new Pen;
				objects[DC_PEN] = stock(dcPen);
				auto* defaultBitmap = new Bitmap;
				defaultBitmap->_width = 1;
				defaultBitmap->_height = 1;
				defaultBitmap->_bitsPerPixel = 1;
				defaultBitmap->_ownedPixels.assign(1, 0xFFFFFFFF);
				defaultBitmap->_pixels = defaultBitmap->_ownedPixels.data();
				objects[STOCK_LAST] = stock(defaultBitmap);
			});
			return index >= 0 && index <= STOCK_LAST ? objects[index] : nullptr;
		}

		void destroy(GdiObject* object)
		{
			untrack(object);
			delete object;
		}
	}

	HGDIOBJ trackObject(GdiObject* object)
	{
		return track(object);
	}

	GdiObject* toObject(HGDIOBJ handle)
	{
		auto* object = static_cast<GdiObject*>(handle);
		std::lock_guard lock{g_objectsMutex};
		return liveObjects().count(object) ? object : nullptr;
	}

	// --- Font -------------------------------------------------------------------------------------

	Font::~Font()
	{
		if (_ctFont)
			CFRelease(_ctFont);
	}

	// --- Bitmap -----------------------------------------------------------------------------------

	Bitmap::~Bitmap()
	{
		if (_context)
			CGContextRelease(_context);
	}

	CGContextRef Bitmap::context()
	{
		if (_context)
			return _context;
		CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		// A bottom-up DIB is drawn through a context whose rows run upward in memory.
		_context = CGBitmapContextCreate(_pixels, static_cast<size_t>(_width), static_cast<size_t>(_height), 8,
			static_cast<size_t>(_width) * 4, colorSpace, static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little));
		CGColorSpaceRelease(colorSpace);
		if (_context && !_bottomUp)
		{
			// CGBitmapContext memory is top-down with a bottom-left origin: flip to a top-left origin.
			CGContextTranslateCTM(_context, 0, _height);
			CGContextScaleCTM(_context, 1, -1);
		}
		else if (_context)
		{
			// Rows run upward in memory, so the bottom-left origin of Core Graphics already matches
			// the rows; drawing still needs a top-left origin in GDI coordinates.
			CGContextTranslateCTM(_context, 0, 0);
		}
		return _context;
	}

	CGImageRef Bitmap::createImageFor(CGContextRef context) const
	{
		if (_hiRes && context)
		{
			const CGSize pixel = CGContextConvertSizeToDeviceSpace(context, CGSizeMake(1, 1));
			if (std::fabs(pixel.width) >= 1.5)
				return _hiRes->createImage();
		}
		return createImage();
	}

	CGImageRef Bitmap::createImage() const
	{
		CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, _pixels, static_cast<size_t>(_width) * static_cast<size_t>(_height) * 4, nullptr);
		CGImageRef image = CGImageCreate(static_cast<size_t>(_width), static_cast<size_t>(_height), 8, 32, static_cast<size_t>(_width) * 4,
			colorSpace, static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little), provider, nullptr, false, kCGRenderingIntentDefault);
		CGDataProviderRelease(provider);
		CGColorSpaceRelease(colorSpace);
		return image;
	}

	// --- Region -----------------------------------------------------------------------------------

	RECT Region::box() const
	{
		RECT result{};
		for (const RECT& rect : _rects)
			UnionRect(&result, &result, &rect);
		return result;
	}

	bool Region::contains(POINT point) const
	{
		for (const RECT& rect : _rects)
		{
			if (PtInRect(&rect, point))
				return true;
		}
		return false;
	}

	// --- Colors -----------------------------------------------------------------------------------

	CGColorRef createColor(COLORREF color, CGFloat alpha)
	{
		const CGFloat components[] = {GetRValue(color) / 255.0, GetGValue(color) / 255.0, GetBValue(color) / 255.0, alpha};
		CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGColorRef result = CGColorCreate(colorSpace, components);
		CGColorSpaceRelease(colorSpace);
		return result;
	}

	void setFillColor(CGContextRef context, COLORREF color)
	{
		CGContextSetRGBFillColor(context, GetRValue(color) / 255.0, GetGValue(color) / 255.0, GetBValue(color) / 255.0, 1.0);
	}

	void setStrokeColor(CGContextRef context, COLORREF color)
	{
		CGContextSetRGBStrokeColor(context, GetRValue(color) / 255.0, GetGValue(color) / 255.0, GetBValue(color) / 255.0, 1.0);
	}

	CGRect toCGRect(const RECT& rect)
	{
		return CGRectMake(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
	}

	COLORREF brushColor(DC* dc, HBRUSH handle)
	{
		auto* brush = objectAs<Brush>(handle, ObjectType::brush);
		if (!brush)
		{
			// System color "brushes" in class backgrounds: COLOR_x + 1.
			const auto index = reinterpret_cast<uintptr_t>(handle);
			if (index > 0 && index <= COLOR_MENUBAR + 1)
				return GetSysColor(static_cast<int>(index) - 1);
			return 0;
		}
		if (brush->_dcBrush)
			return dc ? dc->_state._dcBrushColor : 0xFFFFFF;
		if (brush->_systemColor >= 0)
			return GetSysColor(brush->_systemColor);
		return brush->_color;
	}

	COLORREF penColor(DC* dc, Pen* pen)
	{
		if (!pen)
			return 0;
		if (pen == objectAs<Pen>(stockObject(DC_PEN), ObjectType::pen))
			return dc ? dc->_state._dcPenColor : 0;
		return pen->_color;
	}
}

using namespace nppmac::gdi;

HGDIOBJ WINAPI GetStockObject(int index)
{
	return stockObject(index);
}

HPEN WINAPI CreatePen(int style, int width, COLORREF color)
{
	auto* pen = new Pen;
	pen->_style = style & PS_STYLE_MASK;
	pen->_width = std::max(1, width);
	pen->_color = color & 0xFFFFFF;
	return reinterpret_cast<HPEN>(track(pen));
}

HPEN WINAPI CreatePenIndirect(const LOGPEN* log)
{
	return CreatePen(static_cast<int>(log->lopnStyle), log->lopnWidth.x, log->lopnColor);
}

HPEN WINAPI ExtCreatePen(DWORD style, DWORD width, const LOGBRUSH* brush, DWORD, const DWORD*)
{
	return CreatePen(static_cast<int>(style & PS_STYLE_MASK), static_cast<int>(width), brush ? brush->lbColor : 0);
}

HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
	auto* brush = new Brush;
	brush->_color = color & 0xFFFFFF;
	return reinterpret_cast<HBRUSH>(track(brush));
}

HBRUSH WINAPI CreateHatchBrush(int hatch, COLORREF color)
{
	auto* brush = new Brush;
	brush->_style = BS_HATCHED;
	brush->_hatch = hatch;
	brush->_color = color & 0xFFFFFF;
	return reinterpret_cast<HBRUSH>(track(brush));
}

HBRUSH WINAPI CreatePatternBrush(HBITMAP bitmapHandle)
{
	auto* source = objectAs<Bitmap>(bitmapHandle, ObjectType::bitmap);
	auto* brush = new Brush;
	brush->_style = BS_PATTERN;
	if (source)
	{
		auto copy = std::make_shared<Bitmap>();
		copy->_width = source->_width;
		copy->_height = source->_height;
		copy->_ownedPixels.resize(static_cast<size_t>(source->_width) * static_cast<size_t>(source->_height));
		copy->_pixels = copy->_ownedPixels.data();
		for (int y = 0; y < source->_height; ++y)
			std::copy_n(source->rowPointer(y), source->_width, copy->rowPointer(y));
		brush->_pattern = copy;
		// Its average color stands in where a pattern cannot be drawn.
		brush->_color = RGB(128, 128, 128);
	}
	return reinterpret_cast<HBRUSH>(track(brush));
}

HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH* log)
{
	switch (log->lbStyle)
	{
		case BS_NULL:
			return static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
		case BS_HATCHED:
			return CreateHatchBrush(static_cast<int>(log->lbHatch), log->lbColor);
		case BS_PATTERN:
			return CreatePatternBrush(reinterpret_cast<HBITMAP>(log->lbHatch));
		default:
			return CreateSolidBrush(log->lbColor);
	}
}

HFONT WINAPI CreateFontIndirectW(const LOGFONTW* log)
{
	auto* font = new Font;
	font->_log = *log;
	return reinterpret_cast<HFONT>(track(font));
}

HFONT WINAPI CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline,
	DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision, DWORD quality, DWORD pitchAndFamily, LPCWSTR face)
{
	LOGFONTW log{};
	log.lfHeight = height;
	log.lfWidth = width;
	log.lfEscapement = escapement;
	log.lfOrientation = orientation;
	log.lfWeight = weight;
	log.lfItalic = static_cast<BYTE>(italic);
	log.lfUnderline = static_cast<BYTE>(underline);
	log.lfStrikeOut = static_cast<BYTE>(strikeOut);
	log.lfCharSet = static_cast<BYTE>(charSet);
	log.lfOutPrecision = static_cast<BYTE>(outPrecision);
	log.lfClipPrecision = static_cast<BYTE>(clipPrecision);
	log.lfQuality = static_cast<BYTE>(quality);
	log.lfPitchAndFamily = static_cast<BYTE>(pitchAndFamily);
	if (face)
		wcsncpy_s(log.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
	return CreateFontIndirectW(&log);
}

HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO* info, UINT, void** bits, HANDLE, DWORD)
{
	const BITMAPINFOHEADER& header = info->bmiHeader;
	auto* bitmap = new Bitmap;
	bitmap->_width = header.biWidth;
	bitmap->_height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
	bitmap->_bottomUp = header.biHeight > 0;
	bitmap->_bitsPerPixel = header.biBitCount;
	if (header.biBitCount != 32)
	{
		// Only 32-bit DIB sections hand out their bits; others are converted when drawn into.
		bitmap->_bitsPerPixel = 32;
	}
	bitmap->_ownedPixels.assign(static_cast<size_t>(bitmap->_width) * static_cast<size_t>(bitmap->_height), 0);
	bitmap->_pixels = bitmap->_ownedPixels.data();
	if (bits)
		*bits = bitmap->_pixels;
	return reinterpret_cast<HBITMAP>(track(bitmap));
}

HBITMAP WINAPI CreateBitmap(int width, int height, UINT planes, UINT bitsPerPixel, const void* bits)
{
	auto* bitmap = new Bitmap;
	bitmap->_width = std::max(1, width);
	bitmap->_height = std::max(1, height);
	bitmap->_bitsPerPixel = static_cast<int>(planes * bitsPerPixel);
	bitmap->_ownedPixels.assign(static_cast<size_t>(bitmap->_width) * static_cast<size_t>(bitmap->_height), 0);
	bitmap->_pixels = bitmap->_ownedPixels.data();
	if (bits)
	{
		if (bitmap->_bitsPerPixel == 1)
		{
			// Monochrome rows are WORD-aligned; set bits are white, clear ones black.
			const auto* source = static_cast<const BYTE*>(bits);
			const int stride = ((width + 15) / 16) * 2;
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const bool set = (source[y * stride + x / 8] >> (7 - x % 8)) & 1;
					bitmap->rowPointer(y)[x] = set ? 0xFFFFFFFF : 0xFF000000;
				}
			}
		}
		else if (bitmap->_bitsPerPixel == 32)
		{
			std::memcpy(bitmap->_pixels, bits, bitmap->_ownedPixels.size() * 4);
		}
	}
	else if (bitmap->_bitsPerPixel != 1)
	{
		std::fill(bitmap->_ownedPixels.begin(), bitmap->_ownedPixels.end(), 0xFF000000);
	}
	return reinterpret_cast<HBITMAP>(track(bitmap));
}

HBITMAP WINAPI CreateBitmapIndirect(const BITMAP* info)
{
	return CreateBitmap(info->bmWidth, info->bmHeight, info->bmPlanes, info->bmBitsPixel, info->bmBits);
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC, int width, int height)
{
	return CreateBitmap(width, height, 1, 32, nullptr);
}

HRGN WINAPI CreateRectRgn(int left, int top, int right, int bottom)
{
	auto* region = new Region;
	RECT rect{std::min(left, right), std::min(top, bottom), std::max(left, right), std::max(top, bottom)};
	if (!IsRectEmpty(&rect))
		region->_rects.push_back(rect);
	return reinterpret_cast<HRGN>(track(region));
}

HRGN WINAPI CreateRectRgnIndirect(const RECT* rect)
{
	return CreateRectRgn(rect->left, rect->top, rect->right, rect->bottom);
}

HRGN WINAPI CreateRoundRectRgn(int left, int top, int right, int bottom, int, int)
{
	return CreateRectRgn(left, top, right, bottom);
}

HRGN WINAPI CreateEllipticRgn(int left, int top, int right, int bottom)
{
	return CreateRectRgn(left, top, right, bottom);
}

BOOL WINAPI SetRectRgn(HRGN handle, int left, int top, int right, int bottom)
{
	auto* region = objectAs<Region>(handle, ObjectType::region);
	if (!region)
		return FALSE;
	region->_rects.clear();
	RECT rect{left, top, right, bottom};
	if (!IsRectEmpty(&rect))
		region->_rects.push_back(rect);
	return TRUE;
}

int WINAPI GetRgnBox(HRGN handle, LPRECT rect)
{
	auto* region = objectAs<Region>(handle, ObjectType::region);
	if (!region)
		return ERROR;
	*rect = region->box();
	if (region->_rects.empty())
		return NULLREGION;
	return region->_rects.size() == 1 ? SIMPLEREGION : COMPLEXREGION;
}

int WINAPI CombineRgn(HRGN destination, HRGN source1, HRGN source2, int mode)
{
	auto* dest = objectAs<Region>(destination, ObjectType::region);
	auto* a = objectAs<Region>(source1, ObjectType::region);
	auto* b = objectAs<Region>(source2, ObjectType::region);
	if (!dest || !a)
		return ERROR;
	std::vector<RECT> result;
	switch (mode)
	{
		case RGN_COPY:
			result = a->_rects;
			break;
		case RGN_OR:
		case RGN_XOR:
			result = a->_rects;
			if (b)
				result.insert(result.end(), b->_rects.begin(), b->_rects.end());
			break;
		case RGN_AND:
		{
			if (b)
			{
				for (const RECT& ra : a->_rects)
				{
					for (const RECT& rb : b->_rects)
					{
						RECT intersection{};
						if (IntersectRect(&intersection, &ra, &rb))
							result.push_back(intersection);
					}
				}
			}
			break;
		}
		case RGN_DIFF:
		{
			// Each rectangle minus each subtracted rectangle, split into up to four pieces.
			result = a->_rects;
			if (b)
			{
				for (const RECT& cut : b->_rects)
				{
					std::vector<RECT> next;
					for (const RECT& r : result)
					{
						RECT overlap{};
						if (!IntersectRect(&overlap, &r, &cut))
						{
							next.push_back(r);
							continue;
						}
						if (r.top < overlap.top)
							next.push_back({r.left, r.top, r.right, overlap.top});
						if (overlap.bottom < r.bottom)
							next.push_back({r.left, overlap.bottom, r.right, r.bottom});
						if (r.left < overlap.left)
							next.push_back({r.left, overlap.top, overlap.left, overlap.bottom});
						if (overlap.right < r.right)
							next.push_back({overlap.right, overlap.top, r.right, overlap.bottom});
					}
					result = std::move(next);
				}
			}
			break;
		}
		default:
			return ERROR;
	}
	dest->_rects = std::move(result);
	if (dest->_rects.empty())
		return NULLREGION;
	return dest->_rects.size() == 1 ? SIMPLEREGION : COMPLEXREGION;
}

BOOL WINAPI PtInRegion(HRGN handle, int x, int y)
{
	auto* region = objectAs<Region>(handle, ObjectType::region);
	return region && region->contains({x, y}) ? TRUE : FALSE;
}

int WINAPI OffsetRgn(HRGN handle, int dx, int dy)
{
	auto* region = objectAs<Region>(handle, ObjectType::region);
	if (!region)
		return ERROR;
	for (RECT& rect : region->_rects)
		OffsetRect(&rect, dx, dy);
	return region->_rects.empty() ? NULLREGION : (region->_rects.size() == 1 ? SIMPLEREGION : COMPLEXREGION);
}

BOOL WINAPI DeleteObject(HGDIOBJ handle)
{
	GdiObject* object = toObject(handle);
	if (!object)
		return FALSE;
	if (object->_stock)
		return TRUE;
	if (object->_selectedCount > 0)
	{
		// Windows refuses to delete a selected object; deleting it later avoids leaking it.
		object->_deletePending = true;
		return TRUE;
	}
	destroy(object);
	return TRUE;
}

int WINAPI GetObjectW(HGDIOBJ handle, int size, LPVOID buffer)
{
	GdiObject* object = toObject(handle);
	if (!object)
		return 0;
	switch (object->_type)
	{
		case ObjectType::pen:
		{
			auto* pen = static_cast<Pen*>(object);
			LOGPEN log{static_cast<UINT>(pen->_style), {pen->_width, 0}, pen->_color};
			if (!buffer)
				return sizeof(LOGPEN);
			std::memcpy(buffer, &log, std::min<size_t>(sizeof(log), static_cast<size_t>(size)));
			return std::min<int>(sizeof(log), size);
		}
		case ObjectType::brush:
		{
			auto* brush = static_cast<Brush*>(object);
			LOGBRUSH log{brush->_style, brushColor(nullptr, reinterpret_cast<HBRUSH>(brush)), static_cast<ULONG_PTR>(brush->_hatch)};
			if (!buffer)
				return sizeof(LOGBRUSH);
			std::memcpy(buffer, &log, std::min<size_t>(sizeof(log), static_cast<size_t>(size)));
			return std::min<int>(sizeof(log), size);
		}
		case ObjectType::font:
		{
			auto* font = static_cast<Font*>(object);
			if (!buffer)
				return sizeof(LOGFONTW);
			std::memcpy(buffer, &font->_log, std::min<size_t>(sizeof(LOGFONTW), static_cast<size_t>(size)));
			return std::min<int>(sizeof(LOGFONTW), size);
		}
		case ObjectType::bitmap:
		{
			auto* bitmap = static_cast<Bitmap*>(object);
			if (!buffer)
				return sizeof(BITMAP);
			if (static_cast<size_t>(size) >= sizeof(DIBSECTION))
			{
				DIBSECTION section{};
				section.dsBm = {0, bitmap->_width, bitmap->_height, bitmap->_width * 4, 1, static_cast<WORD>(bitmap->_bitsPerPixel), bitmap->_pixels};
				section.dsBmih.biSize = sizeof(BITMAPINFOHEADER);
				section.dsBmih.biWidth = bitmap->_width;
				section.dsBmih.biHeight = bitmap->_bottomUp ? bitmap->_height : -bitmap->_height;
				section.dsBmih.biPlanes = 1;
				section.dsBmih.biBitCount = 32;
				section.dsBmih.biCompression = BI_RGB;
				std::memcpy(buffer, &section, sizeof(section));
				return sizeof(DIBSECTION);
			}
			BITMAP info{0, bitmap->_width, bitmap->_height, bitmap->_width * 4, 1, static_cast<WORD>(bitmap->_bitsPerPixel), bitmap->_pixels};
			std::memcpy(buffer, &info, std::min<size_t>(sizeof(info), static_cast<size_t>(size)));
			return std::min<int>(sizeof(info), size);
		}
		default:
			return 0;
	}
}

DWORD WINAPI GetObjectType(HGDIOBJ handle)
{
	GdiObject* object = toObject(handle);
	if (!object)
		return 0;
	switch (object->_type)
	{
		case ObjectType::pen:
			return OBJ_PEN;
		case ObjectType::brush:
			return OBJ_BRUSH;
		case ObjectType::font:
			return OBJ_FONT;
		case ObjectType::bitmap:
			return OBJ_BITMAP;
		case ObjectType::region:
			return OBJ_REGION;
		default:
			return 0;
	}
}

HBRUSH WINAPI GetSysColorBrush(int index)
{
	static Brush* brushes[COLOR_MENUBAR + 1] = {};
	if (index < 0 || index > COLOR_MENUBAR)
		return nullptr;
	if (!brushes[index])
	{
		auto* brush = new Brush;
		brush->_systemColor = index;
		brushes[index] = stock(brush);
	}
	return reinterpret_cast<HBRUSH>(brushes[index]);
}
