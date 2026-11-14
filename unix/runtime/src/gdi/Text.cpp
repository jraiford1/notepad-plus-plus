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

// GDI text: fonts, drawing, DrawText layout, measurement, metrics, font enumeration. The platform's
// fonts (Canvas.h's NativeFont) do the shaping and drawing; Windows' rules stay here.

#include "Gdi.h"

#include <algorithm>
#include <cmath>

namespace nppmac::gdi
{
	namespace
	{
		// Windows' own UI faces, which stand for the system's UI font.
		bool isSystemUIFace(std::u16string_view face)
		{
			static const char16_t* const uiFaces[] = {
				u"", u"MS Shell Dlg", u"MS Shell Dlg 2", u"Segoe UI", u"Segoe UI Variable", u"Tahoma", u"Microsoft Sans Serif",
				u"MS Sans Serif", u"System", u"Arial Unicode MS",
			};
			for (const char16_t* uiFace : uiFaces)
			{
				if (_wcsicmp(std::u16string{face}.c_str(), uiFace) == 0)
					return true;
			}
			return false;
		}

		// What GDI asks of the platform for a LOGFONT: the face, Windows' UI faces as the system's, the
		// size (a positive height being the cell height, ascent plus descent), weight, slant and pitch.
		std::unique_ptr<NativeFont> createFontFor(const LOGFONTW& log)
		{
			FontRequest request;
			request._face = log.lfFaceName;
			request._systemFace = isSystemUIFace(request._face);
			request._size = log.lfHeight == 0 ? 12 : std::abs(log.lfHeight);
			request._cellHeight = log.lfHeight > 0;
			request._bold = log.lfWeight >= FW_SEMIBOLD;
			request._italic = log.lfItalic != 0;
			request._fixedPitch = (log.lfPitchAndFamily & 0x3) == FIXED_PITCH;
			return createFont(request);
		}

		int ascentOf(const NativeFont& font)
		{
			return static_cast<int>(std::ceil(font.ascent()));
		}

		int descentOf(const NativeFont& font)
		{
			return static_cast<int>(std::ceil(font.descent()));
		}

		int lineHeightOf(const NativeFont& font)
		{
			return ascentOf(font) + descentOf(font);
		}

		Font* currentFont(DC* dc)
		{
			return objectAs<Font>(dc->_state._font, ObjectType::font);
		}

		// Expands tabs to the next multiple of eight average characters, as DT_EXPANDTABS does.
		std::u16string expandTabs(std::u16string_view text)
		{
			std::u16string result;
			for (const char16_t ch : text)
			{
				if (ch == u'\t')
					result.append(8 - result.size() % 8, u' ');
				else
					result.push_back(ch);
			}
			return result;
		}

