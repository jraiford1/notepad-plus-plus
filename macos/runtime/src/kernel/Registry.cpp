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

// advapi32 registry, in memory. Seeded with the values Notepad++ reads to learn about the system;
// the Windows "apps use light theme" setting follows the macOS appearance, so upstream's own
// dark-mode logic follows it too. Writes (file associations) last for the session.

#include "Immortal.h"
#include "Internal.h"
#include "platform/Platform.h"

#include <map>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <uuid/uuid.h>
#include <vector>

namespace
{
	struct CaseInsensitiveLess
	{
		bool operator()(const std::u16string& lhs, const std::u16string& rhs) const {
			return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
		}
	};

	struct Value
	{
		DWORD _type = REG_NONE;
		std::vector<BYTE> _data;
	};

	struct Key
	{
		std::map<std::u16string, std::unique_ptr<Key>, CaseInsensitiveLess> _subkeys;
		std::map<std::u16string, Value, CaseInsensitiveLess> _values;
	};

	nppmac::Immortal<std::recursive_mutex> g_mutexStorage;

	std::recursive_mutex& g_mutex = g_mutexStorage._value;

	std::map<uintptr_t, std::unique_ptr<Key>>& roots()
	{
		static auto* map = new std::map<uintptr_t, std::unique_ptr<Key>>;
		return *map;
	}

	Value stringValue(const std::u16string& text)
	{
		Value value;
		value._type = REG_SZ;
		const auto* bytes = reinterpret_cast<const BYTE*>(text.c_str());
		value._data.assign(bytes, bytes + (text.size() + 1) * sizeof(char16_t));
		return value;
	}

	Value dwordValue(DWORD number)
	{
		Value value;
		value._type = REG_DWORD;
		const auto* bytes = reinterpret_cast<const BYTE*>(&number);
		value._data.assign(bytes, bytes + sizeof(number));
		return value;
	}

	Key* root(HKEY hkey)
	{
		// The predefined keys are sign-extended 32-bit values (HKEY_LOCAL_MACHINE is
		// 0xFFFFFFFF80000002 in a 64-bit process).
		const uintptr_t raw = reinterpret_cast<uintptr_t>(hkey);
		if ((raw >> 32) != 0xFFFFFFFF && (raw >> 32) != 0)
			return nullptr;
		const uintptr_t value = raw & 0xFFFFFFFF;
		if (value < 0x80000000 || value > 0x80000006)
			return nullptr;
		std::unique_ptr<Key>& slot = roots()[value];
		if (!slot)
			slot = std::make_unique<Key>();
		return slot.get();
	}

	Key* resolve(HKEY hkey);

	Key* walk(Key* key, const char16_t* subKey, bool create)
	{
		if (!key || !subKey || !*subKey)
			return key;
		std::u16string_view path{subKey};
		while (!path.empty() && key)
		{
			const size_t separator = path.find(u'\\');
			const std::u16string part{path.substr(0, separator)};
			path = separator == std::u16string_view::npos ? std::u16string_view{} : path.substr(separator + 1);
			if (part.empty())
				continue;
			auto it = key->_subkeys.find(part);
			if (it == key->_subkeys.end())
			{
				if (!create)
					return nullptr;
				it = key->_subkeys.emplace(part, std::make_unique<Key>()).first;
			}
			key = it->second.get();
		}
		return key;
	}

