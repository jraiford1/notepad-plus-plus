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

// _com_ptr_t, the Microsoft compiler's COM smart pointer (comip.h), for the COM interfaces the
// runtime implements. Holds one reference; assigning a pointer of another interface type
// queries it with QueryInterface, as the Microsoft class does.

#pragma once

#include <cstddef>
#include <objbase.h>

template <typename InterfaceType, const IID* iid>
class _com_IIID
{
public:
	using Interface = InterfaceType;

	static const IID& GetIID() {
		return *iid;
	}
};

template <typename IIID>
class _com_ptr_t
{
public:
	using Interface = typename IIID::Interface;

	_com_ptr_t() noexcept = default;
	_com_ptr_t(std::nullptr_t) noexcept {}

	_com_ptr_t(Interface* pointer) noexcept : _pointer{pointer}
	{
		addRef();
	}

	_com_ptr_t(Interface* pointer, bool addReference) noexcept : _pointer{pointer}
	{
		if (addReference)
			addRef();
	}

	_com_ptr_t(const _com_ptr_t& other) noexcept : _pointer{other._pointer}
	{
		addRef();
	}

	_com_ptr_t(_com_ptr_t&& other) noexcept : _pointer{other._pointer}
	{
		other._pointer = nullptr;
	}

	template <typename Other>
	_com_ptr_t(Other* pointer)
	{
		queryFrom(pointer);
	}

	template <typename OtherIIID>
	_com_ptr_t(const _com_ptr_t<OtherIIID>& other)
	{
		queryFrom(other.GetInterfacePtr());
	}

	~_com_ptr_t()
	{
		Release();
	}

	_com_ptr_t& operator=(const _com_ptr_t& other) noexcept
	{
		if (this != &other)
		{
			other.addRef();
			Release();
			_pointer = other._pointer;
		}
		return *this;
	}

	_com_ptr_t& operator=(_com_ptr_t&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			_pointer = other._pointer;
			other._pointer = nullptr;
		}
		return *this;
	}

	_com_ptr_t& operator=(Interface* pointer) noexcept
	{
		if (pointer)
			pointer->AddRef();
		Release();
		_pointer = pointer;
		return *this;
	}

	template <typename Other>
	_com_ptr_t& operator=(Other* pointer)
	{
		Release();
		queryFrom(pointer);
		return *this;
	}

	template <typename OtherIIID>
	_com_ptr_t& operator=(const _com_ptr_t<OtherIIID>& other)
	{
		Release();
		queryFrom(other.GetInterfacePtr());
		return *this;
	}

	_com_ptr_t& operator=(std::nullptr_t) noexcept
	{
		Release();
		return *this;
	}

	void Attach(Interface* pointer) noexcept
	{
		Release();
		_pointer = pointer;
	}

	Interface* Detach() noexcept
	{
		Interface* pointer = _pointer;
		_pointer = nullptr;
		return pointer;
	}

	void Release() noexcept
	{
		if (_pointer)
		{
			_pointer->Release();
			_pointer = nullptr;
		}
	}

	HRESULT CreateInstance(REFCLSID clsid, IUnknown* outer = nullptr, DWORD context = CLSCTX_ALL) noexcept
	{
		Release();
		return CoCreateInstance(clsid, outer, context, IIID::GetIID(), reinterpret_cast<void**>(&_pointer));
	}

	Interface* GetInterfacePtr() const noexcept {
		return _pointer;
	}

	operator Interface*() const noexcept {
		return _pointer;
	}

	Interface* operator->() const noexcept {
		return _pointer;
	}

	Interface& operator*() const noexcept {
		return *_pointer;
	}

	Interface** operator&() noexcept
	{
		Release();
		return &_pointer;
	}

	explicit operator bool() const noexcept {
		return _pointer != nullptr;
	}

	bool operator==(std::nullptr_t) const noexcept {
		return _pointer == nullptr;
	}

private:
	void addRef() const noexcept
	{
		if (_pointer)
			_pointer->AddRef();
	}

	template <typename Other>
	void queryFrom(Other* pointer)
	{
		_pointer = nullptr;
		if (pointer)
			pointer->QueryInterface(IIID::GetIID(), reinterpret_cast<void**>(&_pointer));
	}

	Interface* _pointer = nullptr;
};
