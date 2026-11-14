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

// Kernel objects behind Win32 HANDLEs: files, find handles, events, mutexes, semaphores, threads,
// processes, file mappings. A HANDLE is an index into one process-wide table, so that a stale or
// foreign handle is detected instead of dereferenced.

#pragma once

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace nppmac::kernel
{
	class KernelObject
	{
	public:
		virtual ~KernelObject() = default;

		// Waitable objects override these; the others are never signaled.
		virtual bool isSignaled() const {
			return false;
		}

		// Called with the wait lock held when a wait on this object succeeds (auto-reset events,
		// mutex ownership, semaphore counts).
		virtual void acquire() {}
	};

	// Inserts an object and returns its new handle.
	HANDLE createHandle(std::shared_ptr<KernelObject> object);

	// The object behind a handle, or nullptr (and ERROR_INVALID_HANDLE) if there is none or it has
	// another type.
	std::shared_ptr<KernelObject> lookupHandle(HANDLE handle);

	template <typename T>
	std::shared_ptr<T> lookup(HANDLE handle)
	{
		std::shared_ptr<T> object = std::dynamic_pointer_cast<T>(lookupHandle(handle));
		if (!object)
			SetLastError(ERROR_INVALID_HANDLE);
		return object;
	}

	bool closeHandle(HANDLE handle);
	HANDLE duplicateHandle(HANDLE handle);

	// All waitable state changes and waits go through one lock and one condition variable: simple,
	// and Notepad++ waits rarely.
	std::mutex& waitMutex();
	void notifyWaiters();

	// Waits until one (waitAll == false) or all of the objects are signaled, or the timeout expires.
	// Returns WAIT_OBJECT_0 + index, WAIT_TIMEOUT or WAIT_FAILED. extraCondition, if set, is checked
	// under the wait lock too and ends the wait with WAIT_OBJECT_0 + count (used for message waits).
	// An alertable wait runs the calling thread's queued APCs and then returns WAIT_IO_COMPLETION.
	DWORD waitForObjects(DWORD count, const HANDLE* handles, bool waitAll, DWORD milliseconds,
		bool (*extraCondition)() = nullptr, bool alertable = false);

	// Asynchronous procedure calls. The calling thread's kernel object (made on first use for threads
	// the runtime did not create) is what an APC is queued to; it runs when that thread waits
	// alertably. hasPendingApcs is called with waitMutex() held.
	std::shared_ptr<KernelObject> currentThreadObject();
	bool queueApc(const std::shared_ptr<KernelObject>& thread, std::function<void()> call);
	bool hasPendingApcs();
	void runPendingApcs();
}
