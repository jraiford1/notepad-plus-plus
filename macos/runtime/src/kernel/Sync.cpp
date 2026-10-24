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

// kernel32: last error, synchronization objects, critical sections, waits, threads.

#include "Immortal.h"
#include "Handles.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <pthread.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

using nppmac::kernel::KernelObject;

namespace
{
	thread_local DWORD t_lastError = ERROR_SUCCESS;

	class Event : public KernelObject
	{
	public:
		Event(bool manualReset, bool signaled) : _manualReset{manualReset}, _signaled{signaled} {}

		bool isSignaled() const override {
			return _signaled;
		}

		void acquire() override
		{
			if (!_manualReset)
				_signaled = false;
		}

		void set(bool signaled)
		{
			std::lock_guard lock{nppmac::kernel::waitMutex()};
			_signaled = signaled;
			if (signaled)
				nppmac::kernel::notifyWaiters();
		}

		void pulse()
		{
			set(true);
			set(false);
		}

	private:
		bool _manualReset = false;
		bool _signaled = false;
	};

	class Mutex : public KernelObject
	{
	public:
		bool isSignaled() const override {
			return _count == 0 || _owner == pthread_self();
		}

		void acquire() override
		{
			_owner = pthread_self();
			++_count;
		}

		bool release()
		{
			std::lock_guard lock{nppmac::kernel::waitMutex()};
			if (_count == 0 || _owner != pthread_self())
				return false;
			if (--_count == 0)
			{
				_owner = nullptr;
				nppmac::kernel::notifyWaiters();
			}
			return true;
		}

	private:
		pthread_t _owner = nullptr;
		unsigned _count = 0;
	};

	class Semaphore : public KernelObject
	{
	public:
		Semaphore(LONG initial, LONG maximum) : _count{initial}, _maximum{maximum} {}

		bool isSignaled() const override {
			return _count > 0;
		}

		void acquire() override {
			--_count;
		}

		bool release(LONG releaseCount, LONG* previous)
		{
			std::lock_guard lock{nppmac::kernel::waitMutex()};
			if (releaseCount <= 0 || _count + releaseCount > _maximum)
				return false;
			if (previous)
				*previous = _count;
			_count += releaseCount;
			nppmac::kernel::notifyWaiters();
			return true;
		}

	private:
		LONG _count = 0;
		LONG _maximum = 0;
	};

	class Thread : public KernelObject
	{
	public:
		bool isSignaled() const override {
			return _finished;
		}

		void finish(DWORD exitCode)
		{
			std::lock_guard lock{nppmac::kernel::waitMutex()};
			_exitCode = exitCode;
			_finished = true;
			nppmac::kernel::notifyWaiters();
		}

		DWORD exitCode() const {
			return _finished ? _exitCode : STILL_ACTIVE;
		}

		pthread_t _thread = nullptr;
		DWORD _id = 0;
		LPTHREAD_START_ROUTINE _start = nullptr;
		void* _parameter = nullptr;
		std::mutex _resumeMutex;
		std::condition_variable _resumeCondition;
		DWORD _suspendCount = 0;
		std::deque<std::function<void()>> _apcs; // under waitMutex()

	private:
		bool _finished = false;
		DWORD _exitCode = STILL_ACTIVE;
	};

	// Named objects are per process: enough for Notepad++'s single-instance mutex, which only has
	// to be found again within the same process.
	nppmac::Immortal<std::mutex> g_namesMutexStorage;
	std::mutex& g_namesMutex = g_namesMutexStorage._value;

	std::unordered_map<std::u16string, std::weak_ptr<KernelObject>>& names()
	{
		static auto* map = new std::unordered_map<std::u16string, std::weak_ptr<KernelObject>>;
		return *map;
	}

	template <typename T, typename Factory>
	HANDLE createNamed(const char16_t* name, Factory factory)
	{
		std::shared_ptr<KernelObject> object;
		bool existed = false;
		if (name && *name)
		{
			std::lock_guard lock{g_namesMutex};
			std::shared_ptr<KernelObject> existing = names()[name].lock();
			if (existing)
			{
				if (!std::dynamic_pointer_cast<T>(existing))
				{
					SetLastError(ERROR_INVALID_HANDLE);
					return nullptr;
				}
				object = existing;
				existed = true;
			}
			else
			{
				object = factory();
				names()[name] = object;
			}
		}
		else
		{
			object = factory();
		}
		HANDLE handle = nppmac::kernel::createHandle(object);
		SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
		return handle;
	}

