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

// Canvas.h on macOS: Core Graphics, Core Text and ImageIO. Every context GDI draws in has a top-left
// origin (flipped at creation, or by the window surface), so images and text are drawn with a local
// flip.

#include "../Canvas.h"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>

namespace nppmac::gdi
{
	namespace
	{
		CGRect toCGRect(const CanvasRect& rect)
		{
			return CGRectMake(rect._x, rect._y, rect._width, rect._height);
		}

		double red(CanvasColor color)
		{
			return (color & 0xFF) / 255.0;
		}

		double green(CanvasColor color)
		{
			return ((color >> 8) & 0xFF) / 255.0;
		}

		double blue(CanvasColor color)
		{
			return ((color >> 16) & 0xFF) / 255.0;
		}

		CGColorSpaceRef sRGB()
		{
			static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
			return space;
		}

		constexpr uint32_t bgraPremultiplied = static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little);

		CFStringRef createString(std::u16string_view text)
		{
			return CFStringCreateWithCharacters(nullptr, reinterpret_cast<const UniChar*>(text.data()), static_cast<CFIndex>(text.size()));
		}

		std::u16string fromCFString(CFStringRef string)
		{
			const CFIndex length = CFStringGetLength(string);
			std::u16string result(static_cast<size_t>(length), u'\0');
			CFStringGetCharacters(string, CFRangeMake(0, length), reinterpret_cast<UniChar*>(result.data()));
			return result;
		}

