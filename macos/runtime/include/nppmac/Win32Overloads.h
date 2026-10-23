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

// Windows is LLP64 and macOS is LP64: on Windows DWORD is unsigned long and LONG is long, on macOS
// (as in Wine's headers) both are 32-bit int types. Upstream code that spells out "unsigned long"
// or "long" where a Win32 function takes DWORD or LONG compiles on Windows only because the types
// are identical there. These overloads accept those spellings.

#pragma once

#ifdef __cplusplus

#include <type_traits>

inline BOOL GetExitCodeProcess(HANDLE process, unsigned long* exitCode)
{
	DWORD code = 0;
	const BOOL result = GetExitCodeProcess(process, &code);
	*exitCode = code;
	return result;
}

// Interlocked operations on the other 32-bit spellings (the Windows SDK's winnt.h declares the
// unsigned ones as C++ overloads) and on long, which is 64-bit here: those use 64-bit atomics.
// InterlockedIncrement and friends are macros for the underscore names in Wine's winnt.h.
template <typename T>
concept InterlockedOperand = std::is_same_v<T, unsigned> || std::is_same_v<T, long> || std::is_same_v<T, unsigned long>;

template <InterlockedOperand T>
inline T _InterlockedIncrement(T volatile* addend)
{
	return __atomic_add_fetch(addend, 1, __ATOMIC_SEQ_CST);
}

template <InterlockedOperand T>
inline T _InterlockedDecrement(T volatile* addend)
{
	return __atomic_sub_fetch(addend, 1, __ATOMIC_SEQ_CST);
}

template <InterlockedOperand T>
inline T _InterlockedExchange(T volatile* target, T value)
{
	return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

#endif
