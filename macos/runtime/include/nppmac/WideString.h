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

// Wide strings on macOS: the migration engine turns upstream's wchar_t into char16_t, so the
// C library's wide functions (which take 4-byte wchar_t) cannot be used. This header supplies
// char16_t versions of the C and Microsoft CRT wide-string functions upstream calls, and the
// few std facilities libc++ cannot provide for char16_t.

#pragma once

#include "nppmac/Char16Facets.h"
#include "nppmac/WideCrt.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <format>
#include <fstream>
#include <iomanip>
#include <istream>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

// --- C wide-string functions, char16_t overloads (global scope, next to the wchar_t ones) ----------

size_t wcslen(const char16_t* str);
size_t wcsnlen(const char16_t* str, size_t maxCount);
int wcscmp(const char16_t* lhs, const char16_t* rhs);
int wcsncmp(const char16_t* lhs, const char16_t* rhs, size_t count);
int wcscoll(const char16_t* lhs, const char16_t* rhs);
char16_t* wcscpy(char16_t* dest, const char16_t* src);
char16_t* wcsncpy(char16_t* dest, const char16_t* src, size_t count);
char16_t* wcscat(char16_t* dest, const char16_t* src);
char16_t* wcsncat(char16_t* dest, const char16_t* src, size_t count);
const char16_t* wcschr(const char16_t* str, char16_t ch);
char16_t* wcschr(char16_t* str, char16_t ch);
const char16_t* wcsrchr(const char16_t* str, char16_t ch);
char16_t* wcsrchr(char16_t* str, char16_t ch);
const char16_t* wcsstr(const char16_t* str, const char16_t* subStr);
char16_t* wcsstr(char16_t* str, const char16_t* subStr);
const char16_t* wcspbrk(const char16_t* str, const char16_t* charSet);
char16_t* wcspbrk(char16_t* str, const char16_t* charSet);
size_t wcsspn(const char16_t* str, const char16_t* charSet);
size_t wcscspn(const char16_t* str, const char16_t* charSet);
char16_t* wcstok(char16_t* str, const char16_t* delimiters, char16_t** context);
// Visual C++'s older wcstok, without a context (it keeps one per thread), and its _wcstok name.
char16_t* _wcstok(char16_t* str, const char16_t* delimiters);
inline char16_t* wcstok(char16_t* str, const char16_t* delimiters)
{
	return _wcstok(str, delimiters);
}
long wcstol(const char16_t* str, char16_t** end, int base);
unsigned long wcstoul(const char16_t* str, char16_t** end, int base);
long long wcstoll(const char16_t* str, char16_t** end, int base);
unsigned long long wcstoull(const char16_t* str, char16_t** end, int base);
double wcstod(const char16_t* str, char16_t** end);
long double wcstold(const char16_t* str, char16_t** end);
float wcstof(const char16_t* str, char16_t** end);
char16_t* wmemcpy(char16_t* dest, const char16_t* src, size_t count);
char16_t* wmemmove(char16_t* dest, const char16_t* src, size_t count);
char16_t* wmemset(char16_t* dest, char16_t ch, size_t count);
int wmemcmp(const char16_t* lhs, const char16_t* rhs, size_t count);
const char16_t* wmemchr(const char16_t* str, char16_t ch, size_t count);
char16_t* wmemchr(char16_t* str, char16_t ch, size_t count);

// printf family. Format strings follow the Microsoft CRT: in a wide function %s and %c take wide
// arguments and %S / %C narrow ones; %ls / %hs, %I64d, %I32d and %Iu are accepted.
int swprintf(char16_t* buffer, size_t count, const char16_t* format, ...);
// Visual C++'s older swprintf, without the buffer's size (plugins use it): the buffer must be big enough.
int swprintf(char16_t* buffer, const char16_t* format, ...);
int vswprintf(char16_t* buffer, size_t count, const char16_t* format, va_list args);

// --- Microsoft CRT wide-string functions -------------------------------------------------------

