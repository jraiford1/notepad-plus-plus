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

// comdlg32's color and print dialogs on the macOS color panel and print panel.

#include "gdi/Gdi.h" // first: Core Graphics comes before the Windows headers
#include "platform/Platform.h"

#include <windows.h>
#include <commdlg.h>

#include <algorithm>

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