		bool sameName(std::u16string_view a, std::u16string_view b)
		{
			const auto lower = [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c; };
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [&](char16_t x, char16_t y) { return lower(x) == lower(y); });
		}

		// --- Images -------------------------------------------------------------------------------

		class Image : public CanvasImage
		{
		public:
			explicit Image(CGImageRef image) : _image{image} {}
			~Image() override
			{
				if (_image)
					CGImageRelease(_image);
			}

			int width() const override
			{
				return static_cast<int>(CGImageGetWidth(_image));
			}

			int height() const override
			{
				return static_cast<int>(CGImageGetHeight(_image));
			}

			std::shared_ptr<CanvasImage> cropped(const CanvasRect& area) const override
			{
				CGImageRef part = CGImageCreateWithImageInRect(_image, toCGRect(area));
				return part ? std::make_shared<Image>(part) : nullptr;
			}

			CGImageRef _image;
		};

		// --- Fonts --------------------------------------------------------------------------------

		// Windows faces that macOS does not have, and what stands in for them.
		std::u16string substituteFace(std::u16string_view face)
		{
			static const std::pair<const char16_t*, const char16_t*> substitutes[] = {
				{u"Consolas", u"Menlo"}, {u"Lucida Console", u"Menlo"}, {u"Cascadia Code", u"Menlo"}, {u"Cascadia Mono", u"Menlo"},
				{u"Fixedsys", u"Menlo"}, {u"Terminal", u"Menlo"}, {u"Segoe UI Symbol", u"Apple Symbols"},
				{u"Segoe UI Emoji", u"Apple Color Emoji"}, {u"Webdings", u"Apple Symbols"}, {u"Wingdings", u"Apple Symbols"},
				{u"Calibri", u"Helvetica Neue"}, {u"Cambria", u"Georgia"},
			};
			for (const auto& [windowsFace, macFace] : substitutes)
			{
				if (sameName(face, windowsFace))
					return macFace;
			}
			return std::u16string{face};
		}

		CTLineRef createLine(CTFontRef font, std::u16string_view text)
		{
			CFStringRef string = createString(text);
			const void* keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
			const void* values[] = {font, kCFBooleanTrue};
			CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			CFAttributedStringRef attributed = CFAttributedStringCreate(nullptr, string, attributes);
			CTLineRef line = CTLineCreateWithAttributedString(attributed);
			CFRelease(attributed);
			CFRelease(attributes);
			CFRelease(string);
			return line;
		}

		class Font : public NativeFont
		{
		public:
			explicit Font(CTFontRef font) : _font{font} {}
			~Font() override
			{
				if (_font)
					CFRelease(_font);
			}

			double ascent() const override
			{
				return CTFontGetAscent(_font);
			}

			double descent() const override
			{
				return CTFontGetDescent(_font);
			}

			double leading() const override
			{
				return CTFontGetLeading(_font);
			}

			double size() const override
			{
				return CTFontGetSize(_font);
			}

			double xHeight() const override
			{
				return CTFontGetXHeight(_font);
			}

			double maxCharWidth() const override
			{
				return CTFontGetBoundingBox(_font).size.width;
			}

			bool monospaced() const override
			{
				return (CTFontGetSymbolicTraits(_font) & kCTFontMonoSpaceTrait) != 0;
			}

			double width(std::u16string_view text) const override
			{
				if (text.empty())
					return 0;
				CTLineRef line = createLine(_font, text);
				const double result = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
				CFRelease(line);
				return result;
			}

			CTFontRef _font;
		};

		// --- Canvas -------------------------------------------------------------------------------

		class Context : public Canvas
		{
		public:
			// Takes a reference to the context (released with the canvas) when owned is false.
			Context(CGContextRef context, bool retain) : _context{context}
			{
				if (retain)
					CGContextRetain(_context);
			}

			~Context() override
			{
				CGContextRelease(_context);
			}

			void save() override
			{
				CGContextSaveGState(_context);
			}

			void restore() override
			{
				CGContextRestoreGState(_context);
			}

			void clipToRect(const CanvasRect& rect) override
			{
				CGContextClipToRect(_context, toCGRect(rect));
			}

			void clipToRects(std::span<const CanvasRect> rects) override
			{
				std::vector<CGRect> clip;
				for (const CanvasRect& rect : rects)
					clip.push_back(toCGRect(rect));
				if (clip.empty())
					clip.push_back(CGRectZero);
				CGContextClipToRects(_context, clip.data(), clip.size());
			}

			void translate(double dx, double dy) override
			{
				CGContextTranslateCTM(_context, dx, dy);
			}

			void scale(double sx, double sy) override
			{
				CGContextScaleCTM(_context, sx, sy);
			}

			void setAntialias(bool antialias) override
			{
				CGContextSetShouldAntialias(_context, antialias);
			}

			void setBlendMode(BlendMode mode) override
			{
				switch (mode)
				{
					case BlendMode::normal:
						CGContextSetBlendMode(_context, kCGBlendModeNormal);
						break;
					case BlendMode::copy:
						CGContextSetBlendMode(_context, kCGBlendModeCopy);
						break;
					case BlendMode::difference:
						CGContextSetBlendMode(_context, kCGBlendModeDifference);
						break;
					case BlendMode::lighten:
						CGContextSetBlendMode(_context, kCGBlendModeLighten);
						break;
					case BlendMode::multiply:
						CGContextSetBlendMode(_context, kCGBlendModeMultiply);
						break;
				}
			}

			void setAlpha(double alpha) override
			{
				CGContextSetAlpha(_context, alpha);
			}

			void setInterpolation(Interpolation interpolation) override
			{
				CGContextSetInterpolationQuality(_context, interpolation == Interpolation::high ? kCGInterpolationHigh : kCGInterpolationNone);
			}

			void setFillColor(CanvasColor color) override
			{
				CGContextSetRGBFillColor(_context, red(color), green(color), blue(color), 1.0);
			}

			void setStrokeColor(CanvasColor color) override
			{
				CGContextSetRGBStrokeColor(_context, red(color), green(color), blue(color), 1.0);
			}

			void setLineWidth(double width) override
			{
				CGContextSetLineWidth(_context, width);
			}

			void setLineCap(LineCap cap) override
			{
				CGContextSetLineCap(_context, cap == LineCap::square ? kCGLineCapSquare : kCGLineCapButt);
			}

			void setLineDash(std::span<const double> lengths) override
			{
				std::vector<CGFloat> dashes(lengths.begin(), lengths.end());
				CGContextSetLineDash(_context, 0, dashes.empty() ? nullptr : dashes.data(), dashes.size());
			}

			void fillRect(const CanvasRect& rect) override
			{
				CGContextFillRect(_context, toCGRect(rect));
			}

			void strokeRect(const CanvasRect& rect) override
			{
				CGContextStrokeRect(_context, toCGRect(rect));
			}

			void strokeLine(CanvasPoint from, CanvasPoint to) override
			{
				CGContextBeginPath(_context);
				CGContextMoveToPoint(_context, from._x, from._y);
				CGContextAddLineToPoint(_context, to._x, to._y);
				CGContextStrokePath(_context);
			}

			void drawPolygon(std::span<const CanvasPoint> points, bool fill, bool stroke) override
			{
				if (points.empty() || (!fill && !stroke))
					return;
				CGContextBeginPath(_context);
				CGContextMoveToPoint(_context, points[0]._x, points[0]._y);
				for (size_t i = 1; i < points.size(); ++i)
					CGContextAddLineToPoint(_context, points[i]._x, points[i]._y);
				CGContextClosePath(_context);
				CGContextDrawPath(_context, fill && stroke ? kCGPathFillStroke : (fill ? kCGPathFill : kCGPathStroke));
			}

			void drawRoundedRect(const CanvasRect& rect, double radiusX, double radiusY, bool fill, bool stroke) override
			{
				CGPathRef path = CGPathCreateWithRoundedRect(toCGRect(rect), radiusX, radiusY, nullptr);
				if (fill)
				{
					CGContextAddPath(_context, path);
					CGContextFillPath(_context);
				}
				if (stroke)
				{
					CGContextAddPath(_context, path);
					CGContextStrokePath(_context);
				}
				CGPathRelease(path);
			}

			void drawEllipse(const CanvasRect& rect, bool fill, bool stroke) override
			{
				if (fill)
					CGContextFillEllipseInRect(_context, toCGRect(rect));
				if (stroke)
					CGContextStrokeEllipseInRect(_context, toCGRect(rect));
			}

			void fillLinearGradient(CanvasPoint from, const CanvasRGB& fromColor, CanvasPoint to, const CanvasRGB& toColor) override
			{
				const CGFloat components[] = {fromColor[0], fromColor[1], fromColor[2], 1, toColor[0], toColor[1], toColor[2], 1};
				CGGradientRef gradient = CGGradientCreateWithColorComponents(sRGB(), components, nullptr, 2);
				CGContextDrawLinearGradient(_context, gradient, CGPointMake(from._x, from._y), CGPointMake(to._x, to._y), 0);
				CGGradientRelease(gradient);
			}

			void drawImage(const CanvasImage& image, const CanvasRect& destination) override
			{
				CGContextSaveGState(_context);
				CGContextTranslateCTM(_context, destination._x, destination._y + destination._height);
				CGContextScaleCTM(_context, 1, -1);
				CGContextDrawImage(_context, CGRectMake(0, 0, destination._width, destination._height), static_cast<const Image&>(image)._image);
				CGContextRestoreGState(_context);
			}

			void drawText(const NativeFont& font, std::u16string_view text, double x, double baseline, CanvasColor color) override
			{
				if (text.empty())
					return;
				CTLineRef line = createLine(static_cast<const Font&>(font)._font, text);
				CGContextSaveGState(_context);
				CGContextSetShouldAntialias(_context, true);
				CGContextSetShouldSmoothFonts(_context, false);
				setFillColor(color);
				CGContextSetTextMatrix(_context, CGAffineTransformMakeScale(1, -1));
				CGContextSetTextPosition(_context, x, baseline);
				CTLineDraw(line, _context);
				CGContextRestoreGState(_context);
				CFRelease(line);
			}

			double deviceScale() const override
			{
				return std::fabs(CGContextConvertSizeToDeviceSpace(_context, CGSizeMake(1, 1)).width);
			}

			std::shared_ptr<CanvasImage> snapshot() const override
			{
				CGImageRef image = CGBitmapContextCreateImage(_context);
				return image ? std::make_shared<Image>(image) : nullptr;
			}

			void* nativeHandle() const override
			{
				return _context;
			}

		private:
			CGContextRef _context;
		};

		// --- PDF ----------------------------------------------------------------------------------

		class Pdf : public PdfDocument
		{
		public:
			Pdf(CGContextRef context, double pageHeight) : _context{context}, _pageHeight{pageHeight} {}
			~Pdf() override
			{
				close();
			}

			Canvas* beginPage() override
			{
				if (!_context || _page)
					return nullptr;
				CGPDFContextBeginPage(_context, nullptr);
				// A top-left origin, as every canvas has.
				CGContextSaveGState(_context);
				CGContextTranslateCTM(_context, 0, _pageHeight);
				CGContextScaleCTM(_context, 1, -1);
				_page = std::make_unique<Context>(_context, true);
				return _page.get();
			}

			void endPage() override
			{
				if (!_page)
					return;
				_page.reset();
				CGContextRestoreGState(_context);
				CGPDFContextEndPage(_context);
			}

			void close() override
			{
				endPage();
				if (_context)
				{
					CGPDFContextClose(_context);
					CGContextRelease(_context);
					_context = nullptr;
				}
			}

		private:
			CGContextRef _context;
			double _pageHeight;
			std::unique_ptr<Context> _page;
		};
	}

	std::shared_ptr<CanvasImage> imageFromPixels(const uint32_t* pixels, int width, int height)
	{
		CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4, nullptr);
		CGImageRef image = CGImageCreate(static_cast<size_t>(width), static_cast<size_t>(height), 8, 32, static_cast<size_t>(width) * 4,
			sRGB(), bgraPremultiplied, provider, nullptr, false, kCGRenderingIntentDefault);
		CGDataProviderRelease(provider);
		return image ? std::make_shared<Image>(image) : nullptr;
	}

	std::unique_ptr<NativeFont> createFont(const FontRequest& request)
	{
		const CGFloat size = request._size;
		CTFontRef font = nullptr;
		if (request._systemFace)
		{
			font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
		}
		else
		{
			const std::u16string face = substituteFace(request._face);
			CFStringRef name = createString(face);
			font = CTFontCreateWithName(name, size, nullptr);
			// Core Text falls back to Helvetica for unknown names: use the system font instead.
			CFStringRef family = CTFontCopyFamilyName(font);
			const std::u16string actual = fromCFString(family);
			CFRelease(family);
			if (!sameName(actual, face) && sameName(actual, u"Helvetica"))
			{
				CFRelease(font);
				font = request._fixedPitch ? CTFontCreateWithName(CFSTR("Menlo"), size, nullptr)
				                           : CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
			}
			CFRelease(name);
		}

		// A cell height (ascent + descent) asked for instead of the em size.
		if (request._cellHeight)
		{
			const CGFloat cell = CTFontGetAscent(font) + CTFontGetDescent(font);
			if (cell > 0)
			{
				CTFontRef resized = CTFontCreateCopyWithAttributes(font, size * size / cell, nullptr, nullptr);
				CFRelease(font);
				font = resized;
			}
		}

		CTFontSymbolicTraits traits = 0;
		if (request._bold)
			traits |= kCTFontBoldTrait;
		if (request._italic)
			traits |= kCTFontItalicTrait;
		if (traits)
		{
			CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(font, 0, nullptr, traits, traits);
			if (styled)
			{
				CFRelease(font);
				font = styled;
			}
		}
		return std::make_unique<Font>(font);
	}

	std::vector<FontFamily> fontFamilies()
	{
		std::vector<FontFamily> result;
		CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
		const CFIndex count = CFArrayGetCount(families);
		for (CFIndex i = 0; i < count; ++i)
		{
			auto family = static_cast<CFStringRef>(CFArrayGetValueAtIndex(families, i));
			const std::u16string name = fromCFString(family);
			if (name.empty() || name[0] == u'.')
				continue;
			CTFontRef font = CTFontCreateWithName(family, 12, nullptr);
			const bool monospaced = (CTFontGetSymbolicTraits(font) & kCTFontMonoSpaceTrait) != 0;
			CFRelease(font);
			result.push_back({name, monospaced});
		}
		CFRelease(families);
		return result;
	}

	std::unique_ptr<Canvas> canvasOverPixels(uint32_t* pixels, int width, int height, bool bottomUp)
	{
		CGContextRef context = CGBitmapContextCreate(pixels, static_cast<size_t>(width), static_cast<size_t>(height), 8,
			static_cast<size_t>(width) * 4, sRGB(), bgraPremultiplied);
		if (!context)
			return nullptr;
		// CGBitmapContext memory is top-down with a bottom-left origin: flipped to a top-left origin. A
		// bottom-up DIB's rows run upward in memory, so the bottom-left origin already matches them.
		if (!bottomUp)
		{
			CGContextTranslateCTM(context, 0, height);
			CGContextScaleCTM(context, 1, -1);
		}
		return std::make_unique<Context>(context, false);
	}

	std::unique_ptr<Canvas> canvasOverSurface(void* surface)
	{
		return surface ? std::make_unique<Context>(static_cast<CGContextRef>(surface), true) : nullptr;
	}

	std::unique_ptr<PdfDocument> createPdf(const std::string& path, double pageWidth, double pageHeight, std::u16string_view title)
	{
		const CGRect paper = CGRectMake(0, 0, pageWidth, pageHeight);
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
		CFStringRef titleString = createString(title);
		const void* keys[] = {kCGPDFContextTitle, kCGPDFContextCreator};
		const void* values[] = {titleString, CFSTR("Notepad++")};
		CFDictionaryRef auxiliary = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CGContextRef context = CGPDFContextCreateWithURL(url, &paper, auxiliary);
		CFRelease(auxiliary);
		CFRelease(titleString);
		CFRelease(url);
		return context ? std::make_unique<Pdf>(context, pageHeight) : nullptr;
	}

	bool decodePng(std::span<const unsigned char> data, int& width, int& height, std::vector<uint32_t>& pixels)
	{
		CFDataRef cfData = CFDataCreate(nullptr, data.data(), static_cast<CFIndex>(data.size()));
		CGImageSourceRef source = CGImageSourceCreateWithData(cfData, nullptr);
		CFRelease(cfData);
		if (!source)
			return false;
		CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
		CFRelease(source);
		if (!image)
			return false;
		width = static_cast<int>(CGImageGetWidth(image));
		height = static_cast<int>(CGImageGetHeight(image));
		pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
		// Drawn upright into the pixels, as into a bitmap's canvas.
		std::unique_ptr<Canvas> canvas = canvasOverPixels(pixels.data(), width, height, false);
		if (canvas)
		{
			Image wrapped{image};
			canvas->drawImage(wrapped, {0, 0, static_cast<double>(width), static_cast<double>(height)});
			wrapped._image = nullptr; // still image's
		}
		CGImageRelease(image);
		return canvas != nullptr;
	}
}
