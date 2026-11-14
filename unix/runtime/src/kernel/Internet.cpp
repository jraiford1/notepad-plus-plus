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

// wininet: URL parsing (InternetCrackUrlW). Notepad++ uses it to recognize URLs, not to connect.

#include <windows.h>
#include <wininet.h>

#include <algorithm>
#include <string>

namespace
{
	// Fills one component: either a pointer into the URL (buffer is null, length non-zero) or a copy.
	bool setComponent(LPWSTR& buffer, DWORD& length, std::u16string_view value, LPCWSTR urlStart, size_t offset)
	{
		if (length == 0 && !buffer)
			return true;
		if (!buffer)
		{
			buffer = const_cast<LPWSTR>(urlStart + offset);
			length = static_cast<DWORD>(value.size());
			return true;
		}
		if (value.size() + 1 > length)
		{
			length = static_cast<DWORD>(value.size() + 1);
			return false;
		}
		std::copy(value.begin(), value.end(), buffer);
		buffer[value.size()] = u'\0';
		length = static_cast<DWORD>(value.size());
		return true;
	}
}

BOOL WINAPI InternetCrackUrlW(LPCWSTR url, DWORD urlLength, DWORD, LPURL_COMPONENTSW components)
{
	const std::u16string_view text{url, urlLength ? urlLength : wcslen(url)};
	const size_t schemeEnd = text.find(u':');
	if (schemeEnd == std::u16string_view::npos || schemeEnd == 0)
	{
		SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME);
		return FALSE;
	}
	const std::u16string_view scheme = text.substr(0, schemeEnd);
	size_t pos = schemeEnd + 1;
	std::u16string_view user;
	std::u16string_view password;
	std::u16string_view host;
	size_t hostOffset = pos;
	size_t userOffset = pos;
	size_t passwordOffset = pos;
	INTERNET_PORT port = 0;
	if (text.substr(pos).starts_with(u"//"))
	{
		pos += 2;
		const size_t authorityEnd = std::min(text.find_first_of(u"/?#", pos), text.size());
		std::u16string_view authority = text.substr(pos, authorityEnd - pos);
		const size_t at = authority.rfind(u'@');
		hostOffset = pos;
		if (at != std::u16string_view::npos)
		{
			const std::u16string_view credentials = authority.substr(0, at);
			const size_t colon = credentials.find(u':');
			user = credentials.substr(0, colon);
			userOffset = pos;
			if (colon != std::u16string_view::npos)
			{
				password = credentials.substr(colon + 1);
				passwordOffset = pos + colon + 1;
			}
			authority = authority.substr(at + 1);
			hostOffset = pos + at + 1;
		}
		const size_t colon = authority.rfind(u':');
		if (colon != std::u16string_view::npos && authority.find(u']') == std::u16string_view::npos)
		{
			port = static_cast<INTERNET_PORT>(nppmac::stoi(std::u16string{authority.substr(colon + 1)}));
			authority = authority.substr(0, colon);
		}
		host = authority;
		pos = authorityEnd;
	}
	const size_t queryStart = std::min(text.find(u'?', pos), text.size());
	const std::u16string_view path = text.substr(pos, queryStart - pos);
	const std::u16string_view extra = text.substr(queryStart);

	components->nScheme = scheme == u"http" ? INTERNET_SCHEME_HTTP : (scheme == u"https" ? INTERNET_SCHEME_HTTPS
		: (scheme == u"ftp" ? INTERNET_SCHEME_FTP : (scheme == u"file" ? INTERNET_SCHEME_FILE : INTERNET_SCHEME_UNKNOWN)));
	if (port == 0)
		port = components->nScheme == INTERNET_SCHEME_HTTPS ? 443 : (components->nScheme == INTERNET_SCHEME_HTTP ? 80 : (components->nScheme == INTERNET_SCHEME_FTP ? 21 : 0));
	components->nPort = port;

	bool ok = setComponent(components->lpszScheme, components->dwSchemeLength, scheme, url, 0);
	ok = setComponent(components->lpszHostName, components->dwHostNameLength, host, url, hostOffset) && ok;
	ok = setComponent(components->lpszUserName, components->dwUserNameLength, user, url, userOffset) && ok;
	ok = setComponent(components->lpszPassword, components->dwPasswordLength, password, url, passwordOffset) && ok;
	ok = setComponent(components->lpszUrlPath, components->dwUrlPathLength, path, url, pos) && ok;
	ok = setComponent(components->lpszExtraInfo, components->dwExtraInfoLength, extra, url, queryStart) && ok;
	if (!ok)
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
	return ok ? TRUE : FALSE;
}
