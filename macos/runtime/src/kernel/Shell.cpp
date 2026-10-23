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

// shlwapi paths and colors, shell32 folders, ShellExecute, file operations and drops, ole32 start-up.
// Paths may use '/' or '\\' as separators; results use '/', the macOS separator.

#include "Internal.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sys/stat.h>
#include <vector>

using namespace nppmac::kernel;

namespace
{
	bool isSeparator(char16_t ch)
	{
		return ch == u'/' || ch == u'\\';
	}

	LPWSTR findFileName(LPWSTR path)
	{
		LPWSTR name = path;
		for (LPWSTR p = path; *p != u'\0'; ++p)
		{
			if (isSeparator(*p) && p[1] != u'\0')
				name = p + 1;
		}
		return name;
	}

	std::u16string joinPaths(std::u16string_view directory, std::u16string_view file)
	{
		if (directory.empty())
			return std::u16string{file};
		if (file.empty())
			return std::u16string{directory};
		std::u16string result{directory};
		while (!file.empty() && isSeparator(file.front()))
			file.remove_prefix(1);
		if (!isSeparator(result.back()))
			result += u'/';
		result += file;
		return result;
	}

	// The macOS folder standing in for a Windows shell folder (CSIDL).
	std::string folderForCsidl(int csidl)
	{
		switch (csidl & 0xFF)
		{
			case CSIDL_APPDATA:
			case CSIDL_LOCAL_APPDATA:
			case CSIDL_COMMON_APPDATA:
				return nppmac::platform::applicationSupportDirectory();
			case CSIDL_PERSONAL:
				return nppmac::platform::documentsDirectory();
			case CSIDL_DESKTOP:
			case CSIDL_DESKTOPDIRECTORY:
				return nppmac::platform::desktopDirectory();
			case CSIDL_PROFILE:
				return nppmac::platform::homeDirectory();
			case CSIDL_PROGRAM_FILES:
			case CSIDL_PROGRAM_FILESX86:
				return "/Applications";
			case CSIDL_WINDOWS:
			case CSIDL_SYSTEM:
				return "/System";
			default:
				return {};
		}
	}
}

// --- shlwapi paths --------------------------------------------------------------------------------

LPWSTR WINAPI PathFindFileNameW(LPCWSTR path)
{
	return findFileName(const_cast<LPWSTR>(path));
}

LPWSTR WINAPI PathFindExtensionW(LPCWSTR path)
{
	LPWSTR name = findFileName(const_cast<LPWSTR>(path));
	LPWSTR dot = nullptr;
	for (LPWSTR p = name; *p != u'\0'; ++p)
	{
		if (*p == u'.')
			dot = p;
		else if (*p == u' ')
			dot = nullptr;
	}
	return dot ? dot : name + wcslen(name);
}

BOOL WINAPI PathRemoveFileSpecW(LPWSTR path)
{
	if (!path || !*path)
		return FALSE;
	LPWSTR lastSeparator = nullptr;
	for (LPWSTR p = path; *p != u'\0'; ++p)
	{
		if (isSeparator(*p))
			lastSeparator = p;
	}
	if (!lastSeparator)
	{
		*path = u'\0';
		return TRUE;
	}
	// Keep the root separator of "/file".
	if (lastSeparator == path)
		lastSeparator[1] = u'\0';
	else
		*lastSeparator = u'\0';
	return TRUE;
}

void WINAPI PathRemoveExtensionW(LPWSTR path)
{
	LPWSTR extension = PathFindExtensionW(path);
	if (*extension == u'.')
		*extension = u'\0';
}

void WINAPI PathStripPathW(LPWSTR path)
{
	LPWSTR name = findFileName(path);
	if (name != path)
		std::memmove(path, name, (wcslen(name) + 1) * sizeof(char16_t));
}

BOOL WINAPI PathIsRelativeW(LPCWSTR path)
{
	if (!path || !*path)
		return TRUE;
	if (isSeparator(path[0]))
		return FALSE;
	if (path[1] == u':')
		return FALSE;
	return std::u16string_view{path}.find(u"://") == std::u16string_view::npos ? TRUE : FALSE;
}

BOOL WINAPI PathIsNetworkPathW(LPCWSTR path)
{
	// UNC paths ("\\\\server\\share") and network volumes mounted under /Volumes.
	if (path && isSeparator(path[0]) && isSeparator(path[1]))
		return TRUE;
	if (!path)
		return FALSE;
	return GetDriveTypeW(path) == DRIVE_REMOTE ? TRUE : FALSE;
}

