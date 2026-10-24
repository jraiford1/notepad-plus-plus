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

// kernel32 initialization files (GetPrivateProfileString and family), which plugins keep their
// settings in. A file is read on each call and written whole on each change, as small as these are.
// A file with a UTF-16 byte order mark stays UTF-16; any other is read and written as UTF-8.

#include "Immortal.h"
#include "Internal.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <vector>

using nppmac::kernel::copyToBuffer;
using nppmac::kernel::toPosixPath;

namespace
{
	nppmac::Immortal<std::mutex> g_profileMutexStorage;
	std::mutex& g_profileMutex = g_profileMutexStorage._value;

	struct Entry
	{
		std::u16string _key;
		std::u16string _value;
	};

	struct Section
	{
		std::u16string _name;
		std::vector<Entry> _entries;
	};

	struct Profile
	{
		std::vector<Section> _sections;
		bool _utf16 = false;
	};

	std::u16string trimmed(std::u16string_view text)
	{
		const auto isSpace = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r'; };
		size_t first = 0;
		size_t last = text.size();
		while (first < last && isSpace(text[first]))
			++first;
		while (last > first && isSpace(text[last - 1]))
			--last;
		return std::u16string{text.substr(first, last - first)};
	}

	bool sameName(std::u16string_view a, std::u16string_view b)
	{
		// Section and key names are case-insensitive, as on Windows.
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char16_t x, char16_t y)
		{
			const auto lower = [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c; };
			return lower(x) == lower(y);
		});
	}

	// An initialization file without a folder is in the Windows folder on Windows; here, in the
	// program's settings folder.
	std::string profilePath(LPCWSTR fileName)
	{
		const std::u16string name = fileName ? fileName : u"win.ini";
		if (name.find_first_of(u"\\/") != std::u16string::npos)
			return toPosixPath(name);
		const char* home = std::getenv("HOME");
		return std::string{home ? home : ""} + "/Library/Application Support/Notepad++/" + nppmac::toUtf8(name);
	}

	Profile readProfile(const std::string& path)
	{
		Profile profile;
		std::ifstream in{path, std::ios::binary};
		if (!in)
			return profile;
		const std::string bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
		std::u16string text;
		if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE)
		{
			profile._utf16 = true;
			text.resize((bytes.size() - 2) / 2);
			std::memcpy(text.data(), bytes.data() + 2, text.size() * 2);
		}
		else
		{
			const size_t start = bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
			text = nppmac::toUtf16(std::string_view{bytes}.substr(start));
		}

		Section* current = nullptr;
		size_t lineStart = 0;
		while (lineStart <= text.size())
		{
			size_t lineEnd = text.find(u'\n', lineStart);
			if (lineEnd == std::u16string::npos)
				lineEnd = text.size();
			const std::u16string line = trimmed(std::u16string_view{text}.substr(lineStart, lineEnd - lineStart));
			lineStart = lineEnd + 1;
			if (line.empty() || line[0] == u';')
				continue;
			if (line[0] == u'[')
			{
				const size_t close = line.find(u']');
				profile._sections.push_back({trimmed(std::u16string_view{line}.substr(1, close == std::u16string::npos ? std::u16string::npos : close - 1)), {}});
				current = &profile._sections.back();
				continue;
			}
			if (!current)
				continue;
			const size_t equals = line.find(u'=');
			if (equals == std::u16string::npos)
				current->_entries.push_back({line, {}});
			else
				current->_entries.push_back({trimmed(std::u16string_view{line}.substr(0, equals)), trimmed(std::u16string_view{line}.substr(equals + 1))});
		}
		return profile;
	}

	bool writeProfile(const std::string& path, const Profile& profile)
	{
		std::u16string text;
		for (const Section& section : profile._sections)
		{
			text += u"[" + section._name + u"]\r\n";
			for (const Entry& entry : section._entries)
				text += entry._key + u"=" + entry._value + u"\r\n";
		}
		std::ofstream out{path, std::ios::binary | std::ios::trunc};
		if (!out)
			return false;
		if (profile._utf16)
		{
			out.write("\xFF\xFE", 2);
			out.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size() * 2));
		}
		else
		{
			const std::string utf8 = nppmac::toUtf8(text);
			out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
		}
		return static_cast<bool>(out);
	}

	Section* findSection(Profile& profile, std::u16string_view name)
	{
		for (Section& section : profile._sections)
		{
			if (sameName(section._name, name))
				return &section;
		}
		return nullptr;
	}

	// A list of strings, each terminated, the list terminated again, cut to the buffer as Windows does:
	// the return value is size - 2 when it did not fit.
	DWORD copyList(const std::vector<std::u16string>& items, LPWSTR buffer, DWORD size)
	{
		if (!buffer || size == 0)
			return 0;
		if (size < 2)
		{
			buffer[0] = u'\0';
			return 0;
		}
		DWORD used = 0;
		for (const std::u16string& item : items)
		{
			for (const char16_t c : item)
			{
				if (used + 2 >= size)
				{
					buffer[used] = u'\0';
					buffer[used + 1] = u'\0';
					return size - 2;
				}
				buffer[used++] = c;
			}
			if (used + 2 >= size)
			{
				buffer[used] = u'\0';
				buffer[used + 1] = u'\0';
				return size - 2;
			}
			buffer[used++] = u'\0';
		}
		buffer[used] = u'\0';
		return used;
	}

	// A single value, cut to the buffer: the return value is size - 1 when it did not fit.
	DWORD copyValue(std::u16string_view value, LPWSTR buffer, DWORD size)
	{
		if (!buffer || size == 0)
			return 0;
		const DWORD count = static_cast<DWORD>(std::min<size_t>(value.size(), size - 1));
		std::copy_n(value.begin(), count, buffer);
		buffer[count] = u'\0';
		return count;
	}

	std::u16string unquoted(const std::u16string& value)
	{
		if (value.size() >= 2 && ((value.front() == u'"' && value.back() == u'"') || (value.front() == u'\'' && value.back() == u'\'')))
			return value.substr(1, value.size() - 2);
		return value;
	}

	std::u16string wide(LPCSTR text)
	{
		if (!text)
			return {};
		const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
		std::u16string result(static_cast<size_t>(std::max(0, length)), u'\0');
		MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), length);
		if (!result.empty())
			result.pop_back();
		return result;
	}

	std::optional<std::u16string> optionalWide(LPCSTR text)
	{
		if (!text)
			return std::nullopt;
		return wide(text);
	}

	// The A functions' results: a wide buffer converted to the caller's code page, lists included.
	DWORD narrowResult(const std::u16string& wideBuffer, DWORD wideCount, LPSTR buffer, DWORD size, bool list)
	{
		if (!buffer || size == 0)
			return 0;
		const int bytes = WideCharToMultiByte(CP_ACP, 0, wideBuffer.data(), static_cast<int>(wideCount + (list ? 1 : 0)), buffer,
			static_cast<int>(size), nullptr, nullptr);
		const DWORD count = static_cast<DWORD>(std::max(0, bytes - (list ? 1 : 0)));
		if (count < size)
			buffer[count] = '\0';
		if (list && count + 1 < size)
			buffer[count + 1] = '\0';
		return count;
	}
}

