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

// bcrypt and CryptoAPI hashing over CommonCrypto; Authenticode verification (wintrust, crypt32),
// which has nothing to verify on macOS, reports that a file is not signed.

#include "Handles.h"

#include <CommonCrypto/CommonDigest.h>
#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <string>
#include <vector>

namespace
{
	enum class HashKind
	{
		md5,
		sha1,
		sha256,
		sha384,
		sha512,
	};

	size_t digestLength(HashKind kind)
	{
		switch (kind)
		{
			case HashKind::md5:
				return CC_MD5_DIGEST_LENGTH;
			case HashKind::sha1:
				return CC_SHA1_DIGEST_LENGTH;
			case HashKind::sha256:
				return CC_SHA256_DIGEST_LENGTH;
			case HashKind::sha384:
				return CC_SHA384_DIGEST_LENGTH;
			case HashKind::sha512:
				return CC_SHA512_DIGEST_LENGTH;
		}
		return 0;
	}

	// Collects the data and digests it at the end: Notepad++ hashes files and short strings.
	struct Hash
	{
		HashKind _kind = HashKind::sha256;
		std::vector<unsigned char> _data;

		std::vector<unsigned char> finish() const
		{
			std::vector<unsigned char> digest(digestLength(_kind));
			const auto length = static_cast<CC_LONG>(_data.size());
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			switch (_kind)
			{
				case HashKind::md5:
					CC_MD5(_data.data(), length, digest.data());
					break;
				case HashKind::sha1:
					CC_SHA1(_data.data(), length, digest.data());
					break;
				case HashKind::sha256:
					CC_SHA256(_data.data(), length, digest.data());
					break;
				case HashKind::sha384:
					CC_SHA384(_data.data(), length, digest.data());
					break;
				case HashKind::sha512:
					CC_SHA512(_data.data(), length, digest.data());
					break;
			}
#pragma clang diagnostic pop
			return digest;
		}
	};

	struct Algorithm
	{
		HashKind _kind = HashKind::sha256;
	};

	bool kindFromName(LPCWSTR name, HashKind& kind)
	{
		const std::u16string id{name ? name : u""};
		if (id == BCRYPT_MD5_ALGORITHM)
			kind = HashKind::md5;
		else if (id == BCRYPT_SHA1_ALGORITHM)
			kind = HashKind::sha1;
		else if (id == BCRYPT_SHA256_ALGORITHM)
			kind = HashKind::sha256;
		else if (id == BCRYPT_SHA384_ALGORITHM)
			kind = HashKind::sha384;
		else if (id == BCRYPT_SHA512_ALGORITHM)
			kind = HashKind::sha512;
		else
			return false;
		return true;
	}

	bool kindFromAlgId(ALG_ID algorithm, HashKind& kind)
	{
		switch (algorithm)
		{
			case CALG_MD5:
				kind = HashKind::md5;
				return true;
			case CALG_SHA1:
				kind = HashKind::sha1;
				return true;
			case CALG_SHA_256:
				kind = HashKind::sha256;
				return true;
			case CALG_SHA_384:
				kind = HashKind::sha384;
				return true;
			case CALG_SHA_512:
				kind = HashKind::sha512;
				return true;
			default:
				return false;
		}
	}

	constexpr NTSTATUS statusSuccess = 0;
	constexpr NTSTATUS statusInvalidParameter = static_cast<NTSTATUS>(0xC000000D);
	constexpr NTSTATUS statusNotSupported = static_cast<NTSTATUS>(0xC00000BB);
	constexpr NTSTATUS statusBufferTooSmall = static_cast<NTSTATUS>(0xC0000023);
}

// --- bcrypt ---------------------------------------------------------------------------------------

NTSTATUS WINAPI BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* algorithm, LPCWSTR algorithmId, LPCWSTR, ULONG)
{
	HashKind kind{};
	if (!kindFromName(algorithmId, kind))
		return statusNotSupported;
	*algorithm = new Algorithm{kind};
	return statusSuccess;
}

NTSTATUS WINAPI BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE algorithm, ULONG)
{
	delete static_cast<Algorithm*>(algorithm);
	return statusSuccess;
}

NTSTATUS WINAPI BCryptGetProperty(BCRYPT_HANDLE handle, LPCWSTR property, PUCHAR output, ULONG outputSize, ULONG* result, ULONG)
{
	const std::u16string name{property ? property : u""};
	ULONG value = 0;
	if (name == BCRYPT_HASH_LENGTH)
		value = static_cast<ULONG>(digestLength(static_cast<Algorithm*>(handle)->_kind));
	else if (name == BCRYPT_OBJECT_LENGTH)
		value = 0; // hash objects are allocated by the runtime
	else
		return statusNotSupported;
	if (result)
		*result = sizeof(value);
	if (!output || outputSize < sizeof(value))
		return output ? statusBufferTooSmall : statusSuccess;
	std::memcpy(output, &value, sizeof(value));
	return statusSuccess;
}

NTSTATUS WINAPI BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* hash, PUCHAR, ULONG, PUCHAR, ULONG, ULONG)
{
	if (!algorithm)
		return statusInvalidParameter;
	*hash = new Hash{static_cast<Algorithm*>(algorithm)->_kind, {}};
	return statusSuccess;
}