		// Removes '&' prefixes ("&File" -> "File", "&&" -> "&"); returns where the underline goes.
		std::u16string stripPrefixes(std::u16string_view text, size_t& underlined)
		{
			std::u16string result;
			underlined = std::u16string::npos;
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == u'&' && i + 1 < text.size())
				{
					++i;
					if (text[i] != u'&' && underlined == std::u16string::npos)
						underlined = result.size();
				}
				result.push_back(text[i]);
			}
			return result;
		}

		// Shortens text with an ellipsis to fit a width; DT_PATH_ELLIPSIS keeps the end of a path.
		std::u16string ellipsize(const NativeFont& font, const std::u16string& text, double width, bool pathEllipsis)
		{
			if (textWidth(font, text) <= width)
				return text;
			const std::u16string ellipsis = u"…";
			if (pathEllipsis)
			{
				const size_t separator = text.find_last_of(u"/\\");
				const std::u16string tail = separator == std::u16string::npos ? std::u16string{} : text.substr(separator);
				std::u16string head = text.substr(0, separator == std::u16string::npos ? text.size() : separator);
				while (!head.empty() && textWidth(font, head + ellipsis + tail) > width)
					head.pop_back();
				if (!head.empty() || textWidth(font, ellipsis + tail) <= width)
					return head + ellipsis + tail;
			}
			std::u16string result = text;
			while (!result.empty() && textWidth(font, result + ellipsis) > width)
				result.pop_back();
			return result + ellipsis;
		}

		// Greedy word wrapping at spaces, for DT_WORDBREAK; explicit line breaks always break.
		std::vector<std::u16string> wrapLines(const NativeFont& font, std::u16string_view text, double width, bool wordBreak, bool singleLine)
		{
			std::vector<std::u16string> lines;
			if (singleLine)
			{
				std::u16string line;
				for (const char16_t ch : text)
				{
					if (ch != u'\r' && ch != u'\n')
						line.push_back(ch);
				}
				lines.push_back(line);
				return lines;
			}
			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find_first_of(u"\r\n", start);
				if (end == std::u16string_view::npos)
					end = text.size();
				std::u16string paragraph{text.substr(start, end - start)};
				if (!wordBreak || textWidth(font, paragraph) <= width)
				{
					lines.push_back(paragraph);
				}
				else
				{
					std::u16string current;
					size_t i = 0;
					while (i < paragraph.size())
					{
						size_t wordEnd = paragraph.find(u' ', i);
						wordEnd = wordEnd == std::u16string::npos ? paragraph.size() : wordEnd + 1;
						const std::u16string word = paragraph.substr(i, wordEnd - i);
						if (!current.empty() && textWidth(font, current + word) > width)
						{
							while (!current.empty() && current.back() == u' ')
								current.pop_back();
							lines.push_back(current);
							current.clear();
						}
						current += word;
						i = wordEnd;
					}
					lines.push_back(current);
				}
				if (end == text.size())
					break;
				start = end + ((text[end] == u'\r' && end + 1 < text.size() && text[end + 1] == u'\n') ? 2 : 1);
			}
			return lines;
		}
	}

	const NativeFont& Font::native()
	{
		if (!_native)
			_native = createFontFor(_log);
		return *_native;
	}

	const NativeFont& fontFor(DC* dc)
	{
		Font* font = currentFont(dc);
		if (!font)
			font = objectAs<Font>(GetStockObject(SYSTEM_FONT), ObjectType::font);
		return font->native();
	}

	double textWidth(const NativeFont& font, std::u16string_view text)
	{
		return text.empty() ? 0 : font.width(text);
	}

	void drawText(DC* dc, Canvas* canvas, std::u16string_view text, double x, double baseline, COLORREF color)
	{
		if (text.empty())
			return;
		const NativeFont& font = fontFor(dc);
		canvas->drawText(font, text, x, baseline, color);

		Font* logical = currentFont(dc);
		if (logical && (logical->_log.lfUnderline || logical->_log.lfStrikeOut))
		{
			const double width = font.width(text);
			canvas->setFillColor(color);
			if (logical->_log.lfUnderline)
				canvas->fillRect({x, std::round(baseline + 1), width, 1});
			if (logical->_log.lfStrikeOut)
				canvas->fillRect({x, std::round(baseline - font.xHeight() / 2), width, 1});
		}
	}
}

using namespace nppmac::gdi;

// --- Drawing --------------------------------------------------------------------------------------

BOOL WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT* rect, LPCWSTR text, UINT count, const INT* dx)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const std::u16string_view string{text ? text : u"", text ? count : 0};
	const NativeFont& font = fontFor(dc);

	if (dc->_state._textAlign & TA_UPDATECP)
	{
		x = dc->_state._position.x;
		y = dc->_state._position.y;
	}
	double width = textWidth(font, string);
	if (dx && count > 0)
	{
		width = 0;
		for (UINT i = 0; i < count; ++i)
			width += dx[i];
	}

	double left = x;
	switch (dc->_state._textAlign & (TA_LEFT | TA_RIGHT | TA_CENTER))
	{
		case TA_RIGHT:
			left = x - width;
			break;
		case TA_CENTER:
			left = x - width / 2;
			break;
		default:
			break;
	}
	double baseline = y + ascentOf(font);
	if ((dc->_state._textAlign & TA_BASELINE) == TA_BASELINE)
		baseline = y;
	else if (dc->_state._textAlign & TA_BOTTOM)
		baseline = y - descentOf(font);

	if ((options & ETO_OPAQUE) && rect)
	{
		HBRUSH background = CreateSolidBrush(dc->_state._backgroundColor);
		fillRect(dc, *rect, background);
		DeleteObject(background);
	}
	else if (dc->_state._backgroundMode == OPAQUE && !string.empty())
	{
		HBRUSH background = CreateSolidBrush(dc->_state._backgroundColor);
		const RECT box{static_cast<LONG>(std::floor(left)), static_cast<LONG>(baseline - ascentOf(font)),
			static_cast<LONG>(std::ceil(left + width)), static_cast<LONG>(baseline + descentOf(font))};
		fillRect(dc, box, background);
		DeleteObject(background);
	}

	Canvas* canvas = dc->begin();
	if (!canvas)
		return FALSE;
	if ((options & ETO_CLIPPED) && rect)
		canvas->clipToRect(toCanvasRect(*rect));
	if (dx && count > 0)
	{
		// Character positions given by the caller: draw each character at its cell.
		double position = left;
		for (UINT i = 0; i < count; ++i)
		{
			drawText(dc, canvas, string.substr(i, 1), position, baseline, dc->_state._textColor);
			position += dx[i];
		}
	}
	else
	{
		drawText(dc, canvas, string, left, baseline, dc->_state._textColor);
	}
	dc->end();

	if (dc->_state._textAlign & TA_UPDATECP)
		dc->_state._position.x = static_cast<LONG>(std::lround(left + width));
	return TRUE;
}

BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR text, int count)
{
	return ExtTextOutW(hdc, x, y, 0, nullptr, text, static_cast<UINT>(std::max(0, count)), nullptr);
}

BOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR text, int count)
{
	const std::u16string wide = nppmac::toUtf16(std::string_view{text, static_cast<size_t>(std::max(0, count))});
	return TextOutW(hdc, x, y, wide.c_str(), static_cast<int>(wide.size()));
}

int WINAPI DrawTextExW(HDC hdc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS)
{
	DC* dc = toDC(hdc);
	if (!dc || !rect)
		return 0;
	std::u16string_view source{text ? text : u"", text ? (count < 0 ? wcslen(text) : static_cast<size_t>(count)) : 0};
	const NativeFont& font = fontFor(dc);
	const int lineHeight = lineHeightOf(font);
	const double availableWidth = std::max<LONG>(0, rect->right - rect->left);

	std::u16string string = (format & DT_EXPANDTABS) ? expandTabs(source) : std::u16string{source};
	size_t underlined = std::u16string::npos;
	if (!(format & DT_NOPREFIX))
		string = stripPrefixes(string, underlined);

	const bool singleLine = (format & DT_SINGLELINE) != 0;
	std::vector<std::u16string> lines = wrapLines(font, string, availableWidth, (format & DT_WORDBREAK) != 0, singleLine);
	if (format & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS))
	{
		for (std::u16string& line : lines)
			line = ellipsize(font, line, availableWidth, (format & DT_PATH_ELLIPSIS) != 0);
		if (format & DT_MODIFYSTRING && text && lines.size() == 1 && count < 0)
			wcscpy(text, lines[0].c_str());
	}

	double widest = 0;
	for (const std::u16string& line : lines)
		widest = std::max(widest, textWidth(font, line));
	const int totalHeight = lineHeight * static_cast<int>(lines.size());

	if (format & DT_CALCRECT)
	{
		rect->right = rect->left + static_cast<LONG>(std::ceil(widest));
		rect->bottom = rect->top + totalHeight;
		return totalHeight;
	}

	int top = rect->top;
	if (singleLine && (format & DT_VCENTER))
		top = rect->top + (rect->bottom - rect->top - lineHeight) / 2;
	else if (singleLine && (format & DT_BOTTOM))
		top = rect->bottom - lineHeight;

	Canvas* canvas = dc->begin();
	if (!canvas)
		return 0;
	if (!(format & DT_NOCLIP))
		canvas->clipToRect(toCanvasRect(*rect));
	if (dc->_state._backgroundMode == OPAQUE)
	{
		canvas->setFillColor(dc->_state._backgroundColor);
		canvas->fillRect({static_cast<double>(rect->left), static_cast<double>(top), static_cast<double>(availableWidth), static_cast<double>(totalHeight)});
	}
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const double width = textWidth(font, lines[i]);
		double left = rect->left;
		if (format & DT_CENTER)
			left = rect->left + (availableWidth - width) / 2;
		else if (format & DT_RIGHT)
			left = rect->right - width;
		const double baseline = top + static_cast<int>(i) * lineHeight + ascentOf(font);
		drawText(dc, canvas, lines[i], left, baseline, dc->_state._textColor);
		if (i == 0 && underlined != std::u16string::npos && underlined < lines[0].size() && !(format & DT_HIDEPREFIX))
		{
			const double start = left + textWidth(font, std::u16string_view{lines[0]}.substr(0, underlined));
			const double charWidth = textWidth(font, std::u16string_view{lines[0]}.substr(underlined, 1));
			canvas->setFillColor(dc->_state._textColor);
			canvas->fillRect({start, std::round(baseline + 1), charWidth, 1});
		}
	}
	dc->end();
	return totalHeight;
}

