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

// The canvas under GDI (gdi/Canvas.h), on whichever platform this is built: the exact pixels GDI relies
// on, the same on macOS (Core Graphics) and Linux (Qt). Text is left out: it needs the application.

#include "gdi/Canvas.h"

#include <cstdio>
#include <vector>

namespace
{
	using namespace nppmac::gdi;

	int g_failures = 0;

	void check(bool condition, const char* expression, int line)
	{
		if (!condition)
		{
			std::printf("FAIL line %d: %s\n", line, expression);
			++g_failures;
		}
	}

#define CHECK(expression) check((expression), #expression, __LINE__)

	constexpr CanvasColor red = 0x0000FF;   // a COLORREF
	constexpr uint32_t redPixel = 0xFFFF0000; // BGRA in memory: 0xAARRGGBB as a number
	constexpr uint32_t white = 0xFFFFFFFF;
	constexpr uint32_t black = 0xFF000000;

	struct Pixels
	{
		int _width;
		int _height;
		std::vector<uint32_t> _data;

		Pixels(int width, int height, uint32_t fill = 0) : _width{width}, _height{height}, _data(static_cast<size_t>(width * height), fill) {}

		uint32_t at(int x, int y) const
		{
			return _data[static_cast<size_t>(y * _width + x)];
		}
	};

	int count(const Pixels& pixels, uint32_t value)
	{
		int result = 0;
		for (const uint32_t pixel : pixels._data)
			result += pixel == value;
		return result;
	}

	void testFill()
	{
		Pixels pixels(4, 4);
		{
			auto canvas = canvasOverPixels(pixels._data.data(), 4, 4, false);
			canvas->setFillColor(red);
			canvas->fillRect({1, 1, 2, 2});
		}
		CHECK(pixels.at(1, 1) == redPixel);
		CHECK(pixels.at(2, 2) == redPixel);
		CHECK(pixels.at(0, 0) == 0);
		CHECK(pixels.at(3, 3) == 0);
		CHECK(count(pixels, redPixel) == 4);
	}

	void testBottomUp()
	{
		// GDI's top row is a bottom-up bitmap's last row in memory.
		Pixels pixels(4, 4);
		{
			auto canvas = canvasOverPixels(pixels._data.data(), 4, 4, true);
			canvas->setFillColor(red);
			canvas->fillRect({0, 0, 4, 1});
		}
		CHECK(pixels.at(0, 3) == redPixel);
		CHECK(pixels.at(3, 3) == redPixel);
		CHECK(pixels.at(0, 0) == 0);
		CHECK(count(pixels, redPixel) == 4);
	}

	void testClipAndTranslate()
	{
		Pixels pixels(8, 8);
		{
			auto canvas = canvasOverPixels(pixels._data.data(), 8, 8, false);
			canvas->save();
			const CanvasRect clip[] = {{0, 0, 2, 2}, {6, 6, 2, 2}};
			canvas->clipToRects(clip);
			canvas->setFillColor(red);
			canvas->fillRect({0, 0, 8, 8});
			canvas->restore();
			// The clip is gone after restore; a translation moves what follows.
			canvas->save();
			canvas->translate(3, 3);
			canvas->setFillColor(0xFFFFFF);
			canvas->fillRect({0, 0, 1, 1});
			canvas->restore();
			// Nothing in the list: everything clipped away.
			canvas->save();
			canvas->clipToRects({});
			canvas->setFillColor(0xFFFFFF);
			canvas->fillRect({0, 0, 8, 8});
			canvas->restore();
		}
		CHECK(count(pixels, redPixel) == 8);
		CHECK(pixels.at(1, 1) == redPixel);
		CHECK(pixels.at(7, 7) == redPixel);
		CHECK(pixels.at(3, 3) == white);
		CHECK(count(pixels, white) == 1);
	}

	void testDifference()
	{
		// White with the difference blend inverts (InvertRect, DSTINVERT, focus rectangles).
		Pixels pixels(2, 1, black);
		pixels._data[1] = redPixel;
		{
			auto canvas = canvasOverPixels(pixels._data.data(), 2, 1, false);
			canvas->setBlendMode(BlendMode::difference);
			canvas->setFillColor(0xFFFFFF);
			canvas->fillRect({0, 0, 2, 1});
		}
		CHECK(pixels.at(0, 0) == white);
		CHECK(pixels.at(1, 0) == 0xFF00FFFF);
	}

