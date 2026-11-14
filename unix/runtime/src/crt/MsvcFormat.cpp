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

#include "MsvcFormat.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "nppmac/WideString.h"

namespace nppmac::crt
{
	namespace
	{
		enum class LengthModifier
		{
			none,
			charSize,   // hh
			shortSize,  // h
			int32,      // l, I32 (Windows long is 32-bit)
			int64,      // ll, I64, q
			pointer,    // I, z, t, j
			longDouble, // L
			wide,       // w (strings and characters only)
		};

		struct Spec
		{
			std::string _flags;
			int _width = -1;
			int _precision = -1;
			bool _leftAlign = false;
			LengthModifier _length = LengthModifier::none;
			char _conversion = '\0';
		};

		// Output is kept as UTF-16 internally and converted once at the end for the narrow family.
		class Output
		{
		public:
			void append(std::u16string_view text) {
				_text.append(text);
			}

			void append(char16_t ch) {
				_text.push_back(ch);
			}

			void appendAscii(std::string_view text) {
				_text.append(text.begin(), text.end());
			}

			void appendPadded(std::u16string_view text, const Spec& spec)
			{
				const size_t width = spec._width > 0 ? static_cast<size_t>(spec._width) : 0;
				const size_t padding = width > text.size() ? width - text.size() : 0;
				if (!spec._leftAlign)
					_text.append(padding, u' ');
				_text.append(text);
				if (spec._leftAlign)
					_text.append(padding, u' ');
			}

			size_t size() const {
				return _text.size();
			}

			const std::u16string& text() const {
				return _text;
			}

		private:
			std::u16string _text;
		};

		template <typename CharT>
		int parseNumber(const CharT*& p)
		{
			int value = 0;
			while (*p >= '0' && *p <= '9')
			{
				value = value * 10 + static_cast<int>(*p - '0');
				++p;
			}
			return value;
		}

		template <typename CharT>
		bool parseSpec(const CharT*& p, Spec& spec, va_list& args)
		{
			// Flags
			for (;; ++p)
			{
				const CharT ch = *p;
				if (ch == '-')
					spec._leftAlign = true;
				else if (ch != '+' && ch != ' ' && ch != '#' && ch != '0' && ch != '\'')
					break;
				spec._flags.push_back(static_cast<char>(ch));
			}

			// Width
			if (*p == '*')
			{
				spec._width = va_arg(args, int);
				if (spec._width < 0)
				{
					spec._leftAlign = true;
					spec._flags.push_back('-');
					spec._width = -spec._width;
				}
				++p;
			}
			else if (*p >= '0' && *p <= '9')
			{
				spec._width = parseNumber(p);
			}

			// Precision
			if (*p == '.')
			{
				++p;
				if (*p == '*')
				{
					spec._precision = va_arg(args, int);
					++p;
				}
				else
				{
					spec._precision = parseNumber(p);
				}
			}

			// Length modifier
			switch (*p)
			{
				case 'h':
				{
					++p;
					spec._length = LengthModifier::shortSize;
					if (*p == 'h')
					{
						++p;
						spec._length = LengthModifier::charSize;
					}
					break;
				}
				case 'l':
				{
					++p;
					spec._length = LengthModifier::int32;
					if (*p == 'l')
					{
						++p;
						spec._length = LengthModifier::int64;
					}
					break;
				}
				case 'q':
				{
					++p;
					spec._length = LengthModifier::int64;
					break;
				}
				case 'L':
				{
					++p;
					spec._length = LengthModifier::longDouble;
					break;
				}
				case 'w':
				{
					++p;
					spec._length = LengthModifier::wide;
					break;
				}
				case 'z':
				case 't':
				case 'j':
				{
					++p;
					spec._length = LengthModifier::pointer;
					break;
				}
				case 'I':
				{
					++p;
					if (p[0] == '6' && p[1] == '4')
					{
						p += 2;
						spec._length = LengthModifier::int64;
					}
					else if (p[0] == '3' && p[1] == '2')
					{
						p += 2;
						spec._length = LengthModifier::int32;
					}
					else
					{
						spec._length = LengthModifier::pointer;
					}
					break;
				}
				default:
					break;
			}

			if (*p == '\0')
				return false;
			spec._conversion = static_cast<char>(*p);
			++p;
			return true;
		}

		std::string narrowSpec(const Spec& spec, const char* lengthAndConversion)
		{
			std::string result = "%";
			for (const char flag : spec._flags)
			{
				if (flag != '\'')
					result.push_back(flag);
			}
			if (spec._width >= 0)
				result += std::to_string(spec._width);
			if (spec._precision >= 0)
				result += "." + std::to_string(spec._precision);
			result += lengthAndConversion;
			return result;
		}

		std::string formatWithLibc(const char* spec, ...)
		{
			va_list args;
			va_start(args, spec);
			char buffer[512]{};
			va_list copy;
			va_copy(copy, args);
			const int needed = std::vsnprintf(buffer, sizeof(buffer), spec, args);
			va_end(args);

			std::string result;
			if (needed >= 0 && static_cast<size_t>(needed) < sizeof(buffer))
			{
				result = buffer;
			}
			else if (needed >= 0)
			{
				result.resize(static_cast<size_t>(needed) + 1);
				std::vsnprintf(result.data(), result.size(), spec, copy);
				result.resize(static_cast<size_t>(needed));
			}
			va_end(copy);
			return result;
		}

		// Whether %s / %c take a wide argument, given the function family and the length modifier.
		bool isWideArgument(const Spec& spec, FormatFamily family)
		{
			if (spec._length == LengthModifier::shortSize)
				return false;
			if (spec._length == LengthModifier::int32 || spec._length == LengthModifier::wide)
				return true;
			const bool upperCase = spec._conversion == 'S' || spec._conversion == 'C';
			return (family == FormatFamily::wide) != upperCase;
		}