int _wcsicmp(const char16_t* lhs, const char16_t* rhs);
int _wcsnicmp(const char16_t* lhs, const char16_t* rhs, size_t count);
int _wcsicoll(const char16_t* lhs, const char16_t* rhs);
char16_t* _wcsdup(const char16_t* str);
inline char16_t* wcsdup(const char16_t* str)
{
	return _wcsdup(str);
}
char16_t* _wcslwr(char16_t* str);
char16_t* _wcsupr(char16_t* str);
inline char16_t* wcslwr(char16_t* str)
{
	return _wcslwr(str);
}
inline char16_t* wcsupr(char16_t* str)
{
	return _wcsupr(str);
}
int _wtoi(const char16_t* str);
long _wtol(const char16_t* str);
long long _wtoi64(const char16_t* str);
double _wtof(const char16_t* str);
char16_t* _itow(int value, char16_t* buffer, int radix);
char16_t* _ltow(long value, char16_t* buffer, int radix);
char16_t* _ultow(unsigned long value, char16_t* buffer, int radix);
char16_t* _i64tow(long long value, char16_t* buffer, int radix);
char16_t* _ui64tow(unsigned long long value, char16_t* buffer, int radix);
int _snwprintf(char16_t* buffer, size_t count, const char16_t* format, ...);
int _vsnwprintf(char16_t* buffer, size_t count, const char16_t* format, va_list args);
int _scwprintf(const char16_t* format, ...);
int _vscwprintf(const char16_t* format, va_list args);
int _wcsnicmp(const char16_t* lhs, const char16_t* rhs, size_t count);
int wcsnicmp(const char16_t* lhs, const char16_t* rhs, size_t count);
int wcsicmp(const char16_t* lhs, const char16_t* rhs);
size_t wcsftime(char16_t* buffer, size_t size, const char16_t* format, const struct tm* time);
size_t wcsftime_l(char16_t* buffer, size_t size, const char16_t* format, const struct tm* time, locale_t locale);
size_t wcstombs(char* dest, const char16_t* src, size_t count);
size_t mbstowcs(char16_t* dest, const char* src, size_t count);
errno_t _wsplitpath_s(const char16_t* path, char16_t* drive, size_t driveSize, char16_t* dir, size_t dirSize,
	char16_t* fileName, size_t fileNameSize, char16_t* ext, size_t extSize);
void _wsplitpath(const char16_t* path, char16_t* drive, char16_t* dir, char16_t* fileName, char16_t* ext);
errno_t _splitpath_s(const char* path, char* drive, size_t driveSize, char* dir, size_t dirSize, char* fileName, size_t fileNameSize,
	char* ext, size_t extSize);
// The absolute form of a path (GetFullPathName); a buffer of its own (malloc) when absolute is null.
char16_t* _wfullpath(char16_t* absolute, const char16_t* relative, size_t size);

// The CRT's forms for arrays, which take their sizes.
template <size_t driveSize, size_t dirSize, size_t fileNameSize, size_t extSize>
errno_t _wsplitpath_s(const char16_t* path, char16_t (&drive)[driveSize], char16_t (&dir)[dirSize], char16_t (&fileName)[fileNameSize],
	char16_t (&ext)[extSize])
{
	return _wsplitpath_s(path, drive, driveSize, dir, dirSize, fileName, fileNameSize, ext, extSize);
}

template <size_t driveSize, size_t dirSize, size_t fileNameSize, size_t extSize>
errno_t _splitpath_s(const char* path, char (&drive)[driveSize], char (&dir)[dirSize], char (&fileName)[fileNameSize], char (&ext)[extSize])
{
	return _splitpath_s(path, drive, driveSize, dir, dirSize, fileName, fileNameSize, ext, extSize);
}
errno_t _wmakepath_s(char16_t* path, size_t size, const char16_t* drive, const char16_t* dir, const char16_t* fileName, const char16_t* ext);

// "Secure" variants: on error they return a non-zero errno_t and leave an empty string, like the CRT
// does when its invalid-parameter handler returns.

