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

#include "KeyCodes.h"

#include <windows.h>

#include <utility>

namespace nppmac::ui
{
	// macOS virtual key code -> Windows virtual key, for keys that do not depend on the layout.
	unsigned char virtualKeyForMacKey(unsigned short keyCode, std::u16string_view charactersIgnoringModifiers)
	{
		switch (keyCode)
		{
			case 0x24: return VK_RETURN;
			case 0x4C: return VK_RETURN;   // keypad Enter
			case 0x30: return VK_TAB;
			case 0x31: return VK_SPACE;
			case 0x33: return VK_BACK;
			case 0x35: return VK_ESCAPE;
			case 0x75: return VK_DELETE;
			case 0x73: return VK_HOME;
			case 0x77: return VK_END;
			case 0x74: return VK_PRIOR;
			case 0x79: return VK_NEXT;
			case 0x7B: return VK_LEFT;
			case 0x7C: return VK_RIGHT;
			case 0x7D: return VK_DOWN;
			case 0x7E: return VK_UP;
			case 0x72: return VK_INSERT;   // Help key on old keyboards
			case 0x7A: return VK_F1;
			case 0x78: return VK_F2;
			case 0x63: return VK_F3;
			case 0x76: return VK_F4;
			case 0x60: return VK_F5;
			case 0x61: return VK_F6;
			case 0x62: return VK_F7;
			case 0x64: return VK_F8;
			case 0x65: return VK_F9;
			case 0x6D: return VK_F10;
			case 0x67: return VK_F11;
			case 0x6F: return VK_F12;
			case 0x69: return VK_F13;
			case 0x6B: return VK_F14;
			case 0x71: return VK_F15;
			case 0x6A: return VK_F16;
			case 0x40: return VK_F17;
			case 0x4F: return VK_F18;
			case 0x50: return VK_F19;
			case 0x5A: return VK_F20;
			case 0x52: return VK_NUMPAD0;
			case 0x53: return VK_NUMPAD1;
			case 0x54: return VK_NUMPAD2;
			case 0x55: return VK_NUMPAD3;
			case 0x56: return VK_NUMPAD4;
			case 0x57: return VK_NUMPAD5;
			case 0x58: return VK_NUMPAD6;
			case 0x59: return VK_NUMPAD7;
			case 0x5B: return VK_NUMPAD8;
			case 0x5C: return VK_NUMPAD9;
			case 0x41: return VK_DECIMAL;
			case 0x43: return VK_MULTIPLY;
			case 0x45: return VK_ADD;
			case 0x4E: return VK_SUBTRACT;
			case 0x4B: return VK_DIVIDE;
			case 0x47: return VK_CLEAR;
			case 0x38: return VK_SHIFT;
			case 0x3C: return VK_SHIFT;
			case 0x3B: return VK_CONTROL;
			case 0x3E: return VK_CONTROL;
			case 0x37: return VK_CONTROL;  // Command acts as Ctrl
			case 0x36: return VK_CONTROL;
			case 0x3A: return VK_MENU;     // Option acts as Alt
			case 0x3D: return VK_MENU;
			case 0x39: return VK_CAPITAL;
			default:
				break;
		}

		// Letters and digits follow the layout, as Windows virtual keys do.
		if (!charactersIgnoringModifiers.empty())
		{
			const char16_t ch = charactersIgnoringModifiers[0];
			if (ch >= u'a' && ch <= u'z')
				return static_cast<BYTE>('A' + (ch - u'a'));
			if (ch >= u'A' && ch <= u'Z')
				return static_cast<BYTE>(ch);
			if (ch >= u'0' && ch <= u'9')
				return static_cast<BYTE>(ch);
		}

		// Punctuation, by position (US layout), as the OEM keys are.
		switch (keyCode)
		{
			case 0x29: return VK_OEM_1;      // ;
			case 0x18: return VK_OEM_PLUS;   // =
			case 0x2B: return VK_OEM_COMMA;  // ,
			case 0x1B: return VK_OEM_MINUS;  // -
			case 0x2F: return VK_OEM_PERIOD; // .
			case 0x2C: return VK_OEM_2;      // /
			case 0x32: return VK_OEM_3;      // `
			case 0x21: return VK_OEM_4;      // [
			case 0x2A: return VK_OEM_5;      // backslash
			case 0x1E: return VK_OEM_6;      // ]
			case 0x27: return VK_OEM_7;      // '
			case 0x0A: return VK_OEM_102;    // section key on ISO keyboards
			default:
				break;
		}
		// Letters and digits by position, when the layout produced something else (Option held).
		static const std::pair<unsigned short, char> positions[] = {
			{0x00, 'A'}, {0x0B, 'B'}, {0x08, 'C'}, {0x02, 'D'}, {0x0E, 'E'}, {0x03, 'F'}, {0x05, 'G'}, {0x04, 'H'},
			{0x22, 'I'}, {0x26, 'J'}, {0x28, 'K'}, {0x25, 'L'}, {0x2E, 'M'}, {0x2D, 'N'}, {0x1F, 'O'}, {0x23, 'P'},
			{0x0C, 'Q'}, {0x0F, 'R'}, {0x01, 'S'}, {0x11, 'T'}, {0x20, 'U'}, {0x09, 'V'}, {0x0D, 'W'}, {0x07, 'X'},
			{0x10, 'Y'}, {0x06, 'Z'}, {0x1D, '0'}, {0x12, '1'}, {0x13, '2'}, {0x14, '3'}, {0x15, '4'}, {0x17, '5'},
			{0x16, '6'}, {0x1A, '7'}, {0x1C, '8'}, {0x19, '9'},
		};
		for (const auto& [code, key] : positions)
		{
			if (code == keyCode)
				return static_cast<BYTE>(key);
		}
		return 0;
	}

	int macKeyForVirtualKey(unsigned char virtualKey)
	{
		for (unsigned short code = 0; code < 0x80; ++code)
		{
			if (virtualKeyForMacKey(code, {}) == virtualKey)
				return code;
		}
		return -1;
	}
}