int WINAPI PathGetDriveNumberW(LPCWSTR path)
{
	if (path && path[0] && path[1] == u':')
	{
		const char16_t letter = static_cast<char16_t>(nppmac::toupper(path[0], std::locale::classic()));
		if (letter >= u'A' && letter <= u'Z')
			return letter - u'A';
	}
	return -1;
}

BOOL WINAPI PathCanonicalizeW(LPWSTR dest, LPCWSTR source)
{
	std::u16string path{source ? source : u""};
	std::replace(path.begin(), path.end(), u'\\', u'/');
	std::vector<std::u16string> parts;
	size_t start = 0;
	const bool absolute = !path.empty() && path[0] == u'/';
	while (start <= path.size())
	{
		size_t end = path.find(u'/', start);
		if (end == std::u16string::npos)
			end = path.size();
		const std::u16string part = path.substr(start, end - start);
		if (part == u"..")
		{
			if (!parts.empty())
				parts.pop_back();
		}
		else if (!part.empty() && part != u".")
		{
			parts.push_back(part);
		}
		start = end + 1;
	}
	std::u16string result = absolute ? u"/" : u"";
	for (size_t i = 0; i < parts.size(); ++i)
		result += (i ? u"/" : u"") + parts[i];
	if (result.size() >= MAX_PATH)
		return FALSE;
	wcscpy_s(dest, MAX_PATH, result.c_str());
	return TRUE;
}

LPWSTR WINAPI PathCombineW(LPWSTR dest, LPCWSTR directory, LPCWSTR file)
{
	if (!dest)
		return nullptr;
	std::u16string combined;
	if (file && !PathIsRelativeW(file))
		combined = file;
	else
		combined = joinPaths(directory ? directory : u"", file ? file : u"");
	char16_t canonical[MAX_PATH]{};
	if (!PathCanonicalizeW(canonical, combined.c_str()))
	{
		dest[0] = u'\0';
		return nullptr;
	}
	wcscpy_s(dest, MAX_PATH, canonical);
	return dest;
}

BOOL WINAPI PathAppendW(LPWSTR path, LPCWSTR more)
{
	const std::u16string joined = joinPaths(path, more ? more : u"");
	char16_t canonical[MAX_PATH]{};
	if (!PathCanonicalizeW(canonical, joined.c_str()))
		return FALSE;
	wcscpy_s(path, MAX_PATH, canonical);
	return TRUE;
}

BOOL WINAPI PathFileExistsW(LPCWSTR path)
{
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? TRUE : FALSE;
}

BOOL WINAPI PathIsDirectoryW(LPCWSTR path)
{
	const DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? FILE_ATTRIBUTE_DIRECTORY : FALSE;
}

BOOL WINAPI PathMatchSpecW(LPCWSTR file, LPCWSTR spec)
{
	// A spec may hold several patterns separated by ';'.
	const std::u16string_view name{PathFindFileNameW(file)};
	std::u16string_view specs{spec};
	while (!specs.empty())
	{
		const size_t separator = specs.find(u';');
		std::u16string_view pattern = specs.substr(0, separator);
		specs = separator == std::u16string_view::npos ? std::u16string_view{} : specs.substr(separator + 1);
		while (!pattern.empty() && pattern.front() == u' ')
			pattern.remove_prefix(1);

		size_t n = 0;
		size_t p = 0;
		size_t starPattern = std::u16string_view::npos;
		size_t starName = 0;
		while (n < name.size())
		{
			if (p < pattern.size() && (pattern[p] == u'?' || nppmac::tolower(pattern[p], std::locale::classic()) == nppmac::tolower(name[n], std::locale::classic())))
			{
				++n;
				++p;
			}
			else if (p < pattern.size() && pattern[p] == u'*')
			{
				starPattern = p++;
				starName = n;
			}
			else if (starPattern != std::u16string_view::npos)
			{
				p = starPattern + 1;
				n = ++starName;
			}
			else
			{
				break;
			}
		}
		while (p < pattern.size() && pattern[p] == u'*')
			++p;
		if (n == name.size() && p == pattern.size())
			return TRUE;
	}
	return FALSE;
}