errno_t wcscpy_s(char16_t* dest, size_t destSize, const char16_t* src);
errno_t wcsncpy_s(char16_t* dest, size_t destSize, const char16_t* src, size_t count);
errno_t wcscat_s(char16_t* dest, size_t destSize, const char16_t* src);
errno_t wcsncat_s(char16_t* dest, size_t destSize, const char16_t* src, size_t count);
errno_t _wcslwr_s(char16_t* str, size_t size);
errno_t _wcsupr_s(char16_t* str, size_t size);
errno_t _itow_s(int value, char16_t* buffer, size_t size, int radix);
errno_t _wfopen_s(FILE** file, const char16_t* fileName, const char16_t* mode);
// The C library's message for an error number (strerror), cut to fit the buffer.
errno_t _wcserror_s(char16_t* buffer, size_t size, int error);
template <size_t size>
errno_t _wcserror_s(char16_t (&buffer)[size], int error)
{
	return _wcserror_s(buffer, size, error);
}
int swprintf_s(char16_t* buffer, size_t size, const char16_t* format, ...);
int vswprintf_s(char16_t* buffer, size_t size, const char16_t* format, va_list args);
int _snwprintf_s(char16_t* buffer, size_t size, size_t count, const char16_t* format, ...);
errno_t strcpy_s(char* dest, size_t destSize, const char* src);
errno_t strncpy_s(char* dest, size_t destSize, const char* src, size_t count);
errno_t strcat_s(char* dest, size_t destSize, const char* src);

template <size_t size>
errno_t wcscpy_s(char16_t (&dest)[size], const char16_t* src)
{
	return wcscpy_s(dest, size, src);
}

template <size_t size>
errno_t wcscat_s(char16_t (&dest)[size], const char16_t* src)
{
	return wcscat_s(dest, size, src);
}

template <size_t size>
errno_t wcsncpy_s(char16_t (&dest)[size], const char16_t* src, size_t count)
{
	return wcsncpy_s(dest, size, src, count);
}

template <size_t size, typename... Args>
int swprintf_s(char16_t (&buffer)[size], const char16_t* format, Args... args)
{
	return swprintf_s(buffer, size, format, args...);
}

template <size_t size>
errno_t strcpy_s(char (&dest)[size], const char* src)
{
	return strcpy_s(dest, size, src);
}

// --- std facilities libc++ does not provide for char16_t ----------------------------------------

namespace nppmac
{
	// The narrow printf family with Microsoft CRT format semantics (see swprintf above). The migration
	// engine sends upstream's calls here, because the C library would read %ld and %lu as 64-bit.
	int sprintf(char* buffer, const char* format, ...);
	int snprintf(char* buffer, size_t count, const char* format, ...);
	int vsprintf(char* buffer, const char* format, va_list args);
	int vsnprintf(char* buffer, size_t count, const char* format, va_list args);
	int sprintf_s(char* buffer, size_t size, const char* format, ...);
	int vsprintf_s(char* buffer, size_t size, const char* format, va_list args);
	int snprintf_s(char* buffer, size_t size, size_t count, const char* format, ...);

	template <size_t size, typename... Args>
	int sprintf_s(char (&buffer)[size], const char* format, Args... args)
	{
		return sprintf_s(buffer, size, format, args...);
	}

	template <size_t size>
	int vsprintf_s(char (&buffer)[size], const char* format, va_list args)
	{
		return vsprintf_s(buffer, size, format, args);
	}

	std::string toUtf8(std::u16string_view str);
	std::u16string toUtf16(std::string_view str);

	// A path as upstream writes it (Windows separators, "\\?\" prefix) as a macOS path, UTF-8.
	// Every place a path leaves upstream code for the macOS C library goes through this.
	std::string nativePath(std::u16string_view path);
	std::string nativePath(std::string_view path);

	FILE* fopen(const char* path, const char* mode);

	template <typename T>
	std::u16string numberToU16string(T value)
	{
		const std::string narrow = std::to_string(value);
		return std::u16string(narrow.begin(), narrow.end());
	}

	// std::to_wstring's overloads, one per number type: a value that converts to a number is taken too.
	inline std::u16string to_u16string(int value) { return numberToU16string(value); }
	inline std::u16string to_u16string(unsigned value) { return numberToU16string(value); }
	inline std::u16string to_u16string(long value) { return numberToU16string(value); }
	inline std::u16string to_u16string(unsigned long value) { return numberToU16string(value); }
	inline std::u16string to_u16string(long long value) { return numberToU16string(value); }
	inline std::u16string to_u16string(unsigned long long value) { return numberToU16string(value); }
	inline std::u16string to_u16string(float value) { return numberToU16string(value); }
	inline std::u16string to_u16string(double value) { return numberToU16string(value); }
	inline std::u16string to_u16string(long double value) { return numberToU16string(value); }

