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

// comctl32 image lists: equally sized images, each kept as premultiplied BGRA with its own alpha.

#include "gdi/Gdi.h"

#include <commctrl.h>

#include <algorithm>
#include <unordered_set>

namespace
{
	using nppmac::gdi::Bitmap;

	struct ImageList
	{
		int _width = 16;
		int _height = 16;
		UINT _flags = 0;
		COLORREF _background = CLR_NONE;
		std::vector<std::shared_ptr<Bitmap>> _images;
	};

	std::unordered_set<ImageList*>& lists()
	{
		static auto* set = new std::unordered_set<ImageList*>;
		return *set;
	}

	ImageList* toList(HIMAGELIST handle)
	{
		auto* list = reinterpret_cast<ImageList*>(handle);
		return lists().count(list) ? list : nullptr;
	}

	std::shared_ptr<Bitmap> newImage(int width, int height)
	{
		auto image = std::make_shared<Bitmap>();
		image->_width = width;
		image->_height = height;
		image->_ownedPixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
		image->_pixels = image->_ownedPixels.data();
		image->_hasAlpha = true;
		return image;
	}

	// Copies part of a source bitmap scaled to the given size.
	std::shared_ptr<Bitmap> fitImage(int width, int height, Bitmap* source, int sourceX, int sourceWidth)
	{
		auto image = newImage(width, height);
		if (!source)
			return image;
		if (sourceWidth == width && source->_height == height)
		{
			for (int y = 0; y < height; ++y)
				std::copy_n(source->rowPointer(y) + sourceX, width, image->rowPointer(y));
			return image;
		}
		CGImageRef whole = source->createImage();
		CGImageRef part = CGImageCreateWithImageInRect(whole, CGRectMake(sourceX, 0, sourceWidth, source->_height));
		CGContextRef context = image->context();
		CGContextSaveGState(context);
		CGContextTranslateCTM(context, 0, height);
		CGContextScaleCTM(context, 1, -1);
		CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
		CGContextDrawImage(context, CGRectMake(0, 0, width, height), part);
		CGContextRestoreGState(context);
		CGImageRelease(part);
		CGImageRelease(whole);
		return image;
	}

	// Copies a source bitmap scaled to the list's size; a larger image it carries for Retina screens
	// comes along at twice that size.
	std::shared_ptr<Bitmap> fitImage(const ImageList& list, Bitmap* source, int sourceX, int sourceWidth)
	{
		auto image = fitImage(list._width, list._height, source, sourceX, sourceWidth);
		if (source && source->_hiRes && sourceX == 0)
			image->_hiRes = fitImage(list._width * 2, list._height * 2, source->_hiRes.get(), 0, source->_hiRes->_width);
		return image;
	}

	// A bitmap strip without alpha takes transparency from a mask bitmap or a mask color.
	void applyMask(Bitmap& image, Bitmap* mask, int maskX, COLORREF maskColor, bool useColor)
	{
		bool hasAlpha = false;
		for (const uint32_t pixel : image._ownedPixels)
		{
			if ((pixel >> 24) != 0xFF && (pixel >> 24) != 0)
				hasAlpha = true;
		}
		for (int y = 0; y < image._height; ++y)
		{
			uint32_t* row = image.rowPointer(y);
			for (int x = 0; x < image._width; ++x)
			{
				uint32_t& pixel = row[x];
				if (useColor)
				{
					const uint32_t key = (static_cast<uint32_t>(GetRValue(maskColor)) << 16) | (static_cast<uint32_t>(GetGValue(maskColor)) << 8) | GetBValue(maskColor);
					if ((pixel & 0xFFFFFF) == key)
						pixel = 0;
					else if (!hasAlpha)
						pixel |= 0xFF000000;
				}
				else if (mask && y < mask->_height && maskX + x < mask->_width)
				{
					const bool transparent = (mask->rowPointer(y)[maskX + x] & 0xFFFFFF) != 0;
					if (transparent)
						pixel = 0;
					else if (!hasAlpha)
						pixel |= 0xFF000000;
				}
				else if (!hasAlpha)
				{
					pixel |= 0xFF000000;
				}
			}
		}
	}