	void seed()
	{
		static bool seeded = false;
		if (seeded)
			return;
		seeded = true;

		Key* hklm = root(HKEY_LOCAL_MACHINE);
		Key* currentVersion = walk(hklm, u"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", true);
		// What Help > Debug Info reports as the OS: macOS, its version and its build. (Version checks
		// go through RtlGetNtVersionNumbers, which still answers Windows 10.)
		const std::u16string build = nppmac::toUtf16(nppmac::platform::systemBuild());
		currentVersion->_values[u"ProductName"] = stringValue(u"macOS");
		currentVersion->_values[u"CurrentBuild"] = stringValue(build);
		currentVersion->_values[u"CurrentBuildNumber"] = stringValue(build);
		currentVersion->_values[u"DisplayVersion"] = stringValue(nppmac::toUtf16(nppmac::platform::systemVersion()));
		currentVersion->_values[u"InstallationType"] = stringValue(u"Client");

		// The graphics processors, where Help > Debug Info lists display adapters.
		const std::vector<std::string> adapters = nppmac::platform::graphicsAdapters();
		for (size_t i = 0; i < adapters.size(); ++i)
		{
			char16_t number[8]{};
			number[0] = u'0';
			number[1] = u'0';
			number[2] = static_cast<char16_t>(u'0' + i / 10 % 10);
			number[3] = static_cast<char16_t>(u'0' + i % 10);
			const std::u16string path = u"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E968-E325-11CE-BFC1-08002BE10318}\\" + std::u16string{number};
			Key* adapter = walk(hklm, path.c_str(), true);
			adapter->_values[u"DriverDesc"] = stringValue(nppmac::toUtf16(adapters[i]));
		}
		currentVersion->_values[u"EditionID"] = stringValue(u"Professional");
		currentVersion->_values[u"CurrentMajorVersionNumber"] = dwordValue(10);
		currentVersion->_values[u"CurrentMinorVersionNumber"] = dwordValue(0);
		currentVersion->_values[u"UBR"] = dwordValue(0);

		// A stable per-machine identifier, as Windows keeps under Cryptography\MachineGuid.
		uuid_t hostId{};
		timespec wait{};
		char guid[40]{};
		if (gethostuuid(hostId, &wait) == 0)
		{
			std::snprintf(guid, sizeof(guid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				hostId[0], hostId[1], hostId[2], hostId[3], hostId[4], hostId[5], hostId[6], hostId[7],
				hostId[8], hostId[9], hostId[10], hostId[11], hostId[12], hostId[13], hostId[14], hostId[15]);
		}
		walk(hklm, u"SOFTWARE\\Microsoft\\Cryptography", true)->_values[u"MachineGuid"] = stringValue(nppmac::toUtf16(guid));

		walk(root(HKEY_CURRENT_USER), u"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", true);
	}

	// Values computed on every read.
	bool dynamicValue(Key* key, const std::u16string& name, Value& value)
	{
		static Key* personalize = walk(root(HKEY_CURRENT_USER), u"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", true);
		if (key == personalize && (_wcsicmp(name.c_str(), u"AppsUseLightTheme") == 0 || _wcsicmp(name.c_str(), u"SystemUsesLightTheme") == 0))
		{
			value = dwordValue(nppmac::platform::isDarkAppearance() ? 0 : 1);
			return true;
		}
		return false;
	}

	// Open keys are Key pointers handed out as HKEYs; the roots are the predefined constants.
	std::map<uintptr_t, Key*>& openKeys()
	{
		static auto* map = new std::map<uintptr_t, Key*>;
		return *map;
	}

	Key* resolve(HKEY hkey)
	{
		seed();
		if (Key* predefined = root(hkey))
			return predefined;
		auto it = openKeys().find(reinterpret_cast<uintptr_t>(hkey));
		return it == openKeys().end() ? nullptr : it->second;
	}

	HKEY openHandle(Key* key)
	{
		openKeys()[reinterpret_cast<uintptr_t>(key)] = key;
		return reinterpret_cast<HKEY>(key);
	}

	LSTATUS readValue(Key* key, LPCWSTR valueName, LPDWORD type, LPBYTE data, LPDWORD size)
	{
		const std::u16string name = valueName ? valueName : u"";
		Value dynamic;
		const Value* value = nullptr;
		if (dynamicValue(key, name, dynamic))
		{
			value = &dynamic;
		}
		else
		{
			auto it = key->_values.find(name);
			if (it == key->_values.end())
				return ERROR_FILE_NOT_FOUND;
			value = &it->second;
		}
		if (type)
			*type = value->_type;
		if (!size)
			return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
		const DWORD needed = static_cast<DWORD>(value->_data.size());
		if (data && *size < needed)
		{
			*size = needed;
			return ERROR_MORE_DATA;
		}
		if (data)
			std::copy(value->_data.begin(), value->_data.end(), data);
		*size = needed;
		return ERROR_SUCCESS;
	}
}

LSTATUS WINAPI RegOpenKeyExW(HKEY hkey, LPCWSTR subKey, DWORD, REGSAM, PHKEY result)
{
	std::lock_guard lock{g_mutex};
	Key* key = walk(resolve(hkey), subKey, false);
	if (!key)
		return ERROR_FILE_NOT_FOUND;
	*result = openHandle(key);
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegOpenKeyExA(HKEY hkey, LPCSTR subKey, DWORD options, REGSAM sam, PHKEY result)
{
	return RegOpenKeyExW(hkey, subKey ? nppmac::toUtf16(subKey).c_str() : nullptr, options, sam, result);
}

LSTATUS WINAPI RegCreateKeyExW(HKEY hkey, LPCWSTR subKey, DWORD, LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY result, LPDWORD disposition)
{
	std::lock_guard lock{g_mutex};
	Key* parent = resolve(hkey);
	const bool existed = walk(parent, subKey, false) != nullptr;
	Key* key = walk(parent, subKey, true);
	if (!key)
		return ERROR_INVALID_HANDLE;
	*result = openHandle(key);
	if (disposition)
		*disposition = existed ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegCloseKey(HKEY)
{
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegQueryValueExW(HKEY hkey, LPCWSTR valueName, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD size)
{
	std::lock_guard lock{g_mutex};
	Key* key = resolve(hkey);
	return key ? readValue(key, valueName, type, data, size) : ERROR_INVALID_HANDLE;
}

LSTATUS WINAPI RegQueryValueExA(HKEY hkey, LPCSTR valueName, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
	// String values are returned converted to UTF-8.
	DWORD valueType = 0;
	DWORD wideSize = 0;
	const std::u16string name = valueName ? nppmac::toUtf16(valueName) : std::u16string{};
	LSTATUS status = RegQueryValueExW(hkey, name.c_str(), reserved, &valueType, nullptr, &wideSize);
	if (status != ERROR_SUCCESS)
		return status;
	std::vector<BYTE> wide(wideSize);
	RegQueryValueExW(hkey, name.c_str(), reserved, &valueType, wide.data(), &wideSize);
	if (type)
		*type = valueType;
	std::vector<BYTE> converted = wide;
	if (valueType == REG_SZ || valueType == REG_EXPAND_SZ)
	{
		const std::string narrow = nppmac::toUtf8(reinterpret_cast<const char16_t*>(wide.data()));
		converted.assign(narrow.begin(), narrow.end());
		converted.push_back(0);
	}
	if (!size)
		return ERROR_SUCCESS;
	if (data && *size < converted.size())
	{
		*size = static_cast<DWORD>(converted.size());
		return ERROR_MORE_DATA;
	}
	if (data)
		std::copy(converted.begin(), converted.end(), data);
	*size = static_cast<DWORD>(converted.size());
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegGetValueW(HKEY hkey, LPCWSTR subKey, LPCWSTR valueName, DWORD flags, LPDWORD type, PVOID data, LPDWORD size)
{
	std::lock_guard lock{g_mutex};
	Key* key = walk(resolve(hkey), subKey, false);
	if (!key)
		return ERROR_FILE_NOT_FOUND;
	DWORD valueType = 0;
	LSTATUS status = readValue(key, valueName, &valueType, static_cast<LPBYTE>(data), size);
	if (status != ERROR_SUCCESS)
		return status;
	const bool typeAllowed = ((flags & RRF_RT_REG_SZ) && valueType == REG_SZ) || ((flags & RRF_RT_REG_DWORD) && valueType == REG_DWORD)
		|| ((flags & RRF_RT_REG_QWORD) && valueType == REG_QWORD) || ((flags & RRF_RT_REG_EXPAND_SZ) && valueType == REG_EXPAND_SZ)
		|| ((flags & RRF_RT_REG_BINARY) && valueType == REG_BINARY) || ((flags & RRF_RT_REG_MULTI_SZ) && valueType == REG_MULTI_SZ)
		|| (flags & RRF_RT_ANY) == RRF_RT_ANY;
	if (!typeAllowed)
		return ERROR_UNSUPPORTED_TYPE;
	if (type)
		*type = valueType;
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegSetValueExW(HKEY hkey, LPCWSTR valueName, DWORD, DWORD type, const BYTE* data, DWORD size)
{
	std::lock_guard lock{g_mutex};
	Key* key = resolve(hkey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	Value& value = key->_values[valueName ? valueName : u""];
	value._type = type;
	value._data.assign(data, data + size);
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDeleteValueW(HKEY hkey, LPCWSTR valueName)
{
	std::lock_guard lock{g_mutex};
	Key* key = resolve(hkey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	return key->_values.erase(valueName ? valueName : u"") ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

LSTATUS WINAPI RegDeleteKeyW(HKEY hkey, LPCWSTR subKey)
{
	std::lock_guard lock{g_mutex};
	const std::u16string path{subKey ? subKey : u""};
	const size_t separator = path.find_last_of(u'\\');
	Key* parent = separator == std::u16string::npos ? resolve(hkey) : walk(resolve(hkey), path.substr(0, separator).c_str(), false);
	if (!parent)
		return ERROR_FILE_NOT_FOUND;
	const std::u16string name = separator == std::u16string::npos ? path : path.substr(separator + 1);
	auto it = parent->_subkeys.find(name);
	if (it == parent->_subkeys.end())
		return ERROR_FILE_NOT_FOUND;
	if (!it->second->_subkeys.empty())
		return ERROR_ACCESS_DENIED;
	openKeys().erase(reinterpret_cast<uintptr_t>(it->second.get()));
	parent->_subkeys.erase(it);
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegEnumKeyExW(HKEY hkey, DWORD index, LPWSTR name, LPDWORD nameSize, LPDWORD, LPWSTR, LPDWORD, PFILETIME)
{
	std::lock_guard lock{g_mutex};
	Key* key = resolve(hkey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	if (index >= key->_subkeys.size())
		return ERROR_NO_MORE_ITEMS;
	auto it = std::next(key->_subkeys.begin(), index);
	if (it->first.size() + 1 > *nameSize)
		return ERROR_MORE_DATA;
	*nameSize = nppmac::kernel::copyToBuffer(it->first, name, *nameSize);
	return ERROR_SUCCESS;
}

LSTATUS WINAPI RegQueryInfoKeyW(HKEY hkey, LPWSTR, LPDWORD, LPDWORD, LPDWORD subKeys, LPDWORD maxSubKeyLength, LPDWORD,
	LPDWORD values, LPDWORD maxValueNameLength, LPDWORD maxValueLength, LPDWORD, PFILETIME lastWrite)
{
	std::lock_guard lock{g_mutex};
	Key* key = resolve(hkey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	if (subKeys)
		*subKeys = static_cast<DWORD>(key->_subkeys.size());
	if (values)
		*values = static_cast<DWORD>(key->_values.size());
	if (maxSubKeyLength)
	{
		*maxSubKeyLength = 0;
		for (const auto& [name, subKey] : key->_subkeys)
			*maxSubKeyLength = std::max<DWORD>(*maxSubKeyLength, static_cast<DWORD>(name.size()));
	}
	if (maxValueNameLength || maxValueLength)
	{
		DWORD nameLength = 0;
		DWORD valueLength = 0;
		for (const auto& [name, value] : key->_values)
		{
			nameLength = std::max<DWORD>(nameLength, static_cast<DWORD>(name.size()));
			valueLength = std::max<DWORD>(valueLength, static_cast<DWORD>(value._data.size()));
		}
		if (maxValueNameLength)
			*maxValueNameLength = nameLength;
		if (maxValueLength)
			*maxValueLength = valueLength;
	}
	if (lastWrite)
		*lastWrite = nppmac::kernel::currentFileTime();
	return ERROR_SUCCESS;
}