INT WINAPI GetPrivateProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue, LPWSTR buffer, UINT size, LPCWSTR fileName)
{
	std::lock_guard lock{g_profileMutex};
	Profile profile = readProfile(profilePath(fileName));
	if (!appName)
	{
		std::vector<std::u16string> names;
		for (const Section& section : profile._sections)
			names.push_back(section._name);
		return static_cast<INT>(copyList(names, buffer, size));
	}
	Section* section = findSection(profile, appName);
	if (!keyName)
	{
		std::vector<std::u16string> keys;
		if (section)
		{
			for (const Entry& entry : section->_entries)
				keys.push_back(entry._key);
		}
		return static_cast<INT>(copyList(keys, buffer, size));
	}
	if (section)
	{
		for (const Entry& entry : section->_entries)
		{
			if (sameName(entry._key, keyName))
				return static_cast<INT>(copyValue(unquoted(entry._value), buffer, size));
		}
	}
	// The default loses its trailing blanks, as on Windows.
	std::u16string fallback = defaultValue ? defaultValue : u"";
	while (!fallback.empty() && (fallback.back() == u' ' || fallback.back() == u'\t'))
		fallback.pop_back();
	SetLastError(ERROR_FILE_NOT_FOUND);
	return static_cast<INT>(copyValue(fallback, buffer, size));
}

UINT WINAPI GetPrivateProfileIntW(LPCWSTR appName, LPCWSTR keyName, INT defaultValue, LPCWSTR fileName)
{
	char16_t value[64]{};
	if (GetPrivateProfileStringW(appName, keyName, u"", value, 64, fileName) == 0)
		return static_cast<UINT>(defaultValue);
	// Leading digits, as Windows reads them (0x for hexadecimal).
	const std::string text = nppmac::toUtf8(value);
	char* end = nullptr;
	const long number = std::strtol(text.c_str(), &end, text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0 ? 16 : 10);
	return end == text.c_str() ? 0 : static_cast<UINT>(number);
}

DWORD WINAPI GetPrivateProfileSectionNamesW(LPWSTR buffer, DWORD size, LPCWSTR fileName)
{
	return static_cast<DWORD>(GetPrivateProfileStringW(nullptr, nullptr, nullptr, buffer, size, fileName));
}

INT WINAPI GetPrivateProfileSectionW(LPCWSTR appName, LPWSTR buffer, DWORD size, LPCWSTR fileName)
{
	std::lock_guard lock{g_profileMutex};
	Profile profile = readProfile(profilePath(fileName));
	std::vector<std::u16string> lines;
	if (Section* section = findSection(profile, appName ? appName : u""))
	{
		for (const Entry& entry : section->_entries)
			lines.push_back(entry._key + u"=" + entry._value);
	}
	return static_cast<INT>(copyList(lines, buffer, size));
}

