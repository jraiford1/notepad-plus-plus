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

// The edit control (Edit): single-line and multi-line text, with selection, caret, clipboard,
// one level of undo, word wrap, the standard context menu and the EM_* messages. Lines are
// separated by "\r\n", as in Windows edit controls. Colors come from the parent through
// WM_CTLCOLOREDIT, or WM_CTLCOLORSTATIC when read-only or disabled.
//
// The rich edit control (RichEdit20W, RichEdit50W: msftedit.dll and riched20.dll) is the same control
// in its rich mode: paragraphs end with "\r", each character has a format (colors, bold, italic,
// underline, strikeout), its colors are its own, and the parent gets the notifications it asks for
// (EN_MSGFILTER, EN_SELCHANGE, EN_CHANGE...), with the EM_* messages of rich edits.

#include "ControlSupport.h"

#include <richedit.h>

#include <cwctype>
#include <map>

namespace nppmac::controls
{
	namespace
	{
		constexpr UINT_PTR caretTimer = 0xCA7E;
		constexpr int borderWidth = 2;
		constexpr int defaultMargin = 1;
		constexpr int wheelLines = 3;
		constexpr UINT maximumLimit = 0x7FFFFFFE;

		enum ContextCommand
		{
			commandUndo = 1,
			commandCut,
			commandCopy,
			commandPaste,
			commandDelete,
			commandSelectAll,
		};

		struct Line
		{
			size_t _start = 0;
			size_t _length = 0; // without the line break
		};

		// A rich edit character format: its colors (automatic: the window's) and effects (CFE_BOLD...).
		struct CharFormat
		{
			bool _autoColor = true;
			COLORREF _color = 0;
			bool _autoBack = true;
			COLORREF _back = 0;
			DWORD _effects = 0;

			bool operator==(const CharFormat&) const = default;
		};

		struct Edit
		{
			std::u16string _text;
			size_t _anchor = 0;            // the selection is between the anchor and the caret
			size_t _caret = 0;
			size_t _firstLine = 0;         // multi-line: the first visible line
			int _scrollX = 0;              // horizontal scroll, in pixels
			int _desiredX = -1;            // where Up and Down aim, kept across short lines
			bool _focused = false;
			bool _caretOn = false;
			bool _selecting = false;
			bool _modified = false;
			UINT _limit = maximumLimit;
			char16_t _passwordChar = 0;
			int _leftMargin = defaultMargin;
			int _rightMargin = defaultMargin;
			std::u16string _cueBanner;
			bool _cueWhenFocused = false;
			bool _canUndo = false;
			size_t _typingEnd = std::u16string::npos; // the end of the current run of typing (one undo step)
			std::u16string _composition;   // the text an input method is composing, shown at the caret
			size_t _compositionCaret = 0;
			std::u16string _undoText;
			size_t _undoAnchor = 0;
			size_t _undoCaret = 0;
			std::vector<Line> _lines;
			bool _layoutValid = false;

			// Rich edit: the format of each character (an index into _formats, where 0 is the default),
			// the one text inserted next gets, the background, and the notifications the parent wants.
			bool _rich = false;
			std::vector<CharFormat> _formats{CharFormat{}};
			std::vector<uint16_t> _formatOf;
			std::vector<uint16_t> _undoFormatOf;
			uint16_t _insertFormat = 0;
			bool _autoBackground = true;
			COLORREF _background = 0;
			DWORD _eventMask = 0;
			bool _noWrap = false;
			HFONT _ownFont = nullptr; // made for EM_SETCHARFORMAT's face and size
			CHARRANGE _notifiedSelection{-1, -1};
			LPARAM _options = 0;
			LPARAM _langOptions = 0;
			LPARAM _imeStatus = 0;
		};

		LONG styleOf(HWND hwnd)
		{
			return GetWindowLongW(hwnd, GWL_STYLE);
		}

		bool isMultiline(HWND hwnd)
		{
			return (styleOf(hwnd) & ES_MULTILINE) != 0;
		}

		bool isReadOnly(HWND hwnd)
		{
			return (styleOf(hwnd) & ES_READONLY) != 0;
		}

		bool wraps(HWND hwnd, const Edit& edit)
		{
			const LONG style = styleOf(hwnd);
			// A rich edit wraps unless EM_SETTARGETDEVICE says otherwise.
			if (edit._rich)
				return (style & ES_MULTILINE) && !edit._noWrap;
			return (style & ES_MULTILINE) && !(style & (ES_AUTOHSCROLL | WS_HSCROLL));
		}

		bool hasBorder(HWND hwnd)
		{
			return (styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE);
		}

		bool isWordCharacter(char16_t ch)
		{
			return ch >= 0x80 || std::iswalnum(static_cast<wint_t>(ch)) || ch == u'_';
		}

		size_t selectionStart(const Edit& edit)
		{
			return std::min(edit._anchor, edit._caret);
		}

		size_t selectionEnd(const Edit& edit)
		{
			return std::max(edit._anchor, edit._caret);
		}

		uint16_t formatIndex(Edit& edit, const CharFormat& format)
		{
			const auto found = std::find(edit._formats.begin(), edit._formats.end(), format);
			if (found != edit._formats.end())
				return static_cast<uint16_t>(found - edit._formats.begin());
			if (edit._formats.size() >= 0xFFFF)
				return 0;
			edit._formats.push_back(format);
			return static_cast<uint16_t>(edit._formats.size() - 1);
		}

		// What text inserted at a position looks like: as the character before it.
		uint16_t formatBefore(const Edit& edit, size_t position)
		{
			if (edit._formatOf.empty())
				return 0;
			return edit._formatOf[std::min(position, edit._formatOf.size()) - (position > 0 ? 1 : 0)];
		}

		// A rich edit keeps "\r" for a paragraph end: "\r\n" and "\n" become "\r".
		std::u16string richLineBreaks(std::u16string_view text)
		{
			std::u16string result;
			result.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
					continue; // the "\n" that follows is the paragraph end
				result += text[i] == u'\n' ? u'\r' : text[i];
			}
			return result;
		}

		// What WM_GETTEXT and the clipboard get from a rich edit: "\r\n" for each paragraph end.
		std::u16string withCrLf(std::u16string_view text)
		{
			std::u16string result;
			result.reserve(text.size());
			for (const char16_t ch : text)
			{
				result += ch;
				if (ch == u'\r')
					result += u'\n';
			}
			return result;
		}