	// The rest of a UTF-8 stream as a UTF-16 stream buffer: read whole the first time it is asked.
	class Utf16RestBuffer : public std::basic_streambuf<char16_t>
	{
	public:
		explicit Utf16RestBuffer(std::istream& source) : _source{source} {}

	protected:
		int_type underflow() override;

	private:
		std::istream& _source;
		std::u16string _text;
		bool _read = false;
	};

	// A stringstream for char16_t text. libc++ has no ctype/num_put facets for char16_t, so
	// formatting is done by a narrow (UTF-8) stream and converted at the boundary.
	class u16stringstream
	{
	public:
		u16stringstream() = default;
		explicit u16stringstream(std::u16string_view text) : _stream{toUtf8(text)} {}

		std::u16string str() const {
			return toUtf16(_stream.str());
		}

		// The text, valid until the stream changes (C++20's view()).
		std::u16string_view view() const {
			_view = str();
			return _view;
		}

		void str(std::u16string_view text) {
			_stream.str(toUtf8(text));
		}

		std::locale imbue(const std::locale& loc) {
			return _stream.imbue(loc);
		}

		std::locale getloc() const {
			return _stream.getloc();
		}

		explicit operator bool() const {
			return static_cast<bool>(_stream);
		}

		bool eof() const {
			return _stream.eof();
		}

		void clear() {
			_stream.clear();
		}

		// Positions in the UTF-8 text: what tellg gives, seekg takes back.
		std::streampos tellg() {
			return _stream.tellg();
		}

		u16stringstream& seekg(std::streampos position) {
			_stream.seekg(position);
			return *this;
		}

		// The text not read yet, as UTF-16 (for std::istreambuf_iterator<char16_t>).
		std::basic_streambuf<char16_t>* rdbuf() {
			_rest = std::make_unique<Utf16RestBuffer>(_stream);
			return _rest.get();
		}

		std::stringstream& narrow() {
			return _stream;
		}

		u16stringstream& operator<<(std::u16string_view text) {
			_stream << toUtf8(text);
			return *this;
		}

		// An exact match, so that a std::u16string does not go to the template below.
		u16stringstream& operator<<(const std::u16string& text) {
			_stream << toUtf8(text);
			return *this;
		}

		u16stringstream& operator<<(const char16_t* text) {
			_stream << toUtf8(text);
			return *this;
		}

		// A char16_t buffer (char16_t[N]), which the template below would otherwise take.
		u16stringstream& operator<<(char16_t* text) {
			_stream << toUtf8(text);
			return *this;
		}

		u16stringstream& operator<<(char16_t ch) {
			_stream << toUtf8(std::u16string_view{&ch, 1});
			return *this;
		}

		u16stringstream& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
			_stream << manipulator;
			return *this;
		}

		u16stringstream& operator<<(std::ios_base& (*manipulator)(std::ios_base&)) {
			_stream << manipulator;
			return *this;
		}

		u16stringstream& operator<<(decltype(std::setfill(u'0')) fill);

		template <typename T>
		u16stringstream& operator<<(const T& value) {
			_stream << value;
			return *this;
		}

		u16stringstream& operator>>(std::u16string& text);

		template <typename T>
		u16stringstream& operator>>(T& value) {
			_stream >> value;
			return *this;
		}

	private:
		std::stringstream _stream;
		std::unique_ptr<Utf16RestBuffer> _rest;
		mutable std::u16string _view;
	};

	// <locale> character classification. For char16_t the classic C rules apply to the BMP code unit
	// (Notepad++ uses these on identifiers, digits and blanks); every other type goes to std.
