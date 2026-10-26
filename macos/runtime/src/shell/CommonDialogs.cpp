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

// comdlg32's color and print dialogs on the macOS color panel and print panel, and its Open and Save
// dialogs (GetOpenFileName, GetSaveFileName: plugins use them) on the Open and Save panels.

#include "gdi/Gdi.h" // first: Core Graphics comes before the Windows headers
#include "kernel/Internal.h"
#include "platform/Platform.h"

#include <windows.h>
#include <cderr.h>
#include <commdlg.h>

#include <algorithm>
#include <string>
#include <vector>

BOOL WINAPI ChooseColorW(LPCHOOSECOLORW choose)
{
	if (!choose)
		return FALSE;
	// COLORREF is 0x00BBGGRR; the panel takes 0xRRGGBB.
	const COLORREF initial = (choose->Flags & CC_RGBINIT) ? choose->rgbResult : RGB(0, 0, 0);
	unsigned rgb = (static_cast<unsigned>(GetRValue(initial)) << 16) | (static_cast<unsigned>(GetGValue(initial)) << 8) | GetBValue(initial);
	// The panel is modal, like the Windows dialog, so the owner is disabled meanwhile.
	const bool ownerWasEnabled = choose->hwndOwner && IsWindowEnabled(choose->hwndOwner);
	if (ownerWasEnabled)
		EnableWindow(choose->hwndOwner, FALSE);
	const bool chosen = nppmac::platform::chooseColor(rgb);
	if (ownerWasEnabled)
		EnableWindow(choose->hwndOwner, TRUE);
	if (!chosen)
		return FALSE;
	choose->rgbResult = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return TRUE;
}

BOOL WINAPI PrintDlgW(LPPRINTDLGW print)
{
	if (!print)
		return FALSE;
	nppmac::platform::PrintSetup setup;
	setup._offerSelection = (print->Flags & PD_SELECTION) && !(print->Flags & PD_NOSELECTION);
	const bool showPanel = !(print->Flags & PD_RETURNDEFAULT);
	// The panel is modal, like the Windows dialog, so the owner is disabled meanwhile.
	const bool ownerWasEnabled = showPanel && print->hwndOwner && IsWindowEnabled(print->hwndOwner);
	if (ownerWasEnabled)
		EnableWindow(print->hwndOwner, FALSE);
	const bool chosen = nppmac::platform::choosePrinter(setup, showPanel);
	if (ownerWasEnabled)
		EnableWindow(print->hwndOwner, TRUE);
	if (!chosen)
		return FALSE;

	print->Flags &= ~(PD_SELECTION | PD_PAGENUMS);
	if (setup._selectionOnly)
		print->Flags |= PD_SELECTION;
	else if (setup._pageRange)
	{
		print->Flags |= PD_PAGENUMS;
		print->nFromPage = static_cast<WORD>(setup._firstPage);
		print->nToPage = static_cast<WORD>(std::min(setup._lastPage, 0xFFFF));
	}
	// Copies are the printer's (PD_USEDEVMODECOPIES): the program prints one.
	print->nCopies = 1;
	if (print->Flags & PD_RETURNDC)
		print->hDC = nppmac::gdi::createPrinterDC(setup);
	return TRUE;
}

// --- Open and Save -------------------------------------------------------------------------------

namespace
{
	DWORD g_dialogError = 0;

	// A filter, "Name\0Patterns\0...\0\0", as its names (the panel's file type menu).
	std::vector<std::string> filterNames(LPCWSTR filter)
	{
		std::vector<std::string> names;
		for (LPCWSTR entry = filter; entry && *entry;)
		{
			const std::u16string name{entry};
			entry += name.size() + 1;
			const size_t patterns = std::u16string_view{entry}.size();
			entry += patterns + 1;
			names.push_back(nppmac::toUtf8(name));
		}
		return names;
	}

	size_t fileNameOffset(std::u16string_view path)
	{
		const size_t separator = path.find_last_of(u"/\\");
		return separator == std::u16string_view::npos ? 0 : separator + 1;
	}

