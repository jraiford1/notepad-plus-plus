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

// std::from_chars for floating point (nppmac/Charconv.h): the text std::from_chars reads is found here,
// and the C library's strtod family, in the C locale, gives its value.

#include "nppmac/Charconv.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <xlocale.h>

namespace
{
	locale_t classicLocale()
	{
		static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
		return locale;
	}

	bool isDigit(char c, bool hex)
	{
		return (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
	}

	bool startsWithNoCase(const char* p, const char* last, std::string_view word)
	{
		if (static_cast<size_t>(last - p) < word.size())
			return false;
		for (size_t i = 0; i < word.size(); ++i)
		{
			if ((p[i] | 0x20) != word[i])
				return false;
		}
		return true;
	}

	template <typename T>
	T parse(const char* text)
	{
		if constexpr (std::is_same_v<T, float>)
			return strtof_l(text, nullptr, classicLocale());
		else if constexpr (std::is_same_v<T, double>)
			return strtod_l(text, nullptr, classicLocale());
		else
			return strtold_l(text, nullptr, classicLocale());
	}

	template <typename T>
	std::from_chars_result fromChars(const char* first, const char* last, T& value, std::chars_format format)
	{
		const char* p = first;
		const bool negative = p != last && *p == '-';
		if (negative)
			++p;

		// Infinity and NaN, in every format: inf or infinity, nan or nan(letters, digits and '_').
		if (startsWithNoCase(p, last, "inf"))
		{
			p += startsWithNoCase(p, last, "infinity") ? 8 : 3;
			value = negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
			return {p, std::errc{}};
		}
		if (startsWithNoCase(p, last, "nan"))
		{
			p += 3;
			if (p != last && *p == '(')
			{
				const char* q = p + 1;
				while (q != last && (isDigit(*q, false) || ((*q | 0x20) >= 'a' && (*q | 0x20) <= 'z') || *q == '_'))
					++q;
				if (q != last && *q == ')')
					p = q + 1;
			}
			value = negative ? -std::numeric_limits<T>::quiet_NaN() : std::numeric_limits<T>::quiet_NaN();
			return {p, std::errc{}};
		}

		// The digits, with a decimal point among them or not; hexadecimal digits without a "0x".
		const bool hex = format == std::chars_format::hex;
		std::string text = negative ? "-" : "";
		if (hex)
			text += "0x";
		const char* mantissa = p;
		bool anyDigit = false;
		bool nonZero = false;
		auto digits = [&]
		{
			for (; p != last && isDigit(*p, hex); ++p)
			{
				anyDigit = true;
				nonZero = nonZero || *p != '0';
			}
		};
		digits();
		if (p != last && *p == '.')
		{
			++p;
			digits();
		}
		if (!anyDigit)
			return {first, std::errc::invalid_argument};
		text.append(mantissa, p);

		// The exponent, e (p in hexadecimal), a sign or not and decimal digits: none in fixed, required in
		// scientific. An 'e' without digits after it is not part of the number.
		bool hasExponent = false;
		if (format != std::chars_format::fixed && p != last && (*p | 0x20) == (hex ? 'p' : 'e'))
		{
			const char* q = p + 1;
			if (q != last && (*q == '+' || *q == '-'))
				++q;
			const char* exponentDigits = q;
			while (q != last && isDigit(*q, false))
				++q;
			if (q != exponentDigits)
			{
				text.append(p, q);
				p = q;
				hasExponent = true;
			}
		}
		if (format == std::chars_format::scientific && !hasExponent)
			return {first, std::errc::invalid_argument};

		// Too large becomes infinity, too small for any nonzero value zero: out of range, with that value
		// stored, as Visual C++'s library does (a subnormal value is in range).
		value = parse<T>(text.c_str());
		if (std::isinf(value) || (value == 0 && nonZero))
			return {p, std::errc::result_out_of_range};
		return {p, std::errc{}};
	}
}

namespace nppmac
{
	std::from_chars_result fromCharsFloat(const char* first, const char* last, float& value, std::chars_format format)
	{
		return fromChars(first, last, value, format);
	}

	std::from_chars_result fromCharsFloat(const char* first, const char* last, double& value, std::chars_format format)
	{
		return fromChars(first, last, value, format);
	}

	std::from_chars_result fromCharsFloat(const char* first, const char* last, long double& value, std::chars_format format)
	{
		return fromChars(first, last, value, format);
	}
}