BOOL WINAPI PathCompactPathExW(LPWSTR out, LPCWSTR source, UINT maxChars, DWORD)
{
	// "/Users/me/very/long/path/file.txt" -> "/Users/me/...ath/file.txt", file name kept whole.
	std::u16string path{source};
	if (maxChars == 0)
		return FALSE;
	if (path.size() < maxChars)
	{
		wcscpy_s(out, maxChars, path.c_str());
		return TRUE;
	}
	const std::u16string name = PathFindFileNameW(source);
	const std::u16string ellipsis = u"...";
	if (name.size() + ellipsis.size() >= maxChars)
	{
		std::u16string truncated = name.substr(0, maxChars > ellipsis.size() + 1 ? maxChars - ellipsis.size() - 1 : 0) + ellipsis;
		wcscpy_s(out, maxChars, truncated.substr(0, maxChars - 1).c_str());
		return TRUE;
	}
	const size_t keep = maxChars - 1 - name.size() - ellipsis.size() - 1;
	const std::u16string compact = path.substr(0, keep) + ellipsis + u"/" + name;
	wcscpy_s(out, maxChars, compact.c_str());
	return TRUE;
}

HRESULT WINAPI PathCreateFromUrlW(LPCWSTR url, LPWSTR path, LPDWORD pathSize, DWORD)
{
	std::u16string_view text{url};
	if (!text.starts_with(u"file:"))
		return E_INVALIDARG;
	text.remove_prefix(5);
	while (text.starts_with(u"//"))
		text.remove_prefix(1);
	// Percent-decode as UTF-8.
	std::string decoded;
	const std::string narrow = nppmac::toUtf8(text);
	for (size_t i = 0; i < narrow.size(); ++i)
	{
		if (narrow[i] == '%' && i + 2 < narrow.size() && std::isxdigit(static_cast<unsigned char>(narrow[i + 1])) && std::isxdigit(static_cast<unsigned char>(narrow[i + 2])))
		{
			decoded.push_back(static_cast<char>(std::stoi(narrow.substr(i + 1, 2), nullptr, 16)));
			i += 2;
		}
		else
		{
			decoded.push_back(narrow[i]);
		}
	}
	const std::u16string result = nppmac::toUtf16(decoded);
	if (result.size() + 1 > *pathSize)
	{
		*pathSize = static_cast<DWORD>(result.size() + 1);
		return E_POINTER;
	}
	*pathSize = copyToBuffer(result, path, *pathSize);
	return S_OK;
}

HRESULT WINAPI AssocQueryStringW(ASSOCF, ASSOCSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD*)
{
	// Windows file associations do not exist here.
	return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
}

// --- shlwapi colors -------------------------------------------------------------------------------

void WINAPI ColorRGBToHLS(COLORREF color, WORD* hue, WORD* luminance, WORD* saturation)
{
	// The HLS scale of shlwapi: each component in 0..240.
	constexpr int hlsMax = 240;
	const int r = GetRValue(color);
	const int g = GetGValue(color);
	const int b = GetBValue(color);
	const int maxValue = std::max({r, g, b});
	const int minValue = std::min({r, g, b});
	const int l = ((maxValue + minValue) * hlsMax + 255) / (2 * 255);
	int h = 0;
	int s = 0;
	if (maxValue != minValue)
	{
		const int sum = maxValue + minValue;
		const int diff = maxValue - minValue;
		s = l <= hlsMax / 2 ? (diff * hlsMax + sum / 2) / sum : (diff * hlsMax + (2 * 255 - sum) / 2) / (2 * 255 - sum);
		const int rDelta = ((maxValue - r) * (hlsMax / 6) + diff / 2) / diff;
		const int gDelta = ((maxValue - g) * (hlsMax / 6) + diff / 2) / diff;
		const int bDelta = ((maxValue - b) * (hlsMax / 6) + diff / 2) / diff;
		if (r == maxValue)
			h = bDelta - gDelta;
		else if (g == maxValue)
			h = hlsMax / 3 + rDelta - bDelta;
		else
			h = 2 * hlsMax / 3 + gDelta - rDelta;
		if (h < 0)
			h += hlsMax;
		if (h > hlsMax)
			h -= hlsMax;
	}
	*hue = static_cast<WORD>(h);
	*luminance = static_cast<WORD>(l);
	*saturation = static_cast<WORD>(s);
}

COLORREF WINAPI ColorHLSToRGB(WORD hue, WORD luminance, WORD saturation)
{
	constexpr double hlsMax = 240.0;
	const double h = hue / hlsMax;
	const double l = luminance / hlsMax;
	const double s = saturation / hlsMax;
	auto channel = [](double p, double q, double t)
	{
		if (t < 0)
			t += 1;
		if (t > 1)
			t -= 1;
		if (t < 1.0 / 6)
			return p + (q - p) * 6 * t;
		if (t < 0.5)
			return q;
		if (t < 2.0 / 3)
			return p + (q - p) * (2.0 / 3 - t) * 6;
		return p;
	};
	double r = l;
	double g = l;
	double b = l;
	if (s != 0)
	{
		const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
		const double p = 2 * l - q;
		r = channel(p, q, h + 1.0 / 3);
		g = channel(p, q, h);
		b = channel(p, q, h - 1.0 / 3);
	}
	return RGB(static_cast<BYTE>(std::lround(r * 255)), static_cast<BYTE>(std::lround(g * 255)), static_cast<BYTE>(std::lround(b * 255)));
}

