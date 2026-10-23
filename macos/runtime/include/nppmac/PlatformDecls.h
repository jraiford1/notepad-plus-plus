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

// Declarations that upstream gets from the Windows-only branches of third-party headers.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// scintilla/include/Scintilla.h declares these only when _WIN32 is defined. The runtime registers
// the "Scintilla" window class over ScintillaView (scintilla/cocoa).
int Scintilla_RegisterClasses(void* hInstance);
int Scintilla_ReleaseResources(void);

#ifdef __cplusplus
}
#endif
