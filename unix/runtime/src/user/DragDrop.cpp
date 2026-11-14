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

// OLE drag and drop, for files: a window's drop target (RegisterDragDrop) gets the files let go over
// it, as a data object with their HDROP (CF_HDROP), the only format a drop from Finder brings here.

#include "Window.h"

#include <shlobj.h>

#include <cstring>
#include <utility>

namespace
{
	// The files dropped, as data in one format: their HDROP, in global memory.
	class FileDropData final : public IDataObject
	{
	public:
		explicit FileDropData(HGLOBAL drop) : _drop{drop} {}

		~FileDropData()
		{
			GlobalFree(_drop);
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
		{
			if (!object)
				return E_POINTER;
			if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject))
			{
				*object = static_cast<IDataObject*>(this);
				AddRef();
				return S_OK;
			}
			*object = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG references = --_references;
			if (references == 0)
				delete this;
			return references;
		}

		HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
		{
			if (!medium)
				return E_INVALIDARG;
			if (const HRESULT available = QueryGetData(format); available != S_OK)
				return available;

			// A copy, which the caller frees (ReleaseStgMedium).
			const SIZE_T size = GlobalSize(_drop);
			HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
			if (!copy)
				return E_OUTOFMEMORY;
			std::memcpy(GlobalLock(copy), GlobalLock(_drop), size);
			GlobalUnlock(_drop);
			GlobalUnlock(copy);
			medium->tymed = TYMED_HGLOBAL;
			medium->hGlobal = copy;
			medium->pUnkForRelease = nullptr;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
		{
			if (!format)
				return E_INVALIDARG;
			if (format->cfFormat != CF_HDROP)
				return DV_E_FORMATETC;
			if (!(format->tymed & TYMED_HGLOBAL))
				return DV_E_TYMED;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* canonical) override
		{
			if (canonical)
				canonical->ptd = nullptr;
			return DATA_S_SAMEFORMATETC;
		}

		HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC** formats) override
		{
			if (formats)
				*formats = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
		{
			return OLE_E_ADVISENOTSUPPORTED;
		}

		HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
		{
			return OLE_E_ADVISENOTSUPPORTED;
		}

		HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
		{
			return OLE_E_ADVISENOTSUPPORTED;
		}

	private:
		HGLOBAL _drop;
		ULONG _references = 1;
	};
}

namespace nppmac::user
{
	void dropOnTarget(IDropTarget* target, HGLOBAL drop, POINT screenPosition)
	{
		// The drag enters and is let go at once: the hovering before is not reported (DragOver).
		auto* data = new FileDropData{drop};
		const POINTL position{screenPosition.x, screenPosition.y};
		DWORD effect = DROPEFFECT_COPY;
		target->AddRef();
		if (SUCCEEDED(target->DragEnter(data, MK_LBUTTON, position, &effect)) && effect != DROPEFFECT_NONE)
		{
			effect = DROPEFFECT_COPY;
			target->Drop(data, 0, position, &effect);
		}
		else
		{
			target->DragLeave();
		}
		target->Release();
		data->Release();
	}
}

using namespace nppmac::user;

HRESULT WINAPI RegisterDragDrop(HWND hwnd, IDropTarget* target)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return DRAGDROP_E_INVALIDHWND;
	if (!target)
		return E_INVALIDARG;
	if (window->_dropTarget)
		return DRAGDROP_E_ALREADYREGISTERED;
	target->AddRef();
	window->_dropTarget = target;
	nppmac::ui::setAcceptsFiles(window->_view, true);
	return S_OK;
}

HRESULT WINAPI RevokeDragDrop(HWND hwnd)
{
	Window* window = fromHandle(hwnd);
	if (!window)
		return DRAGDROP_E_INVALIDHWND;
	if (!window->_dropTarget)
		return DRAGDROP_E_NOTREGISTERED;
	std::exchange(window->_dropTarget, nullptr)->Release();
	nppmac::ui::setAcceptsFiles(window->_view, window->_acceptsFiles || (window->_exStyle & WS_EX_ACCEPTFILES));
	return S_OK;
}

void WINAPI ReleaseStgMedium(STGMEDIUM* medium)
{
	if (!medium)
		return;
	// Memory and GDI objects are the medium's own unless an object owns them (pUnkForRelease);
	// interfaces are released either way.
	switch (medium->tymed)
	{
		case TYMED_HGLOBAL:
			if (!medium->pUnkForRelease)
				GlobalFree(medium->hGlobal);
			break;
		case TYMED_GDI:
			if (!medium->pUnkForRelease)
				DeleteObject(medium->hBitmap);
			break;
		case TYMED_ISTREAM:
			if (medium->pstm)
				medium->pstm->Release();
			break;
		case TYMED_ISTORAGE:
			if (medium->pstg)
				medium->pstg->Release();
			break;
		default:
			break;
	}
	if (medium->pUnkForRelease)
		medium->pUnkForRelease->Release();
	medium->tymed = TYMED_NULL;
	medium->pUnkForRelease = nullptr;
}