	template <typename T>
	HANDLE openNamed(const char16_t* name)
	{
		std::lock_guard lock{g_namesMutex};
		auto it = names().find(name ? name : u"");
		std::shared_ptr<KernelObject> object = it == names().end() ? nullptr : it->second.lock();
		if (!object || !std::dynamic_pointer_cast<T>(object))
		{
			SetLastError(ERROR_FILE_NOT_FOUND);
			return nullptr;
		}
		return nppmac::kernel::createHandle(object);
	}

	thread_local std::shared_ptr<Thread> t_currentThread;
	std::atomic<DWORD> g_nextThreadId{0x1000};
	thread_local DWORD t_threadId = 0;

	void* runThread(void* parameter)
	{
		std::shared_ptr<Thread> thread = *static_cast<std::shared_ptr<Thread>*>(parameter);
		delete static_cast<std::shared_ptr<Thread>*>(parameter);
		t_currentThread = thread;
		t_threadId = thread->_id;
		{
			std::unique_lock lock{thread->_resumeMutex};
			thread->_resumeCondition.wait(lock, [&thread] { return thread->_suspendCount == 0; });
		}
		const DWORD exitCode = thread->_start(thread->_parameter);
		thread->finish(exitCode);
		return nullptr;
	}
}

// --- Asynchronous procedure calls -----------------------------------------------------------------

namespace nppmac::kernel
{
	std::shared_ptr<KernelObject> currentThreadObject()
	{
		if (!t_currentThread)
		{
			t_currentThread = std::make_shared<Thread>();
			t_currentThread->_id = GetCurrentThreadId();
		}
		return t_currentThread;
	}

	bool queueApc(const std::shared_ptr<KernelObject>& object, std::function<void()> call)
	{
		auto thread = std::dynamic_pointer_cast<Thread>(object);
		if (!thread)
			return false;
		std::lock_guard lock{waitMutex()};
		if (thread->isSignaled())
			return false;
		thread->_apcs.push_back(std::move(call));
		notifyWaiters();
		return true;
	}

	bool hasPendingApcs()
	{
		return t_currentThread && !t_currentThread->_apcs.empty();
	}

	void runPendingApcs()
	{
		if (!t_currentThread)
			return;
		for (;;)
		{
			std::function<void()> call;
			{
				std::lock_guard lock{waitMutex()};
				if (t_currentThread->_apcs.empty())
					return;
				call = std::move(t_currentThread->_apcs.front());
				t_currentThread->_apcs.pop_front();
			}
			call();
		}
	}
}

DWORD WINAPI QueueUserAPC(PAPCFUNC function, HANDLE threadHandle, ULONG_PTR data)
{
	std::shared_ptr<KernelObject> thread = threadHandle == GetCurrentThread() ? nppmac::kernel::currentThreadObject() : nppmac::kernel::lookupHandle(threadHandle);
	if (!nppmac::kernel::queueApc(thread, [function, data] { function(data); }))
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return 0;
	}
	return 1;
}

// --- Last error -----------------------------------------------------------------------------------

DWORD WINAPI GetLastError()
{
	return t_lastError;
}

void WINAPI SetLastError(DWORD errorCode)
{
	t_lastError = errorCode;
}

// --- Handles and waits ----------------------------------------------------------------------------

BOOL WINAPI CloseHandle(HANDLE handle)
{
	if (nppmac::kernel::closeHandle(handle))
		return TRUE;
	SetLastError(ERROR_INVALID_HANDLE);
	return FALSE;
}

BOOL WINAPI DuplicateHandle(HANDLE, HANDLE sourceHandle, HANDLE, LPHANDLE targetHandle, DWORD, BOOL, DWORD options)
{
	HANDLE duplicate = nullptr;
	if (sourceHandle == GetCurrentThread())
	{
		// A real handle for the calling thread (one not created by CreateThread never finishes).
		duplicate = nppmac::kernel::createHandle(nppmac::kernel::currentThreadObject());
	}
	else
	{
		duplicate = nppmac::kernel::duplicateHandle(sourceHandle);
	}
	if (!duplicate)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	if (options & DUPLICATE_CLOSE_SOURCE)
		CloseHandle(sourceHandle);
	*targetHandle = duplicate;
	return TRUE;
}

