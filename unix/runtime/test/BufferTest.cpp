// This file is part of Notepad++ project (macOS and Linux build)
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

// A double buffer for a scaled screen: a bitmap compatible with a DC that draws at more than one pixel
// per unit has as many, and is copied to such a DC pixel for pixel; GetPixel and SetPixel see one pixel
// per unit, as the program expects.

#include "gdi/Gdi.h"

#include <cstdlib>
#include <iostream>
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

	nppmac::gdi::Bitmap* bitmapOf(HBITMAP handle)
	{
		return nppmac::gdi::objectAs<nppmac::gdi::Bitmap>(handle, nppmac::gdi::ObjectType::bitmap);
	}

	void testScale(double scale, int scaledWidth, int scaledHeight)
	{
		// The screen: a memory DC holding a bitmap at the scale, as a window's surface is.
		HDC screen = CreateCompatibleDC(nullptr);
		HBITMAP surface = CreateBitmap(6, 3, 1, 32, nullptr);
		nppmac::gdi::Bitmap* target = bitmapOf(surface);
		target->makeScaled(scale);
		HGDIOBJ oldSurface = SelectObject(screen, surface);

		HDC buffer = CreateCompatibleDC(screen);
		HBITMAP back = CreateCompatibleBitmap(buffer, 6, 3);
		nppmac::gdi::Bitmap* source = bitmapOf(back);
		CHECK(source && source->scaled() && source->_scale == scale);
		CHECK(source && source->_width == 6 && source->_height == 3);
		CHECK(source && source->_scaledWidth == scaledWidth && source->_scaledHeight == scaledHeight);
		if (!source)
			return;
		HGDIOBJ oldBack = SelectObject(buffer, back);
		BITMAP info{};
		CHECK(GetObjectW(back, sizeof(info), &info) == sizeof(info) && info.bmWidth == 6 && info.bmBits == nullptr);

		// Every scaled pixel different, as fine drawing leaves them.
		RECT all{0, 0, 6, 3};
		FillRect(buffer, &all, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		source->willDraw();
		for (size_t i = 0; i < source->_scaledPixels.size(); ++i)
			source->_scaledPixels[i] = 0xFF000000 | static_cast<uint32_t>(i);

		// Copied whole, then in part, pixel for pixel.
		CHECK(BitBlt(screen, 0, 0, 6, 3, buffer, 0, 0, SRCCOPY));
		target->image();
		CHECK(target->_scaledPixels == source->_scaledPixels);
		const std::vector<uint32_t> before = target->_scaledPixels;
		source->willDraw();
		for (uint32_t& pixel : source->_scaledPixels)
			pixel |= 0x00FF0000;
		CHECK(BitBlt(screen, 3, 0, 3, 3, buffer, 3, 0, SRCCOPY));
		const int split = static_cast<int>(3 * scale + 0.5);
		bool leftKept = true;
		bool rightCopied = true;
		for (int y = 0; y < scaledHeight; ++y)
		{
			for (int x = 0; x < scaledWidth; ++x)
			{
				const size_t i = static_cast<size_t>(y * scaledWidth + x);
				if (x < split)
					leftKept = leftKept && target->_scaledPixels[i] == before[i];
				else
					rightCopied = rightCopied && target->_scaledPixels[i] == source->_scaledPixels[i];
			}
		}
		CHECK(leftKept);
		CHECK(rightCopied);

		// A unit pixel is the scaled pixel at its centre; written, it is every scaled pixel there.
		const int cx = static_cast<int>(1.5 * scale);
		const int cy = static_cast<int>(1.5 * scale);
		const uint32_t centre = source->_scaledPixels[static_cast<size_t>(cy * scaledWidth + cx)];
		CHECK(GetPixel(buffer, 1, 1) == RGB((centre >> 16) & 0xFF, (centre >> 8) & 0xFF, centre & 0xFF));
		SetPixel(buffer, 0, 0, RGB(255, 0, 0));
		CHECK(GetPixel(buffer, 0, 0) == RGB(255, 0, 0));
		CHECK(BitBlt(screen, 0, 0, 6, 3, buffer, 0, 0, SRCCOPY));
		target->image();
		CHECK(target->_scaledPixels[0] == 0xFFFF0000);
		CHECK(target->_scaledPixels[1] == 0xFFFF0000);
		CHECK(target->_scaledPixels[static_cast<size_t>(scaledWidth)] == 0xFFFF0000);

		// Drawing after a pixel was written starts from the unit pixels.
		RECT corner{5, 2, 6, 3};
		FillRect(buffer, &corner, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		CHECK(GetPixel(buffer, 0, 0) == RGB(255, 0, 0));
		CHECK(GetPixel(buffer, 5, 2) == RGB(0, 0, 0));

		SelectObject(buffer, oldBack);
		DeleteObject(back);
		DeleteDC(buffer);
		SelectObject(screen, oldSurface);
		DeleteObject(surface);
		DeleteDC(screen);
	}
}

int main()
{
	testScale(2, 12, 6);
	testScale(5.0 / 3.0, 10, 5);

	// A memory DC made for nothing (the screen DC here) makes bitmaps of one pixel per unit.
	HDC plain = CreateCompatibleDC(nullptr);
	HBITMAP bitmap = CreateCompatibleBitmap(plain, 4, 4);
	CHECK(bitmapOf(bitmap) && !bitmapOf(bitmap)->scaled());
	DeleteObject(bitmap);
	DeleteDC(plain);

	std::cout << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
