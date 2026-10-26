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

// kernel32 and ole32 memory: Global*, Local*, Heap*, Virtual*, CoTaskMem*.

#include "Immortal.h"
#include <windows.h>

#include <cstdlib>
#include <malloc/malloc.h>
#include <cstring>
#include <mutex>
#include <sys/mman.h>
#include <unordered_map>

namespace
{
	// GMEM_MOVEABLE blocks (clipboard data, DROPFILES) are handles that GlobalLock turns into
	// pointers; GMEM_FIXED blocks are their own pointers. Both kinds are recorded with their size.
	struct Block
	{
		void* _data = nullptr;
		size_t _size = 0;
		bool _moveable = false;
		unsigned _lockCount = 0;
	};

	nppmac::Immortal<std::mutex> g_blocksMutexStorage;

	std::mutex& g_blocksMutex = g_blocksMutexStorage._value;

	// Created on first use: memory is allocated by upstream's static initializers too.
	std::unordered_map<const void*, Block*>& handles() // handle -> block, for moveable blocks
	{
		static auto* map = new std::unordered_map<const void*, Block*>;
		return *map;
	}

	std::unordered_map<const void*, SIZE_T>& virtualSizes() // VirtualAlloc block -> its size
	{
		static auto* map = new std::unordered_map<const void*, SIZE_T>;
		return *map;
	}

	std::unordered_map<const void*, Block*>& pointers() // data pointer -> block, for both kinds
	{
		static auto* map = new std::unordered_map<const void*, Block*>;
		return *map;
	}

	HGLOBAL allocate(UINT flags, SIZE_T bytes)
	{
		auto* block = new Block;
		block->_size = bytes;
		block->_moveable = (flags & GMEM_MOVEABLE) != 0;
		block->_data = (flags & GMEM_ZEROINIT) ? std::calloc(1, bytes ? bytes : 1) : std::malloc(bytes ? bytes : 1);
		if (!block->_data)
		{
			delete block;
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return nullptr;
		}
		std::lock_guard lock{g_blocksMutex};
		pointers()[block->_data] = block;
		if (block->_moveable)
		{
			handles()[block] = block;
			return static_cast<HGLOBAL>(block);
		}
		return static_cast<HGLOBAL>(block->_data);
	}

	Block* find(const void* handleOrPointer)
	{
		std::lock_guard lock{g_blocksMutex};
		if (auto it = handles().find(handleOrPointer); it != handles().end())
			return it->second;
		if (auto it = pointers().find(handleOrPointer); it != pointers().end())
			return it->second;
		return nullptr;
	}

	HGLOBAL release(HGLOBAL handle)
	{
		if (!handle)
			return nullptr;
		Block* block = find(handle);
		if (!block)
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return handle;
		}
		{
			std::lock_guard lock{g_blocksMutex};
			pointers().erase(block->_data);
			handles().erase(block);
		}
		std::free(block->_data);
		delete block;
		return nullptr;
	}
}

HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T bytes)
{
	return allocate(flags, bytes);
}

HGLOBAL WINAPI GlobalReAlloc(HGLOBAL handle, SIZE_T bytes, UINT flags)
{
	Block* block = find(handle);
	if (!block)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return nullptr;
	}
	void* data = std::realloc(block->_data, bytes ? bytes : 1);
	if (!data)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return nullptr;
	}
	if ((flags & GMEM_ZEROINIT) && bytes > block->_size)
		std::memset(static_cast<char*>(data) + block->_size, 0, bytes - block->_size);
	std::lock_guard lock{g_blocksMutex};
	pointers().erase(block->_data);
	block->_data = data;
	block->_size = bytes;
	pointers()[data] = block;
	return block->_moveable ? static_cast<HGLOBAL>(block) : static_cast<HGLOBAL>(data);
}

LPVOID WINAPI GlobalLock(HGLOBAL handle)
{
	Block* block = find(handle);
	if (!block)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return nullptr;
	}
	++block->_lockCount;
	return block->_data;
}

BOOL WINAPI GlobalUnlock(HGLOBAL handle)
{
	Block* block = find(handle);
	if (!block || block->_lockCount == 0)
	{
		SetLastError(ERROR_NOT_LOCKED);
		return FALSE;
	}
	SetLastError(ERROR_SUCCESS);
	return --block->_lockCount != 0;
}

