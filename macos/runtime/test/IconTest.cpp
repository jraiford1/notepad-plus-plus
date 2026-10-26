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
// that size, and image lists and DrawIconEx draw it where a surface has two pixels per point. Also
// .bmp files: OS/2's header is read, and a header with an impossible size fails instead of allocating.

#include "gdi/Gdi.h"

#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

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

	// Writes a .bmp file: a file header, an OS/2 header (BITMAPCOREHEADER) of the given size, and
	// 24-bit pixels, all of one color (blue, green, red). Returns its path.
	std::string writeCoreBitmap(int width, int height, int pixelRows, const BYTE (&color)[3])
	{
		const size_t stride = ((static_cast<size_t>(width) * 24 + 31) / 32) * 4;
		std::vector<BYTE> pixels(stride * static_cast<size_t>(pixelRows));
		for (int y = 0; y < pixelRows; ++y)
		{
			for (int x = 0; x < width; ++x)
				std::memcpy(&pixels[static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 3], color, 3);
		}
		BITMAPCOREHEADER core{};
		core.bcSize = sizeof(core);
		core.bcWidth = static_cast<WORD>(width);
		core.bcHeight = static_cast<WORD>(height);
		core.bcPlanes = 1;
		core.bcBitCount = 24;
		const DWORD bitsOffset = 14 + sizeof(core);
		const DWORD fileSize = bitsOffset + static_cast<DWORD>(pixels.size());
		std::vector<BYTE> file{'B', 'M'};
		file.resize(14);
		std::memcpy(&file[2], &fileSize, 4);
		std::memcpy(&file[10], &bitsOffset, 4);
		file.insert(file.end(), reinterpret_cast<const BYTE*>(&core), reinterpret_cast<const BYTE*>(&core) + sizeof(core));
		file.insert(file.end(), pixels.begin(), pixels.end());
		char path[] = "/tmp/npp-bitmap-XXXXXX";
		const int fd = mkstemp(path);
		write(fd, file.data(), file.size());
		close(fd);
		return path;
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

	// An OS/2 bitmap (BlitzSearch's toolbar image): its 16-bit sizes, not a BITMAPINFOHEADER's.
	const BYTE red[3] = {0x00, 0x00, 0xFF};
	const std::string os2 = writeCoreBitmap(20, 20, 20, red);
	HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(nullptr, nppmac::toUtf16(os2).c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE));
	auto* loaded = nppmac::gdi::objectAs<nppmac::gdi::Bitmap>(bitmap, nppmac::gdi::ObjectType::bitmap);
	CHECK(loaded && loaded->_width == 20 && loaded->_height == 20);
	CHECK(loaded && (loaded->rowPointer(10)[10] & 0xFFFFFF) == 0xFF0000);
	DeleteObject(bitmap);
	unlink(os2.c_str());

	// A header promising more pixels than the file holds fails, and so does an impossible size.
	const std::string truncated = writeCoreBitmap(20, 20, 5, red);
	CHECK(LoadImageW(nullptr, nppmac::toUtf16(truncated).c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE) == nullptr);
	unlink(truncated.c_str());
	CHECK(CreateBitmap(1 << 20, 1 << 20, 1, 32, nullptr) == nullptr);
	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
