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

// Tests of icons on Retina screens: an icon loaded at its Windows size keeps the file's image of twice
// that size, and image lists and DrawIconEx draw it where a surface has two pixels per point.

#include "gdi/Gdi.h"

#include <commctrl.h>
#include <cstdlib>
#include <iostream>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void check(bool condition, const char* expression, int line)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::cerr << "FAIL line " << line << ": " << expression << "\n";
		}
	}

#define CHECK(expression) check((expression), #expression, __LINE__)

	// Draws image 0 of the list on a surface of the given scale (pixels per point) and returns how
	// many pixels differ from the expected image by more than a rounding error. The surface is
	// opaque black: there, a premultiplied image leaves exactly its own color channels.
	int drawAndCompare(HIMAGELIST list, int scale, const nppmac::gdi::Bitmap& expected)
	{
		HDC hdc = CreateCompatibleDC(nullptr);
		HBITMAP surface = CreateBitmap(16 * scale, 16 * scale, 1, 32, nullptr);
		HGDIOBJ old = SelectObject(hdc, surface);
		auto* bitmap = nppmac::gdi::objectAs<nppmac::gdi::Bitmap>(surface, nppmac::gdi::ObjectType::bitmap);
		CGContextScaleCTM(bitmap->context(), scale, scale);
		ImageList_Draw(list, 0, hdc, 0, 0, ILD_TRANSPARENT);
		int differences = 0;
		for (int y = 0; y < expected._height; ++y)
		{
			for (int x = 0; x < expected._width; ++x)
			{
				const uint32_t a = bitmap->rowPointer(y)[x];
				const uint32_t b = const_cast<nppmac::gdi::Bitmap&>(expected).rowPointer(y)[x];
				for (int shift = 0; shift < 24; shift += 8)
				{
					if (std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)) > 2)
					{
						++differences;
						break;
					}
				}
			}
		}
		SelectObject(hdc, old);
		DeleteObject(surface);
		DeleteDC(hdc);
		return differences;
	}
}

int main()
{
	const std::u16string path = nppmac::toUtf16(NPP_ROOT "/PowerEditor/src/icons/light/toolbar/filled/copy_off.ico");
	HICON icon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
	auto* object = nppmac::gdi::objectAs<nppmac::gdi::Icon>(icon, nppmac::gdi::ObjectType::icon);
	CHECK(object && object->_color);
	if (!object || !object->_color)
		return EXIT_FAILURE;
	CHECK(object->_color->_width == 16);
	CHECK(object->_color->_hiRes && object->_color->_hiRes->_width == 32);

	HIMAGELIST list = ImageList_Create(16, 16, ILC_COLOR32, 1, 1);
	CHECK(ImageList_AddIcon(list, icon) == 0);
	// Two pixels per point: the 32-pixel image, unscaled. One: the 16-pixel image.
	CHECK(drawAndCompare(list, 2, *object->_color->_hiRes) == 0);
	CHECK(drawAndCompare(list, 1, *object->_color) == 0);

	ImageList_Destroy(list);
	DestroyIcon(icon);
	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