SIZE_T WINAPI GlobalSize(HGLOBAL handle)
{
	Block* block = find(handle);
	return block ? block->_size : 0;
}

HGLOBAL WINAPI GlobalFree(HGLOBAL handle)
{
	return release(handle);
}

HGLOBAL WINAPI GlobalHandle(LPCVOID pointer)
{
	Block* block = find(pointer);
	if (!block)
		return nullptr;
	return block->_moveable ? static_cast<HGLOBAL>(block) : static_cast<HGLOBAL>(block->_data);
}

HLOCAL WINAPI LocalAlloc(UINT flags, SIZE_T bytes)
{
	return allocate(flags & (LMEM_MOVEABLE | LMEM_ZEROINIT), bytes);
}

HLOCAL WINAPI LocalFree(HLOCAL handle)
{
	return release(handle);
}

LPVOID WINAPI LocalLock(HLOCAL handle)
{
	return GlobalLock(handle);
}

BOOL WINAPI LocalUnlock(HLOCAL handle)
{
	return GlobalUnlock(handle);
}

SIZE_T WINAPI LocalSize(HLOCAL handle)
{
	return GlobalSize(handle);
}

HANDLE WINAPI GetProcessHeap()
{
	static int processHeap = 0;
	return &processHeap;
}

LPVOID WINAPI HeapAlloc(HANDLE, DWORD flags, SIZE_T bytes)
{
	return (flags & HEAP_ZERO_MEMORY) ? std::calloc(1, bytes ? bytes : 1) : std::malloc(bytes ? bytes : 1);
}

LPVOID WINAPI HeapReAlloc(HANDLE, DWORD, LPVOID memory, SIZE_T bytes)
{
	return std::realloc(memory, bytes ? bytes : 1);
}

BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID memory)
{
	std::free(memory);
	return TRUE;
}

SIZE_T WINAPI HeapSize(HANDLE, DWORD, LPCVOID memory)
{
	return malloc_size(memory);
}

// Plugins use VirtualAlloc for plain buffers: committed, zeroed, read-write pages from the system.
LPVOID WINAPI VirtualAlloc(LPVOID address, SIZE_T bytes, DWORD allocationType, DWORD)
{
	if (address)
	{
		// Committing within a reservation: it is all committed already.
		if (allocationType & MEM_COMMIT)
			return address;
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	if (!(allocationType & (MEM_COMMIT | MEM_RESERVE)) || bytes == 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	void* memory = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (memory == MAP_FAILED)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return nullptr;
	}
	std::lock_guard lock{g_blocksMutex};
	virtualSizes()[memory] = bytes;
	return memory;
}

BOOL WINAPI VirtualFree(LPVOID address, SIZE_T, DWORD freeType)
{
	// MEM_DECOMMIT keeps the pages: they are given back when the block is released.
	if (!(freeType & MEM_RELEASE))
		return address != nullptr;
	SIZE_T bytes = 0;
	{
		std::lock_guard lock{g_blocksMutex};
		const auto found = virtualSizes().find(address);
		if (found == virtualSizes().end())
		{
			SetLastError(ERROR_INVALID_ADDRESS);
			return FALSE;
		}
		bytes = found->second;
		virtualSizes().erase(found);
	}
	return ::munmap(address, bytes) == 0;
}

BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD newProtect, PDWORD oldProtect)
{
	// Only used by upstream's dark-mode code to patch Windows' import tables, which do not exist here.
	if (oldProtect)
		*oldProtect = newProtect;
	SetLastError(ERROR_NOT_SUPPORTED);
	return FALSE;
}

LPVOID WINAPI CoTaskMemAlloc(SIZE_T bytes)
{
	return std::malloc(bytes ? bytes : 1);
}

LPVOID WINAPI CoTaskMemRealloc(LPVOID memory, SIZE_T bytes)
{
	return std::realloc(memory, bytes ? bytes : 1);
}

void WINAPI CoTaskMemFree(LPVOID memory)
{
	std::free(memory);
}