		void formatInteger(Output& out, const Spec& spec, va_list& args)
		{
			const bool isSigned = spec._conversion == 'd' || spec._conversion == 'i';
			const char conversion[] = {'l', 'l', spec._conversion == 'i' ? 'd' : spec._conversion, '\0'};
			const std::string format = narrowSpec(spec, conversion);

			if (isSigned)
			{
				long long value = 0;
				switch (spec._length)
				{
					case LengthModifier::charSize:
						value = static_cast<signed char>(va_arg(args, int));
						break;
					case LengthModifier::shortSize:
						value = static_cast<short>(va_arg(args, int));
						break;
					case LengthModifier::int64:
					case LengthModifier::pointer:
						value = va_arg(args, long long);
						break;
					default:
						value = va_arg(args, int);
				}
				out.appendAscii(formatWithLibc(format.c_str(), value));
			}
			else
			{
				unsigned long long value = 0;
				switch (spec._length)
				{
					case LengthModifier::charSize:
						value = static_cast<unsigned char>(va_arg(args, unsigned int));
						break;
					case LengthModifier::shortSize:
						value = static_cast<unsigned short>(va_arg(args, unsigned int));
						break;
					case LengthModifier::int64:
					case LengthModifier::pointer:
						value = va_arg(args, unsigned long long);
						break;
					default:
						value = va_arg(args, unsigned int);
				}
				out.appendAscii(formatWithLibc(format.c_str(), value));
			}
		}

		void formatString(Output& out, const Spec& spec, FormatFamily family, va_list& args)
		{
			std::u16string text;
			if (isWideArgument(spec, family))
			{
				const char16_t* str = va_arg(args, const char16_t*);
				text = str ? std::u16string{str} : std::u16string{u"(null)"};
			}
			else
			{
				const char* str = va_arg(args, const char*);
				text = str ? toUtf16(str) : std::u16string{u"(null)"};
			}
			if (spec._precision >= 0 && text.size() > static_cast<size_t>(spec._precision))
				text.resize(static_cast<size_t>(spec._precision));
			out.appendPadded(text, spec);
		}

		void formatCharacter(Output& out, const Spec& spec, FormatFamily family, va_list& args)
		{
			const int value = va_arg(args, int);
			std::u16string text;
			if (isWideArgument(spec, family))
			{
				text.push_back(static_cast<char16_t>(value));
			}
			else
			{
				const char ch = static_cast<char>(value);
				text = toUtf16(std::string_view{&ch, 1});
			}
			out.appendPadded(text, spec);
		}

		template <typename CharT>
		std::u16string format(const CharT* format, va_list args, FormatFamily family)
		{
			Output out;
			va_list ap;
			va_copy(ap, args);

			const CharT* p = format;
			while (*p != '\0')
			{
				if (*p != '%')
				{
					// Copy a run of literal text.
					const CharT* start = p;
					while (*p != '\0' && *p != '%')
						++p;
					if constexpr (sizeof(CharT) == 1)
						out.append(toUtf16(std::string_view{reinterpret_cast<const char*>(start), static_cast<size_t>(p - start)}));
					else
						out.append(std::u16string_view{reinterpret_cast<const char16_t*>(start), static_cast<size_t>(p - start)});
					continue;
				}

				++p;
				if (*p == '%')
				{
					out.append(u'%');
					++p;
					continue;
				}

				Spec spec;
				const CharT* specStart = p;
				if (!parseSpec(p, spec, ap))
				{
					// Incomplete specification at the end of the format: output it as it is.
					out.append(u'%');
					for (const CharT* q = specStart; q != p; ++q)
						out.append(static_cast<char16_t>(*q));
					break;
				}

				switch (spec._conversion)
				{
					case 'd':
					case 'i':
					case 'u':
					case 'o':
					case 'x':
					case 'X':
					{
						formatInteger(out, spec, ap);
						break;
					}
					case 'e':
					case 'E':
					case 'f':
					case 'F':
					case 'g':
					case 'G':
					case 'a':
					case 'A':
					{
						// long double is double on Windows.
						const double value = va_arg(ap, double);
						const char conversion[] = {spec._conversion, '\0'};
						out.appendAscii(formatWithLibc(narrowSpec(spec, conversion).c_str(), value));
						break;
					}
					case 's':
					case 'S':
					case 'Z':
					{
						formatString(out, spec, family, ap);
						break;
					}
					case 'c':
					case 'C':
					{
						formatCharacter(out, spec, family, ap);
						break;
					}
					case 'p':
					{
						// The Microsoft CRT prints pointers as fixed-width upper-case hex without "0x".
						const void* value = va_arg(ap, const void*);
						char buffer[32]{};
						std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(value)));
						out.appendPadded(toUtf16(buffer), spec);
						break;
					}
					case 'n':
					{
						int* count = va_arg(ap, int*);
						if (count)
							*count = static_cast<int>(out.size());
						break;
					}
					default:
					{
						// Unknown conversion: output it literally, as the CRT does in its permissive mode.
						out.append(u'%');
						for (const CharT* q = specStart; q != p; ++q)
							out.append(static_cast<char16_t>(*q));
					}
				}
			}

			va_end(ap);
			return out.text();
		}
	}

	std::string formatNarrow(const char* format, va_list args)
	{
		return toUtf8(nppmac::crt::format(format, args, FormatFamily::narrow));
	}

	std::u16string formatWide(const char16_t* format, va_list args)
	{
		return nppmac::crt::format(format, args, FormatFamily::wide);
	}
}
