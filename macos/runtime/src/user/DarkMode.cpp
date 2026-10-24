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

// Windows' undocumented dark-mode functions, which Notepad++ (like other Win32 programs) finds by
// ordinal in uxtheme.dll and by name in user32.dll, on macOS appearances: the application's
// preferred mode is NSApp's appearance, so menus, panels and alerts follow it, and a window's
// dark title bar is its NSWindow's appearance.

#include "DarkMode.h"
#include "Window.h"
#include "platform/Platform.h"

#include <cstring>
#include <dwmapi.h>
#include <unordered_map>
#include <uxtheme.h>

namespace nppmac::user::darkmode
{
	namespace
	{
		enum PreferredAppMode
		{
			appModeDefault,
			appModeAllowDark,
			appModeForceDark,
			appModeForceLight,
			appModeMax,
		};

		enum WindowCompositionAttribute
		{
			wcaUseDarkModeColors = 26,
		};

		struct WindowCompositionAttributeData
		{
			int _attribute;
			void* _data;
			SIZE_T _size;
		};

		PreferredAppMode g_appMode = appModeDefault;

		std::unordered_map<HWND, bool>& allowedWindows()
		{
			static auto* windows = new std::unordered_map<HWND, bool>;
			return *windows;
		}

		void setTitleBarDark(HWND hwnd, bool dark)
		{
			Window* window = fromHandle(hwnd);
			if (window && window->isTopLevel())
				ui::setWindowAppearance(window->_view, dark ? ui::Appearance::dark : ui::Appearance::system);
		}

		// uxtheme #49
		HTHEME WINAPI openNcThemeData(HWND hwnd, LPCWSTR classList)
		{
			return OpenThemeData(hwnd, classList);
		}

		// uxtheme #104
		void WINAPI refreshImmersiveColorPolicyState()
		{
		}

		// uxtheme #106
		bool WINAPI getIsImmersiveColorUsingHighContrast(int)
		{
			return false;
		}

		// uxtheme #132
		bool WINAPI shouldAppsUseDarkMode()
		{
			return platform::isDarkAppearance();
		}

		// uxtheme #133
		bool WINAPI allowDarkModeForWindow(HWND hwnd, bool allow)
		{
			auto& windows = allowedWindows();
			const bool previous = windows.count(hwnd) ? windows[hwnd] : false;
			windows[hwnd] = allow;
			return previous;
		}

		// uxtheme #135 (Windows 10 1903 and later): the application's light or dark preference.
		PreferredAppMode WINAPI setPreferredAppMode(PreferredAppMode mode)
		{
			const PreferredAppMode previous = g_appMode;
			g_appMode = mode;
			switch (mode)
			{
				case appModeForceDark:
					ui::setApplicationAppearance(ui::Appearance::dark);
					break;
				case appModeForceLight:
					ui::setApplicationAppearance(ui::Appearance::light);
					break;
				default:
					ui::setApplicationAppearance(ui::Appearance::system);
					break;
			}
			return previous;
		}

		// uxtheme #136
		void WINAPI flushMenuThemes()
		{
		}

		// uxtheme #137
		bool WINAPI isDarkModeAllowedForWindow(HWND hwnd)
		{
			auto& windows = allowedWindows();
			return windows.count(hwnd) ? windows[hwnd] : false;
		}

		// uxtheme #138
		bool WINAPI shouldSystemUseDarkMode()
		{
			return platform::isDarkAppearance();
		}

		// uxtheme #139
		bool WINAPI isDarkModeAllowedForApp()
		{
			return g_appMode == appModeAllowDark || g_appMode == appModeForceDark;
		}

		// user32
		BOOL WINAPI setWindowCompositionAttribute(HWND hwnd, WindowCompositionAttributeData* data)
		{
			if (!data || data->_attribute != wcaUseDarkModeColors || !data->_data || data->_size < sizeof(BOOL))
				return FALSE;
			setTitleBarDark(hwnd, *static_cast<const BOOL*>(data->_data) != FALSE);
			return TRUE;
		}
	}

	FARPROC uxthemeOrdinal(WORD ordinal)
	{
		switch (ordinal)
		{
			case 49:
				return reinterpret_cast<FARPROC>(&openNcThemeData);
			case 104:
				return reinterpret_cast<FARPROC>(&refreshImmersiveColorPolicyState);
			case 106:
				return reinterpret_cast<FARPROC>(&getIsImmersiveColorUsingHighContrast);
			case 132:
				return reinterpret_cast<FARPROC>(&shouldAppsUseDarkMode);
			case 133:
				return reinterpret_cast<FARPROC>(&allowDarkModeForWindow);
			case 135:
				return reinterpret_cast<FARPROC>(&setPreferredAppMode);
			case 136:
				return reinterpret_cast<FARPROC>(&flushMenuThemes);
			case 137:
				return reinterpret_cast<FARPROC>(&isDarkModeAllowedForWindow);
			case 138:
				return reinterpret_cast<FARPROC>(&shouldSystemUseDarkMode);
			case 139:
				return reinterpret_cast<FARPROC>(&isDarkModeAllowedForApp);
			default:
				return nullptr;
		}
	}

	FARPROC user32Export(const char* name)
	{
		if (std::strcmp(name, "SetWindowCompositionAttribute") == 0)
			return reinterpret_cast<FARPROC>(&setWindowCompositionAttribute);
		return nullptr;
	}
}

// DWMWA_USE_IMMERSIVE_DARK_MODE gives a window a dark title bar.
HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD attribute, LPCVOID value, DWORD size)
{
	constexpr DWORD useImmersiveDarkModeBefore20H1 = 19;
	if ((attribute == DWMWA_USE_IMMERSIVE_DARK_MODE || attribute == useImmersiveDarkModeBefore20H1) && value && size >= sizeof(BOOL))
		nppmac::user::darkmode::setTitleBarDark(hwnd, *static_cast<const BOOL*>(value) != FALSE);
	return S_OK;
}
