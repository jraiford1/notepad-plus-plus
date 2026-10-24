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

// kernel32: version and system information, time, locale, environment, modules, messages,
// processes; advapi32 security queries. The runtime presents itself as Windows 10 (build 19045).

#include "Handles.h"
#include "Internal.h"
#include "Modules.h"
#include "nppmac/Startup.h"
#include "platform/Platform.h"
#include "user/DarkMode.h"

#include <dbghelp.h>
#include <sensapi.h>
#include <tlhelp32.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <unordered_map>
#include <libkern/OSByteOrder.h>
#include <libproc.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <vector>

using namespace nppmac::kernel;

namespace
{
	constexpr DWORD reportedMajorVersion = 10;
	constexpr DWORD reportedMinorVersion = 0;
	constexpr DWORD reportedBuild = 19045;

	// The main executable's module handle: the address of its Mach-O header, as on Windows.
	// Windows' system DLLs are not there, but the few functions programs look up in them by name or
	// ordinal are: these modules answer GetModuleHandle, LoadLibrary and GetProcAddress for them.
	enum class SystemModule
	{
		ntdll,
		kernel32,
		user32,
		uxtheme,
		comctl32,
		count,
	};

	const char16_t* const g_systemModuleNames[] = {u"ntdll", u"kernel32", u"user32", u"uxtheme", u"comctl32"};

	// A module handle is its image's base address, and programs read the PE headers there: Notepad++'s
	// dark mode walks comctl32's delay-import table. Each stand-in is a valid, empty PE header (no data
	// directories), so such walks find nothing instead of reading stray memory.
	struct SystemModuleImage
	{
		IMAGE_DOS_HEADER _dos;
		IMAGE_NT_HEADERS64 _nt;
	};

	SystemModuleImage* systemModuleImages()
	{
		static auto* images = []
		{
			auto* created = new SystemModuleImage[static_cast<int>(SystemModule::count)]{};
			for (int i = 0; i < static_cast<int>(SystemModule::count); ++i)
			{
				created[i]._dos.e_magic = IMAGE_DOS_SIGNATURE;
				created[i]._dos.e_lfanew = offsetof(SystemModuleImage, _nt);
				created[i]._nt.Signature = IMAGE_NT_SIGNATURE;
				created[i]._nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
				created[i]._nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
			}
			return created;
		}();
		return images;
	}

	HMODULE systemModuleHandle(SystemModule module)
	{
		return reinterpret_cast<HMODULE>(&systemModuleImages()[static_cast<int>(module)]);
	}

	bool isSystemModule(HMODULE module, SystemModule* which = nullptr)
	{
		const auto* image = reinterpret_cast<const SystemModuleImage*>(module);
		const SystemModuleImage* first = systemModuleImages();
		if (image < first || image >= first + static_cast<int>(SystemModule::count))
			return false;
		if (which)
			*which = static_cast<SystemModule>(image - first);
		return true;
	}

