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

// What GDI draws with: a canvas (a window's surface, a bitmap's pixels, a printed page), images, fonts
// and PDF documents. GDI keeps Windows' rules (coordinates, clipping, pens and brushes, raster
// operations, text alignment, DrawText's layout); this is only the rendering beneath them: Core Graphics
// and Core Text on macOS (gdi/macos), Qt's QPainter and fonts on Linux (gdi/linux). Plain C++ types
// only, so that both the Win32 side and the native side include it.
//
// Coordinates are GDI's: y down, one unit a point (a device pixel on a 1x screen). Colors are COLORREFs
// (0x00BBGGRR). Pixels are 32-bit BGRA, premultiplied, as GDI's bitmaps keep them.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nppmac::gdi
{
	using CanvasColor = uint32_t; // a COLORREF: 0x00BBGGRR
	using CanvasRGB = std::array<double, 3>; // red, green and blue from 0 to 1, for gradients' finer colors

	struct CanvasPoint
	{
		double _x = 0;
		double _y = 0;
	};

	struct CanvasRect
	{
		double _x = 0;
		double _y = 0;
		double _width = 0;
		double _height = 0;
	};

	enum class LineCap
	{
		butt,
		square,
	};

	// What a drawing does with what is under it: the raster operations GDI maps onto blending.
	enum class BlendMode
	{
		normal,     // source over
		copy,       // the source replaces what is there
		difference, // inverts where the source is white (R2_NOT, DSTINVERT, focus rectangles)
		lighten,    // SRCPAINT
		multiply,   // SRCAND
	};

	enum class Interpolation
	{
		none,
		high,
	};

	// An image: pixels to draw, made from a bitmap's (not copied: the bitmap outlives the image), cut from
	// another, or read back from a surface.
	class CanvasImage
	{
	public:
		virtual ~CanvasImage() = default;
		virtual int width() const = 0;  // in pixels
		virtual int height() const = 0;
		// A part of the image (pixels; clamped to it), or null when nothing is left.
		virtual std::shared_ptr<CanvasImage> cropped(const CanvasRect& area) const = 0;
	};

	// Pixels read top row first; a bottom-up bitmap's image is its rows as they are in memory, as on
	// Windows the bits of a bottom-up DIB section are.
	std::shared_ptr<CanvasImage> imageFromPixels(const uint32_t* pixels, int width, int height);

	// What GDI asks of a font: the face it was given (Windows' name, after the shared substitutions) and
	// the platform's choice of the closest one it has.
	struct FontRequest
	{
		std::u16string _face;     // Windows' face name
		bool _systemFace = false; // a Windows UI face (MS Shell Dlg, Segoe UI...): the system's UI font
		double _size = 12;        // em size, or the cell height with _cellHeight
		bool _cellHeight = false; // LOGFONT's positive height: ascent + descent
		bool _bold = false;
		bool _italic = false;
		bool _fixedPitch = false; // FIXED_PITCH: a monospaced fallback for an unknown face
	};

	class NativeFont
	{
	public:
		virtual ~NativeFont() = default;
		virtual double ascent() const = 0;
		virtual double descent() const = 0;
		virtual double leading() const = 0;  // the external leading
		virtual double size() const = 0;     // the em size
		virtual double xHeight() const = 0;
		virtual double maxCharWidth() const = 0;
		virtual bool monospaced() const = 0;
		// The advance of a run of text, kerned and with fallback fonts as drawing does it.
		virtual double width(std::u16string_view text) const = 0;
	};

	std::unique_ptr<NativeFont> createFont(const FontRequest& request);

	struct FontFamily
	{
		std::u16string _name;
		bool _monospaced = false;
	};
	// The installed font families, as the system names them (hidden ones left out).
	std::vector<FontFamily> fontFamilies();

	// Drawing state is saved and restored as a whole (save/restore); colors, line and blending settings
	// stay until changed or restored.
	class Canvas
	{
	public:
		virtual ~Canvas() = default;

		virtual void save() = 0;
		virtual void restore() = 0;
		// Clipping only narrows; restore() widens it again. An empty list clips everything away.
		virtual void clipToRect(const CanvasRect& rect) = 0;
		virtual void clipToRects(std::span<const CanvasRect> rects) = 0;
		virtual void translate(double dx, double dy) = 0;
		virtual void scale(double sx, double sy) = 0;

		virtual void setAntialias(bool antialias) = 0;
		virtual void setBlendMode(BlendMode mode) = 0;
		virtual void setAlpha(double alpha) = 0; // applied to everything drawn
		virtual void setInterpolation(Interpolation interpolation) = 0;
		virtual void setFillColor(CanvasColor color) = 0;
		virtual void setStrokeColor(CanvasColor color) = 0;
		virtual void setLineWidth(double width) = 0;
		virtual void setLineCap(LineCap cap) = 0;
		virtual void setLineDash(std::span<const double> lengths) = 0; // empty: solid

		virtual void fillRect(const CanvasRect& rect) = 0;
		virtual void strokeRect(const CanvasRect& rect) = 0;
		virtual void strokeLine(CanvasPoint from, CanvasPoint to) = 0;
		virtual void drawPolygon(std::span<const CanvasPoint> points, bool fill, bool stroke) = 0;
		virtual void drawRoundedRect(const CanvasRect& rect, double radiusX, double radiusY, bool fill, bool stroke) = 0;
		virtual void drawEllipse(const CanvasRect& rect, bool fill, bool stroke) = 0;
		// A gradient from one color at one point to another at the other, filling the clip.
		virtual void fillLinearGradient(CanvasPoint from, const CanvasRGB& fromColor, CanvasPoint to, const CanvasRGB& toColor) = 0;
		// The image's top row at the rectangle's top.
		virtual void drawImage(const CanvasImage& image, const CanvasRect& destination) = 0;
		// A run of text, its baseline at y, in the fill color's place: the given color.
		virtual void drawText(const NativeFont& font, std::u16string_view text, double x, double baseline, CanvasColor color) = 0;

		// Device pixels per unit: 2 on a Retina surface.
		virtual double deviceScale() const = 0;
		// What the whole surface shows now, at its device scale (BitBlt from a window).
		virtual std::shared_ptr<CanvasImage> snapshot() const = 0;
		// The platform's own drawing object: a CGContextRef on macOS, a QPainter* on Linux (Scintilla
		// prints through it).
		virtual void* nativeHandle() const = 0;
	};

	// A canvas over a bitmap's pixels (kept by the bitmap). A bottom-up bitmap's rows run upward in memory;
	// its canvas still has GDI's top-left origin.
	std::unique_ptr<Canvas> canvasOverPixels(uint32_t* pixels, int width, int height, bool bottomUp);
	// A canvas over a window's surface, the window backend's native drawing object (a CGContextRef on
	// macOS, a QPainter* on Linux); kept alive by the canvas.
	std::unique_ptr<Canvas> canvasOverSurface(void* surface);

	// A PDF document, for printing: pages of the given size in points, drawn on with a canvas whose origin
	// is the page's top-left corner.
	class PdfDocument
	{
	public:
		virtual ~PdfDocument() = default;
		virtual Canvas* beginPage() = 0;
		virtual void endPage() = 0;
		virtual void close() = 0;
	};

	std::unique_ptr<PdfDocument> createPdf(const std::string& path, double pageWidth, double pageHeight, std::u16string_view title);

	// A PNG file's image as BGRA premultiplied pixels, top row first. False when it cannot be read.
	bool decodePng(std::span<const unsigned char> data, int& width, int& height, std::vector<uint32_t>& pixels);
}