#define NPPMAC_CHAR_CLASS(name) \
	bool name(char16_t ch, const std::locale& loc); \
	template <typename CharT> \
	bool name(CharT ch, const std::locale& loc) { return std::name(ch, loc); } \
	inline int name(int ch) { return std::name(ch); }

	NPPMAC_CHAR_CLASS(isdigit)
	NPPMAC_CHAR_CLASS(isspace)
	NPPMAC_CHAR_CLASS(isalpha)
	NPPMAC_CHAR_CLASS(isalnum)
	NPPMAC_CHAR_CLASS(isupper)
	NPPMAC_CHAR_CLASS(islower)
	NPPMAC_CHAR_CLASS(ispunct)
	NPPMAC_CHAR_CLASS(isxdigit)
	NPPMAC_CHAR_CLASS(iscntrl)
	NPPMAC_CHAR_CLASS(isprint)
	NPPMAC_CHAR_CLASS(isgraph)
	NPPMAC_CHAR_CLASS(isblank)
#undef NPPMAC_CHAR_CLASS

	char16_t tolower(char16_t ch, const std::locale& loc);
	char16_t toupper(char16_t ch, const std::locale& loc);

	template <typename CharT>
	CharT tolower(CharT ch, const std::locale& loc)
	{
		return std::tolower(ch, loc);
	}

	template <typename CharT>
	CharT toupper(CharT ch, const std::locale& loc)
	{
		return std::toupper(ch, loc);
	}

	inline int tolower(int ch)
	{
		return std::tolower(ch);
	}

	inline int toupper(int ch)
	{
		return std::toupper(ch);
	}

	// std::format with a UTF-16 format string (std::format has no char16_t support): the format
	// string and string arguments are converted to UTF-8, formatted by std::vformat, and converted back.
	template <typename T>
	decltype(auto) formatArgument(const T& value)
	{
		if constexpr (std::is_convertible_v<const T&, std::u16string_view>)
			return toUtf8(std::u16string_view{value});
		else
			return (value);
	}

	template <typename... Args>
	std::u16string format(std::u16string_view formatString, const Args&... args)
	{
		const std::string narrowFormat = toUtf8(formatString);
		auto converted = std::make_tuple(formatArgument(args)...);
		return std::apply([&narrowFormat](auto&... values) {
			return toUtf16(std::vformat(narrowFormat, std::make_format_args(values...)));
		}, converted);
	}

	template <typename... Args>
	std::string format(std::format_string<Args...> formatString, Args&&... args)
	{
		return std::format(formatString, std::forward<Args>(args)...);
	}

	// File streams with UTF-16 paths, as the Microsoft library provides.
	template <typename Stream, std::ios_base::openmode defaultMode>
	class PathStream : public Stream
	{
	public:
		using Stream::Stream;
		using Stream::open;

		PathStream() = default;

		explicit PathStream(const char16_t* fileName, std::ios_base::openmode mode = defaultMode)
			: Stream{nativePath(std::u16string_view{fileName}), mode} {}

		explicit PathStream(const std::u16string& fileName, std::ios_base::openmode mode = defaultMode)
			: Stream{nativePath(std::u16string_view{fileName}), mode} {}

		explicit PathStream(const std::string& fileName, std::ios_base::openmode mode = defaultMode)
			: Stream{nativePath(std::string_view{fileName}), mode} {}

		explicit PathStream(const char* fileName, std::ios_base::openmode mode = defaultMode)
			: Stream{nativePath(std::string_view{fileName}), mode} {}

		void open(const char16_t* fileName, std::ios_base::openmode mode = defaultMode) {
			Stream::open(nativePath(std::u16string_view{fileName}), mode);
		}

		void open(const std::u16string& fileName, std::ios_base::openmode mode = defaultMode) {
			Stream::open(nativePath(std::u16string_view{fileName}), mode);
		}
	};

	using ifstream = PathStream<std::ifstream, std::ios_base::in>;
	using ofstream = PathStream<std::ofstream, std::ios_base::out>;
	using fstream = PathStream<std::fstream, std::ios_base::in | std::ios_base::out>;

	// What the classic locale makes of a file's bytes in a wide stream on Windows: one wide character
	// for each byte. (libc++'s codecvt<char16_t, char> is UTF-8, and deprecated.)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	class ByteConversion : public std::codecvt<char16_t, char, std::mbstate_t>
	{
	public:
		explicit ByteConversion(size_t refs = 0) : std::codecvt<char16_t, char, std::mbstate_t>{refs} {}

	protected:
		result do_out(state_type&, const char16_t* from, const char16_t* fromEnd, const char16_t*& fromNext, char* to, char* toEnd,
			char*& toNext) const override {
			while (from < fromEnd && to < toEnd)
				*to++ = static_cast<char>(*from++);
			fromNext = from;
			toNext = to;
			return from == fromEnd ? ok : partial;
		}

		result do_in(state_type&, const char* from, const char* fromEnd, const char*& fromNext, char16_t* to, char16_t* toEnd,
			char16_t*& toNext) const override {
			while (from < fromEnd && to < toEnd)
				*to++ = static_cast<unsigned char>(*from++);
			fromNext = from;
			toNext = to;
			return from == fromEnd ? ok : partial;
		}

		result do_unshift(state_type&, char* to, char*, char*& toNext) const override {
			toNext = to;
			return noconv;
		}

		int do_encoding() const noexcept override {
			return 1;
		}

		bool do_always_noconv() const noexcept override {
			return false;
		}

		int do_length(state_type&, const char* from, const char* fromEnd, size_t most) const override {
			return static_cast<int>(std::min(static_cast<size_t>(fromEnd - from), most));
		}

		int do_max_length() const noexcept override {
			return 1;
		}
	};
