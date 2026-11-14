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

// uxtheme and the task dialog (signatures as in the Wine headers): visual styles are off, so OpenThemeData finds no theme and callers
// draw classic controls, as they do on Windows with styles turned off. Every theme function fails
// cleanly for the rare caller that goes on without checking.

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

HTHEME WINAPI OpenThemeData(HWND, LPCWSTR)
{
	SetLastError(ERROR_NOT_SUPPORTED);
	return nullptr;
}

HRESULT WINAPI CloseThemeData(HTHEME)
{
	return S_OK;
}

HRESULT WINAPI SetWindowTheme(HWND, LPCWSTR, LPCWSTR)
{
	return S_OK;
}

HRESULT WINAPI EnableThemeDialogTexture(HWND, DWORD)
{
	return S_OK;
}

HRESULT WINAPI DrawThemeBackground(HTHEME, HDC, int, int, const RECT*, const RECT*)
{
	return E_NOTIMPL;
}

HRESULT WINAPI DrawThemeParentBackground(HWND, HDC, RECT*)
{
	return E_NOTIMPL;
}

HRESULT WINAPI DrawThemeTextEx(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, LPRECT, const DTTOPTS*)
{
	return E_NOTIMPL;
}

HRESULT WINAPI GetThemeBackgroundContentRect(HTHEME, HDC, int, int, LPCRECT bounds, LPRECT content)
{
	if (bounds && content)
		*content = *bounds;
	return E_NOTIMPL;
}

HRESULT WINAPI GetThemeColor(HTHEME, int, int, int, COLORREF* color)
{
	if (color)
		*color = 0;
	return E_NOTIMPL;
}

HRESULT WINAPI GetThemeFont(HTHEME, HDC, int, int, int, LOGFONTW* font)
{
	if (font)
		*font = LOGFONTW{};
	return E_NOTIMPL;
}

HRESULT WINAPI GetThemePartSize(HTHEME, HDC, int, int, RECT*, THEMESIZE, SIZE* size)
{
	if (size)
		*size = SIZE{};
	return E_NOTIMPL;
}

HRESULT WINAPI GetThemeTransitionDuration(HTHEME, int, int, int, int, DWORD* duration)
{
	if (duration)
		*duration = 0;
	return E_NOTIMPL;
}

// Buffered animation is never started, so painting always takes the plain path.
HANIMATIONBUFFER WINAPI BeginBufferedAnimation(HWND, HDC, const RECT*, BP_BUFFERFORMAT, BP_PAINTPARAMS*, BP_ANIMATIONPARAMS*, HDC* from, HDC* to)
{
	if (from)
		*from = nullptr;
	if (to)
		*to = nullptr;
	return nullptr;
}

HRESULT WINAPI EndBufferedAnimation(HANIMATIONBUFFER, BOOL)
{
	return S_OK;
}

BOOL WINAPI BufferedPaintRenderAnimation(HWND, HDC)
{
	return FALSE;
}

HRESULT WINAPI BufferedPaintStopAllAnimations(HWND)
{
	return S_OK;
}

HRESULT WINAPI TaskDialogIndirect(const TASKDIALOGCONFIG*, int* button, int* radioButton, BOOL* verificationChecked)
{
	if (button)
		*button = 0;
	if (radioButton)
		*radioButton = 0;
	if (verificationChecked)
		*verificationChecked = FALSE;
	return E_NOTIMPL;
}