NTSTATUS WINAPI BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR input, ULONG size, ULONG)
{
	auto* state = static_cast<Hash*>(hash);
	state->_data.insert(state->_data.end(), input, input + size);
	return statusSuccess;
}

NTSTATUS WINAPI BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR output, ULONG size, ULONG)
{
	const std::vector<unsigned char> digest = static_cast<Hash*>(hash)->finish();
	if (size != digest.size())
		return statusInvalidParameter;
	std::copy(digest.begin(), digest.end(), output);
	return statusSuccess;
}

NTSTATUS WINAPI BCryptDestroyHash(BCRYPT_HASH_HANDLE hash)
{
	delete static_cast<Hash*>(hash);
	return statusSuccess;
}

// --- CryptoAPI hashing ----------------------------------------------------------------------------

BOOL WINAPI CryptAcquireContextW(HCRYPTPROV* provider, LPCWSTR, LPCWSTR, DWORD, DWORD)
{
	static int context = 0;
	*provider = reinterpret_cast<HCRYPTPROV>(&context);
	return TRUE;
}

BOOL WINAPI CryptReleaseContext(HCRYPTPROV, DWORD)
{
	return TRUE;
}

BOOL WINAPI CryptCreateHash(HCRYPTPROV, ALG_ID algorithm, HCRYPTKEY, DWORD, HCRYPTHASH* hash)
{
	HashKind kind{};
	if (!kindFromAlgId(algorithm, kind))
	{
		SetLastError(static_cast<DWORD>(NTE_BAD_ALGID));
		return FALSE;
	}
	*hash = reinterpret_cast<HCRYPTHASH>(new Hash{kind, {}});
	return TRUE;
}

BOOL WINAPI CryptHashData(HCRYPTHASH hash, const BYTE* data, DWORD size, DWORD)
{
	auto* state = reinterpret_cast<Hash*>(hash);
	state->_data.insert(state->_data.end(), data, data + size);
	return TRUE;
}

BOOL WINAPI CryptGetHashParam(HCRYPTHASH hash, DWORD parameter, BYTE* data, DWORD* size, DWORD)
{
	auto* state = reinterpret_cast<Hash*>(hash);
	if (parameter == HP_HASHSIZE)
	{
		const DWORD length = static_cast<DWORD>(digestLength(state->_kind));
		if (data)
			std::memcpy(data, &length, sizeof(length));
		*size = sizeof(length);
		return TRUE;
	}
	if (parameter != HP_HASHVAL)
	{
		SetLastError(static_cast<DWORD>(NTE_BAD_TYPE));
		return FALSE;
	}
	const std::vector<unsigned char> digest = state->finish();
	if (data && *size < digest.size())
	{
		*size = static_cast<DWORD>(digest.size());
		SetLastError(ERROR_MORE_DATA);
		return FALSE;
	}
	if (data)
		std::copy(digest.begin(), digest.end(), data);
	*size = static_cast<DWORD>(digest.size());
	return TRUE;
}

BOOL WINAPI CryptDestroyHash(HCRYPTHASH hash)
{
	delete reinterpret_cast<Hash*>(hash);
	return TRUE;
}

// --- Authenticode: nothing is signed the Windows way on macOS --------------------------------------

LONG WINAPI WinVerifyTrust(HWND, GUID*, LPVOID)
{
	return TRUST_E_NOSIGNATURE;
}

BOOL WINAPI CryptQueryObject(DWORD, const void*, DWORD, DWORD, DWORD, DWORD*, DWORD*, DWORD*, HCERTSTORE*, HCRYPTMSG*, const void**)
{
	SetLastError(static_cast<DWORD>(CRYPT_E_NO_MATCH));
	return FALSE;
}

BOOL WINAPI CryptMsgGetParam(HCRYPTMSG, DWORD, DWORD, void*, DWORD*)
{
	SetLastError(static_cast<DWORD>(CRYPT_E_INVALID_MSG_TYPE));
	return FALSE;
}

BOOL WINAPI CryptMsgClose(HCRYPTMSG)
{
	return TRUE;
}

PCCERT_CONTEXT WINAPI CertFindCertificateInStore(HCERTSTORE, DWORD, DWORD, DWORD, const void*, PCCERT_CONTEXT)
{
	SetLastError(static_cast<DWORD>(CRYPT_E_NOT_FOUND));
	return nullptr;
}

BOOL WINAPI CertFreeCertificateContext(PCCERT_CONTEXT)
{
	return TRUE;
}

BOOL WINAPI CertCloseStore(HCERTSTORE, DWORD)
{
	return TRUE;
}

BOOL WINAPI CertGetCertificateContextProperty(PCCERT_CONTEXT, DWORD, void*, DWORD*)
{
	SetLastError(static_cast<DWORD>(CRYPT_E_NOT_FOUND));
	return FALSE;
}

DWORD WINAPI CertGetNameStringW(PCCERT_CONTEXT, DWORD, DWORD, void*, LPWSTR name, DWORD size)
{
	if (name && size > 0)
		name[0] = u'\0';
	return 1;
}

DWORD WINAPI CertNameToStrW(DWORD, PCERT_NAME_BLOB, DWORD, LPWSTR name, DWORD size)
{
	if (name && size > 0)
		name[0] = u'\0';
	return 1;
}
