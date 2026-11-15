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

// Canvas.h on Linux: Qt. Images are QImages over GDI's own pixels (Format_ARGB32_Premultiplied is BGRA
// in memory, as GDI keeps them), canvases QPainters, fonts QFonts measured at 96 dots per inch (one GDI
// unit, one pixel), PDF documents QPdfWriter. The painter's state stands in for Core Graphics's:
// colors in its brush and pen, lines in its pen (Core Graphics's defaults: butt caps, miter joins),
// blending in its composition mode.

#include "../Canvas.h"

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QColorSpace>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontInfo>
#include <QtGui/QFontMetricsF>
#include <QtGui/QImage>
#include <QtGui/QLinearGradient>
#include <QtGui/QPageSize>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPdfWriter>

#include <algorithm>
#include <cmath>

namespace nppmac::gdi
{
	namespace
	{
		// 96 dots per inch: QImage's default follows the screen, which a test run may not have.
		constexpr int dotsPerMeter = 3780;

		QString toQString(std::u16string_view text)
		{
			return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
		}

		QColor toQColor(CanvasColor color)
		{
			return QColor(static_cast<int>(color & 0xFF), static_cast<int>((color >> 8) & 0xFF), static_cast<int>((color >> 16) & 0xFF));
		}

		QRectF toQRect(const CanvasRect& rect)
		{
			return {rect._x, rect._y, rect._width, rect._height};
		}

		bool sameName(std::u16string_view a, std::u16string_view b)
		{
			const auto lower = [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c; };
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [&](char16_t x, char16_t y) { return lower(x) == lower(y); });
		}

		// The device fonts are measured against: 96 dots per inch, as every canvas here has.
		QPaintDevice* measuringDevice()
		{
			static QImage* device = []
			{
				auto* image = new QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
				image->setDotsPerMeterX(dotsPerMeter);
				image->setDotsPerMeterY(dotsPerMeter);
				return image;
			}();
			return device;
		}

		// --- Images -------------------------------------------------------------------------------

		class Image : public CanvasImage
		{
		public:
			explicit Image(QImage image) : _image{std::move(image)} {}

			int width() const override
			{
				return _image.width();
			}

			int height() const override
			{
				return _image.height();
			}