int WINAPI DrawTextW(HDC hdc, LPCWSTR text, int count, LPRECT rect, UINT format)
{
	return DrawTextExW(hdc, const_cast<LPWSTR>(text), count, rect, format & ~DT_MODIFYSTRING, nullptr);
}

int WINAPI DrawTextA(HDC hdc, LPCSTR text, int count, LPRECT rect, UINT format)
{
	const std::u16string wide = nppmac::toUtf16(std::string_view{text, count < 0 ? std::strlen(text) : static_cast<size_t>(count)});
	return DrawTextW(hdc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
}

// --- Measurement ----------------------------------------------------------------------------------

BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR text, int count, LPSIZE size)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const NativeFont& font = fontFor(dc);
	const std::u16string_view string{text ? text : u"", text ? static_cast<size_t>(std::max(0, count)) : 0};
	size->cx = static_cast<LONG>(std::ceil(textWidth(font, string)));
	size->cy = lineHeightOf(font);
	return TRUE;
}

BOOL WINAPI GetTextExtentPointW(HDC hdc, LPCWSTR text, int count, LPSIZE size)
{
	return GetTextExtentPoint32W(hdc, text, count, size);
}

BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR text, int count, LPSIZE size)
{
	const std::u16string wide = nppmac::toUtf16(std::string_view{text, static_cast<size_t>(std::max(0, count))});
	return GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), size);
}

BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR text, int count, int maxExtent, LPINT fit, LPINT dx, LPSIZE size)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const NativeFont& font = fontFor(dc);
	int fitting = 0;
	for (int i = 1; i <= count; ++i)
	{
		const double width = textWidth(font, std::u16string_view{text, static_cast<size_t>(i)});
		if (dx)
			dx[i - 1] = static_cast<int>(std::ceil(width));
		if (width <= maxExtent)
			fitting = i;
	}
	if (fit)
		*fit = fitting;
	return GetTextExtentPoint32W(hdc, text, count, size);
}

BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW metrics)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const NativeFont& font = fontFor(dc);
	std::memset(metrics, 0, sizeof(*metrics));
	metrics->tmAscent = ascentOf(font);
	metrics->tmDescent = descentOf(font);
	metrics->tmHeight = metrics->tmAscent + metrics->tmDescent;
	metrics->tmInternalLeading = std::max<LONG>(0, metrics->tmHeight - static_cast<LONG>(std::lround(font.size())));
	metrics->tmExternalLeading = static_cast<LONG>(std::lround(font.leading()));
	// The average width Windows uses for dialog units: the alphabet's average.
	metrics->tmAveCharWidth = static_cast<LONG>(std::lround(textWidth(font, u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") / 52));
	metrics->tmMaxCharWidth = static_cast<LONG>(std::ceil(font.maxCharWidth()));
	Font* logical = currentFont(dc);
	metrics->tmWeight = logical && logical->_log.lfWeight ? logical->_log.lfWeight : FW_NORMAL;
	metrics->tmItalic = logical ? logical->_log.lfItalic : 0;
	metrics->tmUnderlined = logical ? logical->_log.lfUnderline : 0;
	metrics->tmStruckOut = logical ? logical->_log.lfStrikeOut : 0;
	metrics->tmFirstChar = 0x20;
	metrics->tmLastChar = 0xFFFC;
	metrics->tmDefaultChar = 0x1F;
	metrics->tmBreakChar = 0x20;
	metrics->tmDigitizedAspectX = 96;
	metrics->tmDigitizedAspectY = 96;
	metrics->tmCharSet = ANSI_CHARSET;
	// TMPF_FIXED_PITCH set means variable pitch (the name is the wrong way round on Windows).
	const bool monospaced = font.monospaced();
	metrics->tmPitchAndFamily = static_cast<BYTE>((monospaced ? 0 : TMPF_FIXED_PITCH) | TMPF_TRUETYPE | TMPF_VECTOR);
	return TRUE;
}

int WINAPI GetTextFaceW(HDC hdc, int size, LPWSTR face)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return 0;
	Font* font = objectAs<Font>(dc->_state._font, ObjectType::font);
	const std::u16string name = font ? font->_log.lfFaceName : u"MS Shell Dlg";
	if (!face)
		return static_cast<int>(name.size() + 1);
	lstrcpynW(face, name.c_str(), size);
	return lstrlenW(face);
}

BOOL WINAPI GetCharWidth32W(HDC hdc, UINT first, UINT last, LPINT widths)
{
	DC* dc = toDC(hdc);
	if (!dc)
		return FALSE;
	const NativeFont& font = fontFor(dc);
	for (UINT ch = first; ch <= last; ++ch)
	{
		const char16_t c = static_cast<char16_t>(ch);
		widths[ch - first] = static_cast<int>(std::lround(textWidth(font, std::u16string_view{&c, 1})));
	}
	return TRUE;
}

BOOL WINAPI GetCharWidthW(HDC hdc, UINT first, UINT last, LPINT widths)
{
	return GetCharWidth32W(hdc, first, last, widths);
}

// --- Enumeration ----------------------------------------------------------------------------------

int WINAPI EnumFontFamiliesExW(HDC, LPLOGFONTW filter, FONTENUMPROCW callback, LPARAM parameter, DWORD)
{
	const std::vector<FontFamily> families = fontFamilies();
	const std::u16string wanted = filter ? filter->lfFaceName : u"";
	int result = 1;
	for (size_t i = 0; i < families.size() && result; ++i)
	{
		const std::u16string& name = families[i]._name;
		if (name.empty() || name[0] == u'.' || name.size() >= LF_FACESIZE)
			continue;
		if (!wanted.empty() && _wcsicmp(wanted.c_str(), name.c_str()) != 0)
			continue;

		ENUMLOGFONTEXW logFont{};
		wcsncpy_s(logFont.elfLogFont.lfFaceName, LF_FACESIZE, name.c_str(), _TRUNCATE);
		wcsncpy_s(logFont.elfFullName, LF_FULLFACESIZE, name.c_str(), _TRUNCATE);
		logFont.elfLogFont.lfCharSet = ANSI_CHARSET;
		logFont.elfLogFont.lfHeight = -12;
		logFont.elfLogFont.lfWeight = FW_NORMAL;

		const bool monospaced = families[i]._monospaced;
		logFont.elfLogFont.lfPitchAndFamily = static_cast<BYTE>((monospaced ? FIXED_PITCH : VARIABLE_PITCH) | (monospaced ? FF_MODERN : FF_SWISS));

		NEWTEXTMETRICEXW metrics{};
		metrics.ntmTm.tmHeight = 16;
		metrics.ntmTm.tmAscent = 12;
		metrics.ntmTm.tmDescent = 4;
		metrics.ntmTm.tmCharSet = ANSI_CHARSET;
		metrics.ntmTm.tmPitchAndFamily = static_cast<BYTE>((monospaced ? 0 : TMPF_FIXED_PITCH) | TMPF_TRUETYPE);
		metrics.ntmTm.ntmFlags = NTM_REGULAR;
		result = callback(&logFont.elfLogFont, reinterpret_cast<const TEXTMETRICW*>(&metrics), TRUETYPE_FONTTYPE, parameter);
	}
	return result;
}

int WINAPI EnumFontFamiliesW(HDC hdc, LPCWSTR family, FONTENUMPROCW callback, LPARAM parameter)
{
	LOGFONTW filter{};
	if (family)
		wcsncpy_s(filter.lfFaceName, LF_FACESIZE, family, _TRUNCATE);
	filter.lfCharSet = DEFAULT_CHARSET;
	return EnumFontFamiliesExW(hdc, &filter, callback, parameter, 0);
}

int WINAPI AddFontResourceExW(LPCWSTR, DWORD, PVOID)
{
	return 0;
}

BOOL WINAPI RemoveFontResourceExW(LPCWSTR, DWORD, PVOID)
{
	return FALSE;
}