	// "C:\Windows\System32\UxTheme.dll" and "uxtheme" name the same module.
	HMODULE findSystemModule(LPCWSTR fileName)
	{
		if (!fileName)
			return nullptr;
		std::u16string name{fileName};
		const size_t separator = name.find_last_of(u"\\/");
		if (separator != std::u16string::npos)
			name.erase(0, separator + 1);
		if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, u".dll") == 0)
			name.erase(name.size() - 4);
		for (int i = 0; i < static_cast<int>(SystemModule::count); ++i)
		{
			if (_wcsicmp(name.c_str(), g_systemModuleNames[i]) == 0)
				return systemModuleHandle(static_cast<SystemModule>(i));
		}
		return nullptr;
	}

	// ntdll: the version Notepad++ checks before using the dark mode functions (Windows 10 22H2).
	void WINAPI rtlGetNtVersionNumbers(LPDWORD major, LPDWORD minor, LPDWORD build)
	{
		constexpr DWORD windows10Build = 19045;
		if (major)
			*major = 10;
		if (minor)
			*minor = 0;
		if (build)
			*build = windows10Build | 0xF0000000;
	}

	FARPROC systemExport(SystemModule module, LPCSTR procName)
	{
		const bool byOrdinal = reinterpret_cast<uintptr_t>(procName) <= 0xFFFF;
		switch (module)
		{
			case SystemModule::ntdll:
				// Not wine_get_version: Notepad++ would take this for Wine.
				return !byOrdinal && std::strcmp(procName, "RtlGetNtVersionNumbers") == 0 ? reinterpret_cast<FARPROC>(&rtlGetNtVersionNumbers) : nullptr;
			case SystemModule::uxtheme:
				if (byOrdinal)
					return nppmac::user::darkmode::uxthemeOrdinal(static_cast<WORD>(reinterpret_cast<uintptr_t>(procName)));
				break;
			case SystemModule::user32:
				if (!byOrdinal)
				{
					if (FARPROC dark = nppmac::user::darkmode::user32Export(procName))
						return dark;
				}
				break;
			default:
				break;
		}
		// The runtime's own implementation of a function the module exports, if there is one.
		return byOrdinal ? nullptr : reinterpret_cast<FARPROC>(::dlsym(RTLD_DEFAULT, procName));
	}

	// A Mach-O image for this processor, or a universal one with a slice for it.
	bool isLoadableMachO(const void* base)
	{
#if defined(__arm64__)
		constexpr cpu_type_t hostCpu = CPU_TYPE_ARM64;
#else
		constexpr cpu_type_t hostCpu = CPU_TYPE_X86_64;
#endif
		const auto* bytes = static_cast<const uint8_t*>(base);
		uint32_t magic = 0;
		std::memcpy(&magic, bytes, sizeof(magic));
		if (magic == MH_MAGIC_64)
		{
			mach_header_64 header{};
			std::memcpy(&header, bytes, sizeof(header));
			return header.cputype == hostCpu;
		}
		if (OSSwapBigToHostInt32(magic) == FAT_MAGIC)
		{
			fat_header fat{};
			std::memcpy(&fat, bytes, sizeof(fat));
			const uint32_t count = OSSwapBigToHostInt32(fat.nfat_arch);
			for (uint32_t i = 0; i < count && i < 16; ++i)
			{
				fat_arch slice{};
				std::memcpy(&slice, bytes + sizeof(fat) + i * sizeof(slice), sizeof(slice));
				if (static_cast<cpu_type_t>(OSSwapBigToHostInt32(static_cast<uint32_t>(slice.cputype))) == hostCpu)
					return true;
			}
		}
		return false;
	}

	HMODULE mainModule()
	{
		static HMODULE module = []
		{
			Dl_info info{};
			dladdr(reinterpret_cast<const void*>(&mainModule), &info);
			return static_cast<HMODULE>(const_cast<void*>(info.dli_fbase));
		}();
		return module;
	}

	SYSTEMTIME toSystemTime(const tm& t, int milliseconds)
	{
		SYSTEMTIME st{};
		st.wYear = static_cast<WORD>(t.tm_year + 1900);
		st.wMonth = static_cast<WORD>(t.tm_mon + 1);
		st.wDayOfWeek = static_cast<WORD>(t.tm_wday);
		st.wDay = static_cast<WORD>(t.tm_mday);
		st.wHour = static_cast<WORD>(t.tm_hour);
		st.wMinute = static_cast<WORD>(t.tm_min);
		st.wSecond = static_cast<WORD>(t.tm_sec);
		st.wMilliseconds = static_cast<WORD>(milliseconds);
		return st;
	}

	tm toTm(const SYSTEMTIME& st)
	{
		tm t{};
		t.tm_year = st.wYear - 1900;
		t.tm_mon = st.wMonth - 1;
		t.tm_mday = st.wDay;
		t.tm_hour = st.wHour;
		t.tm_min = st.wMinute;
		t.tm_sec = st.wSecond;
		t.tm_wday = st.wDayOfWeek;
		t.tm_isdst = -1;
		return t;
	}

	// Environment variables Notepad++ expands that macOS does not define.
	std::string environmentValue(const std::string& name)
	{
		if (const char* value = std::getenv(name.c_str()))
			return value;
		if (strcasecmp(name.c_str(), "APPDATA") == 0 || strcasecmp(name.c_str(), "LOCALAPPDATA") == 0)
			return nppmac::platform::applicationSupportDirectory();
		if (strcasecmp(name.c_str(), "USERPROFILE") == 0 || strcasecmp(name.c_str(), "HOMEPATH") == 0)
			return nppmac::platform::homeDirectory();
		if (strcasecmp(name.c_str(), "TEMP") == 0 || strcasecmp(name.c_str(), "TMP") == 0)
		{
			const char* tmp = std::getenv("TMPDIR");
			return tmp ? tmp : "/tmp";
		}
		if (strcasecmp(name.c_str(), "USERNAME") == 0)
			return nppmac::platform::userName();
		if (strcasecmp(name.c_str(), "COMPUTERNAME") == 0)
			return nppmac::platform::computerName();
		if (strcasecmp(name.c_str(), "PROGRAMFILES") == 0)
			return "/Applications";
		return {};
	}
}

// --- Version and system ---------------------------------------------------------------------------

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW info)
{
	info->dwMajorVersion = reportedMajorVersion;
	info->dwMinorVersion = reportedMinorVersion;
	info->dwBuildNumber = reportedBuild;
	info->dwPlatformId = VER_PLATFORM_WIN32_NT;
	info->szCSDVersion[0] = u'\0';
	if (info->dwOSVersionInfoSize >= sizeof(OSVERSIONINFOEXW))
	{
		auto* extended = reinterpret_cast<LPOSVERSIONINFOEXW>(info);
		extended->wServicePackMajor = 0;
		extended->wServicePackMinor = 0;
		extended->wSuiteMask = VER_SUITE_SINGLEUSERTS;
		extended->wProductType = VER_NT_WORKSTATION;
	}
	return TRUE;
}

