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


// Included before every C++ translation unit of a plugin on Linux, ahead of the runtime's forced
// include (ForcedInclude.h, which then reads as already included): that include first, then the whole
// C++ standard library. A plugin gets windows.h's min and max macros unless it asks not to (NOMINMAX),
// as on Windows; libc++ keeps them out of its own headers, GNU's libstdc++ does not, so the library is
// read before windows.h can define them. And libc++'s headers include more of each other than
// libstdc++'s, which code written against them or Visual C++'s counts on (std::ceil without <cmath>).

#pragma once

#include "nppmac/ForcedInclude.h"
#include <bits/stdc++.h>