#pragma clang diagnostic pop

	// Wide file streams (std::wifstream...), on char16_t: byte for byte unless the code imbues another
	// conversion (codecvt_utf16, codecvt_utf8_utf16), as it does for Windows. Their locale has the
	// char16_t ctype (Char16Facets.h), which getline and the >> operators use.
	template <typename Stream, std::ios_base::openmode defaultMode>
	class WidePathStream : public PathStream<Stream, defaultMode>
	{
	public:
		WidePathStream() {
			useClassicConversion();
		}

		explicit WidePathStream(const char16_t* fileName, std::ios_base::openmode mode = defaultMode) {
			useClassicConversion();
			this->open(fileName, mode);
		}

		explicit WidePathStream(const std::u16string& fileName, std::ios_base::openmode mode = defaultMode) {
			useClassicConversion();
			this->open(fileName, mode);
		}

		explicit WidePathStream(const std::string& fileName, std::ios_base::openmode mode = defaultMode) {
			useClassicConversion();
			Stream::open(nativePath(std::string_view{fileName}), mode);
		}

		explicit WidePathStream(const char* fileName, std::ios_base::openmode mode = defaultMode) {
			useClassicConversion();
			Stream::open(nativePath(std::string_view{fileName}), mode);
		}

	private:
		void useClassicConversion() {
			this->imbue(std::locale{std::locale{this->getloc(), new ByteConversion}, new std::ctype<char16_t>});
		}
	};

	using wifstream = WidePathStream<std::basic_ifstream<char16_t>, std::ios_base::in>;
	using wofstream = WidePathStream<std::basic_ofstream<char16_t>, std::ios_base::out>;
	using wfstream = WidePathStream<std::basic_fstream<char16_t>, std::ios_base::in | std::ios_base::out>;

	using u16ostringstream = u16stringstream;
	using u16istringstream = u16stringstream;

	bool getline(u16stringstream& stream, std::u16string& line, char16_t delimiter = u'\n');

	// std::sto* for UTF-16 strings; narrow strings go to std.
	int stoi(const std::u16string& str, size_t* pos = nullptr, int base = 10);
	long stol(const std::u16string& str, size_t* pos = nullptr, int base = 10);
	unsigned long stoul(const std::u16string& str, size_t* pos = nullptr, int base = 10);
	long long stoll(const std::u16string& str, size_t* pos = nullptr, int base = 10);
	unsigned long long stoull(const std::u16string& str, size_t* pos = nullptr, int base = 10);
	float stof(const std::u16string& str, size_t* pos = nullptr);
	double stod(const std::u16string& str, size_t* pos = nullptr);

	using std::stoi;
	using std::stol;
	using std::stoul;
	using std::stoll;
	using std::stoull;
	using std::stof;
	using std::stod;

	template <typename... Args>
	decltype(auto) getline(Args&&... args)
	{
		return std::getline(std::forward<Args>(args)...);
	}
}