DWORD WINAPI WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
	return nppmac::kernel::waitForObjects(1, &handle, false, milliseconds);
}

DWORD WINAPI WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable)
{
	return nppmac::kernel::waitForObjects(1, &handle, false, milliseconds, nullptr, alertable != FALSE);
}

DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds)
{
	return nppmac::kernel::waitForObjects(count, handles, waitAll != FALSE, milliseconds);
}

DWORD WINAPI WaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds, BOOL alertable)
{
	return nppmac::kernel::waitForObjects(count, handles, waitAll != FALSE, milliseconds, nullptr, alertable != FALSE);
}

// --- Events, mutexes, semaphores ------------------------------------------------------------------

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manualReset, BOOL initialState, LPCWSTR name)
{
	return createNamed<Event>(name, [&] { return std::make_shared<Event>(manualReset != FALSE, initialState != FALSE); });
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset, BOOL initialState, LPCSTR name)
{
	const std::u16string wideName = name ? nppmac::toUtf16(name) : std::u16string{};
	return CreateEventW(attributes, manualReset, initialState, name ? wideName.c_str() : nullptr);
}

HANDLE WINAPI OpenEventW(DWORD, BOOL, LPCWSTR name)
{
	return openNamed<Event>(name);
}

BOOL WINAPI SetEvent(HANDLE handle)
{
	auto event = nppmac::kernel::lookup<Event>(handle);
	if (!event)
		return FALSE;
	event->set(true);
	return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE handle)
{
	auto event = nppmac::kernel::lookup<Event>(handle);
	if (!event)
		return FALSE;
	event->set(false);
	return TRUE;
}

BOOL WINAPI PulseEvent(HANDLE handle)
{
	auto event = nppmac::kernel::lookup<Event>(handle);
	if (!event)
		return FALSE;
	event->pulse();
	return TRUE;
}

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL initialOwner, LPCWSTR name)
{
	HANDLE handle = createNamed<Mutex>(name, [] { return std::make_shared<Mutex>(); });
	const DWORD error = GetLastError();
	if (handle && initialOwner && error != ERROR_ALREADY_EXISTS)
		WaitForSingleObject(handle, 0);
	SetLastError(error);
	return handle;
}

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name)
{
	const std::u16string wideName = name ? nppmac::toUtf16(name) : std::u16string{};
	return CreateMutexW(attributes, initialOwner, name ? wideName.c_str() : nullptr);
}

HANDLE WINAPI OpenMutexW(DWORD, BOOL, LPCWSTR name)
{
	return openNamed<Mutex>(name);
}

BOOL WINAPI ReleaseMutex(HANDLE handle)
{
	auto mutex = nppmac::kernel::lookup<Mutex>(handle);
	if (!mutex)
		return FALSE;
	if (!mutex->release())
	{
		SetLastError(ERROR_NOT_OWNER);
		return FALSE;
	}
	return TRUE;
}

HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES, LONG initialCount, LONG maximumCount, LPCWSTR name)
{
	return createNamed<Semaphore>(name, [&] { return std::make_shared<Semaphore>(initialCount, maximumCount); });
}

BOOL WINAPI ReleaseSemaphore(HANDLE handle, LONG releaseCount, LPLONG previousCount)
{
	auto semaphore = nppmac::kernel::lookup<Semaphore>(handle);
	if (!semaphore)
		return FALSE;
	if (!semaphore->release(releaseCount, previousCount))
	{
		SetLastError(ERROR_TOO_MANY_POSTS);
		return FALSE;
	}
	return TRUE;
}

// --- Critical sections ----------------------------------------------------------------------------
// The CRITICAL_SECTION's LockSemaphore field holds the recursive mutex that implements it.

void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION section)
{
	std::memset(section, 0, sizeof(*section));
	section->LockSemaphore = reinterpret_cast<HANDLE>(new std::recursive_mutex);
}

BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION section, DWORD)
{
	InitializeCriticalSection(section);
	return TRUE;
}