	int addBitmap(ImageList* list, HBITMAP bitmapHandle, HBITMAP maskHandle, COLORREF maskColor, bool useColor)
	{
		auto* bitmap = nppmac::gdi::objectAs<Bitmap>(bitmapHandle, nppmac::gdi::ObjectType::bitmap);
		auto* mask = nppmac::gdi::objectAs<Bitmap>(maskHandle, nppmac::gdi::ObjectType::bitmap);
		if (!list || !bitmap)
			return -1;
		// A strip: as many images as fit across.
		const int first = static_cast<int>(list->_images.size());
		const int count = std::max(1, bitmap->_width / std::max(1, list->_width));
		for (int i = 0; i < count; ++i)
		{
			auto image = fitImage(*list, bitmap, i * list->_width, std::min(list->_width, bitmap->_width));
			// A bitmap that carries its own alpha (LR_LOADTRANSPARENT, 32-bit DIBs) needs no mask.
			if (!bitmap->_hasAlpha || mask || useColor)
				applyMask(*image, mask, i * list->_width, maskColor, useColor);
			list->_images.push_back(image);
		}
		return first;
	}

	std::shared_ptr<Bitmap> iconImage(const ImageList& list, HICON iconHandle)
	{
		auto* icon = nppmac::gdi::objectAs<nppmac::gdi::Icon>(iconHandle, nppmac::gdi::ObjectType::icon);
		if (!icon || !icon->_color)
			return nullptr;
		return fitImage(list, icon->_color.get(), 0, icon->_color->_width);
	}

	// Drag images: the image follows the mouse in a small popup-less overlay. Not shown on macOS.
	struct DragState
	{
		HIMAGELIST _list = nullptr;
		int _index = -1;
	};

	DragState g_drag;
}

HIMAGELIST WINAPI ImageList_Create(int width, int height, UINT flags, int, int)
{
	auto* list = new ImageList;
	list->_width = std::max(1, width);
	list->_height = std::max(1, height);
	list->_flags = flags;
	lists().insert(list);
	return reinterpret_cast<HIMAGELIST>(list);
}

BOOL WINAPI ImageList_Destroy(HIMAGELIST handle)
{
	ImageList* list = toList(handle);
	if (!list)
		return FALSE;
	lists().erase(list);
	delete list;
	return TRUE;
}

int WINAPI ImageList_GetImageCount(HIMAGELIST handle)
{
	ImageList* list = toList(handle);
	return list ? static_cast<int>(list->_images.size()) : 0;
}

BOOL WINAPI ImageList_SetImageCount(HIMAGELIST handle, UINT count)
{
	ImageList* list = toList(handle);
	if (!list)
		return FALSE;
	while (list->_images.size() < count)
		list->_images.push_back(newImage(list->_width, list->_height));
	list->_images.resize(count);
	return TRUE;
}

int WINAPI ImageList_Add(HIMAGELIST handle, HBITMAP image, HBITMAP mask)
{
	return addBitmap(toList(handle), image, mask, 0, false);
}

int WINAPI ImageList_AddMasked(HIMAGELIST handle, HBITMAP image, COLORREF maskColor)
{
	return addBitmap(toList(handle), image, nullptr, maskColor, true);
}

int WINAPI ImageList_ReplaceIcon(HIMAGELIST handle, int index, HICON icon)
{
	ImageList* list = toList(handle);
	if (!list)
		return -1;
	auto image = iconImage(*list, icon);
	if (!image)
		return -1;
	if (index < 0 || static_cast<size_t>(index) >= list->_images.size())
	{
		list->_images.push_back(image);
		return static_cast<int>(list->_images.size()) - 1;
	}
	list->_images[static_cast<size_t>(index)] = image;
	return index;
}

BOOL WINAPI ImageList_Replace(HIMAGELIST handle, int index, HBITMAP image, HBITMAP mask)
{
	ImageList* list = toList(handle);
	if (!list || index < 0 || static_cast<size_t>(index) >= list->_images.size())
		return FALSE;
	const size_t before = list->_images.size();
	if (addBitmap(list, image, mask, 0, false) < 0)
		return FALSE;
	list->_images[static_cast<size_t>(index)] = list->_images[before];
	list->_images.resize(before);
	return TRUE;
}

BOOL WINAPI ImageList_Remove(HIMAGELIST handle, int index)
{
	ImageList* list = toList(handle);
	if (!list)
		return FALSE;
	if (index < 0)
	{
		list->_images.clear();
		return TRUE;
	}
	if (static_cast<size_t>(index) >= list->_images.size())
		return FALSE;
	list->_images.erase(list->_images.begin() + index);
	return TRUE;
}

BOOL WINAPI ImageList_GetIconSize(HIMAGELIST handle, int* width, int* height)
{
	ImageList* list = toList(handle);
	if (!list)
		return FALSE;
	*width = list->_width;
	*height = list->_height;
	return TRUE;
}

