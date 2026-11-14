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

// Visual C++'s intrin.h: Apple's only forwards to it.

#pragma once

#include <windows.h>

#ifdef __cplusplus

// The few intrinsics plugins use that Wine's winnt.h does not define (it has _BitScanForward, the
// Interlocked functions and others), on the compiler's built-ins.
inline unsigned short _byteswap_ushort(unsigned short value)
{
	return __builtin_bswap16(value);
}

inline unsigned long _byteswap_ulong(unsigned long value)
{
	return __builtin_bswap32(static_cast<unsigned int>(value));
}

inline unsigned long long _byteswap_uint64(unsigned long long value)
{
	return __builtin_bswap64(value);
}

inline unsigned int __popcnt(unsigned int value)
{
	return static_cast<unsigned int>(__builtin_popcount(value));
}

inline unsigned long long __popcnt64(unsigned long long value)
{
	return static_cast<unsigned long long>(__builtin_popcountll(value));
}

inline unsigned int _rotl(unsigned int value, int shift)
{
	return __builtin_rotateleft32(value, static_cast<unsigned int>(shift));
}

inline unsigned int _rotr(unsigned int value, int shift)
{
	return __builtin_rotateright32(value, static_cast<unsigned int>(shift));
}

#endif // __cplusplus