DWORD WINAPI GetVersion()
{
	return (reportedBuild << 16) | (reportedMinorVersion << 8) | reportedMajorVersion;
}

BOOL WINAPI GetProductInfo(DWORD, DWORD, DWORD, DWORD, PDWORD productType)
{
	*productType = PRODUCT_PROFESSIONAL;
	return TRUE;
}

void WINAPI GetSystemInfo(LPSYSTEM_INFO info)
{
	std::memset(info, 0, sizeof(*info));
#if defined(__aarch64__)
	info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
#else
	info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#endif
	info->dwPageSize = static_cast<DWORD>(getpagesize());
	info->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(0x10000);
	info->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(0x7FFFFFFEFFFF);
	info->dwActiveProcessorMask = 1;
	info->dwNumberOfProcessors = static_cast<DWORD>(sysconf(_SC_NPROCESSORS_ONLN));
	info->dwAllocationGranularity = 0x10000;
}

void WINAPI GetNativeSystemInfo(LPSYSTEM_INFO info)
{
	GetSystemInfo(info);
}

BOOL WINAPI IsWow64Process(HANDLE, PBOOL wow64)
{
	*wow64 = FALSE;
	return TRUE;
}

int WINAPI MulDiv(int number, int numerator, int denominator)
{
	if (denominator == 0)
		return -1;
	const long long product = static_cast<long long>(number) * numerator;
	// Rounded to nearest, halves away from zero, as Windows does.
	const long long half = (denominator < 0 ? -denominator : denominator) / 2;
	const long long result = ((product < 0) != (denominator < 0)) ? (product - half) / denominator : (product + half) / denominator;
	return result > INT_MAX || result < INT_MIN ? -1 : static_cast<int>(result);
}

// --- Time -----------------------------------------------------------------------------------------

ULONGLONG WINAPI GetTickCount64()
{
	return static_cast<ULONGLONG>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

DWORD WINAPI GetTickCount()
{
	return static_cast<DWORD>(GetTickCount64());
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* counter)
{
	counter->QuadPart = static_cast<LONGLONG>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
	return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
	frequency->QuadPart = 1000000000LL;
	return TRUE;
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME fileTime)
{
	*fileTime = currentFileTime();
}

void WINAPI GetSystemTime(LPSYSTEMTIME systemTime)
{
	timespec now{};
	clock_gettime(CLOCK_REALTIME, &now);
	tm t{};
	gmtime_r(&now.tv_sec, &t);
	*systemTime = toSystemTime(t, static_cast<int>(now.tv_nsec / 1000000));
}

void WINAPI GetLocalTime(LPSYSTEMTIME systemTime)
{
	timespec now{};
	clock_gettime(CLOCK_REALTIME, &now);
	tm t{};
	localtime_r(&now.tv_sec, &t);
	*systemTime = toSystemTime(t, static_cast<int>(now.tv_nsec / 1000000));
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME* fileTime, LPSYSTEMTIME systemTime)
{
	const timespec ts = toTimespec(*fileTime);
	tm t{};
	gmtime_r(&ts.tv_sec, &t);
	*systemTime = toSystemTime(t, static_cast<int>(ts.tv_nsec / 1000000));
	return TRUE;
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* systemTime, LPFILETIME fileTime)
{
	tm t = toTm(*systemTime);
	timespec ts{};
	ts.tv_sec = timegm(&t);
	ts.tv_nsec = static_cast<long>(systemTime->wMilliseconds) * 1000000;
	*fileTime = toFileTime(ts);
	return TRUE;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME* fileTime, LPFILETIME localFileTime)
{
	const timespec ts = toTimespec(*fileTime);
	tm t{};
	localtime_r(&ts.tv_sec, &t);
	timespec local = ts;
	local.tv_sec += t.tm_gmtoff;
	*localFileTime = toFileTime(local);
	return TRUE;
}

BOOL WINAPI LocalFileTimeToFileTime(const FILETIME* localFileTime, LPFILETIME fileTime)
{
	timespec ts = toTimespec(*localFileTime);
	tm t{};
	localtime_r(&ts.tv_sec, &t);
	ts.tv_sec -= t.tm_gmtoff;
	*fileTime = toFileTime(ts);
	return TRUE;
}

BOOL WINAPI SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION*, const SYSTEMTIME* universal, LPSYSTEMTIME local)
{
	FILETIME utc{};
	FILETIME localTime{};
	SystemTimeToFileTime(universal, &utc);
	FileTimeToLocalFileTime(&utc, &localTime);
	return FileTimeToSystemTime(&localTime, local);
}

LONG WINAPI CompareFileTime(const FILETIME* lhs, const FILETIME* rhs)
{
	const ULONGLONG a = (static_cast<ULONGLONG>(lhs->dwHighDateTime) << 32) | lhs->dwLowDateTime;
	const ULONGLONG b = (static_cast<ULONGLONG>(rhs->dwHighDateTime) << 32) | rhs->dwLowDateTime;
	return a < b ? -1 : (a > b ? 1 : 0);
}

DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION information)
{
	std::memset(information, 0, sizeof(*information));
	const time_t now = std::time(nullptr);
	tm t{};
	localtime_r(&now, &t);
	information->Bias = static_cast<LONG>(-t.tm_gmtoff / 60);
	return t.tm_isdst > 0 ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

namespace
{
	// Formats a date or time with a Windows picture string ("yyyy-MM-dd", "HH:mm:ss", "h:mm tt").
	std::u16string formatPicture(const SYSTEMTIME& st, std::u16string_view picture)
	{
		static const char16_t* const days[] = {u"Sunday", u"Monday", u"Tuesday", u"Wednesday", u"Thursday", u"Friday", u"Saturday"};
		static const char16_t* const months[] = {u"January", u"February", u"March", u"April", u"May", u"June", u"July", u"August", u"September", u"October", u"November", u"December"};
		std::u16string out;
		size_t i = 0;
		while (i < picture.size())
		{
			const char16_t ch = picture[i];
			if (ch == u'\'')
			{
				const size_t end = picture.find(u'\'', i + 1);
				out.append(picture.substr(i + 1, (end == std::u16string_view::npos ? picture.size() : end) - i - 1));
				i = end == std::u16string_view::npos ? picture.size() : end + 1;
				continue;
			}
			size_t run = 1;
			while (i + run < picture.size() && picture[i + run] == ch)
				++run;
			auto number = [&out](int value, size_t width)
			{
				std::u16string digits = nppmac::to_u16string(value);
				if (digits.size() < width)
					digits.insert(0, width - digits.size(), u'0');
				out += digits;
			};
			switch (ch)
			{
				case u'd':
				{
					if (run <= 2)
						number(st.wDay, run);
					else if (run == 3)
						out += std::u16string{days[st.wDayOfWeek]}.substr(0, 3);
					else
						out += days[st.wDayOfWeek];
					break;
				}
				case u'M':
				{
					if (run <= 2)
						number(st.wMonth, run);
					else if (run == 3)
						out += std::u16string{months[st.wMonth - 1]}.substr(0, 3);
					else
						out += months[st.wMonth - 1];
					break;
				}
				case u'y':
				{
					if (run <= 2)
						number(st.wYear % 100, run == 1 ? 1 : 2);
					else
						number(st.wYear, 4);
					break;
				}
				case u'h':
					number(st.wHour % 12 == 0 ? 12 : st.wHour % 12, run);
					break;
				case u'H':
					number(st.wHour, run);
					break;
				case u'm':
					number(st.wMinute, run);
					break;
				case u's':
					number(st.wSecond, run);
					break;
				case u't':
				{
					const char16_t* marker = st.wHour < 12 ? u"AM" : u"PM";
					out += run == 1 ? std::u16string(1, marker[0]) : std::u16string{marker};
					break;
				}
				default:
					out.append(run, ch);
			}
			i += run;
		}
		return out;
	}

	int writeFormatted(const std::u16string& text, LPWSTR buffer, int size)
	{
		if (size == 0)
			return static_cast<int>(text.size() + 1);
		if (text.size() + 1 > static_cast<size_t>(size))
		{
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return 0;
		}
		std::copy(text.begin(), text.end(), buffer);
		buffer[text.size()] = u'\0';
		return static_cast<int>(text.size() + 1);
	}

	SYSTEMTIME timeOrNow(const SYSTEMTIME* time)
	{
		SYSTEMTIME now{};
		if (!time)
			GetLocalTime(&now);
		return time ? *time : now;
	}
}

int WINAPI GetDateFormatW(LCID, DWORD flags, const SYSTEMTIME* date, LPCWSTR format, LPWSTR buffer, int size)
{
	const std::u16string picture = format ? format : ((flags & DATE_LONGDATE) ? u"dddd, MMMM d, yyyy" : u"yyyy-MM-dd");
	return writeFormatted(formatPicture(timeOrNow(date), picture), buffer, size);
}

int WINAPI GetDateFormatEx(LPCWSTR, DWORD flags, const SYSTEMTIME* date, LPCWSTR format, LPWSTR buffer, int size, LPCWSTR)
{
	return GetDateFormatW(0, flags, date, format, buffer, size);
}

int WINAPI GetTimeFormatW(LCID, DWORD flags, const SYSTEMTIME* time, LPCWSTR format, LPWSTR buffer, int size)
{
	const std::u16string picture = format ? format : ((flags & TIME_NOSECONDS) ? u"HH:mm" : u"HH:mm:ss");
	return writeFormatted(formatPicture(timeOrNow(time), picture), buffer, size);
}

int WINAPI GetTimeFormatEx(LPCWSTR, DWORD flags, const SYSTEMTIME* time, LPCWSTR format, LPWSTR buffer, int size)
{
	return GetTimeFormatW(0, flags, time, format, buffer, size);
}

// --- Locale ---------------------------------------------------------------------------------------

int WINAPI GetLocaleInfoEx(LPCWSTR, LCTYPE type, LPWSTR data, int size)
{
	const std::string language = nppmac::platform::preferredLanguage();
	std::u16string value;
	switch (type & ~LOCALE_NOUSEROVERRIDE & ~LOCALE_RETURN_NUMBER)
	{
		case LOCALE_SNAME:
			value = nppmac::toUtf16(language);
			break;
		case LOCALE_SISO639LANGNAME:
			value = nppmac::toUtf16(language.substr(0, language.find('-')));
			break;
		case LOCALE_SDECIMAL:
			value = u".";
			break;
		case LOCALE_STHOUSAND:
			value = u",";
			break;
		case LOCALE_SSHORTDATE:
			value = u"yyyy-MM-dd";
			break;
		case LOCALE_SLONGDATE:
			value = u"dddd, MMMM d, yyyy";
			break;
		case LOCALE_STIMEFORMAT:
			value = u"HH:mm:ss";
			break;
		case LOCALE_IDEFAULTANSICODEPAGE:
			value = nppmac::to_u16string(GetACP());
			break;
		default:
			value = u"";
	}
	return writeFormatted(value, data, size);
}

int WINAPI GetLocaleInfoW(LCID, LCTYPE type, LPWSTR data, int size)
{
	return GetLocaleInfoEx(nullptr, type, data, size);
}

LCID WINAPI GetUserDefaultLCID()
{
	return MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
}

LANGID WINAPI GetUserDefaultUILanguage()
{
	return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

LANGID WINAPI GetUserDefaultLangID()
{
	return GetUserDefaultUILanguage();
}

LANGID WINAPI GetSystemDefaultUILanguage()
{
	return GetUserDefaultUILanguage();
}

int WINAPI GetUserDefaultLocaleName(LPWSTR name, int size)
{
	return writeFormatted(nppmac::toUtf16(nppmac::platform::preferredLanguage()), name, size);
}

// --- Environment ----------------------------------------------------------------------------------

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size)
{
	const std::string value = environmentValue(nppmac::toUtf8(name));
	if (value.empty())
	{
		SetLastError(ERROR_ENVVAR_NOT_FOUND);
		return 0;
	}
	return copyToBuffer(nppmac::toUtf16(value), buffer, size);
}

BOOL WINAPI SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value)
{
	const std::string key = nppmac::toUtf8(name);
	const int result = value ? ::setenv(key.c_str(), nppmac::toUtf8(value).c_str(), 1) : ::unsetenv(key.c_str());
	return result == 0 ? TRUE : FALSE;
}

DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR source, LPWSTR dest, DWORD size)
{
	std::u16string result;
	const std::u16string_view text{source};
	size_t i = 0;
	while (i < text.size())
	{
		const size_t start = text.find(u'%', i);
		const size_t end = start == std::u16string_view::npos ? std::u16string_view::npos : text.find(u'%', start + 1);
		if (end == std::u16string_view::npos)
		{
			result.append(text.substr(i));
			break;
		}
		result.append(text.substr(i, start - i));
		const std::string value = environmentValue(nppmac::toUtf8(text.substr(start + 1, end - start - 1)));
		if (value.empty())
			result.append(text.substr(start, end - start + 1)); // unknown variables stay as they are
		else
			result.append(nppmac::toUtf16(value));
		i = end + 1;
	}
	// The returned size includes the terminator, on success too.
	const DWORD needed = static_cast<DWORD>(result.size() + 1);
	if (dest && size >= needed)
	{
		std::copy(result.begin(), result.end(), dest);
		dest[result.size()] = u'\0';
	}
	return needed;
}

BOOL WINAPI GetComputerNameW(LPWSTR buffer, LPDWORD size)
{
	const std::u16string name = nppmac::toUtf16(nppmac::platform::computerName());
	if (name.size() + 1 > *size)
	{
		*size = static_cast<DWORD>(name.size() + 1);
		SetLastError(ERROR_BUFFER_OVERFLOW);
		return FALSE;
	}
	*size = copyToBuffer(name, buffer, *size);
	return TRUE;
}

BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size)
{
	const std::u16string name = nppmac::toUtf16(nppmac::platform::userName());
	if (name.size() + 1 > *size)
	{
		*size = static_cast<DWORD>(name.size() + 1);
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}
	copyToBuffer(name, buffer, *size);
	*size = static_cast<DWORD>(name.size() + 1);
	return TRUE;
}

LPWSTR WINAPI GetCommandLineW()
{
	return const_cast<LPWSTR>(nppmac::commandLine());
}

// --- Modules --------------------------------------------------------------------------------------
// The program's own module is "notepad++.exe" in Contents/Resources: that is where Notepad++ looks
// for langs.model.xml, themes/, localization/ and the rest, as next to the .exe on Windows.

