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

#include "Handles.h"

#include <unordered_map>
#include <vector>

namespace nppmac::kernel
{
	namespace
	{
		// Upstream's static initializers already create handles (a thread, during settings
		// loading), so the runtime's own state must not depend on initialization order: it is
		// created on first use and never destroyed.
		std::mutex g_tableMutex;
		uintptr_t g_nextHandle = 0x100;

		std::unordered_map<uintptr_t, std::shared_ptr<KernelObject>>& table()
		{
			static auto* handles = new std::unordered_map<uintptr_t, std::shared_ptr<KernelObject>>;
			return *handles;
		}

		std::condition_variable& waitCondition()
		{
			static auto* condition = new std::condition_variable;
			return *condition;
		}

		// Handles are multiples of 4, as on Windows (some code tests the low bits).
		constexpr uintptr_t handleStep = 4;

		std::mutex g_waitMutex;
	}

	HANDLE createHandle(std::shared_ptr<KernelObject> object)
	{
		std::lock_guard lock{g_tableMutex};
		const uintptr_t value = g_nextHandle;
		g_nextHandle += handleStep;
		table().emplace(value, std::move(object));
		return reinterpret_cast<HANDLE>(value);
	}

	std::shared_ptr<KernelObject> lookupHandle(HANDLE handle)
	{
		std::lock_guard lock{g_tableMutex};
		auto it = table().find(reinterpret_cast<uintptr_t>(handle));
		return it == table().end() ? nullptr : it->second;
	}

	bool closeHandle(HANDLE handle)
	{
		std::shared_ptr<KernelObject> object;
		{
			std::lock_guard lock{g_tableMutex};
			auto it = table().find(reinterpret_cast<uintptr_t>(handle));
			if (it == table().end())
				return false;
			object = std::move(it->second);
			table().erase(it);
		}
		// The object is destroyed here, outside the table lock (a file flushes, a thread detaches).
		return true;
	}

	HANDLE duplicateHandle(HANDLE handle)
	{
		std::shared_ptr<KernelObject> object = lookupHandle(handle);
		return object ? createHandle(object) : nullptr;
	}

	std::mutex& waitMutex()
	{
		return g_waitMutex;
	}

	void notifyWaiters()
	{
		waitCondition().notify_all();
	}

	DWORD waitForObjects(DWORD count, const HANDLE* handles, bool waitAll, DWORD milliseconds, bool (*extraCondition)())
	{
		std::vector<std::shared_ptr<KernelObject>> objects;
		for (DWORD i = 0; i < count; ++i)
		{
			std::shared_ptr<KernelObject> object = lookupHandle(handles[i]);
			if (!object)
			{
				SetLastError(ERROR_INVALID_HANDLE);
				return WAIT_FAILED;
			}
			objects.push_back(std::move(object));
		}

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{milliseconds};
		std::unique_lock lock{g_waitMutex};
		for (;;)
		{
			if (waitAll)
			{
				bool all = true;
				for (const auto& object : objects)
					all = all && object->isSignaled();
				if (all && count > 0)
				{
					for (const auto& object : objects)
						object->acquire();
					return WAIT_OBJECT_0;
				}
			}
			else
			{
				for (DWORD i = 0; i < count; ++i)
				{
					if (objects[i]->isSignaled())
					{
						objects[i]->acquire();
						return WAIT_OBJECT_0 + i;
					}
				}
			}

			if (extraCondition && extraCondition())
				return WAIT_OBJECT_0 + count;

			if (milliseconds == 0)
				return WAIT_TIMEOUT;
			if (milliseconds == INFINITE)
			{
				// Message waits poll their condition, since the message queue lives elsewhere.
				if (extraCondition)
					waitCondition().wait_for(lock, std::chrono::milliseconds{10});
				else
					waitCondition().wait(lock);
			}
			else
			{
				const auto now = std::chrono::steady_clock::now();
				if (now >= deadline)
					return WAIT_TIMEOUT;
				const auto step = extraCondition ? std::min<std::chrono::steady_clock::duration>(deadline - now, std::chrono::milliseconds{10}) : deadline - now;
				waitCondition().wait_for(lock, step);
			}
		}
	}
}
