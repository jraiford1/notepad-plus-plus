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

// gdi32 printing: a print job draws its pages into a PDF, which is printed when the job ends.

#include "Gdi.h"

#include <cstdlib>
#include <unistd.h>

using namespace nppmac::gdi;

nppmac::gdi::PrintDocument::~PrintDocument()
{
	if (_pdf)
		_pdf->close();
	if (!_path.empty())
		::unlink(_path.c_str());
}

namespace
{
	std::shared_ptr<PrintDocument> printDocumentOf(HDC hdc)
	{
		DC* dc = toDC(hdc);
		return dc && dc->_kind == DCKind::printer ? dc->_print : nullptr;
	}
}

int WINAPI StartDocW(HDC hdc, const DOCINFOW* info)
{
	std::shared_ptr<PrintDocument> document = printDocumentOf(hdc);
	if (!document || document->_pdf)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return SP_ERROR;
	}
	const char* folder = std::getenv("TMPDIR");
	std::string path = std::string{folder && *folder ? folder : "/tmp/"} + "/npp-print-XXXXXX.pdf";
	const int fd = ::mkstemps(path.data(), 4);
	if (fd < 0)
		return SP_ERROR;
	::close(fd);
	document->_path = path;
	document->_name = info && info->lpszDocName ? std::u16string{info->lpszDocName} : std::u16string{u"Notepad++"};
	document->_pdf = createPdf(path, document->_setup._paperWidth, document->_setup._paperHeight, document->_name);
	return document->_pdf ? 1 : SP_ERROR;
}

int WINAPI StartPage(HDC hdc)
{
	std::shared_ptr<PrintDocument> document = printDocumentOf(hdc);
	if (!document || !document->_pdf || document->_page)
		return SP_ERROR;
	Canvas* page = document->_pdf->beginPage();
	if (!page)
		return SP_ERROR;
	// The DC draws as every DC does: top-left origin at the printable area, in device units.
	page->save();
	page->translate(document->_setup._printableLeft, document->_setup._printableTop);
	page->scale(1 / printerUnitsPerPoint, 1 / printerUnitsPerPoint);
	document->_page = page;
	toDC(hdc)->_canvas = page;
	return 1;
}

int WINAPI EndPage(HDC hdc)
{
	std::shared_ptr<PrintDocument> document = printDocumentOf(hdc);
	if (!document || !document->_page)
		return SP_ERROR;
	toDC(hdc)->_canvas = nullptr;
	document->_page->restore();
	document->_pdf->endPage();
	document->_page = nullptr;
	return 1;
}

int WINAPI EndDoc(HDC hdc)
{
	std::shared_ptr<PrintDocument> document = printDocumentOf(hdc);
	if (!document || !document->_pdf)
		return SP_ERROR;
	if (document->_page)
		EndPage(hdc);
	document->_pdf->close();
	document->_pdf.reset();
	const bool printed = nppmac::platform::printPdf(document->_path, nppmac::toUtf8(document->_name));
	::unlink(document->_path.c_str());
	document->_path.clear();
	return printed ? 1 : SP_ERROR;
}

int WINAPI AbortDoc(HDC hdc)
{
	DC* dc = toDC(hdc);
	if (!dc || dc->_kind != DCKind::printer)
		return SP_ERROR;
	// A new document for the same printer, the abandoned one discarded.
	auto fresh = std::make_shared<PrintDocument>();
	fresh->_setup = dc->_print->_setup;
	dc->_canvas = nullptr;
	dc->_print = fresh;
	return 1;
}
