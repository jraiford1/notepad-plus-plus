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

// ATL's string conversion macros (USES_CONVERSION, W2A, A2W, T2A...), which plugins use without the
// rest of ATL. ATL converts into the stack of the function using them; here each thread keeps a ring
// of buffers, so a result lasts its statement and some after, as code expects. Text is UTF-8.

#pragma once

#include <array>
#include <string>

namespace nppmac::atl
{
	inline const char* toNarrow(const char16_t* text)
	{
		thread_local std::array<std::string, 16> buffers;
		thread_local size_t next = 0;
		std::string& buffer = buffers[next++ % buffers.size()];
		buffer = text ? nppmac::toUtf8(text) : std::string{};
		return buffer.c_str();
	}

	inline const char16_t* toWide(const char* text)
	{
		thread_local std::array<std::u16string, 16> buffers;
		thread_local size_t next = 0;
		std::u16string& buffer = buffers[next++ % buffers.size()];
		buffer = text ? nppmac::toUtf16(text) : std::u16string{};
		return buffer.c_str();
	}

	inline const char16_t* same(const char16_t* text)
	{
		return text;
	}
}

#define USES_CONVERSION ((void)0)
#define W2A(text) nppmac::atl::toNarrow(text)
#define W2CA(text) nppmac::atl::toNarrow(text)
#define A2W(text) nppmac::atl::toWide(text)
#define A2CW(text) nppmac::atl::toWide(text)
#define T2A(text) nppmac::atl::toNarrow(text)
#define T2CA(text) nppmac::atl::toNarrow(text)
#define A2T(text) nppmac::atl::toWide(text)
#define A2CT(text) nppmac::atl::toWide(text)
#define W2T(text) nppmac::atl::same(text)
#define T2W(text) nppmac::atl::same(text)
#define OLE2A(text) nppmac::atl::toNarrow(text)
#define A2OLE(text) nppmac::atl::toWide(text)
#define CW2A(text) nppmac::atl::toNarrow(text)
#define CA2W(text) nppmac::atl::toWide(text)
#define CT2A(text) nppmac::atl::toNarrow(text)
#define CA2T(text) nppmac::atl::toWide(text)