COLORREF WINAPI ColorAdjustLuma(COLORREF color, int luma, BOOL scale)
{
	WORD h = 0;
	WORD l = 0;
	WORD s = 0;
	ColorRGBToHLS(color, &h, &l, &s);
	int newLuma = scale ? l + l * luma / 1000 : l + luma;
	newLuma = std::clamp(newLuma, 0, 240);
	return ColorHLSToRGB(h, static_cast<WORD>(newLuma), s);
}

// --- shell32 folders and operations ---------------------------------------------------------------

HRESULT WINAPI SHGetFolderPathW(HWND, int csidl, HANDLE, DWORD, LPWSTR path)
{
	const std::string folder = folderForCsidl(csidl);
	if (folder.empty())
	{
		path[0] = u'\0';
		return E_INVALIDARG;
	}
	if (csidl & CSIDL_FLAG_CREATE)
		::mkdir(folder.c_str(), 0755);
	wcscpy_s(path, MAX_PATH, fromPosixPath(folder).c_str());
	return S_OK;
}

int WINAPI SHCreateDirectoryExW(HWND, LPCWSTR path, LPSECURITY_ATTRIBUTES)
{
	std::string posix = toPosixPath(path);
	struct stat info{};
	if (::stat(posix.c_str(), &info) == 0)
		return S_ISDIR(info.st_mode) ? ERROR_ALREADY_EXISTS : ERROR_FILE_EXISTS;
	for (size_t i = 1; i <= posix.size(); ++i)
	{
		if (i == posix.size() || posix[i] == '/')
		{
			const std::string part = posix.substr(0, i);
			if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
				return static_cast<int>(win32ErrorFromErrno(errno));
		}
	}
	return ERROR_SUCCESS;
}

int WINAPI SHCreateDirectory(HWND window, LPCWSTR path)
{
	return SHCreateDirectoryExW(window, path, nullptr);
}

int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW operation)
{
	// pFrom and pTo are lists of paths, each terminated, the list doubly terminated.
	std::vector<std::u16string> sources;
	for (LPCWSTR p = operation->pFrom; p && *p; p += wcslen(p) + 1)
		sources.emplace_back(p);
	operation->fAnyOperationsAborted = FALSE;

	for (const std::u16string& source : sources)
	{
		bool ok = false;
		switch (operation->wFunc)
		{
			case FO_DELETE:
			{
				const std::string posix = toPosixPath(source);
				ok = (operation->fFlags & FOF_ALLOWUNDO) ? nppmac::platform::moveToTrash(posix) : ::remove(posix.c_str()) == 0;
				break;
			}
			case FO_MOVE:
			case FO_RENAME:
			{
				std::u16string target = operation->pTo ? operation->pTo : u"";
				if (GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_DIRECTORY && GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
					target = joinPaths(target, PathFindFileNameW(source.c_str()));
				ok = MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED) != FALSE;
				break;
			}
			case FO_COPY:
			{
				std::u16string target = operation->pTo ? operation->pTo : u"";
				if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES && (GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_DIRECTORY))
					target = joinPaths(target, PathFindFileNameW(source.c_str()));
				ok = CopyFileW(source.c_str(), target.c_str(), FALSE) != FALSE;
				break;
			}
			default:
				break;
		}
		if (!ok)
			return ERROR_ACCESS_DENIED;
	}
	return 0;
}

HRESULT WINAPI SHOpenFolderAndSelectItems(PCIDLIST_ABSOLUTE folder, UINT count, PCUITEMID_CHILD_ARRAY items, DWORD)
{
	// The item lists come from SHParseDisplayName below, which stores a path.
	std::vector<std::string> paths;
	if (count == 0 && folder)
		paths.push_back(toPosixPath(reinterpret_cast<const char16_t*>(folder)));
	for (UINT i = 0; i < count; ++i)
		paths.push_back(toPosixPath(reinterpret_cast<const char16_t*>(items[i])));
	return nppmac::platform::revealInFinder(paths) ? S_OK : E_FAIL;
}