HMODULE WINAPI GetModuleHandleW(LPCWSTR moduleName)
{
	if (!moduleName)
		return mainModule();
	if (HMODULE system = findSystemModule(moduleName))
		return system;
	SetLastError(ERROR_MOD_NOT_FOUND);
	return nullptr;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR moduleName)
{
	return moduleName ? GetModuleHandleW(nppmac::toUtf16(moduleName).c_str()) : mainModule();
}

DWORD WINAPI GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size)
{
	std::string file;
	if (module && module != mainModule())
	{
		// A plugin's own file: plugins find their folder from it.
		file = nppmac::kernel::libraryPath(module);
		if (file.empty())
		{
			SetLastError(ERROR_MOD_NOT_FOUND);
			return 0;
		}
	}
	else
	{
		file = nppmac::platform::resourcesDirectory() + "/notepad++.exe";
	}
	const std::u16string path = nppmac::toUtf16(file);
	if (path.size() + 1 > size)
	{
		// Truncated and terminated, as Windows does.
		if (size > 0)
		{
			std::copy_n(path.begin(), size - 1, buffer);
			buffer[size - 1] = u'\0';
		}
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return size;
	}
	return copyToBuffer(path, buffer, size);
}

DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size)
{
	char16_t wide[MAX_PATH * 4]{};
	if (!GetModuleFileNameW(module, wide, MAX_PATH * 4))
		return 0;
	return copyToBuffer(nppmac::toUtf8(wide), buffer, size);
}

namespace
{
	// The libraries LoadLibrary loaded: their files and how many loads each still has.
	struct Library
	{
		std::string _path;
		int _loads = 0;
	};

	std::mutex& librariesMutex()
	{
		static auto* mutex = new std::mutex;
		return *mutex;
	}

	std::unordered_map<HMODULE, Library>& libraries()
	{
		static auto* map = new std::unordered_map<HMODULE, Library>;
		return *map;
	}

	using DllMainFunction = BOOL (*)(HMODULE, DWORD, LPVOID);

	// A library's DllMain, which plugins declare as a C++ function with HANDLE, HINSTANCE or HMODULE
	// (HINSTANCE and HMODULE are one type): looked up under each of those names, and as a C function.
	DllMainFunction findDllMain(HMODULE module)
	{
		for (const char* name : {"DllMain", "_Z7DllMainPvjS_", "_Z7DllMainP11HINSTANCE__jPv"})
		{
			if (void* symbol = ::dlsym(module, name))
				return reinterpret_cast<DllMainFunction>(symbol);
		}
		return nullptr;
	}
}

namespace nppmac::kernel
{
	std::string libraryPath(HMODULE module)
	{
		std::lock_guard lock{librariesMutex()};
		auto it = libraries().find(module);
		return it == libraries().end() ? std::string{} : it->second._path;
	}
}

HMODULE WINAPI LoadLibraryExW(LPCWSTR fileName, HANDLE, DWORD)
{
	// Of the Windows system DLLs, the runtime's stand-ins; anything else is a macOS library, whatever
	// its name: a plugin keeps upstream's file name, plugins/<Name>/<Name>.dll.
	if (HMODULE system = findSystemModule(fileName))
		return system;
	const std::string path = toPosixPath(fileName);
	void* library = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!library)
	{
		if (const char* error = ::dlerror())
			std::fprintf(stderr, "nppmac: LoadLibrary %s: %s\n", path.c_str(), error);
		SetLastError(ERROR_MOD_NOT_FOUND);
		return nullptr;
	}
	HMODULE module = static_cast<HMODULE>(library);
	bool first = false;
	{
		std::lock_guard lock{librariesMutex()};
		Library& entry = libraries()[module];
		first = entry._loads++ == 0;
		if (first)
			entry._path = path;
	}
	// As Windows does, the first load runs the library's DllMain, with its module handle: plugins
	// keep it for their dialogs and resources.
	if (first)
	{
		if (DllMainFunction dllMain = findDllMain(module))
			dllMain(module, DLL_PROCESS_ATTACH, nullptr);
	}
	return module;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR fileName)
{
	return LoadLibraryExW(fileName, nullptr, 0);
}

BOOL WINAPI FreeLibrary(HMODULE module)
{
	if (!module || module == mainModule() || isSystemModule(module))
		return TRUE;
	bool last = false;
	{
		std::lock_guard lock{librariesMutex()};
		auto it = libraries().find(module);
		if (it != libraries().end() && --it->second._loads == 0)
		{
			libraries().erase(it);
			last = true;
		}
	}
	if (last)
	{
		if (DllMainFunction dllMain = findDllMain(module))
			dllMain(module, DLL_PROCESS_DETACH, nullptr);
	}
	::dlclose(module);
	return TRUE;
}

FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR procName)
{
	SystemModule system{};
	if (isSystemModule(module, &system))
	{
		FARPROC found = systemExport(system, procName);
		if (!found)
			SetLastError(ERROR_PROC_NOT_FOUND);
		return found;
	}
	if (!module || reinterpret_cast<uintptr_t>(procName) <= 0xFFFF)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return nullptr;
	}
	void* symbol = ::dlsym(module == mainModule() ? RTLD_DEFAULT : module, procName);
	if (!symbol)
		SetLastError(ERROR_PROC_NOT_FOUND);
	return reinterpret_cast<FARPROC>(symbol);
}

PIMAGE_NT_HEADERS WINAPI ImageNtHeader(PVOID base)
{
	// A Mach-O image this Mac can load (a plugin is one) reads as a PE image for the machine Notepad++
	// was built for, with no data directories, so upstream's plugin architecture check passes. Nothing
	// else is a PE image: upstream's dark-mode import-table patching finds none and does nothing.
	if (!base || !isLoadableMachO(base))
		return nullptr;
	static IMAGE_NT_HEADERS header = []
	{
		IMAGE_NT_HEADERS result{};
		result.Signature = IMAGE_NT_SIGNATURE;
#ifdef _M_ARM64
		result.FileHeader.Machine = IMAGE_FILE_MACHINE_ARM64;
#else
		result.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
#endif
		result.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		return result;
	}();
	return &header;
}

// --- Messages -------------------------------------------------------------------------------------

DWORD WINAPI FormatMessageW(DWORD flags, LPCVOID source, DWORD messageId, DWORD, LPWSTR buffer, DWORD size, va_list*)
{
	std::u16string message;
	if (flags & FORMAT_MESSAGE_FROM_STRING)
	{
		message = static_cast<const char16_t*>(source);
	}
	else
	{
		static const std::pair<DWORD, const char16_t*> messages[] = {
			{ERROR_SUCCESS, u"The operation completed successfully."},
			{ERROR_FILE_NOT_FOUND, u"The system cannot find the file specified."},
			{ERROR_PATH_NOT_FOUND, u"The system cannot find the path specified."},
			{ERROR_ACCESS_DENIED, u"Access is denied."},
			{ERROR_INVALID_HANDLE, u"The handle is invalid."},
			{ERROR_NOT_ENOUGH_MEMORY, u"Not enough memory resources are available to process this command."},
			{ERROR_SHARING_VIOLATION, u"The process cannot access the file because it is being used by another process."},
			{ERROR_DISK_FULL, u"There is not enough space on the disk."},
			{ERROR_FILE_EXISTS, u"The file exists."},
			{ERROR_ALREADY_EXISTS, u"Cannot create a file when that file already exists."},
			{ERROR_INVALID_PARAMETER, u"The parameter is incorrect."},
			{ERROR_DIR_NOT_EMPTY, u"The directory is not empty."},
			{ERROR_FILENAME_EXCED_RANGE, u"The filename or extension is too long."},
		};
		message = u"Unknown error.";
		for (const auto& [id, text] : messages)
		{
			if (id == messageId)
				message = text;
		}
		message += u"\r\n";
	}

	if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
	{
		auto* allocated = static_cast<LPWSTR>(LocalAlloc(LMEM_FIXED, (message.size() + 1) * sizeof(char16_t)));
		std::copy(message.begin(), message.end(), allocated);
		allocated[message.size()] = u'\0';
		*reinterpret_cast<LPWSTR*>(buffer) = allocated;
		return static_cast<DWORD>(message.size());
	}
	if (message.size() + 1 > size)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	return copyToBuffer(message, buffer, size);
}

void WINAPI OutputDebugStringW(LPCWSTR text)
{
	if (std::getenv("NPPMAC_DEBUG_OUTPUT"))
		std::fprintf(stderr, "%s", nppmac::toUtf8(text).c_str());
}

void WINAPI OutputDebugStringA(LPCSTR text)
{
	if (std::getenv("NPPMAC_DEBUG_OUTPUT"))
		std::fprintf(stderr, "%s", text);
}

BOOL WINAPI IsDebuggerPresent()
{
	return FALSE;
}

// --- Processes ------------------------------------------------------------------------------------

namespace
{
	class Process : public KernelObject
	{
	public:
		explicit Process(pid_t pid) : _pid{pid} {}

		bool isSignaled() const override {
			return ::kill(_pid, 0) != 0 && errno == ESRCH;
		}

		pid_t _pid = 0;
	};

	class ProcessSnapshot : public KernelObject
	{
	public:
		std::vector<pid_t> _pids;
		size_t _next = 0;
	};

	bool fillProcessEntry(pid_t pid, LPPROCESSENTRY32W entry)
	{
		char path[PROC_PIDPATHINFO_MAXSIZE]{};
		if (proc_pidpath(pid, path, sizeof(path)) <= 0)
			return false;
		std::string name = path;
		name = name.substr(name.find_last_of('/') + 1);
		// Notepad++ looks for other running instances by their executable name.
		if (name == "Notepad++")
			name = "notepad++.exe";
		entry->th32ProcessID = static_cast<DWORD>(pid);
		entry->cntThreads = 1;
		entry->th32ParentProcessID = 0;
		const std::u16string wideName = nppmac::toUtf16(name);
		wcsncpy_s(entry->szExeFile, MAX_PATH, wideName.c_str(), _TRUNCATE);
		return true;
	}
}

HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD processId)
{
	if (::kill(static_cast<pid_t>(processId), 0) != 0 && errno == ESRCH)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	return createHandle(std::make_shared<Process>(static_cast<pid_t>(processId)));
}

BOOL WINAPI GetExitCodeProcess(HANDLE handle, LPDWORD exitCode)
{
	auto process = lookup<Process>(handle);
	if (!process)
		return FALSE;
	*exitCode = process->isSignaled() ? 0 : STILL_ACTIVE;
	return TRUE;
}

BOOL WINAPI QueryFullProcessImageNameW(HANDLE handle, DWORD, LPWSTR buffer, PDWORD size)
{
	pid_t pid = getpid();
	if (handle != GetCurrentProcess())
	{
		auto process = lookup<Process>(handle);
		if (!process)
			return FALSE;
		pid = process->_pid;
	}
	char path[PROC_PIDPATHINFO_MAXSIZE]{};
	if (proc_pidpath(pid, path, sizeof(path)) <= 0)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}
	const std::u16string wide = nppmac::toUtf16(path);
	if (wide.size() + 1 > *size)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}
	*size = copyToBuffer(wide, buffer, *size);
	return TRUE;
}

HANDLE WINAPI CreateToolhelp32Snapshot(DWORD, DWORD)
{
	auto snapshot = std::make_shared<ProcessSnapshot>();
	const int count = proc_listallpids(nullptr, 0);
	std::vector<pid_t> pids(static_cast<size_t>(count > 0 ? count : 0) + 16);
	const int filled = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
	pids.resize(static_cast<size_t>(filled > 0 ? filled : 0));
	snapshot->_pids = std::move(pids);
	return createHandle(snapshot);
}

BOOL WINAPI Process32NextW(HANDLE handle, LPPROCESSENTRY32W entry)
{
	auto snapshot = lookup<ProcessSnapshot>(handle);
	if (!snapshot)
		return FALSE;
	while (snapshot->_next < snapshot->_pids.size())
	{
		if (fillProcessEntry(snapshot->_pids[snapshot->_next++], entry))
			return TRUE;
	}
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

BOOL WINAPI Process32FirstW(HANDLE handle, LPPROCESSENTRY32W entry)
{
	auto snapshot = lookup<ProcessSnapshot>(handle);
	if (!snapshot)
		return FALSE;
	snapshot->_next = 0;
	return Process32NextW(handle, entry);
}

HRESULT WINAPI RegisterApplicationRestart(PCWSTR, DWORD)
{
	return S_OK;
}

HRESULT WINAPI UnregisterApplicationRestart()
{
	return S_OK;
}

HRESULT WINAPI GetApplicationRestartSettings(HANDLE, PWSTR, PDWORD, PDWORD)
{
	return E_FAIL;
}

// --- Security: the user is never an elevated administrator ----------------------------------------

BOOL WINAPI OpenProcessToken(HANDLE, DWORD, PHANDLE token)
{
	*token = nullptr;
	SetLastError(ERROR_NOT_SUPPORTED);
	return FALSE;
}

BOOL WINAPI GetTokenInformation(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD returnLength)
{
	if (returnLength)
		*returnLength = 0;
	SetLastError(ERROR_NOT_SUPPORTED);
	return FALSE;
}

BOOL WINAPI AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, PSID* sid)
{
	static int placeholder = 0;
	*sid = &placeholder;
	return TRUE;
}

BOOL WINAPI CheckTokenMembership(HANDLE, PSID, PBOOL isMember)
{
	*isMember = FALSE;
	return TRUE;
}

PVOID WINAPI FreeSid(PSID)
{
	return nullptr;
}

PUCHAR WINAPI GetSidSubAuthorityCount(PSID)
{
	static UCHAR count = 0;
	return &count;
}

PDWORD WINAPI GetSidSubAuthority(PSID, DWORD)
{
	static DWORD authority = SECURITY_MANDATORY_MEDIUM_RID;
	return &authority;
}

// --- Network state (sensapi): assume online ---------------------------------------------------------

BOOL WINAPI IsNetworkAlive(LPDWORD flags)
{
	if (flags)
		*flags = NETWORK_ALIVE_LAN;
	return TRUE;
}

BOOL WINAPI IsDestinationReachableW(LPCWSTR, LPQOCINFO)
{
	return TRUE;
}

HANDLE WINAPI FindFirstStreamW(LPCWSTR, STREAM_INFO_LEVELS, LPVOID, DWORD)
{
	// macOS files have no alternate data streams.
	SetLastError(ERROR_HANDLE_EOF);
	return INVALID_HANDLE_VALUE;
}