	// The result as Windows writes it into lpstrFile: a path, or for several files their folder, then
	// each name, each ended by a 0, then a 0. Fails with FNERR_BUFFERTOOSMALL when it does not fit,
	// the size it needs in the buffer's first WORD.
	bool writeResult(LPOPENFILENAMEW dialog, const std::vector<std::u16string>& paths)
	{
		std::u16string result;
		if (paths.size() == 1)
		{
			result = paths[0];
			result += u'\0';
		}
		else
		{
			const std::u16string& first = paths[0];
			const size_t offset = fileNameOffset(first);
			result = first.substr(0, offset > 1 ? offset - 1 : offset);
			result += u'\0';
			for (const std::u16string& path : paths)
			{
				result += path.substr(fileNameOffset(path));
				result += u'\0';
			}
		}
		result += u'\0';
		if (result.size() > dialog->nMaxFile)
		{
			g_dialogError = FNERR_BUFFERTOOSMALL;
			if (dialog->nMaxFile >= 1)
				dialog->lpstrFile[0] = static_cast<WCHAR>(std::min<size_t>(result.size(), 0xFFFF));
			return false;
		}
		std::copy(result.begin(), result.end(), dialog->lpstrFile);

		const std::u16string& path = paths[0];
		const size_t offset = fileNameOffset(path);
		// The first name's place: after the folder, for several files.
		dialog->nFileOffset = static_cast<WORD>(paths.size() == 1 ? offset : result.find(u'\0') + 1);
		dialog->nFileExtension = 0;
		if (paths.size() == 1)
		{
			// The extension's offset, or the end's when the name has none.
			const size_t dot = path.find_last_of(u'.');
			dialog->nFileExtension = static_cast<WORD>(dot != std::u16string::npos && dot > offset ? dot + 1 : path.size());
		}
		if (dialog->lpstrFileTitle && dialog->nMaxFileTitle > 0)
		{
			const std::u16string title = path.substr(fileNameOffset(path));
			const size_t count = std::min<size_t>(title.size(), dialog->nMaxFileTitle - 1);
			std::copy_n(title.begin(), count, dialog->lpstrFileTitle);
			dialog->lpstrFileTitle[count] = u'\0';
		}
		return true;
	}

	BOOL runFileDialog(LPOPENFILENAMEW dialog, bool save)
	{
		g_dialogError = 0;
		if (!dialog || !dialog->lpstrFile || dialog->nMaxFile == 0)
		{
			g_dialogError = CDERR_INITIALIZATION;
			return FALSE;
		}
		nppmac::platform::FilePanelRequest request;
		request._save = save;
		request._multipleSelection = !save && (dialog->Flags & OFN_ALLOWMULTISELECT);
		if (dialog->lpstrTitle)
			request._title = nppmac::toUtf8(dialog->lpstrTitle);
		// The buffer may hold a name to start with, or a path.
		const std::u16string initial{dialog->lpstrFile};
		const size_t nameOffset = fileNameOffset(initial);
		request._fileName = nppmac::toUtf8(initial.substr(nameOffset));
		if (dialog->lpstrInitialDir && *dialog->lpstrInitialDir)
			request._directory = nppmac::kernel::toPosixPath(dialog->lpstrInitialDir);
		else if (nameOffset > 0)
			request._directory = nppmac::kernel::toPosixPath(initial.substr(0, nameOffset));
		request._fileTypeNames = filterNames(dialog->lpstrFilter);
		request._fileTypeIndex = dialog->nFilterIndex > 0 ? static_cast<int>(dialog->nFilterIndex) - 1 : 0;

		// The panel is modal, like the Windows dialog, so the owner is disabled meanwhile.
		const bool ownerWasEnabled = dialog->hwndOwner && IsWindowEnabled(dialog->hwndOwner);
		if (ownerWasEnabled)
			EnableWindow(dialog->hwndOwner, FALSE);
		const nppmac::platform::FilePanelResult result = nppmac::platform::runFilePanel(request);
		if (ownerWasEnabled)
			EnableWindow(dialog->hwndOwner, TRUE);
		if (!result._ok || result._paths.empty())
			return FALSE;

		std::vector<std::u16string> paths;
		for (const std::string& path : result._paths)
			paths.push_back(nppmac::kernel::fromPosixPath(path));
		// A name typed without an extension gets the default one, as Windows gives it.
		if (save && dialog->lpstrDefExt && *dialog->lpstrDefExt)
		{
			std::u16string& path = paths[0];
			if (path.find(u'.', fileNameOffset(path)) == std::u16string::npos)
				path += u"." + std::u16string{dialog->lpstrDefExt};
		}
		if (dialog->lpstrFilter)
			dialog->nFilterIndex = static_cast<DWORD>(result._fileTypeIndex) + 1;
		return writeResult(dialog, paths) ? TRUE : FALSE;
	}
}

BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW dialog)
{
	return runFileDialog(dialog, false);
}

BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW dialog)
{
	return runFileDialog(dialog, true);
}

DWORD WINAPI CommDlgExtendedError()
{
	return g_dialogError;
}