	void testImages()
	{
		// A 2x2 image drawn at 4x4 without interpolation: each pixel becomes a 2x2 block.
		const uint32_t source[] = {redPixel, white, black, 0xFF00FF00};
		const std::shared_ptr<CanvasImage> image = imageFromPixels(source, 2, 2);
		CHECK(image && image->width() == 2 && image->height() == 2);
		Pixels pixels(4, 4);
		{
			auto canvas = canvasOverPixels(pixels._data.data(), 4, 4, false);
			canvas->setBlendMode(BlendMode::copy);
			canvas->setInterpolation(Interpolation::none);
			canvas->drawImage(*image, {0, 0, 4, 4});
		}
		CHECK(pixels.at(0, 0) == redPixel);
		CHECK(pixels.at(1, 1) == redPixel);
		CHECK(pixels.at(3, 0) == white);
		CHECK(pixels.at(0, 3) == black);
		CHECK(pixels.at(3, 3) == 0xFF00FF00);

		// A part, as BitBlt takes one.
		const std::shared_ptr<CanvasImage> part = image->cropped({1, 0, 1, 2});
		CHECK(part && part->width() == 1 && part->height() == 2);
		Pixels target(1, 2);
		{
			auto canvas = canvasOverPixels(target._data.data(), 1, 2, false);
			canvas->setBlendMode(BlendMode::copy);
			canvas->drawImage(*part, {0, 0, 1, 2});
		}
		CHECK(target.at(0, 0) == white);
		CHECK(target.at(0, 1) == 0xFF00FF00);
		CHECK(image->cropped({5, 5, 1, 1}) == nullptr);

		// Drawn on a scaled canvas, an image covers its size in device pixels.
		Pixels scaled(4, 4);
		{
			auto canvas = canvasOverPixels(scaled._data.data(), 4, 4, false);
			canvas->scale(2, 2);
			CHECK(canvas->deviceScale() == 2);
			canvas->setBlendMode(BlendMode::copy);
			canvas->setInterpolation(Interpolation::none);
			canvas->drawImage(*image, {0, 0, 2, 2});
		}
		CHECK(scaled.at(1, 1) == redPixel);
		CHECK(scaled.at(1, 3) == black);
		CHECK(scaled.at(2, 3) == 0xFF00FF00);
	}

	void testLines()
	{
		// A one-unit line along a row's middle, without antialiasing, colors exactly that row
		// (LineTo's thin lines).
		Pixels pixels(4, 3);
		{
			auto canvas = canvasOverPixels(pixels._data.data(), 4, 3, false);
			canvas->setAntialias(false);
			canvas->setStrokeColor(red);
			canvas->setLineWidth(1);
			canvas->setLineCap(LineCap::butt);
			canvas->strokeLine({0, 1.5}, {4, 1.5});
		}
		CHECK(pixels.at(0, 1) == redPixel);
		CHECK(pixels.at(3, 1) == redPixel);
		CHECK(count(pixels, redPixel) == 4);

		// A rectangle's outline, as Rectangle strokes it (half a unit in).
		Pixels frame(4, 4);
		{
			auto canvas = canvasOverPixels(frame._data.data(), 4, 4, false);
			canvas->setAntialias(false);
			canvas->setStrokeColor(red);
			canvas->setLineWidth(1);
			canvas->strokeRect({0.5, 0.5, 3, 3});
		}
		CHECK(frame.at(0, 0) == redPixel);
		CHECK(frame.at(3, 3) == redPixel);
		CHECK(frame.at(1, 1) == 0);
		CHECK(count(frame, redPixel) == 12);
	}
}

int main()
{
	testFill();
	testBottomUp();
	testClipAndTranslate();
	testDifference();
	testImages();
	testLines();
	if (g_failures == 0)
		std::printf("canvas: all passed\n");
	return g_failures == 0 ? 0 : 1;
}
