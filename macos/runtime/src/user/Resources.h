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

// Access to the program's compiled resources, for the modules that build menus, accelerators,
// dialogs and image lists from them.

#pragma once

#include <memory>
#include <span>

#include "gdi/Gdi.h"

namespace nppmac::user
{
	struct ResourceData;

	const ResourceData* findResource(LPCWSTR name, LPCWSTR type);
	std::span<const BYTE> resourceBytes(const ResourceData* resource);
	std::span<const BYTE> resourceBytes(LPCWSTR name, LPCWSTR type);

	// Decodes an icon or cursor image (a DIB with an AND mask, or a PNG) into premultiplied BGRA.
	std::shared_ptr<gdi::Bitmap> decodeIconImage(const BYTE* data, size_t size);
	HICON createIcon(std::shared_ptr<gdi::Bitmap> image, bool isIcon, POINT hotspot);
	HICON loadIconResource(LPCWSTR name, int width, int height, bool cursor);
	HBITMAP loadBitmapResource(LPCWSTR name);
}