BOOL WINAPI WritePrivateProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR value, LPCWSTR fileName)
{
	if (!appName)
		return TRUE; // flushing a cache that is not kept
	std::lock_guard lock{g_profileMutex};
	const std::string path = profilePath(fileName);
	Profile profile = readProfile(path);
	Section* section = findSection(profile, appName);
	if (!keyName)
	{
		// No key: the section goes.
		profile._sections.erase(std::remove_if(profile._sections.begin(), profile._sections.end(),
			[appName](const Section& s) { return sameName(s._name, appName); }), profile._sections.end());
		return writeProfile(path, profile) ? TRUE : FALSE;
	}
	if (!section)
	{
		if (!value)
			return TRUE;
		profile._sections.push_back({appName, {}});
		section = &profile._sections.back();
	}
	auto entry = std::find_if(section->_entries.begin(), section->_entries.end(), [keyName](const Entry& e) { return sameName(e._key, keyName); });
	if (!value)
	{
		// No value: the key goes.
		if (entry != section->_entries.end())
			section->_entries.erase(entry);
	}
	else if (entry != section->_entries.end())
	{
		entry->_value = value;
	}
	else
	{
		section->_entries.push_back({keyName, value});
	}
	if (!writeProfile(path, profile))
	{
		nppmac::kernel::setErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI WritePrivateProfileSectionW(LPCWSTR appName, LPCWSTR lines, LPCWSTR fileName)
{
	if (!appName)
		return FALSE;
	std::lock_guard lock{g_profileMutex};
	const std::string path = profilePath(fileName);
	Profile profile = readProfile(path);
	Section* section = findSection(profile, appName);
	if (!lines)
	{
		profile._sections.erase(std::remove_if(profile._sections.begin(), profile._sections.end(),
			[appName](const Section& s) { return sameName(s._name, appName); }), profile._sections.end());
		return writeProfile(path, profile) ? TRUE : FALSE;
	}
	if (!section)
	{
		profile._sections.push_back({appName, {}});
		section = &profile._sections.back();
	}
	section->_entries.clear();
	for (const char16_t* line = lines; *line; line += std::char_traits<char16_t>::length(line) + 1)
	{
		const std::u16string text = line;
		const size_t equals = text.find(u'=');
		if (equals == std::u16string::npos)
			section->_entries.push_back({trimmed(text), {}});
		else
			section->_entries.push_back({trimmed(std::u16string_view{text}.substr(0, equals)), trimmed(std::u16string_view{text}.substr(equals + 1))});
	}
	return writeProfile(path, profile) ? TRUE : FALSE;
}

// --- ANSI ---------------------------------------------------------------------------------------

INT WINAPI GetPrivateProfileStringA(LPCSTR appName, LPCSTR keyName, LPCSTR defaultValue, LPSTR buffer, UINT size, LPCSTR fileName)
{
	const auto app = optionalWide(appName);
	const auto key = optionalWide(keyName);
	const auto fallback = optionalWide(defaultValue);
	const std::u16string file = wide(fileName);
	std::u16string wideBuffer(size + 2, u'\0');
	const DWORD count = static_cast<DWORD>(GetPrivateProfileStringW(app ? app->c_str() : nullptr, key ? key->c_str() : nullptr,
		fallback ? fallback->c_str() : nullptr, wideBuffer.data(), size, fileName ? file.c_str() : nullptr));
	return static_cast<INT>(narrowResult(wideBuffer, count, buffer, size, !appName || !keyName));
}

UINT WINAPI GetPrivateProfileIntA(LPCSTR appName, LPCSTR keyName, INT defaultValue, LPCSTR fileName)
{
	const std::u16string file = wide(fileName);
	return GetPrivateProfileIntW(wide(appName).c_str(), wide(keyName).c_str(), defaultValue, fileName ? file.c_str() : nullptr);
}

BOOL WINAPI WritePrivateProfileStringA(LPCSTR appName, LPCSTR keyName, LPCSTR value, LPCSTR fileName)
{
	const auto app = optionalWide(appName);
	const auto key = optionalWide(keyName);
	const auto text = optionalWide(value);
	const std::u16string file = wide(fileName);
	return WritePrivateProfileStringW(app ? app->c_str() : nullptr, key ? key->c_str() : nullptr, text ? text->c_str() : nullptr,
		fileName ? file.c_str() : nullptr);
}

DWORD WINAPI GetPrivateProfileSectionNamesA(LPSTR buffer, DWORD size, LPCSTR fileName)
{
	return static_cast<DWORD>(GetPrivateProfileStringA(nullptr, nullptr, nullptr, buffer, size, fileName));
}

// --- win.ini ------------------------------------------------------------------------------------

INT WINAPI GetProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue, LPWSTR buffer, UINT size)
{
	return GetPrivateProfileStringW(appName, keyName, defaultValue, buffer, size, nullptr);
}

UINT WINAPI GetProfileIntW(LPCWSTR appName, LPCWSTR keyName, INT defaultValue)
{
	return GetPrivateProfileIntW(appName, keyName, defaultValue, nullptr);
}

BOOL WINAPI WriteProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR value)
{
	return WritePrivateProfileStringW(appName, keyName, value, nullptr);
}