			std::shared_ptr<CanvasImage> cropped(const CanvasRect& area) const override
			{
				// As Core Graphics does: the area made whole pixels, then clamped to the image.
				const int left = static_cast<int>(std::floor(area._x));
				const int top = static_cast<int>(std::floor(area._y));
				const int right = static_cast<int>(std::ceil(area._x + area._width));
				const int bottom = static_cast<int>(std::ceil(area._y + area._height));
				const QRect part = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1)).intersected(_image.rect());
				if (part.isEmpty())
					return nullptr;
				return std::make_shared<Image>(_image.copy(part));
			}

			QImage _image;
		};

		// --- Fonts --------------------------------------------------------------------------------

		// Windows faces that Linux does not have, and what stands in for them: fontconfig's aliases.
		std::u16string substituteFace(std::u16string_view face)
		{
			static const std::pair<const char16_t*, const char16_t*> substitutes[] = {
				{u"Consolas", u"Monospace"}, {u"Lucida Console", u"Monospace"}, {u"Cascadia Code", u"Monospace"},
				{u"Cascadia Mono", u"Monospace"}, {u"Fixedsys", u"Monospace"}, {u"Terminal", u"Monospace"}, {u"Courier New", u"Monospace"},
				{u"Segoe UI Symbol", u"Noto Sans Symbols"}, {u"Segoe UI Emoji", u"Noto Color Emoji"}, {u"Calibri", u"Sans Serif"},
				{u"Cambria", u"Serif"}, {u"Arial", u"Sans Serif"}, {u"Times New Roman", u"Serif"},
			};
			for (const auto& [windowsFace, linuxFace] : substitutes)
			{
				if (sameName(face, windowsFace))
					return linuxFace;
			}
			return std::u16string{face};
		}

		class Font : public NativeFont
		{
		public:
			Font(const QFont& font, double size) : _font{font}, _metrics{font, measuringDevice()}, _size{size} {}

			double ascent() const override
			{
				return _metrics.ascent();
			}

			double descent() const override
			{
				return _metrics.descent();
			}

			double leading() const override
			{
				return _metrics.leading();
			}

			double size() const override
			{
				return _size;
			}

			double xHeight() const override
			{
				return _metrics.xHeight();
			}

			double maxCharWidth() const override
			{
				return _metrics.maxWidth();
			}

			bool monospaced() const override
			{
				return QFontInfo(_font).fixedPitch();
			}

			double width(std::u16string_view text) const override
			{
				return text.empty() ? 0 : _metrics.horizontalAdvance(toQString(text));
			}

			QFont _font;
			QFontMetricsF _metrics;
			double _size;
		};

		// A size in GDI units (pixels at 96 dots per inch) as Qt's points.
		void setSize(QFont& font, double size)
		{
			font.setPointSizeF(size * 72.0 / 96.0);
		}

		// --- Canvas -------------------------------------------------------------------------------

		class Painter : public Canvas
		{
		public:
			// A painter already painting gets Core Graphics's starting state now. A derived canvas's own
			// painter does not exist yet while this is made: it is given that state once it starts.
			explicit Painter(QPainter* painter, bool painting = true) : _painter{painter}
			{
				if (painting)
					initialize();
			}

			void save() override
			{
				_painter->save();
				_dashes.push_back(_dashes.back());
			}

			void restore() override
			{
				_painter->restore();
				if (_dashes.size() > 1)
					_dashes.pop_back();
			}

			void clipToRect(const CanvasRect& rect) override
			{
				_painter->setClipRect(toQRect(rect), Qt::IntersectClip);
			}

			void clipToRects(std::span<const CanvasRect> rects) override
			{
				QPainterPath area;
				area.setFillRule(Qt::WindingFill);
				for (const CanvasRect& rect : rects)
					area.addRect(toQRect(rect));
				if (rects.empty())
					_painter->setClipRect(QRectF(0, 0, 0, 0), Qt::IntersectClip);
				else
					_painter->setClipPath(area, Qt::IntersectClip);
			}

			void translate(double dx, double dy) override
			{
				_painter->translate(dx, dy);
			}

			void scale(double sx, double sy) override
			{
				_painter->scale(sx, sy);
			}

			void setAntialias(bool antialias) override
			{
				_painter->setRenderHint(QPainter::Antialiasing, antialias);
			}

			void setBlendMode(BlendMode mode) override
			{
				switch (mode)
				{
					case BlendMode::normal:
						_painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
						break;
					case BlendMode::copy:
						_painter->setCompositionMode(QPainter::CompositionMode_Source);
						break;
					case BlendMode::difference:
						_painter->setCompositionMode(QPainter::CompositionMode_Difference);
						break;
					case BlendMode::lighten:
						_painter->setCompositionMode(QPainter::CompositionMode_Lighten);
						break;
					case BlendMode::multiply:
						_painter->setCompositionMode(QPainter::CompositionMode_Multiply);
						break;
				}
			}

			void setAlpha(double alpha) override
			{
				_painter->setOpacity(alpha);
			}

			void setInterpolation(Interpolation interpolation) override
			{
				_painter->setRenderHint(QPainter::SmoothPixmapTransform, interpolation == Interpolation::high);
			}

			void setFillColor(CanvasColor color) override
			{
				_painter->setBrush(toQColor(color));
			}

			void setStrokeColor(CanvasColor color) override
			{
				QPen pen = _painter->pen();
				pen.setColor(toQColor(color));
				_painter->setPen(pen);
			}

			void setLineWidth(double width) override
			{
				QPen pen = _painter->pen();
				pen.setWidthF(width);
				applyDashes(pen);
				_painter->setPen(pen);
			}

			void setLineCap(LineCap cap) override
			{
				QPen pen = _painter->pen();
				pen.setCapStyle(cap == LineCap::square ? Qt::SquareCap : Qt::FlatCap);
				_painter->setPen(pen);
			}

			void setLineDash(std::span<const double> lengths) override
			{
				_dashes.back().assign(lengths.begin(), lengths.end());
				QPen pen = _painter->pen();
				applyDashes(pen);
				_painter->setPen(pen);
			}

			void fillRect(const CanvasRect& rect) override
			{
				_painter->fillRect(toQRect(rect), _painter->brush());
			}

			void strokeRect(const CanvasRect& rect) override
			{
				QPainterPath path;
				path.addRect(toQRect(rect));
				_painter->strokePath(path, _painter->pen());
			}

			void strokeLine(CanvasPoint from, CanvasPoint to) override
			{
				QPainterPath path;
				path.moveTo(from._x, from._y);
				path.lineTo(to._x, to._y);
				_painter->strokePath(path, _painter->pen());
			}

			void drawPolygon(std::span<const CanvasPoint> points, bool fill, bool stroke) override
			{
				if (points.empty())
					return;
				QPainterPath path;
				path.moveTo(points[0]._x, points[0]._y);
				for (size_t i = 1; i < points.size(); ++i)
					path.lineTo(points[i]._x, points[i]._y);
				path.closeSubpath();
				draw(path, fill, stroke);
			}

			void drawRoundedRect(const CanvasRect& rect, double radiusX, double radiusY, bool fill, bool stroke) override
			{
				QPainterPath path;
				path.addRoundedRect(toQRect(rect), radiusX, radiusY);
				draw(path, fill, stroke);
			}

			void drawEllipse(const CanvasRect& rect, bool fill, bool stroke) override
			{
				QPainterPath path;
				path.addEllipse(toQRect(rect));
				draw(path, fill, stroke);
			}

			void fillLinearGradient(CanvasPoint from, const CanvasRGB& fromColor, CanvasPoint to, const CanvasRGB& toColor) override
			{
				QLinearGradient gradient(from._x, from._y, to._x, to._y);
				gradient.setColorAt(0, QColor::fromRgbF(static_cast<float>(fromColor[0]), static_cast<float>(fromColor[1]), static_cast<float>(fromColor[2])));
				gradient.setColorAt(1, QColor::fromRgbF(static_cast<float>(toColor[0]), static_cast<float>(toColor[1]), static_cast<float>(toColor[2])));
				_painter->fillRect(_painter->clipBoundingRect(), QBrush(gradient));
			}

			void drawImage(const CanvasImage& image, const CanvasRect& destination) override
			{
				_painter->drawImage(toQRect(destination), static_cast<const Image&>(image)._image);
			}

			void drawText(const NativeFont& font, std::u16string_view text, double x, double baseline, CanvasColor color) override
			{
				if (text.empty())
					return;
				_painter->save();
				_painter->setRenderHint(QPainter::Antialiasing, true);
				_painter->setRenderHint(QPainter::TextAntialiasing, true);
				_painter->setFont(static_cast<const Font&>(font)._font);
				_painter->setPen(toQColor(color));
				_painter->drawText(QPointF(x, baseline), toQString(text));
				_painter->restore();
			}

			double deviceScale() const override
			{
				return std::fabs(_painter->deviceTransform().m11());
			}

			std::shared_ptr<CanvasImage> snapshot() const override
			{
				auto* image = dynamic_cast<QImage*>(_painter->device());
				return image ? std::make_shared<Image>(image->copy()) : nullptr;
			}

			void* nativeHandle() const override
			{
				return _painter;
			}

		protected:
			QPainter* _painter;

			// Core Graphics's starting state: black, a one-unit line with butt caps and miter joins.
			void initialize()
			{
				QPen pen(Qt::black);
				pen.setWidthF(1);
				pen.setCapStyle(Qt::FlatCap);
				pen.setJoinStyle(Qt::MiterJoin);
				_painter->setPen(pen);
				_painter->setBrush(Qt::black);
			}

		private:
			// The dash lengths last set, per saved state: Qt's dashes are in pen widths, Core Graphics's
			// in units, so they are set again when the width changes.
			std::vector<std::vector<double>> _dashes{{}};

			void applyDashes(QPen& pen) const
			{
				const std::vector<double>& lengths = _dashes.back();
				if (lengths.empty())
				{
					pen.setStyle(Qt::SolidLine);
					return;
				}
				const double width = pen.widthF() > 0 ? pen.widthF() : 1;
				QList<qreal> pattern;
				for (const double length : lengths)
					pattern << length / width;
				pen.setDashPattern(pattern);
			}

			void draw(const QPainterPath& path, bool fill, bool stroke)
			{
				if (fill)
					_painter->fillPath(path, _painter->brush());
				if (stroke)
					_painter->strokePath(path, _painter->pen());
			}
		};

		// A canvas over pixels it keeps a painter on.
		class PixelPainter : public Painter
		{
		public:
			PixelPainter(uint32_t* pixels, int width, int height, bool bottomUp)
				: Painter{&_ownPainter, false},
				  _image{reinterpret_cast<uchar*>(pixels), width, height, static_cast<qsizetype>(width) * 4, QImage::Format_ARGB32_Premultiplied}
			{
				_image.setDotsPerMeterX(dotsPerMeter);
				_image.setDotsPerMeterY(dotsPerMeter);
				_ownPainter.begin(&_image);
				// A bottom-up DIB's rows run upward in memory: GDI's top row is the last one there.
				if (bottomUp)
				{
					_ownPainter.translate(0, height);
					_ownPainter.scale(1, -1);
				}
				initialize();
			}

			~PixelPainter() override
			{
				_ownPainter.end();
			}

		private:
			QPainter _ownPainter;
			QImage _image;
		};

		// --- PDF ----------------------------------------------------------------------------------

		class Pdf : public PdfDocument
		{
		public:
			Pdf(const std::string& path, double pageWidth, double pageHeight, std::u16string_view title) : _writer{QString::fromStdString(path)}
			{
				_writer.setResolution(72); // one unit, one point
				_writer.setPageSize(QPageSize(QSizeF(pageWidth, pageHeight), QPageSize::Point));
				_writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
				_writer.setTitle(toQString(title));
				_writer.setCreator("Notepad++");
			}

			~Pdf() override
			{
				close();
			}

			Canvas* beginPage() override
			{
				if (_closed || _page)
					return nullptr;
				if (!_painter.isActive())
				{
					if (!_painter.begin(&_writer))
						return nullptr;
				}
				else
					_writer.newPage();
				_page = std::make_unique<Painter>(&_painter);
				return _page.get();
			}

			void endPage() override
			{
				_page.reset();
			}

			void close() override
			{
				endPage();
				if (_painter.isActive())
					_painter.end();
				_closed = true;
			}

		private:
			QPdfWriter _writer;
			QPainter _painter;
			std::unique_ptr<Painter> _page;
			bool _closed = false;
		};
	}

	std::shared_ptr<CanvasImage> imageFromPixels(const uint32_t* pixels, int width, int height)
	{
		// Not copied: the image reads the bitmap's own pixels, as Core Graphics's does.
		return std::make_shared<Image>(QImage(reinterpret_cast<const uchar*>(pixels), width, height,
			static_cast<qsizetype>(width) * 4, QImage::Format_ARGB32_Premultiplied));
	}

	std::unique_ptr<NativeFont> createFont(const FontRequest& request)
	{
		QFont font;
		if (request._systemFace)
			font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
		else
		{
			const QString face = toQString(substituteFace(request._face));
			const bool alias = face == "Monospace" || face == "Sans Serif" || face == "Serif";
			if (alias || QFontDatabase::hasFamily(face))
			{
				font = QFont(face);
				if (face == "Monospace")
					font.setStyleHint(QFont::Monospace);
			}
			else
				// An unknown name: the system's font, or its fixed-pitch one when that was asked for.
				font = QFontDatabase::systemFont(request._fixedPitch ? QFontDatabase::FixedFont : QFontDatabase::GeneralFont);
		}
		double size = request._size;
		setSize(font, size);
		// A cell height (ascent + descent) asked for instead of the em size.
		if (request._cellHeight)
		{
			const QFontMetricsF metrics(font, measuringDevice());
			const double cell = metrics.ascent() + metrics.descent();
			if (cell > 0)
			{
				size = size * size / cell;
				setSize(font, size);
			}
		}
		font.setBold(request._bold);
		font.setItalic(request._italic);
		return std::make_unique<Font>(font, size);
	}

	std::vector<FontFamily> fontFamilies()
	{
		std::vector<FontFamily> result;
		for (const QString& family : QFontDatabase::families())
		{
			if (family.isEmpty() || family.startsWith('.') || QFontDatabase::isPrivateFamily(family))
				continue;
			result.push_back({std::u16string{reinterpret_cast<const char16_t*>(family.utf16()), static_cast<size_t>(family.size())},
				QFontDatabase::isFixedPitch(family)});
		}
		return result;
	}

	std::unique_ptr<Canvas> canvasOverPixels(uint32_t* pixels, int width, int height, bool bottomUp)
	{
		if (!pixels || width <= 0 || height <= 0)
			return nullptr;
		return std::make_unique<PixelPainter>(pixels, width, height, bottomUp);
	}

	std::unique_ptr<Canvas> canvasOverSurface(void* surface)
	{
		// The window backend's painter on its surface, which it keeps while the surface lasts.
		return surface ? std::make_unique<Painter>(static_cast<QPainter*>(surface)) : nullptr;
	}

	std::unique_ptr<PdfDocument> createPdf(const std::string& path, double pageWidth, double pageHeight, std::u16string_view title)
	{
		return std::make_unique<Pdf>(path, pageWidth, pageHeight, title);
	}

	bool decodePng(std::span<const unsigned char> data, int& width, int& height, std::vector<uint32_t>& pixels)
	{
		QImage image = QImage::fromData(data.data(), static_cast<int>(data.size()), "PNG");
		if (image.isNull())
			return false;
		// In sRGB, as Core Graphics draws it into a bitmap.
		if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb))
			image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		width = image.width();
		height = image.height();
		pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
		for (int y = 0; y < height; ++y)
		{
			const auto* row = reinterpret_cast<const uint32_t*>(image.constScanLine(y));
			std::copy(row, row + width, pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width));
		}
		return true;
	}
}
