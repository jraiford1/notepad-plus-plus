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

// GDI: the objects behind HDC, HPEN, HBRUSH, HFONT, HBITMAP and HRGN, drawing on the platform's canvas
// (Canvas.h: Core Graphics and Core Text on macOS, Qt on Linux). One GDI unit is one point; canvases are
// already scaled for the display.

#pragma once

#include <windows.h>

#include "Canvas.h"
#include "platform/Platform.h"

#include <memory>
#include <string>
#include <vector>

namespace nppmac::user
{
	class Window;
}

namespace nppmac::gdi
{
	enum class ObjectType
	{
		pen,
		brush,
		font,
		bitmap,
		region,
		palette,
		icon,
	};

	struct GdiObject
	{
		explicit GdiObject(ObjectType type) : _type{type} {}
		virtual ~GdiObject() = default;

		ObjectType _type;
		bool _stock = false;
		unsigned _selectedCount = 0; // a selected object is not deleted until it is deselected
		bool _deletePending = false;
	};

	struct Pen : GdiObject
	{
		Pen() : GdiObject{ObjectType::pen} {}
		int _style = PS_SOLID;
		int _width = 1;
		COLORREF _color = 0;
	};

	struct Bitmap;

	struct Brush : GdiObject
	{
		Brush() : GdiObject{ObjectType::brush} {}
		UINT _style = BS_SOLID;
		COLORREF _color = 0;
		int _hatch = 0;
		std::shared_ptr<Bitmap> _pattern; // a copy of the pattern bitmap
		int _systemColor = -1;             // brushes of GetSysColorBrush follow the system colors
		bool _dcBrush = false;             // the DC_BRUSH stock object
	};

	struct Font : GdiObject
	{
		Font() : GdiObject{ObjectType::font} {}
		LOGFONTW _log{};
		std::unique_ptr<NativeFont> _native; // created on first use
		const NativeFont& native();
	};

	// The largest bitmap the runtime makes: 2^28 pixels (1 GB, 16384 x 16384). Windows fails a bitmap it
	// cannot allocate; here a misread header (a corrupt or unexpected format) must fail too, rather than
	// take all the memory there is.
	constexpr long long maxBitmapPixels = 1LL << 28;

	inline bool isAllowedBitmapSize(long long width, long long height)
	{
		return width > 0 && height > 0 && width <= maxBitmapPixels / height;
	}

	// A device-independent bitmap: 32-bit BGRA (premultiplied), top-down rows, as a top-down DIB.
	// Monochrome and other depths are converted on creation.
	struct Bitmap : GdiObject
	{
		Bitmap() : GdiObject{ObjectType::bitmap} {}
		int _width = 0;
		int _height = 0;
		int _bitsPerPixel = 32;   // what GetObject reports (1 for monochrome bitmaps)
		bool _bottomUp = false;   // a bottom-up DIB section (its bits are seen upside down)
		bool _hasAlpha = false;   // the bits carry real alpha (AlphaBlend with AC_SRC_ALPHA)
		std::vector<uint32_t> _ownedPixels;
		uint32_t* _pixels = nullptr; // _ownedPixels.data(), or the DIB section's bits
		std::unique_ptr<Canvas> _canvas;
		// The same image with twice the pixels, for Retina screens: an icon file's larger image. The
		// program sees only this bitmap's size; drawing on a Retina surface uses the larger image.
		std::shared_ptr<Bitmap> _hiRes;

		// A canvas drawing into the pixels with a top-left origin.
		Canvas* canvas();
		std::shared_ptr<CanvasImage> image() const;
		// The image to draw on a canvas: the larger one where the canvas has more than one pixel per
		// point (Retina), when there is one.
		std::shared_ptr<CanvasImage> imageFor(const Canvas* canvas) const;
		uint32_t* rowPointer(int y) {
			return _pixels + static_cast<size_t>(_bottomUp ? _height - 1 - y : y) * static_cast<size_t>(_width);
		}
	};

	struct Region : GdiObject
	{
		Region() : GdiObject{ObjectType::region} {}
		std::vector<RECT> _rects; // non-overlapping is not required: the region is their union
		RECT box() const;
		bool contains(POINT point) const;
	};

	struct Icon : GdiObject
	{
		Icon() : GdiObject{ObjectType::icon} {}
		std::shared_ptr<Bitmap> _color;
		std::shared_ptr<Bitmap> _mask;
		bool _isIcon = true;
		POINT _hotspot{};
		bool _shared = false; // loaded with LR_SHARED or a system icon: never destroyed
		int _cursorShape = -1; // system cursors: the ui::CursorShape they stand for
	};