BOOL WINAPI InitializeCriticalSectionEx(LPCRITICAL_SECTION section, DWORD, DWORD)
{
	InitializeCriticalSection(section);
	return TRUE;
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION section)
{
	reinterpret_cast<std::recursive_mutex*>(section->LockSemaphore)->lock();
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION section)
{
	return reinterpret_cast<std::recursive_mutex*>(section->LockSemaphore)->try_lock() ? TRUE : FALSE;
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION section)
{
	reinterpret_cast<std::recursive_mutex*>(section->LockSemaphore)->unlock();
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION section)
{
	delete reinterpret_cast<std::recursive_mutex*>(section->LockSemaphore);
	section->LockSemaphore = nullptr;
}

// --- Threads and processes ------------------------------------------------------------------------

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags, LPDWORD threadId)
{
	auto thread = std::make_shared<Thread>();
	thread->_id = g_nextThreadId++;
	thread->_start = start;
	thread->_parameter = parameter;
	thread->_suspendCount = (flags & CREATE_SUSPENDED) ? 1 : 0;

	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (stackSize != 0)
		pthread_attr_setstacksize(&attributes, std::max<size_t>(stackSize, PTHREAD_STACK_MIN));
	auto* parameterCopy = new std::shared_ptr<Thread>{thread};
	const int result = pthread_create(&thread->_thread, &attributes, runThread, parameterCopy);
	pthread_attr_destroy(&attributes);
	if (result != 0)
	{
		delete parameterCopy;
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return nullptr;
	}
	if (threadId)
		*threadId = thread->_id;
	return nppmac::kernel::createHandle(thread);
}

void WINAPI ExitThread(DWORD exitCode)
{
	if (t_currentThread)
		t_currentThread->finish(exitCode);
	pthread_exit(nullptr);
}

BOOL WINAPI TerminateThread(HANDLE handle, DWORD exitCode)
{
	// Threads cannot be killed safely; the handle is marked finished so waiters are released.
	auto thread = nppmac::kernel::lookup<Thread>(handle);
	if (!thread)
		return FALSE;
	thread->finish(exitCode);
	return TRUE;
}

BOOL WINAPI GetExitCodeThread(HANDLE handle, LPDWORD exitCode)
{
	auto thread = nppmac::kernel::lookup<Thread>(handle);
	if (!thread)
		return FALSE;
	std::lock_guard lock{nppmac::kernel::waitMutex()};
	*exitCode = thread->exitCode();
	return TRUE;
}

DWORD WINAPI ResumeThread(HANDLE handle)
{
	auto thread = nppmac::kernel::lookup<Thread>(handle);
	if (!thread)
		return static_cast<DWORD>(-1);
	std::lock_guard lock{thread->_resumeMutex};
	const DWORD previous = thread->_suspendCount;
	if (thread->_suspendCount > 0 && --thread->_suspendCount == 0)
		thread->_resumeCondition.notify_all();
	return previous;
}

DWORD WINAPI SuspendThread(HANDLE)
{
	// Suspending a running thread is not supported (Notepad++ only creates threads suspended).
	return 0;
}

BOOL WINAPI SetThreadPriority(HANDLE, int)
{
	return TRUE;
}

DWORD WINAPI GetCurrentThreadId()
{
	if (t_threadId == 0)
		t_threadId = g_nextThreadId++;
	return t_threadId;
}

HANDLE WINAPI GetCurrentThread()
{
	return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}

DWORD WINAPI GetCurrentProcessId()
{
	return static_cast<DWORD>(getpid());
}

HANDLE WINAPI GetCurrentProcess()
{
	return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}

void WINAPI Sleep(DWORD milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds{milliseconds});
}

DWORD WINAPI SleepEx(DWORD milliseconds, BOOL alertable)
{
	if (!alertable)
	{
		Sleep(milliseconds);
		return 0;
	}
	// A wait on nothing ends with the timeout, or early when an APC arrives.
	const DWORD result = nppmac::kernel::waitForObjects(0, nullptr, false, milliseconds, nullptr, true);
	return result == WAIT_IO_COMPLETION ? WAIT_IO_COMPLETION : 0;
}

void WINAPI ExitProcess(UINT exitCode)
{
	std::exit(static_cast<int>(exitCode));
}

BOOL WINAPI TerminateProcess(HANDLE process, UINT exitCode)
{
	if (process == GetCurrentProcess())
		std::_Exit(static_cast<int>(exitCode));
	SetLastError(ERROR_ACCESS_DENIED);
	return FALSE;
}