HRESULT WINAPI SHParseDisplayName(PCWSTR name, IBindCtx*, PIDLIST_ABSOLUTE* idList, SFGAOF, SFGAOF* attributes)
{
	// An "item ID list" here is simply a copy of the path, freed with CoTaskMemFree / ILFree.
	if (GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES)
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	const size_t bytes = (wcslen(name) + 1) * sizeof(char16_t);
	auto* copy = static_cast<char16_t*>(CoTaskMemAlloc(bytes));
	std::memcpy(copy, name, bytes);
	*idList = reinterpret_cast<PIDLIST_ABSOLUTE>(copy);
	if (attributes)
		*attributes = 0;
	return S_OK;
}

void WINAPI ILFree(LPITEMIDLIST idList)
{
	CoTaskMemFree(idList);
}

HINSTANCE WINAPI ShellExecuteW(HWND, LPCWSTR operation, LPCWSTR file, LPCWSTR parameters, LPCWSTR, INT)
{
	const std::u16string verb = operation ? operation : u"open";
	const std::string target = nppmac::toUtf8(file ? file : u"");
	bool ok = false;
	if (verb == u"explore")
	{
		ok = nppmac::platform::openUrlOrFile(toPosixPath(file));
	}
	else if (target.find("://") != std::string::npos || target.starts_with("mailto:"))
	{
		ok = nppmac::platform::openUrlOrFile(target);
	}
	else
	{
		// "open" on a program with parameters (e.g. notepad++.exe, a shell) cannot run Windows
		// programs; a document opens with its default application.
		const std::string path = toPosixPath(file);
		if (parameters && *parameters && path.ends_with(".app"))
			ok = nppmac::platform::openUrlOrFile(toPosixPath(parameters), path);
		else
			ok = nppmac::platform::openUrlOrFile(path);
	}
	// Values above 32 mean success.
	return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(ok ? 42 : SE_ERR_FNF));
}

BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW* info)
{
	HINSTANCE result = ShellExecuteW(info->hwnd, info->lpVerb, info->lpFile, info->lpParameters, info->lpDirectory, info->nShow);
	info->hInstApp = result;
	info->hProcess = nullptr;
	if (reinterpret_cast<intptr_t>(result) <= 32)
	{
		SetLastError(ERROR_FILE_NOT_FOUND);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI Shell_NotifyIconW(DWORD, PNOTIFYICONDATAW)
{
	// There is no system tray; minimize-to-tray behaves as minimize.
	return FALSE;
}

// --- Dropped files ---------------------------------------------------------------------------------
// HDROP is a GMEM_MOVEABLE block holding a DROPFILES header and the paths, as on Windows; the
// window manager builds it when files are dropped on a window that accepts them.

UINT WINAPI DragQueryFileW(HDROP drop, UINT index, LPWSTR file, UINT size)
{
	auto* header = static_cast<DROPFILES*>(GlobalLock(drop));
	if (!header)
		return 0;
	const auto* p = reinterpret_cast<const char16_t*>(reinterpret_cast<const BYTE*>(header) + header->pFiles);
	UINT count = 0;
	UINT result = 0;
	while (*p)
	{
		const size_t length = wcslen(p);
		if (count == index)
		{
			result = static_cast<UINT>(length);
			if (file && size > 0)
			{
				lstrcpynW(file, p, static_cast<int>(size));
				result = static_cast<UINT>(wcslen(file));
			}
			break;
		}
		++count;
		p += length + 1;
	}
	GlobalUnlock(drop);
	return index == 0xFFFFFFFF ? count : result;
}

BOOL WINAPI DragQueryPoint(HDROP drop, POINT* point)
{
	auto* header = static_cast<DROPFILES*>(GlobalLock(drop));
	if (!header)
		return FALSE;
	*point = header->pt;
	const BOOL client = header->fNC ? FALSE : TRUE;
	GlobalUnlock(drop);
	return client;
}

void WINAPI DragFinish(HDROP drop)
{
	GlobalFree(drop);
}

// --- ole32 start-up -------------------------------------------------------------------------------

HRESULT WINAPI CoInitializeEx(LPVOID, DWORD)
{
	return S_OK;
}

HRESULT WINAPI CoInitialize(LPVOID reserved)
{
	return CoInitializeEx(reserved, COINIT_APARTMENTTHREADED);
}

void WINAPI CoUninitialize()
{
}

// No COM classes are registered yet: callers take their fallback paths, as on a Windows without them.
HRESULT WINAPI CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID* object)
{
	if (object)
		*object = nullptr;
	return REGDB_E_CLASSNOTREG;
}

HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR, IBindCtx*, REFIID, void** item)
{
	if (item)
		*item = nullptr;
	return E_NOTIMPL;
}

HRESULT WINAPI OleInitialize(LPVOID)
{
	return S_OK;
}

void WINAPI OleUninitialize()
{
}