BOOL WINAPI ImageList_SetIconSize(HIMAGELIST handle, int width, int height)
{
	ImageList* list = toList(handle);
	if (!list)
		return FALSE;
	// Windows empties the list when the size changes.
	list->_width = std::max(1, width);
	list->_height = std::max(1, height);
	list->_images.clear();
	return TRUE;
}

COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST handle, COLORREF color)
{
	ImageList* list = toList(handle);
	if (!list)
		return CLR_NONE;
	const COLORREF previous = list->_background;
	list->_background = color;
	return previous;
}

COLORREF WINAPI ImageList_GetBkColor(HIMAGELIST handle)
{
	ImageList* list = toList(handle);
	return list ? list->_background : CLR_NONE;
}

BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST handle, int index, IMAGEINFO* info)
{
	ImageList* list = toList(handle);
	if (!list || index < 0 || static_cast<size_t>(index) >= list->_images.size())
		return FALSE;
	info->hbmImage = nullptr;
	info->hbmMask = nullptr;
	info->rcImage = {0, 0, list->_width, list->_height};
	return TRUE;
}

HICON WINAPI ImageList_GetIcon(HIMAGELIST handle, int index, UINT)
{
	ImageList* list = toList(handle);
	if (!list || index < 0 || static_cast<size_t>(index) >= list->_images.size())
		return nullptr;
	auto* icon = new nppmac::gdi::Icon;
	icon->_color = fitImage(*list, list->_images[static_cast<size_t>(index)].get(), 0, list->_width);
	return reinterpret_cast<HICON>(nppmac::gdi::trackObject(icon));
}

BOOL WINAPI ImageList_DrawEx(HIMAGELIST handle, int index, HDC hdc, int x, int y, int width, int height, COLORREF background, COLORREF, UINT style)
{
	ImageList* list = toList(handle);
	nppmac::gdi::DC* dc = nppmac::gdi::toDC(hdc);
	if (!list || !dc || index < 0 || static_cast<size_t>(index) >= list->_images.size())
		return FALSE;
	if (width <= 0)
		width = list->_width;
	if (height <= 0)
		height = list->_height;
	if (background != CLR_NONE && background != CLR_DEFAULT && !(style & ILD_TRANSPARENT))
	{
		HBRUSH brush = CreateSolidBrush(background);
		nppmac::gdi::fillRect(dc, {x, y, x + width, y + height}, brush);
		DeleteObject(brush);
	}
	CGContextRef context = dc->begin();
	if (context)
	{
		CGImageRef image = list->_images[static_cast<size_t>(index)]->createImageFor(context);
		CGContextSaveGState(context);
		CGContextTranslateCTM(context, x, y + height);
		CGContextScaleCTM(context, 1, -1);
		CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
		if (style & (ILD_BLEND25 | ILD_BLEND50))
			CGContextSetAlpha(context, 0.5);
		CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
		CGContextRestoreGState(context);
		CGImageRelease(image);
		dc->end();
	}
	return TRUE;
}

BOOL WINAPI ImageList_Draw(HIMAGELIST handle, int index, HDC hdc, int x, int y, UINT style)
{
	return ImageList_DrawEx(handle, index, hdc, x, y, 0, 0, CLR_DEFAULT, CLR_DEFAULT, style);
}

BOOL WINAPI ImageList_DrawIndirect(IMAGELISTDRAWPARAMS* params)
{
	return ImageList_DrawEx(params->himl, params->i, params->hdcDst, params->x, params->y, params->cx, params->cy,
		params->rgbBk, params->rgbFg, params->fStyle);
}

HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST handle)
{
	ImageList* list = toList(handle);
	if (!list)
		return nullptr;
	auto* copy = new ImageList(*list);
	lists().insert(copy);
	return reinterpret_cast<HIMAGELIST>(copy);
}

BOOL WINAPI ImageList_BeginDrag(HIMAGELIST list, int index, int, int)
{
	g_drag = {list, index};
	return TRUE;
}

BOOL WINAPI ImageList_DragEnter(HWND, int, int)
{
	return TRUE;
}

BOOL WINAPI ImageList_DragMove(int, int)
{
	return TRUE;
}

BOOL WINAPI ImageList_DragLeave(HWND)
{
	return TRUE;
}

BOOL WINAPI ImageList_DragShowNolock(BOOL)
{
	return TRUE;
}

void WINAPI ImageList_EndDrag()
{
	g_drag = {};
}
