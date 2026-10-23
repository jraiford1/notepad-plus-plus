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

// The AppKit side of the "Scintilla" window class: Scintilla's own Cocoa view (scintilla/cocoa).
// Plain C++ types only, so that the Win32 side can include it.

#pragma once

#include <cstdint>
#include <string>

namespace nppmac::scintilla
{
	// Scintilla Cocoa's notification codes for its callback (ScintillaView.h).
	constexpr unsigned cocoaCommand = 1001;
	constexpr unsigned cocoaNotify = 1002;

	using NotifyFunc = void (*)(intptr_t window, unsigned message, uintptr_t wParam, uintptr_t lParam);
	using ContextMenuFunc = void (*)(intptr_t window, int screenX, int screenY);

	// Creates a ScintillaView (retained; the caller takes ownership) that reports to notify.
	void* createView(intptr_t window, NotifyFunc notify, ContextMenuFunc contextMenu);
	void forgetView(void* view);

	intptr_t message(void* view, unsigned message, uintptr_t wParam, intptr_t lParam);
	void insertText(void* view, const std::u16string& text);
}