	enum class DCKind
	{
		window,  // a window's surface (GetDC, BeginPaint)
		memory,  // CreateCompatibleDC: draws into the selected bitmap
		info,    // a screen DC used for measurement only
		printer, // PrintDlg's DC: draws into the print job's current page
	};

	// A print job, StartDoc to EndDoc: a PDF of the pages, printed when it ends.
	struct PrintDocument
	{
		platform::PrintSetup _setup;
		std::string _path;   // the PDF
		std::u16string _name;
		std::unique_ptr<PdfDocument> _pdf;
		Canvas* _page = nullptr; // the page being drawn, between StartPage and EndPage

		~PrintDocument();
	};

	// Printer device units are 1/96 inch, as the screen's are here, so text measures the same on
	// paper as on screen; a PDF page's canvas works in points.
	constexpr double printerUnitsPerPoint = 96.0 / 72.0;

	struct DCState
	{
		HPEN _pen = nullptr;
		HBRUSH _brush = nullptr;
		HFONT _font = nullptr;
		COLORREF _textColor = 0;
		COLORREF _backgroundColor = 0xFFFFFF;
		COLORREF _dcBrushColor = 0xFFFFFF;
		COLORREF _dcPenColor = 0;
		int _backgroundMode = OPAQUE;
		UINT _textAlign = TA_LEFT | TA_TOP;
		int _rop2 = R2_COPYPEN;
		int _stretchMode = BLACKONWHITE;
		POINT _position{};
		POINT _windowOrigin{};   // logical coordinates of the device origin
		POINT _viewportOrigin{}; // device coordinates of the logical origin
		POINT _brushOrigin{};
		std::vector<RECT> _clip;  // in device coordinates; empty with _clipped set means nothing visible
		bool _clipped = false;
	};

	struct DC
	{
		DCKind _kind = DCKind::info;
		user::Window* _window = nullptr;
		HBITMAP _bitmap = nullptr;    // memory DCs: the selected bitmap
		Canvas* _canvas = nullptr;   // the selected bitmap's, the window surface's or the printed page's
		std::unique_ptr<Canvas> _surface; // window DCs: their surface's canvas, kept until released
		int _width = 0;              // device size
		int _height = 0;
		DCState _state;
		std::vector<DCState> _saved;
		RECT _paintClip{};           // BeginPaint's update rectangle, intersected with every clip
		bool _hasPaintClip = false;
		std::shared_ptr<PrintDocument> _print; // printer DCs

		// Prepares the canvas for drawing: clip and logical-to-device offset applied. The caller
		// must call end() afterwards.
		Canvas* begin();
		void end();
	};

	// --- Handles ----------------------------------------------------------------------------------

	DC* toDC(HDC hdc);
	HDC fromDC(DC* dc);
	GdiObject* toObject(HGDIOBJ handle);
	// Registers a new object; its handle is the object's address.
	HGDIOBJ trackObject(GdiObject* object);
	template <typename T>
	T* objectAs(HGDIOBJ handle, ObjectType type)
	{
		GdiObject* object = toObject(handle);
		return object && object->_type == type ? static_cast<T*>(object) : nullptr;
	}

	HDC createWindowDC(user::Window* window);
	// A printer DC for the paper and printable area chosen; its size is the printable area.
	HDC createPrinterDC(const platform::PrintSetup& setup);
	void releaseWindowDC(HDC hdc);

	// --- Colors and conversions ---------------------------------------------------------------------

	inline CanvasRect toCanvasRect(const RECT& rect)
	{
		return {static_cast<double>(rect.left), static_cast<double>(rect.top), static_cast<double>(rect.right - rect.left),
			static_cast<double>(rect.bottom - rect.top)};
	}

	// Fills a rectangle (device context coordinates) with a brush, honouring the DC's clip.
	void fillRect(DC* dc, const RECT& rect, HBRUSH brush);
	COLORREF brushColor(DC* dc, HBRUSH brush);
	COLORREF penColor(DC* dc, Pen* pen);

	// --- Text -------------------------------------------------------------------------------------

	const NativeFont& fontFor(DC* dc);
	// Width of a run of text in the DC's font.
	double textWidth(const NativeFont& font, std::u16string_view text);
	void drawText(DC* dc, Canvas* canvas, std::u16string_view text, double x, double baseline, COLORREF color);
}