		// EN_SELCHANGE, when the parent asked for it (ENM_SELCHANGE) and the selection is not the one it
		// was last told.
		void notifySelection(HWND hwnd, Edit& edit)
		{
			if (!edit._rich || !(edit._eventMask & ENM_SELCHANGE))
				return;
			const CHARRANGE range{static_cast<LONG>(selectionStart(edit)), static_cast<LONG>(selectionEnd(edit))};
			if (range.cpMin == edit._notifiedSelection.cpMin && range.cpMax == edit._notifiedSelection.cpMax)
				return;
			edit._notifiedSelection = range;
			SELCHANGE change{};
			change.nmhdr.hwndFrom = hwnd;
			change.nmhdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(hwnd));
			change.nmhdr.code = EN_SELCHANGE;
			change.chrg = range;
			change.seltyp = static_cast<WORD>(range.cpMin == range.cpMax ? SEL_EMPTY : SEL_TEXT);
			SendMessageW(GetParent(hwnd), WM_NOTIFY, change.nmhdr.idFrom, reinterpret_cast<LPARAM>(&change));
		}

		// The area the text is drawn in: inside the border and the margins.
		RECT textArea(HWND hwnd, const Edit& edit)
		{
			RECT area{};
			GetClientRect(hwnd, &area);
			if (hasBorder(hwnd))
				InflateRect(&area, -borderWidth, -borderWidth);
			area.left += edit._leftMargin;
			area.right -= edit._rightMargin;
			if (area.right < area.left)
				area.right = area.left;
			return area;
		}

		class EditDC
		{
		public:
			explicit EditDC(HWND hwnd) : _hwnd{hwnd}, _hdc{GetDC(hwnd)}
			{
				_oldFont = SelectObject(_hdc, fontOf(hwnd));
			}

			~EditDC()
			{
				SelectObject(_hdc, _oldFont);
				ReleaseDC(_hwnd, _hdc);
			}

			HDC get() const { return _hdc; }

		private:
			HWND _hwnd;
			HDC _hdc;
			HGDIOBJ _oldFont;
		};

		int lineHeight(HDC hdc)
		{
			TEXTMETRICW metrics{};
			GetTextMetricsW(hdc, &metrics);
			return std::max<int>(1, metrics.tmHeight);
		}

		// What is drawn: the text, or the password character for each character.
		std::u16string displayText(const Edit& edit, size_t start, size_t length)
		{
			if (edit._passwordChar)
				return std::u16string(length, edit._passwordChar);
			return edit._text.substr(start, length);
		}

		int widthOf(HDC hdc, const std::u16string& text)
		{
			if (text.empty())
				return 0;
			SIZE size{};
			GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
			return size.cx;
		}

		// How many characters of the text fit in the width.
		size_t fittingCharacters(HDC hdc, const std::u16string& text, int width)
		{
			if (text.empty())
				return 0;
			int fit = 0;
			SIZE size{};
			GetTextExtentExPointW(hdc, text.c_str(), static_cast<int>(text.size()), std::max(0, width), &fit, nullptr, &size);
			return static_cast<size_t>(std::max(0, fit));
		}

		void layout(HWND hwnd, Edit& edit)
		{
			if (edit._layoutValid)
				return;
			edit._lines.clear();
			if (!isMultiline(hwnd))
			{
				edit._lines.push_back({0, edit._text.size()});
				edit._layoutValid = true;
				return;
			}

			EditDC dc{hwnd};
			const bool wrap = wraps(hwnd, edit);
			const RECT area = textArea(hwnd, edit);
			const int width = area.right - area.left;
			size_t position = 0;
			for (;;)
			{
				size_t end = edit._text.find(edit._rich ? u'\r' : u'\n', position);
				const size_t paragraphEnd = end == std::u16string::npos ? edit._text.size() : end;
				size_t contentEnd = paragraphEnd;
				if (!edit._rich && contentEnd > position && edit._text[contentEnd - 1] == u'\r')
					--contentEnd;

				// Word wrap: break after the last space that fits, or where the width runs out.
				size_t start = position;
				while (wrap && contentEnd - start > 0)
				{
					const std::u16string rest = displayText(edit, start, contentEnd - start);
					size_t fit = fittingCharacters(dc.get(), rest, width);
					if (fit >= rest.size())
						break;
					size_t breakAt = fit;
					while (breakAt > 0 && rest[breakAt - 1] != u' ' && rest[breakAt - 1] != u'\t')
						--breakAt;
					if (breakAt == 0)
						breakAt = std::max<size_t>(1, fit);
					edit._lines.push_back({start, breakAt});
					start += breakAt;
				}
				edit._lines.push_back({start, contentEnd - start});
				if (end == std::u16string::npos)
					break;
				position = end + 1;
			}
			edit._layoutValid = true;
		}

		void invalidateLayout(Edit& edit)
		{
			edit._layoutValid = false;
		}

		size_t lineOf(HWND hwnd, Edit& edit, size_t position)
		{
			layout(hwnd, edit);
			// The last line whose start is at or before the position.
			size_t line = 0;
			for (size_t i = 0; i < edit._lines.size(); ++i)
			{
				if (edit._lines[i]._start <= position)
					line = i;
				else
					break;
			}
			return line;
		}

		int visibleLines(HWND hwnd, const Edit& edit, HDC hdc)
		{
			const RECT area = textArea(hwnd, edit);
			return std::max(1, static_cast<int>(area.bottom - area.top) / lineHeight(hdc));
		}

		// The x of a position within its line, relative to the text area, before scrolling.
		int xOf(HWND hwnd, Edit& edit, HDC hdc, size_t position)
		{
			const Line& line = edit._lines[lineOf(hwnd, edit, position)];
			const size_t offset = std::min(position, line._start + line._length) - line._start;
			return widthOf(hdc, displayText(edit, line._start, offset));
		}

		// Single-line ES_CENTER / ES_RIGHT: where the line starts.
		int alignmentOffset(HWND hwnd, Edit& edit, HDC hdc, const Line& line)
		{
			const LONG align = styleOf(hwnd) & (ES_CENTER | ES_RIGHT);
			if (!align)
				return 0;
			const RECT area = textArea(hwnd, edit);
			const int free = (area.right - area.left) - widthOf(hdc, displayText(edit, line._start, line._length));
			if (free <= 0)
				return 0;
			return (align & ES_CENTER) ? free / 2 : free;
		}

		int lineTop(HWND hwnd, Edit& edit, HDC hdc, size_t line)
		{
			const RECT area = textArea(hwnd, edit);
			const int height = lineHeight(hdc);
			if (!isMultiline(hwnd))
				return area.top + std::max(0, static_cast<int>(area.bottom - area.top - height) / 2);
			return area.top + (static_cast<int>(line) - static_cast<int>(edit._firstLine)) * height;
		}

		// The character position nearest to a client point.
		size_t positionAt(HWND hwnd, Edit& edit, POINT point)
		{
			layout(hwnd, edit);
			EditDC dc{hwnd};
			const RECT area = textArea(hwnd, edit);
			const int height = lineHeight(dc.get());
			size_t line = 0;
			if (isMultiline(hwnd))
			{
				const int row = (point.y - area.top) >= 0 ? (point.y - area.top) / height : -1;
				const long index = static_cast<long>(edit._firstLine) + row;
				line = static_cast<size_t>(std::clamp<long>(index, 0, static_cast<long>(edit._lines.size()) - 1));
			}
			const Line& current = edit._lines[line];
			const int x = point.x - area.left + edit._scrollX - alignmentOffset(hwnd, edit, dc.get(), current);
			const std::u16string text = displayText(edit, current._start, current._length);
			// The nearest character boundary.
			size_t best = 0;
			int previous = 0;
			for (size_t i = 1; i <= text.size(); ++i)
			{
				const int right = widthOf(dc.get(), text.substr(0, i));
				if (x < (previous + right) / 2)
					break;
				best = i;
				previous = right;
			}
			return current._start + best;
		}

		void ensureCaretVisible(HWND hwnd, Edit& edit)
		{
			layout(hwnd, edit);
			EditDC dc{hwnd};
			const RECT area = textArea(hwnd, edit);
			const int width = area.right - area.left;
			const size_t line = lineOf(hwnd, edit, edit._caret);
			if (isMultiline(hwnd))
			{
				const size_t visible = static_cast<size_t>(visibleLines(hwnd, edit, dc.get()));
				if (line < edit._firstLine)
					edit._firstLine = line;
				else if (line >= edit._firstLine + visible)
					edit._firstLine = line - visible + 1;
			}
			if (wraps(hwnd, edit))
			{
				edit._scrollX = 0;
				return;
			}
			const int x = xOf(hwnd, edit, dc.get(), edit._caret);
			if (x < edit._scrollX)
				edit._scrollX = std::max(0, x - width / 4);
			else if (x > edit._scrollX + width - 1)
				edit._scrollX = x - width + 1 + width / 4;
			// Do not leave empty space on the right once the text fits.
			const Line& current = edit._lines[line];
			const int lineWidth = widthOf(dc.get(), displayText(edit, current._start, current._length));
			if (edit._scrollX > 0 && lineWidth - edit._scrollX < width - 1)
				edit._scrollX = std::max(0, lineWidth - width + 1);
		}

		void restartCaret(HWND hwnd, Edit& edit)
		{
			if (!edit._focused)
				return;
			edit._caretOn = true;
			SetTimer(hwnd, caretTimer, GetCaretBlinkTime(), nullptr);
		}

		void changed(HWND hwnd, Edit& edit, bool notify)
		{
			invalidateLayout(edit);
			ensureCaretVisible(hwnd, edit);
			restartCaret(hwnd, edit);
			InvalidateRect(hwnd, nullptr, FALSE);
			if (notify)
			{
				// A rich edit tells its parent what it asked for (EM_SETEVENTMASK).
				if (!edit._rich || (edit._eventMask & ENM_UPDATE))
					sendCommand(hwnd, EN_UPDATE);
				if (!edit._rich || (edit._eventMask & ENM_CHANGE))
					sendCommand(hwnd, EN_CHANGE);
			}
			notifySelection(hwnd, edit);
		}

		void setSelection(HWND hwnd, Edit& edit, size_t anchor, size_t caret)
		{
			edit._anchor = std::min(anchor, edit._text.size());
			edit._caret = std::min(caret, edit._text.size());
			if (edit._rich)
				edit._insertFormat = formatBefore(edit, selectionStart(edit));
			ensureCaretVisible(hwnd, edit);
			restartCaret(hwnd, edit);
			InvalidateRect(hwnd, nullptr, FALSE);
			notifySelection(hwnd, edit);
		}

		void saveUndo(Edit& edit)
		{
			edit._undoText = edit._text;
			edit._undoFormatOf = edit._formatOf;
			edit._undoAnchor = edit._anchor;
			edit._undoCaret = edit._caret;
			edit._canUndo = true;
		}

		// Replaces the selection with text, as typing and EM_REPLACESEL do.
		void replaceSelection(HWND hwnd, Edit& edit, std::u16string text, bool canUndo)
		{
			const LONG style = styleOf(hwnd);
			if (!isMultiline(hwnd))
			{
				// A single line keeps what comes before the first line break.
				const size_t lineBreak = text.find_first_of(u"\r\n");
				if (lineBreak != std::u16string::npos)
					text.erase(lineBreak);
			}
			if (style & ES_UPPERCASE)
				std::transform(text.begin(), text.end(), text.begin(), [](char16_t ch) { return static_cast<char16_t>(std::towupper(ch)); });
			else if (style & ES_LOWERCASE)
				std::transform(text.begin(), text.end(), text.begin(), [](char16_t ch) { return static_cast<char16_t>(std::towlower(ch)); });
			if (edit._rich)
				text = richLineBreaks(text);

			const size_t start = selectionStart(edit);
			const size_t end = selectionEnd(edit);
			const size_t room = edit._limit - std::min<size_t>(edit._limit, edit._text.size() - (end - start));
			if (text.size() > room)
			{
				text.erase(room);
				sendCommand(hwnd, EN_MAXTEXT);
			}
			if (text.empty() && start == end)
				return;
			if (canUndo)
				saveUndo(edit);
			edit._typingEnd = std::u16string::npos;
			edit._text.replace(start, end - start, text);
			if (edit._rich)
			{
				const auto formats = edit._formatOf.begin();
				edit._formatOf.erase(formats + static_cast<std::ptrdiff_t>(start), formats + static_cast<std::ptrdiff_t>(end));
				edit._formatOf.insert(edit._formatOf.begin() + static_cast<std::ptrdiff_t>(start), text.size(), edit._insertFormat);
			}
			edit._anchor = edit._caret = start + text.size();
			edit._modified = true;
			edit._desiredX = -1;
			changed(hwnd, edit, true);
		}

		std::u16string selectedText(const Edit& edit)
		{
			return edit._text.substr(selectionStart(edit), selectionEnd(edit) - selectionStart(edit));
		}

		void copyToClipboard(HWND hwnd, const Edit& edit)
		{
			// Password text is never copied.
			if (edit._passwordChar || selectionStart(edit) == selectionEnd(edit))
				return;
			const std::u16string text = edit._rich ? withCrLf(selectedText(edit)) : selectedText(edit);
			if (!OpenClipboard(hwnd))
				return;
			EmptyClipboard();
			HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
			if (memory)
			{
				auto* buffer = static_cast<char16_t*>(GlobalLock(memory));
				std::copy(text.begin(), text.end(), buffer);
				buffer[text.size()] = u'\0';
				GlobalUnlock(memory);
				SetClipboardData(CF_UNICODETEXT, memory);
			}
			CloseClipboard();
		}

		void pasteFromClipboard(HWND hwnd, Edit& edit)
		{
			if (isReadOnly(hwnd) || !OpenClipboard(hwnd))
				return;
			std::u16string text;
			if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
			{
				if (const auto* buffer = static_cast<const char16_t*>(GlobalLock(data)))
				{
					text = buffer;
					GlobalUnlock(data);
				}
			}
			CloseClipboard();
			if (isMultiline(hwnd))
			{
				// Lone "\n" from other programs become "\r\n".
				std::u16string normalized;
				for (size_t i = 0; i < text.size(); ++i)
				{
					if (text[i] == u'\n' && (i == 0 || text[i - 1] != u'\r'))
						normalized += u'\r';
					normalized += text[i];
				}
				text = std::move(normalized);
			}
			replaceSelection(hwnd, edit, text, true);
		}

		void undo(HWND hwnd, Edit& edit)
		{
			if (!edit._canUndo)
				return;
			// Undo can be undone: the states swap.
			std::swap(edit._text, edit._undoText);
			std::swap(edit._formatOf, edit._undoFormatOf);
			std::swap(edit._anchor, edit._undoAnchor);
			std::swap(edit._caret, edit._undoCaret);
			edit._typingEnd = std::u16string::npos;
			edit._anchor = std::min(edit._anchor, edit._text.size());
			edit._caret = std::min(edit._caret, edit._text.size());
			edit._modified = true;
			changed(hwnd, edit, true);
		}

		size_t previousWordStart(const Edit& edit, size_t position)
		{
			while (position > 0 && !isWordCharacter(edit._text[position - 1]))
				--position;
			while (position > 0 && isWordCharacter(edit._text[position - 1]))
				--position;
			return position;
		}

		size_t nextWordStart(const Edit& edit, size_t position)
		{
			const size_t size = edit._text.size();
			while (position < size && isWordCharacter(edit._text[position]))
				++position;
			while (position < size && !isWordCharacter(edit._text[position]))
				++position;
			return position;
		}

		// A position never falls between "\r" and "\n".
		size_t previousPosition(const Edit& edit, size_t position)
		{
			if (position == 0)
				return 0;
			if (position >= 2 && edit._text[position - 1] == u'\n' && edit._text[position - 2] == u'\r')
				return position - 2;
			return position - 1;
		}

		size_t nextPosition(const Edit& edit, size_t position)
		{
			if (position >= edit._text.size())
				return edit._text.size();
			if (edit._text[position] == u'\r' && position + 1 < edit._text.size() && edit._text[position + 1] == u'\n')
				return position + 2;
			return position + 1;
		}

		// Up and Down: the position on another line nearest to the remembered x.
		size_t verticalMove(HWND hwnd, Edit& edit, long lines)
		{
			layout(hwnd, edit);
			EditDC dc{hwnd};
			if (edit._desiredX < 0)
				edit._desiredX = xOf(hwnd, edit, dc.get(), edit._caret);
			const long target = std::clamp<long>(static_cast<long>(lineOf(hwnd, edit, edit._caret)) + lines, 0, static_cast<long>(edit._lines.size()) - 1);
			const Line& line = edit._lines[static_cast<size_t>(target)];
			const std::u16string text = displayText(edit, line._start, line._length);
			size_t best = 0;
			int previous = 0;
			for (size_t i = 1; i <= text.size(); ++i)
			{
				const int right = widthOf(dc.get(), text.substr(0, i));
				if (edit._desiredX < (previous + right) / 2)
					break;
				best = i;
				previous = right;
			}
			return line._start + best;
		}

		void moveCaret(HWND hwnd, Edit& edit, size_t position, bool extend, bool keepDesiredX = false)
		{
			if (!keepDesiredX)
				edit._desiredX = -1;
			setSelection(hwnd, edit, extend ? edit._anchor : position, position);
		}

		void keyDown(HWND hwnd, Edit& edit, WPARAM key)
		{
			const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
			const bool multiline = isMultiline(hwnd);
			layout(hwnd, edit);
			const Line& line = edit._lines[lineOf(hwnd, edit, edit._caret)];
			switch (key)
			{
				case VK_LEFT:
					if (!shift && selectionStart(edit) != selectionEnd(edit))
						moveCaret(hwnd, edit, selectionStart(edit), false);
					else
						moveCaret(hwnd, edit, control ? previousWordStart(edit, edit._caret) : previousPosition(edit, edit._caret), shift);
					break;
				case VK_RIGHT:
					if (!shift && selectionStart(edit) != selectionEnd(edit))
						moveCaret(hwnd, edit, selectionEnd(edit), false);
					else
						moveCaret(hwnd, edit, control ? nextWordStart(edit, edit._caret) : nextPosition(edit, edit._caret), shift);
					break;
				case VK_HOME:
					moveCaret(hwnd, edit, control || !multiline ? 0 : line._start, shift);
					break;
				case VK_END:
					moveCaret(hwnd, edit, control || !multiline ? edit._text.size() : line._start + line._length, shift);
					break;
				case VK_UP:
				case VK_DOWN:
				case VK_PRIOR:
				case VK_NEXT:
				{
					if (!multiline)
					{
						if (key == VK_UP || key == VK_PRIOR)
							moveCaret(hwnd, edit, previousPosition(edit, edit._caret), shift);
						else
							moveCaret(hwnd, edit, nextPosition(edit, edit._caret), shift);
						break;
					}
					EditDC dc{hwnd};
					const long page = std::max(1, visibleLines(hwnd, edit, dc.get()) - 1);
					const long lines = key == VK_UP ? -1 : (key == VK_DOWN ? 1 : (key == VK_PRIOR ? -page : page));
					moveCaret(hwnd, edit, verticalMove(hwnd, edit, lines), shift, true);
					break;
				}
				case VK_DELETE:
				{
					if (isReadOnly(hwnd))
						break;
					if (shift)
					{
						// Shift+Delete cuts.
						copyToClipboard(hwnd, edit);
						replaceSelection(hwnd, edit, {}, true);
						break;
					}
					if (selectionStart(edit) == selectionEnd(edit))
						edit._anchor = control ? nextWordStart(edit, edit._caret) : nextPosition(edit, edit._caret);
					replaceSelection(hwnd, edit, {}, true);
					break;
				}
				case VK_INSERT:
					if (control)
						copyToClipboard(hwnd, edit);
					else if (shift)
						pasteFromClipboard(hwnd, edit);
					break;
				default:
					break;
			}
		}

		void character(HWND hwnd, Edit& edit, char16_t ch)
		{
			switch (ch)
			{
				case 0x01: // Ctrl+A
					setSelection(hwnd, edit, 0, edit._text.size());
					return;
				case 0x03: // Ctrl+C
					copyToClipboard(hwnd, edit);
					return;
				case 0x16: // Ctrl+V
					pasteFromClipboard(hwnd, edit);
					return;
				case 0x18: // Ctrl+X
					if (!isReadOnly(hwnd))
					{
						copyToClipboard(hwnd, edit);
						replaceSelection(hwnd, edit, {}, true);
					}
					return;
				case 0x1A: // Ctrl+Z
					if (!isReadOnly(hwnd))
						undo(hwnd, edit);
					return;
				case 0x7F: // Ctrl+Backspace deletes the word before the caret
				case 0x08:
				{
					if (isReadOnly(hwnd))
						return;
					if (selectionStart(edit) == selectionEnd(edit))
						edit._anchor = ch == 0x7F ? previousWordStart(edit, edit._caret) : previousPosition(edit, edit._caret);
					replaceSelection(hwnd, edit, {}, true);
					return;
				}
				case u'\r':
				case u'\n':
					if (isMultiline(hwnd) && !isReadOnly(hwnd))
						replaceSelection(hwnd, edit, u"\r\n", true);
					return;
				case u'\t':
					if (isMultiline(hwnd) && !isReadOnly(hwnd))
						replaceSelection(hwnd, edit, u"\t", true);
					return;
				default:
					break;
			}
			if (ch < 0x20 || isReadOnly(hwnd))
				return;
			if ((styleOf(hwnd) & ES_NUMBER) && !(ch >= u'0' && ch <= u'9'))
			{
				MessageBeep(MB_OK);
				return;
			}
			// Typing is one undo step while the caret stays at the end of the run, as in Windows' edit
			// control: Ctrl+Z takes back the whole word typed, not its last letter.
			const bool continuing = edit._canUndo && selectionStart(edit) == selectionEnd(edit) && edit._caret == edit._typingEnd;
			replaceSelection(hwnd, edit, std::u16string(1, ch), !continuing);
			edit._typingEnd = edit._caret;
		}

		// The caret's rectangle, in client coordinates (hdc has the edit's font).
		RECT caretRect(HWND hwnd, Edit& edit, HDC hdc)
		{
			const RECT area = textArea(hwnd, edit);
			const size_t line = lineOf(hwnd, edit, edit._caret);
			const int top = lineTop(hwnd, edit, hdc, line);
			const int x = area.left - edit._scrollX + alignmentOffset(hwnd, edit, hdc, edit._lines[line]) + xOf(hwnd, edit, hdc, edit._caret);
			return RECT{x, top, x + 1, top + lineHeight(hdc)};
		}

		// The fonts of a rich edit's runs: the window's font with the run's effects.
		class RichFonts
		{
		public:
			explicit RichFonts(HFONT base) : _base{base} {}

			~RichFonts()
			{
				for (const auto& [effects, font] : _fonts)
					DeleteObject(font);
			}

			RichFonts(const RichFonts&) = delete;
			RichFonts& operator=(const RichFonts&) = delete;

			HFONT get(DWORD effects)
			{
				effects &= CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT;
				if (effects == 0)
					return _base;
				if (const auto found = _fonts.find(effects); found != _fonts.end())
					return found->second;
				LOGFONTW log{};
				GetObjectW(_base, sizeof(log), &log);
				if (effects & CFE_BOLD)
					log.lfWeight = FW_BOLD;
				if (effects & CFE_ITALIC)
					log.lfItalic = TRUE;
				if (effects & CFE_UNDERLINE)
					log.lfUnderline = TRUE;
				if (effects & CFE_STRIKEOUT)
					log.lfStrikeOut = TRUE;
				HFONT font = CreateFontIndirectW(&log);
				_fonts[effects] = font;
				return font;
			}

		private:
			HFONT _base;
			std::map<DWORD, HFONT> _fonts;
		};

		// A rich edit's line, run by run, each with its format's colors and effects. Places are measured
		// with the window's font: bold and italic runs keep their width in the monospaced fonts of the
		// consoles that use rich edits.
		void drawRichLine(HDC hdc, const Edit& edit, const Line& line, int x, int top, int height, COLORREF autoColor, RichFonts& fonts)
		{
			const size_t end = line._start + line._length;
			size_t runStart = line._start;
			while (runStart < end)
			{
				const uint16_t index = runStart < edit._formatOf.size() ? edit._formatOf[runStart] : 0;
				size_t runEnd = runStart + 1;
				while (runEnd < end && runEnd < edit._formatOf.size() && edit._formatOf[runEnd] == index)
					++runEnd;
				const CharFormat& format = edit._formats[index];
				const std::u16string text = displayText(edit, runStart, runEnd - runStart);
				const int left = x + widthOf(hdc, displayText(edit, line._start, runStart - line._start));
				if (!format._autoBack)
				{
					RECT box{left, top, left + widthOf(hdc, text), top + height};
					HBRUSH brush = CreateSolidBrush(format._back);
					FillRect(hdc, &box, brush);
					DeleteObject(brush);
				}
				SetTextColor(hdc, format._autoColor ? autoColor : format._color);
				HGDIOBJ previous = SelectObject(hdc, fonts.get(format._effects));
				TextOutW(hdc, left, top, text.c_str(), static_cast<int>(text.size()));
				SelectObject(hdc, previous);
				runStart = runEnd;
			}
		}

		void paint(HWND hwnd, Edit& edit, HDC hdc)
		{
			layout(hwnd, edit);
			RECT client{};
			GetClientRect(hwnd, &client);
			const bool enabled = IsWindowEnabled(hwnd) != FALSE;
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			// A rich edit has its own colors (EM_SETBKGNDCOLOR, its formats'); an edit asks its parent.
			HBRUSH ownBackground = nullptr;
			HBRUSH background = nullptr;
			if (edit._rich)
			{
				ownBackground = CreateSolidBrush(edit._autoBackground ? GetSysColor(COLOR_WINDOW) : edit._background);
				background = ownBackground;
				SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
			}
			else
			{
				background = controlColors(hwnd, hdc, isReadOnly(hwnd) || !enabled ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT);
			}
			FillRect(hdc, &client, background);
			RichFonts richFonts{fontOf(hwnd)};
			if (hasBorder(hwnd))
			{
				HBRUSH frame = CreateSolidBrush(edit._focused ? controlPalette()._accent : controlPalette()._frame);
				FrameRect(hdc, &client, frame);
				DeleteObject(frame);
			}
			const COLORREF textColor = enabled ? GetTextColor(hdc) : GetSysColor(COLOR_GRAYTEXT);

			const RECT area = textArea(hwnd, edit);
			IntersectClipRect(hdc, area.left, area.top, area.right, area.bottom);
			SetBkMode(hdc, TRANSPARENT);
			const int height = lineHeight(hdc);

			if (edit._text.empty() && !edit._cueBanner.empty() && (!edit._focused || edit._cueWhenFocused))
			{
				SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
				TextOutW(hdc, area.left, lineTop(hwnd, edit, hdc, 0), edit._cueBanner.c_str(), static_cast<int>(edit._cueBanner.size()));
			}

			const bool showSelection = edit._focused || (styleOf(hwnd) & ES_NOHIDESEL);
			const size_t selStart = selectionStart(edit);
			const size_t selEnd = selectionEnd(edit);
			const size_t first = isMultiline(hwnd) ? edit._firstLine : 0;
			for (size_t i = first; i < edit._lines.size(); ++i)
			{
				const int top = lineTop(hwnd, edit, hdc, i);
				if (top >= area.bottom)
					break;
				const Line& line = edit._lines[i];
				const std::u16string text = displayText(edit, line._start, line._length);
				const int x = area.left - edit._scrollX + alignmentOffset(hwnd, edit, hdc, line);
				if (edit._rich)
				{
					drawRichLine(hdc, edit, line, x, top, height, textColor, richFonts);
				}
				else
				{
					SetTextColor(hdc, textColor);
					TextOutW(hdc, x, top, text.c_str(), static_cast<int>(text.size()));
				}

				// The selected part is drawn again over a highlight.
				const size_t from = std::max(selStart, line._start);
				const size_t to = std::min(selEnd, line._start + line._length);
				if (showSelection && from < to)
				{
					const int left = x + widthOf(hdc, text.substr(0, from - line._start));
					const int right = x + widthOf(hdc, text.substr(0, to - line._start));
					RECT highlight{left, top, right, top + height};
					FillRect(hdc, &highlight, GetSysColorBrush(edit._focused ? COLOR_HIGHLIGHT : COLOR_BTNFACE));
					SetTextColor(hdc, edit._focused ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor);
					const std::u16string part = text.substr(from - line._start, to - from);
					TextOutW(hdc, left, top, part.c_str(), static_cast<int>(part.size()));
				}
			}

			RECT caret = caretRect(hwnd, edit, hdc);
			if (edit._focused && !edit._composition.empty())
			{
				// The composition is drawn over the text at the caret, underlined, as Windows' IME
				// window does; the caret moves within it.
				const int width = widthOf(hdc, edit._composition);
				RECT box{caret.left, caret.top, caret.left + width, caret.bottom};
				FillRect(hdc, &box, background);
				SetTextColor(hdc, textColor);
				TextOutW(hdc, box.left, box.top, edit._composition.c_str(), static_cast<int>(edit._composition.size()));
				RECT underline{box.left, box.bottom - 1, box.right, box.bottom};
				HBRUSH underlineBrush = CreateSolidBrush(textColor);
				FillRect(hdc, &underline, underlineBrush);
				DeleteObject(underlineBrush);
				const int offset = widthOf(hdc, edit._composition.substr(0, edit._compositionCaret));
				caret.left += offset;
				caret.right += offset;
			}
			if (edit._focused && edit._caretOn)
			{
				HBRUSH caretBrush = CreateSolidBrush(textColor);
				FillRect(hdc, &caret, caretBrush);
				DeleteObject(caretBrush);
			}
			RestoreDC(hdc, saved);
			if (ownBackground)
				DeleteObject(ownBackground);
		}

		void showContextMenu(HWND hwnd, Edit& edit, LPARAM lParam)
		{
			const bool readOnly = isReadOnly(hwnd);
			const bool hasSelection = selectionStart(edit) != selectionEnd(edit);
			const bool canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
			HMENU menu = CreatePopupMenu();
			AppendMenuW(menu, MF_STRING | (edit._canUndo && !readOnly ? 0 : MF_GRAYED), commandUndo, u"&Undo");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING | (hasSelection && !readOnly && !edit._passwordChar ? 0 : MF_GRAYED), commandCut, u"Cu&t");
			AppendMenuW(menu, MF_STRING | (hasSelection && !edit._passwordChar ? 0 : MF_GRAYED), commandCopy, u"&Copy");
			AppendMenuW(menu, MF_STRING | (canPaste && !readOnly ? 0 : MF_GRAYED), commandPaste, u"&Paste");
			AppendMenuW(menu, MF_STRING | (hasSelection && !readOnly ? 0 : MF_GRAYED), commandDelete, u"&Delete");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING | (edit._text.empty() ? MF_GRAYED : 0), commandSelectAll, u"Select &All");

			POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			if (lParam == -1)
			{
				RECT window{};
				GetWindowRect(hwnd, &window);
				point = {window.left + 4, window.bottom};
			}
			const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
			DestroyMenu(menu);
			switch (command)
			{
				case commandUndo:
					undo(hwnd, edit);
					break;
				case commandCut:
					copyToClipboard(hwnd, edit);
					replaceSelection(hwnd, edit, {}, true);
					break;
				case commandCopy:
					copyToClipboard(hwnd, edit);
					break;
				case commandPaste:
					pasteFromClipboard(hwnd, edit);
					break;
				case commandDelete:
					replaceSelection(hwnd, edit, {}, true);
					break;
				case commandSelectAll:
					setSelection(hwnd, edit, 0, edit._text.size());
					break;
				default:
					break;
			}
		}

		LRESULT copyText(const std::u16string& text, WPARAM size, LPARAM buffer)
		{
			if (size == 0 || !buffer)
				return 0;
			const size_t count = std::min<size_t>(text.size(), size - 1);
			auto* out = reinterpret_cast<char16_t*>(buffer);
			std::copy_n(text.c_str(), count, out);
			out[count] = u'\0';
			return static_cast<LRESULT>(count);
		}

		// --- Rich edit ------------------------------------------------------------------------------

		// A rich edit's own sizes: twips for a font's height, as CHARFORMAT has them.
		constexpr int twipsPerInch = 1440;
		constexpr int pixelsPerInch = 96;

		// What a format becomes with what a CHARFORMAT sets (its mask).
		CharFormat applyFormat(CharFormat format, const CHARFORMATW& change)
		{
			const DWORD mask = change.dwMask;
			if (mask & CFM_COLOR)
			{
				format._autoColor = (change.dwEffects & CFE_AUTOCOLOR) != 0;
				format._color = change.crTextColor;
			}
			const DWORD effects = mask & (CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT);
			format._effects = (format._effects & ~effects) | (change.dwEffects & effects);
			if ((mask & CFM_BACKCOLOR) && change.cbSize >= sizeof(CHARFORMAT2W))
			{
				const auto& change2 = reinterpret_cast<const CHARFORMAT2W&>(change);
				format._autoBack = (change2.dwEffects & CFE_AUTOBACKCOLOR) != 0;
				format._back = change2.crBackColor;
			}
			return format;
		}

		// EM_SETCHARFORMAT's face and size are the window's font: runs differ in colors and effects only.
		void applyFont(HWND hwnd, Edit& edit, const CHARFORMATW& change)
		{
			if (!(change.dwMask & (CFM_FACE | CFM_SIZE)))
				return;
			LOGFONTW log{};
			GetObjectW(fontOf(hwnd), sizeof(log), &log);
			if (change.dwMask & CFM_FACE)
			{
				const size_t length = std::min<size_t>(std::char_traits<char16_t>::length(change.szFaceName), LF_FACESIZE - 1);
				std::copy_n(change.szFaceName, length, log.lfFaceName);
				log.lfFaceName[length] = u'\0';
			}
			if ((change.dwMask & CFM_SIZE) && change.yHeight > 0)
				log.lfHeight = -MulDiv(change.yHeight, pixelsPerInch, twipsPerInch);
			if (change.dwMask & CFM_CHARSET)
				log.lfCharSet = change.bCharSet;
			log.lfWeight = FW_NORMAL;
			log.lfItalic = FALSE;
			HFONT font = CreateFontIndirectW(&log);
			SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
			if (edit._ownFont)
				DeleteObject(edit._ownFont);
			edit._ownFont = font;
		}

		void setCharFormat(HWND hwnd, Edit& edit, WPARAM scope, const CHARFORMATW& change)
		{
			std::map<uint16_t, uint16_t> changed; // each format, changed once
			auto change1 = [&](uint16_t index)
			{
				const auto found = changed.find(index);
				if (found != changed.end())
					return found->second;
				const uint16_t result = formatIndex(edit, applyFormat(edit._formats[index], change));
				changed[index] = result;
				return result;
			};
			if (scope & SCF_ALL)
			{
				applyFont(hwnd, edit, change);
				for (uint16_t& index : edit._formatOf)
					index = change1(index);
				edit._insertFormat = change1(edit._insertFormat);
				edit._formats[0] = applyFormat(edit._formats[0], change);
			}
			else if (scope & SCF_SELECTION)
			{
				const size_t start = selectionStart(edit);
				const size_t end = selectionEnd(edit);
				for (size_t i = start; i < end && i < edit._formatOf.size(); ++i)
					edit._formatOf[i] = change1(edit._formatOf[i]);
				edit._insertFormat = change1(edit._insertFormat);
			}
			else
			{
				// SCF_DEFAULT: the default format, and the font.
				applyFont(hwnd, edit, change);
				edit._formats[0] = applyFormat(edit._formats[0], change);
			}
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		DWORD getCharFormat(HWND hwnd, const Edit& edit, WPARAM scope, CHARFORMATW& out)
		{
			const size_t start = selectionStart(edit);
			uint16_t index = 0;
			if (scope & SCF_SELECTION)
				index = start == selectionEnd(edit) || start >= edit._formatOf.size() ? edit._insertFormat : edit._formatOf[start];
			const CharFormat& format = edit._formats[index];
			LOGFONTW log{};
			GetObjectW(fontOf(hwnd), sizeof(log), &log);
			out.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_FACE | CFM_SIZE | CFM_CHARSET;
			out.dwEffects = format._effects | (format._autoColor ? CFE_AUTOCOLOR : 0);
			out.crTextColor = format._autoColor ? GetSysColor(COLOR_WINDOWTEXT) : format._color;
			out.yHeight = MulDiv(std::abs(log.lfHeight), twipsPerInch, pixelsPerInch);
			out.yOffset = 0;
			out.bCharSet = log.lfCharSet;
			out.bPitchAndFamily = log.lfPitchAndFamily;
			std::copy_n(log.lfFaceName, LF_FACESIZE, out.szFaceName);
			if (out.cbSize >= sizeof(CHARFORMAT2W))
			{
				auto& out2 = reinterpret_cast<CHARFORMAT2W&>(out);
				out2.dwMask |= CFM_BACKCOLOR;
				out2.dwEffects |= format._autoBack ? CFE_AUTOBACKCOLOR : 0;
				out2.crBackColor = format._autoBack ? GetSysColor(COLOR_WINDOW) : format._back;
			}
			return out.dwMask;
		}

		// A character position as rich edits take it: -1 (and any other negative) is the end.
		size_t richPosition(const Edit& edit, LONG position)
		{
			return position < 0 ? edit._text.size() : std::min<size_t>(static_cast<size_t>(position), edit._text.size());
		}

		void setRichText(HWND hwnd, Edit& edit, std::u16string text, bool notify)
		{
			edit._text = richLineBreaks(text);
			if (edit._text.size() > edit._limit)
				edit._text.erase(edit._limit);
			edit._formatOf.assign(edit._text.size(), 0);
			edit._insertFormat = 0;
			edit._anchor = edit._caret = 0;
			edit._firstLine = 0;
			edit._scrollX = 0;
			edit._modified = false;
			edit._canUndo = false;
			changed(hwnd, edit, notify);
		}

		LONG findText(const Edit& edit, WPARAM flags, CHARRANGE range, const char16_t* what)
		{
			if (!what || !*what)
				return -1;
			const std::u16string_view needle{what};
			const bool down = (flags & FR_DOWN) != 0;
			const bool matchCase = (flags & FR_MATCHCASE) != 0;
			const bool wholeWord = (flags & FR_WHOLEWORD) != 0;
			const size_t size = edit._text.size();
			// Down: from cpMin to cpMax; up: from cpMin back to cpMax. -1 is the end.
			const size_t from = richPosition(edit, range.cpMin);
			const size_t to = richPosition(edit, range.cpMax);
			auto matchesAt = [&](size_t at)
			{
				if (at + needle.size() > size)
					return false;
				for (size_t i = 0; i < needle.size(); ++i)
				{
					char16_t a = edit._text[at + i];
					char16_t b = needle[i];
					if (!matchCase)
					{
						a = static_cast<char16_t>(std::towlower(a));
						b = static_cast<char16_t>(std::towlower(b));
					}
					if (a != b)
						return false;
				}
				if (wholeWord)
				{
					if (at > 0 && isWordCharacter(edit._text[at - 1]))
						return false;
					if (at + needle.size() < size && isWordCharacter(edit._text[at + needle.size()]))
						return false;
				}
				return true;
			};
			if (down)
			{
				for (size_t at = from; at + needle.size() <= to; ++at)
				{
					if (matchesAt(at))
						return static_cast<LONG>(at);
				}
			}
			else
			{
				const size_t lowest = std::min(from, to);
				for (size_t at = from + 1; at-- > lowest;)
				{
					if (at + needle.size() <= std::max(from, to) && matchesAt(at))
						return static_cast<LONG>(at);
				}
			}
			return -1;
		}

		// EM_STREAMOUT and EM_STREAMIN: the text as bytes (UTF-16 with SF_UNICODE, else the ANSI code
		// page), paragraphs ending with "\r\n". Rich text format is not written or read: its plain text is.
		std::string streamBytes(std::u16string_view text, WPARAM format)
		{
			if (format & SF_UNICODE)
				return std::string(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(char16_t));
			const int length = WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string bytes(static_cast<size_t>(std::max(0, length)), '\0');
			if (length > 0)
				WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), bytes.data(), length, nullptr, nullptr);
			return bytes;
		}

		std::u16string streamText(const std::string& bytes, WPARAM format)
		{
			if (format & SF_UNICODE)
				return std::u16string(reinterpret_cast<const char16_t*>(bytes.data()), bytes.size() / sizeof(char16_t));
			const int length = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
			std::u16string text(static_cast<size_t>(std::max(0, length)), u'\0');
			if (length > 0)
				MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
			return text;
		}

		// EN_MSGFILTER: the parent sees the keyboard and mouse messages it asked for (ENM_KEYEVENTS,
		// ENM_MOUSEEVENTS) first, may change them, and takes them by answering non-zero.
		bool filtered(HWND hwnd, const Edit& edit, UINT& message, WPARAM& wParam, LPARAM& lParam)
		{
			const bool key = message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_SYSKEYDOWN
				|| message == WM_SYSKEYUP || message == WM_SYSCHAR;
			const bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
			if (!((key && (edit._eventMask & ENM_KEYEVENTS)) || (mouse && (edit._eventMask & ENM_MOUSEEVENTS))))
				return false;
			MSGFILTER filter{};
			filter.nmhdr.hwndFrom = hwnd;
			filter.nmhdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(hwnd));
			filter.nmhdr.code = EN_MSGFILTER;
			filter.msg = message;
			filter.wParam = wParam;
			filter.lParam = lParam;
			if (SendMessageW(GetParent(hwnd), WM_NOTIFY, filter.nmhdr.idFrom, reinterpret_cast<LPARAM>(&filter)) != 0)
				return true;
			message = filter.msg;
			wParam = filter.wParam;
			lParam = filter.lParam;
			return false;
		}

		// The messages a rich edit answers its own way; false for those it takes as an edit does.
		bool richMessage(HWND hwnd, Edit& edit, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
		{
			result = 0;
			switch (message)
			{
				case WM_DESTROY:
					if (edit._ownFont)
					{
						DeleteObject(edit._ownFont);
						edit._ownFont = nullptr;
					}
					return false;
				case WM_SETTEXT:
					setRichText(hwnd, edit, lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"", false);
					result = TRUE;
					return true;
				case WM_GETTEXT:
					result = copyText(withCrLf(edit._text), wParam, lParam);
					return true;
				case WM_GETTEXTLENGTH:
					result = static_cast<LRESULT>(withCrLf(edit._text).size());
					return true;
				case EM_GETLINE:
				{
					// A rich edit's line has its paragraph's "\r", when it ends one.
					layout(hwnd, edit);
					if (wParam >= edit._lines.size() || !lParam)
						return true;
					const WORD size = *reinterpret_cast<const WORD*>(lParam);
					const Line& line = edit._lines[wParam];
					const size_t end = line._start + line._length;
					const size_t length = line._length + (end < edit._text.size() && edit._text[end] == u'\r' ? 1 : 0);
					const size_t count = std::min<size_t>(length, size);
					std::copy_n(edit._text.c_str() + line._start, count, reinterpret_cast<char16_t*>(lParam));
					result = static_cast<LRESULT>(count);
					return true;
				}
				case EM_SETSEL:
				{
					const LONG start = static_cast<LONG>(static_cast<int>(wParam));
					const LONG end = static_cast<LONG>(static_cast<int>(lParam));
					edit._desiredX = -1;
					setSelection(hwnd, edit, richPosition(edit, start), richPosition(edit, end));
					return true;
				}
				case EM_EXSETSEL:
				{
					if (const auto* range = reinterpret_cast<const CHARRANGE*>(lParam))
					{
						edit._desiredX = -1;
						setSelection(hwnd, edit, richPosition(edit, range->cpMin), richPosition(edit, range->cpMax));
					}
					result = static_cast<LRESULT>(edit._caret);
					return true;
				}
				case EM_EXGETSEL:
					if (auto* range = reinterpret_cast<CHARRANGE*>(lParam))
					{
						range->cpMin = static_cast<LONG>(selectionStart(edit));
						range->cpMax = static_cast<LONG>(selectionEnd(edit));
					}
					return true;
				case EM_LIMITTEXT:
				case EM_EXLIMITTEXT:
				{
					const LPARAM limit = message == EM_LIMITTEXT ? static_cast<LPARAM>(wParam) : lParam;
					edit._limit = limit > 0 ? static_cast<UINT>(std::min<LPARAM>(limit, maximumLimit)) : 65536;
					return true;
				}
				case EM_EXLINEFROMCHAR:
					result = static_cast<LRESULT>(lineOf(hwnd, edit, richPosition(edit, static_cast<LONG>(lParam))));
					return true;
				case EM_POSFROMCHAR:
				{
					// A rich edit's form: the point is wParam's, the position lParam.
					auto* point = reinterpret_cast<POINTL*>(wParam);
					if (!point)
						return true;
					layout(hwnd, edit);
					EditDC dc{hwnd};
					const size_t position = std::min<size_t>(static_cast<size_t>(lParam), edit._text.size());
					const size_t line = lineOf(hwnd, edit, position);
					const RECT area = textArea(hwnd, edit);
					point->x = area.left - edit._scrollX + alignmentOffset(hwnd, edit, dc.get(), edit._lines[line]) + xOf(hwnd, edit, dc.get(), position);
					point->y = lineTop(hwnd, edit, dc.get(), line);
					return true;
				}
				case EM_CHARFROMPOS:
				{
					const auto* point = reinterpret_cast<const POINTL*>(lParam);
					if (point)
						result = static_cast<LRESULT>(positionAt(hwnd, edit, {point->x, point->y}));
					return true;
				}
				case EM_SETCHARFORMAT:
					if (lParam)
						setCharFormat(hwnd, edit, wParam, *reinterpret_cast<const CHARFORMATW*>(lParam));
					result = TRUE;
					return true;
				case EM_GETCHARFORMAT:
					if (lParam)
						result = static_cast<LRESULT>(getCharFormat(hwnd, edit, wParam, *reinterpret_cast<CHARFORMATW*>(lParam)));
					return true;
				case EM_GETTEXTRANGE:
				{
					auto* range = reinterpret_cast<TEXTRANGEW*>(lParam);
					if (!range || !range->lpstrText)
						return true;
					const size_t start = richPosition(edit, range->chrg.cpMin);
					const size_t end = std::max(start, richPosition(edit, range->chrg.cpMax));
					std::copy(edit._text.begin() + static_cast<std::ptrdiff_t>(start), edit._text.begin() + static_cast<std::ptrdiff_t>(end), range->lpstrText);
					range->lpstrText[end - start] = u'\0';
					result = static_cast<LRESULT>(end - start);
					return true;
				}
				case EM_GETSELTEXT:
				{
					const std::u16string text = selectedText(edit);
					if (lParam)
					{
						std::copy(text.begin(), text.end(), reinterpret_cast<char16_t*>(lParam));
						reinterpret_cast<char16_t*>(lParam)[text.size()] = u'\0';
					}
					result = static_cast<LRESULT>(text.size());
					return true;
				}
				case EM_FINDTEXT:
				case EM_FINDTEXTW:
				{
					// Unicode programs give FINDTEXTW to either.
					if (const auto* find = reinterpret_cast<const FINDTEXTW*>(lParam))
						result = findText(edit, wParam, find->chrg, find->lpstrText);
					else
						result = -1;
					return true;
				}
				case EM_FINDTEXTEX:
				case EM_FINDTEXTEXW:
				{
					auto* find = reinterpret_cast<FINDTEXTEXW*>(lParam);
					result = find ? findText(edit, wParam, find->chrg, find->lpstrText) : -1;
					if (find)
					{
						const LONG length = result < 0 ? 0 : static_cast<LONG>(std::char_traits<char16_t>::length(find->lpstrText));
						find->chrgText = {static_cast<LONG>(result), result < 0 ? -1 : static_cast<LONG>(result) + length};
					}
					return true;
				}
				case EM_GETTEXTLENGTHEX:
				{
					const auto* query = reinterpret_cast<const GETTEXTLENGTHEX*>(wParam);
					const bool crlf = query && (query->flags & GTL_USECRLF);
					result = static_cast<LRESULT>(crlf ? withCrLf(edit._text).size() : edit._text.size());
					return true;
				}
				case EM_GETTEXTEX:
				{
					const auto* query = reinterpret_cast<const GETTEXTEX*>(wParam);
					if (!query || !lParam)
						return true;
					std::u16string text = (query->flags & GT_SELECTION) ? selectedText(edit) : edit._text;
					if (query->flags & GT_USECRLF)
						text = withCrLf(text);
					if (query->codepage == 1200)
					{
						const size_t room = query->cb / sizeof(char16_t);
						result = copyText(text, room, lParam);
					}
					else if (query->cb > 0)
					{
						std::string bytes = streamBytes(text, 0);
						bytes.resize(std::min<size_t>(bytes.size(), query->cb - 1));
						std::memcpy(reinterpret_cast<char*>(lParam), bytes.c_str(), bytes.size() + 1);
						result = static_cast<LRESULT>(bytes.size());
					}
					return true;
				}
				case EM_SETTEXTEX:
				{
					const auto* how = reinterpret_cast<const SETTEXTEX*>(wParam);
					std::u16string text;
					if (lParam)
					{
						if (how && how->codepage != 1200)
							text = streamText(reinterpret_cast<const char*>(lParam), 0);
						else
							text = reinterpret_cast<LPCWSTR>(lParam);
					}
					if (how && (how->flags & ST_SELECTION))
						replaceSelection(hwnd, edit, text, (how->flags & ST_KEEPUNDO) != 0);
					else
						setRichText(hwnd, edit, text, true);
					result = 1;
					return true;
				}
				case EM_SETBKGNDCOLOR:
				{
					result = static_cast<LRESULT>(edit._autoBackground ? GetSysColor(COLOR_WINDOW) : edit._background);
					edit._autoBackground = wParam != 0;
					edit._background = static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return true;
				}
				case EM_SETEVENTMASK:
					result = static_cast<LRESULT>(edit._eventMask);
					edit._eventMask = static_cast<DWORD>(lParam);
					return true;
				case EM_GETEVENTMASK:
					result = static_cast<LRESULT>(edit._eventMask);
					return true;
				case EM_STREAMOUT:
				{
					auto* stream = reinterpret_cast<EDITSTREAM*>(lParam);
					if (!stream || !stream->pfnCallback)
						return true;
					const std::u16string text = withCrLf((wParam & SFF_SELECTION) ? selectedText(edit) : edit._text);
					std::string bytes = streamBytes(text, wParam);
					size_t written = 0;
					stream->dwError = 0;
					while (written < bytes.size())
					{
						LONG done = 0;
						const LONG chunk = static_cast<LONG>(std::min<size_t>(bytes.size() - written, 4096));
						stream->dwError = stream->pfnCallback(stream->dwCookie, reinterpret_cast<LPBYTE>(bytes.data() + written), chunk, &done);
						if (stream->dwError != 0 || done <= 0)
							break;
						written += static_cast<size_t>(done);
					}
					result = static_cast<LRESULT>(text.size());
					return true;
				}
				case EM_STREAMIN:
				{
					auto* stream = reinterpret_cast<EDITSTREAM*>(lParam);
					if (!stream || !stream->pfnCallback)
						return true;
					std::string bytes;
					char buffer[4096];
					stream->dwError = 0;
					for (;;)
					{
						LONG done = 0;
						stream->dwError = stream->pfnCallback(stream->dwCookie, reinterpret_cast<LPBYTE>(buffer), sizeof(buffer), &done);
						if (stream->dwError != 0 || done <= 0)
							break;
						bytes.append(buffer, static_cast<size_t>(done));
					}
					const std::u16string text = streamText(bytes, wParam);
					if (wParam & SFF_SELECTION)
						replaceSelection(hwnd, edit, text, true);
					else
						setRichText(hwnd, edit, text, true);
					result = static_cast<LRESULT>(text.size());
					return true;
				}
				case EM_SETTARGETDEVICE:
					// A line width of 1: no wrapping; 0: wrapping at the window's width.
					edit._noWrap = lParam == 1;
					invalidateLayout(edit);
					ensureCaretVisible(hwnd, edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					result = TRUE;
					return true;
				case EM_CANPASTE:
					result = IsClipboardFormatAvailable(CF_UNICODETEXT);
					return true;
				case EM_PASTESPECIAL:
					pasteFromClipboard(hwnd, edit);
					return true;
				case EM_CANREDO:
				case EM_REDO:
				case EM_AUTOURLDETECT:
				case EM_HIDESELECTION:
				case EM_SHOWSCROLLBAR:
				case EM_SETEDITSTYLE:
				case EM_GETEDITSTYLE:
				case EM_GETPARAFORMAT:
				case EM_SETTEXTMODE:
					return true;
				case EM_SETPARAFORMAT:
				case EM_SETTYPOGRAPHYOPTIONS:
				case EM_SETOLECALLBACK:
					result = TRUE;
					return true;
				case EM_SETUNDOLIMIT:
					result = static_cast<LRESULT>(wParam);
					return true;
				case EM_GETTEXTMODE:
					result = TM_RICHTEXT | TM_SINGLELEVELUNDO;
					return true;
				case EM_GETOLEINTERFACE:
					if (lParam)
						*reinterpret_cast<void**>(lParam) = nullptr;
					return true;
				case EM_SETOPTIONS:
				{
					if (wParam == ECOOP_SET)
						edit._options = lParam;
					else if (wParam == ECOOP_OR)
						edit._options |= lParam;
					else if (wParam == ECOOP_AND)
						edit._options &= lParam;
					else if (wParam == ECOOP_XOR)
						edit._options ^= lParam;
					if (edit._options & ECO_READONLY)
						SendMessageW(hwnd, EM_SETREADONLY, TRUE, 0);
					result = edit._options;
					return true;
				}
				case EM_GETOPTIONS:
					result = edit._options | ((styleOf(hwnd) & ES_READONLY) ? ECO_READONLY : 0);
					return true;
				case EM_SETLANGOPTIONS:
					edit._langOptions = lParam;
					result = TRUE;
					return true;
				case EM_GETLANGOPTIONS:
					result = edit._langOptions;
					return true;
				case EM_SETIMESTATUS:
					result = edit._imeStatus;
					edit._imeStatus = lParam;
					return true;
				case EM_GETIMESTATUS:
					result = edit._imeStatus;
					return true;
				case EM_GETZOOM:
					if (wParam)
						*reinterpret_cast<int*>(wParam) = 0;
					if (lParam)
						*reinterpret_cast<int*>(lParam) = 0;
					result = TRUE;
					return true;
				case EM_SETZOOM:
					result = TRUE;
					return true;
				case EM_GETSCROLLPOS:
				{
					if (auto* point = reinterpret_cast<POINT*>(lParam))
					{
						EditDC dc{hwnd};
						*point = {edit._scrollX, static_cast<LONG>(edit._firstLine) * lineHeight(dc.get())};
					}
					result = 1;
					return true;
				}
				case EM_SETSCROLLPOS:
				{
					if (const auto* point = reinterpret_cast<const POINT*>(lParam))
					{
						EditDC dc{hwnd};
						layout(hwnd, edit);
						const long line = point->y / lineHeight(dc.get());
						edit._firstLine = static_cast<size_t>(std::clamp<long>(line, 0, std::max<long>(0, static_cast<long>(edit._lines.size()) - 1)));
						edit._scrollX = std::max<int>(0, point->x);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					result = 1;
					return true;
				}
				default:
					return false;
			}
		}

		LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			Edit* edit = dataOf<Edit>(hwnd);
			if (edit && edit->_rich)
			{
				if (filtered(hwnd, *edit, message, wParam, lParam))
					return 0;
				if (LRESULT result = 0; richMessage(hwnd, *edit, message, wParam, lParam, result))
					return result;
			}
			switch (message)
			{
				case WM_NCCREATE:
				{
					Edit* created = createData<Edit>(hwnd);
					const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					if (create->lpszName && !IS_INTRESOURCE(create->lpszName))
						created->_text = create->lpszName;
					if (create->style & ES_PASSWORD)
						created->_passwordChar = u'•';
					return TRUE;
				}
				case WM_DESTROY:
					KillTimer(hwnd, caretTimer);
					return 0;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *edit, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *edit, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SIZE:
					invalidateLayout(*edit);
					ensureCaretVisible(hwnd, *edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_SETFONT:
					DefWindowProcW(hwnd, message, wParam, lParam);
					invalidateLayout(*edit);
					if (LOWORD(lParam))
						InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_ENABLE:
				case WM_STYLECHANGED:
					invalidateLayout(*edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_GETDLGCODE:
				{
					LRESULT code = DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS;
					if (isMultiline(hwnd))
					{
						code |= DLGC_WANTALLKEYS;
						// Without ES_WANTRETURN, Enter goes to the default button; Escape and Tab always
						// go to the dialog.
						const auto* msg = reinterpret_cast<const MSG*>(lParam);
						if (msg && msg->message == WM_KEYDOWN)
						{
							if ((msg->wParam == VK_RETURN && !(styleOf(hwnd) & ES_WANTRETURN)) || msg->wParam == VK_ESCAPE || msg->wParam == VK_TAB)
								code &= ~DLGC_WANTALLKEYS;
						}
					}
					return code;
				}
				case WM_SETFOCUS:
					edit->_focused = true;
					restartCaret(hwnd, *edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					sendCommand(hwnd, EN_SETFOCUS);
					return 0;
				case WM_KILLFOCUS:
					edit->_focused = false;
					edit->_caretOn = false;
					edit->_composition.clear();
					KillTimer(hwnd, caretTimer);
					InvalidateRect(hwnd, nullptr, FALSE);
					sendCommand(hwnd, EN_KILLFOCUS);
					return 0;
				case WM_TIMER:
					if (wParam == caretTimer)
					{
						edit->_caretOn = !edit->_caretOn;
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					return DefWindowProcW(hwnd, message, wParam, lParam);
				case WM_KEYDOWN:
					// VK_PROCESSKEY: the input method took the key.
					if (wParam != VK_PROCESSKEY)
						keyDown(hwnd, *edit, wParam);
					return 0;
				case user::imeComposition:
					edit->_composition = *reinterpret_cast<const std::u16string*>(lParam);
					edit->_compositionCaret = std::min<size_t>(wParam, edit->_composition.size());
					restartCaret(hwnd, *edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case user::imeCaretRect:
				{
					layout(hwnd, *edit);
					EditDC dc{hwnd};
					*reinterpret_cast<RECT*>(lParam) = caretRect(hwnd, *edit, dc.get());
					return TRUE;
				}
				case WM_CHAR:
					character(hwnd, *edit, static_cast<char16_t>(wParam));
					return 0;
				case WM_LBUTTONDOWN:
				{
					if (GetFocus() != hwnd)
						SetFocus(hwnd);
					const size_t position = positionAt(hwnd, *edit, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
					moveCaret(hwnd, *edit, position, (wParam & MK_SHIFT) != 0);
					edit->_selecting = true;
					SetCapture(hwnd);
					return 0;
				}
				case WM_LBUTTONDBLCLK:
				{
					// A double click selects the word.
					const size_t position = positionAt(hwnd, *edit, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
					size_t start = position;
					size_t end = position;
					while (start > 0 && isWordCharacter(edit->_text[start - 1]))
						--start;
					while (end < edit->_text.size() && isWordCharacter(edit->_text[end]))
						++end;
					setSelection(hwnd, *edit, start, end);
					return 0;
				}
				case WM_MOUSEMOVE:
					if (edit->_selecting)
					{
						const size_t position = positionAt(hwnd, *edit, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
						if (position != edit->_caret)
							moveCaret(hwnd, *edit, position, true);
					}
					return 0;
				case WM_LBUTTONUP:
				case WM_CAPTURECHANGED:
					if (edit->_selecting)
					{
						edit->_selecting = false;
						if (message == WM_LBUTTONUP)
							ReleaseCapture();
					}
					return 0;
				case WM_MOUSEWHEEL:
				{
					if (!isMultiline(hwnd))
						return 0;
					const int lines = -GET_WHEEL_DELTA_WPARAM(wParam) * wheelLines / WHEEL_DELTA;
					SendMessageW(hwnd, EM_LINESCROLL, 0, lines);
					return 0;
				}
				case WM_CONTEXTMENU:
					if (GetFocus() != hwnd)
						SetFocus(hwnd);
					showContextMenu(hwnd, *edit, lParam);
					return 0;
				case WM_SETTEXT:
				{
					edit->_text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"";
					if (edit->_text.size() > edit->_limit)
						edit->_text.erase(edit->_limit);
					edit->_anchor = edit->_caret = 0;
					edit->_firstLine = 0;
					edit->_scrollX = 0;
					edit->_modified = false;
					edit->_canUndo = false;
					// As on Windows, multi-line edits do not report text set this way.
					changed(hwnd, *edit, !isMultiline(hwnd));
					return TRUE;
				}
				case WM_GETTEXT:
					return copyText(edit->_text, wParam, lParam);
				case WM_GETTEXTLENGTH:
					return static_cast<LRESULT>(edit->_text.size());
				case WM_CUT:
					copyToClipboard(hwnd, *edit);
					if (!isReadOnly(hwnd))
						replaceSelection(hwnd, *edit, {}, true);
					return 0;
				case WM_COPY:
					copyToClipboard(hwnd, *edit);
					return 0;
				case WM_PASTE:
					pasteFromClipboard(hwnd, *edit);
					return 0;
				case WM_CLEAR:
					if (!isReadOnly(hwnd))
						replaceSelection(hwnd, *edit, {}, true);
					return 0;
				case WM_UNDO:
				case EM_UNDO:
					undo(hwnd, *edit);
					return edit->_canUndo;
				case EM_CANUNDO:
					return edit->_canUndo;
				case EM_EMPTYUNDOBUFFER:
					edit->_canUndo = false;
					return 0;
				case EM_GETSEL:
				{
					const size_t start = selectionStart(*edit);
					const size_t end = selectionEnd(*edit);
					if (wParam)
						*reinterpret_cast<DWORD*>(wParam) = static_cast<DWORD>(start);
					if (lParam)
						*reinterpret_cast<DWORD*>(lParam) = static_cast<DWORD>(end);
					return MAKELRESULT(std::min<size_t>(start, 0xFFFF), std::min<size_t>(end, 0xFFFF));
				}
				case EM_SETSEL:
				{
					const long start = static_cast<long>(static_cast<int>(wParam));
					const long end = static_cast<long>(static_cast<int>(lParam));
					const size_t size = edit->_text.size();
					if (start < 0)
					{
						// -1 removes the selection, keeping the caret.
						setSelection(hwnd, *edit, edit->_caret, edit->_caret);
						return 0;
					}
					const size_t anchor = std::min<size_t>(static_cast<size_t>(start), size);
					const size_t caret = end < 0 ? size : std::min<size_t>(static_cast<size_t>(end), size);
					edit->_desiredX = -1;
					setSelection(hwnd, *edit, anchor, caret);
					return 0;
				}
				case EM_REPLACESEL:
					replaceSelection(hwnd, *edit, lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"", wParam != 0);
					return 0;
				case EM_LIMITTEXT:
					edit->_limit = wParam ? static_cast<UINT>(wParam) : maximumLimit;
					return 0;
				case EM_GETLIMITTEXT:
					return edit->_limit;
				case EM_GETMODIFY:
					return edit->_modified;
				case EM_SETMODIFY:
					edit->_modified = wParam != 0;
					return 0;
				case EM_SETREADONLY:
				{
					const LONG style = styleOf(hwnd);
					SetWindowLongW(hwnd, GWL_STYLE, wParam ? (style | ES_READONLY) : (style & ~static_cast<LONG>(ES_READONLY)));
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case EM_SETPASSWORDCHAR:
					edit->_passwordChar = static_cast<char16_t>(wParam);
					invalidateLayout(*edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case EM_GETPASSWORDCHAR:
					return edit->_passwordChar;
				case EM_GETLINECOUNT:
					layout(hwnd, *edit);
					return static_cast<LRESULT>(edit->_lines.size());
				case EM_LINEINDEX:
				{
					layout(hwnd, *edit);
					const long line = static_cast<long>(static_cast<int>(wParam));
					const size_t index = line < 0 ? lineOf(hwnd, *edit, edit->_caret) : static_cast<size_t>(line);
					return index < edit->_lines.size() ? static_cast<LRESULT>(edit->_lines[index]._start) : -1;
				}
				case EM_LINELENGTH:
				{
					layout(hwnd, *edit);
					const long position = static_cast<long>(static_cast<int>(wParam));
					if (position < 0)
						return static_cast<LRESULT>(edit->_lines[lineOf(hwnd, *edit, edit->_caret)]._length);
					return static_cast<LRESULT>(edit->_lines[lineOf(hwnd, *edit, static_cast<size_t>(position))]._length);
				}
				case EM_LINEFROMCHAR:
				{
					const long position = static_cast<long>(static_cast<int>(wParam));
					return static_cast<LRESULT>(lineOf(hwnd, *edit, position < 0 ? edit->_caret : static_cast<size_t>(position)));
				}
				case EM_GETLINE:
				{
					layout(hwnd, *edit);
					if (wParam >= edit->_lines.size() || !lParam)
						return 0;
					// The buffer's first WORD holds its size; the line is not terminated.
					const WORD size = *reinterpret_cast<const WORD*>(lParam);
					const Line& line = edit->_lines[wParam];
					const size_t count = std::min<size_t>(line._length, size);
					std::copy_n(edit->_text.c_str() + line._start, count, reinterpret_cast<char16_t*>(lParam));
					return static_cast<LRESULT>(count);
				}
				case EM_GETFIRSTVISIBLELINE:
					return isMultiline(hwnd) ? static_cast<LRESULT>(edit->_firstLine) : 0;
				case EM_LINESCROLL:
				{
					if (!isMultiline(hwnd))
						return FALSE;
					layout(hwnd, *edit);
					const long first = static_cast<long>(edit->_firstLine) + static_cast<long>(lParam);
					edit->_firstLine = static_cast<size_t>(std::clamp<long>(first, 0, std::max<long>(0, static_cast<long>(edit->_lines.size()) - 1)));
					if (!wraps(hwnd, *edit))
						edit->_scrollX = std::max(0, edit->_scrollX + static_cast<int>(wParam) * fontHeight(hwnd) / 2);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case EM_SCROLL:
				{
					EditDC dc{hwnd};
					const long page = std::max(1, visibleLines(hwnd, *edit, dc.get()) - 1);
					const long lines = wParam == SB_LINEUP ? -1 : (wParam == SB_LINEDOWN ? 1 : (wParam == SB_PAGEUP ? -page : (wParam == SB_PAGEDOWN ? page : 0)));
					SendMessageW(hwnd, EM_LINESCROLL, 0, lines);
					return MAKELRESULT(static_cast<WORD>(std::abs(lines)), TRUE);
				}
				case EM_SCROLLCARET:
					ensureCaretVisible(hwnd, *edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case EM_POSFROMCHAR:
				{
					layout(hwnd, *edit);
					EditDC dc{hwnd};
					const size_t position = std::min<size_t>(wParam, edit->_text.size());
					const size_t line = lineOf(hwnd, *edit, position);
					const RECT area = textArea(hwnd, *edit);
					const int x = area.left - edit->_scrollX + alignmentOffset(hwnd, *edit, dc.get(), edit->_lines[line]) + xOf(hwnd, *edit, dc.get(), position);
					return MAKELRESULT(static_cast<WORD>(x), static_cast<WORD>(lineTop(hwnd, *edit, dc.get(), line)));
				}
				case EM_CHARFROMPOS:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const size_t position = positionAt(hwnd, *edit, point);
					return MAKELRESULT(static_cast<WORD>(position), static_cast<WORD>(lineOf(hwnd, *edit, position)));
				}
				case EM_SETMARGINS:
				{
					if (wParam & EC_LEFTMARGIN)
						edit->_leftMargin = LOWORD(lParam) == EC_USEFONTINFO ? defaultMargin : LOWORD(lParam);
					if (wParam & EC_RIGHTMARGIN)
						edit->_rightMargin = HIWORD(lParam) == EC_USEFONTINFO ? defaultMargin : HIWORD(lParam);
					invalidateLayout(*edit);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case EM_GETMARGINS:
					return MAKELRESULT(edit->_leftMargin, edit->_rightMargin);
				case EM_GETRECT:
				{
					if (lParam)
						*reinterpret_cast<RECT*>(lParam) = textArea(hwnd, *edit);
					return 0;
				}
				case EM_SETRECT:
				case EM_SETRECTNP:
					return 0;
				case EM_SETCUEBANNER:
					edit->_cueBanner = lParam ? reinterpret_cast<LPCWSTR>(lParam) : u"";
					edit->_cueWhenFocused = wParam != 0;
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				case EM_GETCUEBANNER:
					copyText(edit->_cueBanner, static_cast<WPARAM>(lParam), static_cast<LPARAM>(wParam));
					return TRUE;
				case EM_SHOWBALLOONTIP:
				case EM_HIDEBALLOONTIP:
				case EM_SETTABSTOPS:
				case EM_FMTLINES:
					return TRUE;
				case EM_SETWORDBREAKPROC:
				case EM_SETHANDLE:
					return 0;
				case EM_GETHANDLE:
				case EM_GETWORDBREAKPROC:
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}

		// A rich edit is an edit in rich mode from its creation: what its creation text and style give.
		LRESULT CALLBACK richEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (message != WM_NCCREATE)
				return editProc(hwnd, message, wParam, lParam);
			const LRESULT result = editProc(hwnd, message, wParam, lParam);
			if (Edit* edit = dataOf<Edit>(hwnd))
			{
				edit->_rich = true;
				edit->_limit = 32767; // a rich edit's, until EM_EXLIMITTEXT
				edit->_text = richLineBreaks(edit->_text);
				edit->_formatOf.assign(edit->_text.size(), 0);
				edit->_noWrap = (reinterpret_cast<const CREATESTRUCTW*>(lParam)->style & WS_HSCROLL) != 0;
			}
			return result;
		}
	}

	void registerEdit()
	{
		registerControl(WC_EDITW, editProc, CS_DBLCLKS, IDC_IBEAM);
		user::setClassEditsText(WC_EDITW);
		// Rich edits: riched20.dll's and msftedit.dll's classes (RICHEDIT_CLASS, MSFTEDIT_CLASS).
		for (const char16_t* name : {u"RichEdit20W", u"RichEdit20A", u"RichEdit50W"})
		{
			registerControl(name, richEditProc, CS_DBLCLKS, IDC_IBEAM);
			user::setClassEditsText(name);
		}
	}
}
