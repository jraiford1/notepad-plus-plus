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

// Resources: the program's compiled .rc files (Contents/Resources/res/*.res, written by llvm-rc) and
// the system's cursors and icons. FindResource and friends, strings, icons, cursors, bitmaps and
// version information. Menus, accelerators and dialogs are built from these in their own modules.

#include <ImageIO/ImageIO.h>

#include "Resources.h"
#include "Window.h"
#include "platform/Platform.h"

#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <iterator>

namespace nppmac::user
{
	HCURSOR loadSystemCursor(LPCWSTR id);
	HICON loadSystemIcon(LPCWSTR id);

	namespace
	{
		struct ResourceId
		{
			bool _isOrdinal = true;
			WORD _ordinal = 0;
			std::u16string _name;

			bool matches(LPCWSTR id) const
			{
				if (IS_INTRESOURCE(id))
					return _isOrdinal && _ordinal == static_cast<WORD>(reinterpret_cast<uintptr_t>(id));
				if (id[0] == u'#') // "#123" names an ordinal
					return _isOrdinal && _ordinal == static_cast<WORD>(_wtoi(id + 1));
				return !_isOrdinal && _wcsicmp(_name.c_str(), id) == 0;
			}
		};

		struct Entry
		{
			ResourceId _type;
			ResourceId _name;
			WORD _language = 0;
			const BYTE* _data = nullptr;
			DWORD _size = 0;
		};

		struct Store
		{
			std::vector<std::vector<BYTE>> _files;
			std::vector<Entry> _entries;
		};

		// A type or name: 0xFFFF then an ordinal, or a terminated UTF-16 string.
		const BYTE* readId(const BYTE* p, ResourceId& id)
		{
			WORD first = 0;
			std::memcpy(&first, p, 2);
			if (first == 0xFFFF)
			{
				id._isOrdinal = true;
				std::memcpy(&id._ordinal, p + 2, 2);
				return p + 4;
			}
			id._isOrdinal = false;
			size_t length = 0;
			for (;;)
			{
				char16_t ch = 0;
				std::memcpy(&ch, p + length * 2, 2);
				if (ch == 0)
					break;
				id._name.push_back(ch);
				++length;
			}
			return p + (length + 1) * 2;
		}

		// The 32-bit .res format: entries of DataSize, HeaderSize, Type, Name, (aligned) DataVersion,
		// MemoryFlags, LanguageId, Version, Characteristics, then the data, DWORD-aligned.
		void parse(Store& store, const std::vector<BYTE>& file)
		{
			const BYTE* p = file.data();
			const BYTE* end = p + file.size();
			while (p + 8 <= end)
			{
				DWORD dataSize = 0;
				DWORD headerSize = 0;
				std::memcpy(&dataSize, p, 4);
				std::memcpy(&headerSize, p + 4, 4);
				if (headerSize < 16 || p + headerSize + dataSize > end)
					break;
				Entry entry;
				const BYTE* field = readId(p + 8, entry._type);
				field = readId(field, entry._name);
				const size_t offset = static_cast<size_t>(field - p);
				const BYTE* fixed = p + ((offset + 3) & ~static_cast<size_t>(3));
				std::memcpy(&entry._language, fixed + 6, 2);
				entry._data = p + headerSize;
				entry._size = dataSize;
				if (dataSize > 0)
					store._entries.push_back(entry);
				p += headerSize + ((dataSize + 3) & ~static_cast<DWORD>(3));
			}
		}

		Store& store()
		{
			static Store* resources = []
			{
				auto* result = new Store;
				const std::string directory = platform::resourcesDirectory() + "/res";
				std::vector<std::string> names;
				if (DIR* dir = ::opendir(directory.c_str()))
				{
					while (dirent* entry = ::readdir(dir))
					{
						const std::string name = entry->d_name;
						if (name.size() > 4 && name.compare(name.size() - 4, 4, ".res") == 0)
							names.push_back(name);
					}
					::closedir(dir);
				}
				// Notepad_plus.res first: its resources win when two files use the same id.
				std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b)
				{
					const bool aMain = a == "Notepad_plus.res";
					const bool bMain = b == "Notepad_plus.res";
					return aMain != bMain ? aMain : a < b;
				});
				result->_files.reserve(names.size());
				for (const std::string& name : names)
				{
					std::ifstream in{directory + "/" + name, std::ios::binary};
					result->_files.emplace_back(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
					parse(*result, result->_files.back());
				}
				return result;
			}();
			return *resources;
		}

#pragma pack(push, 2)
		struct GroupIconEntry
		{
			BYTE _width;
			BYTE _height;
			BYTE _colorCount;
			BYTE _reserved;
			WORD _planes;
			WORD _bitCount;
			DWORD _bytesInResource;
			WORD _id;
		};
#pragma pack(pop)

		int sizeScore(int width, int wanted, int bitCount)
		{
			// Exact size first, then the smallest larger one, then the largest smaller one.
			int score = 0;
			if (width == wanted)
				score = 1000000;
			else if (width > wanted)
				score = 500000 - (width - wanted) * 100;
			else
				score = 100000 - (wanted - width) * 100;
			return score + bitCount;
		}

		std::shared_ptr<gdi::Bitmap> newBitmap(int width, int height)
		{
			auto bitmap = std::make_shared<gdi::Bitmap>();
			bitmap->_width = width;
			bitmap->_height = height;
			bitmap->_ownedPixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
			bitmap->_pixels = bitmap->_ownedPixels.data();
			return bitmap;
		}

		std::shared_ptr<gdi::Bitmap> decodePng(const BYTE* data, size_t size)
		{
			CFDataRef cfData = CFDataCreate(nullptr, data, static_cast<CFIndex>(size));
			CGImageSourceRef source = CGImageSourceCreateWithData(cfData, nullptr);
			CFRelease(cfData);
			if (!source)
				return nullptr;
			CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
			CFRelease(source);
			if (!image)
				return nullptr;
			auto bitmap = newBitmap(static_cast<int>(CGImageGetWidth(image)), static_cast<int>(CGImageGetHeight(image)));
			CGContextRef context = bitmap->context();
			// The context has a top-left origin; draw the image upright.
			CGContextSaveGState(context);
			CGContextTranslateCTM(context, 0, bitmap->_height);
			CGContextScaleCTM(context, 1, -1);
			CGContextDrawImage(context, CGRectMake(0, 0, bitmap->_width, bitmap->_height), image);
			CGContextRestoreGState(context);
			CGImageRelease(image);
			bitmap->_hasAlpha = true;
			return bitmap;
		}
	}

	const ResourceData* findResource(LPCWSTR name, LPCWSTR type)
	{
		for (const Entry& entry : store()._entries)
		{
			if (entry._type.matches(type) && entry._name.matches(name))
				return reinterpret_cast<const ResourceData*>(&entry);
		}
		return nullptr;
	}

	std::span<const BYTE> resourceBytes(const ResourceData* resource)
	{
		const auto* entry = reinterpret_cast<const Entry*>(resource);
		return entry ? std::span<const BYTE>{entry->_data, entry->_size} : std::span<const BYTE>{};
	}

	std::span<const BYTE> resourceBytes(LPCWSTR name, LPCWSTR type)
	{
		return resourceBytes(findResource(name, type));
	}

	std::shared_ptr<gdi::Bitmap> decodeIconImage(const BYTE* data, size_t size)
	{
		if (size >= 8 && std::memcmp(data, "\x89PNG", 4) == 0)
			return decodePng(data, size);
		if (size < sizeof(BITMAPINFOHEADER))
			return nullptr;
		BITMAPINFOHEADER header{};
		std::memcpy(&header, data, sizeof(header));
		const int width = header.biWidth;
		const int height = std::abs(header.biHeight) / 2; // the color and mask bitmaps together
		const int bitCount = header.biBitCount;
		if (width <= 0 || height <= 0)
			return nullptr;
		const int colors = bitCount <= 8 ? (header.biClrUsed ? static_cast<int>(header.biClrUsed) : (1 << bitCount)) : 0;
		const auto* palette = reinterpret_cast<const RGBQUAD*>(data + header.biSize);
		const BYTE* xorBits = data + header.biSize + static_cast<size_t>(colors) * 4;
		const size_t xorStride = ((static_cast<size_t>(width) * static_cast<size_t>(bitCount) + 31) / 32) * 4;
		const BYTE* andBits = xorBits + xorStride * static_cast<size_t>(height);
		const size_t andStride = ((static_cast<size_t>(width) + 31) / 32) * 4;
		const bool hasAndMask = andBits + andStride * static_cast<size_t>(height) <= data + size;

		auto bitmap = newBitmap(width, height);
		bool hasAlpha = false;
		for (int y = 0; y < height; ++y)
		{
			// Rows are stored bottom-up.
			const BYTE* row = xorBits + xorStride * static_cast<size_t>(height - 1 - y);
			const BYTE* mask = andBits + andStride * static_cast<size_t>(height - 1 - y);
			uint32_t* out = bitmap->rowPointer(y);
			for (int x = 0; x < width; ++x)
			{
				uint32_t a = 0xFF;
				uint32_t r = 0;
				uint32_t g = 0;
				uint32_t b = 0;
				if (bitCount == 32)
				{
					b = row[x * 4];
					g = row[x * 4 + 1];
					r = row[x * 4 + 2];
					a = row[x * 4 + 3];
					hasAlpha = hasAlpha || a != 0;
				}
				else if (bitCount == 24)
				{
					b = row[x * 3];
					g = row[x * 3 + 1];
					r = row[x * 3 + 2];
				}
				else
				{
					int index = 0;
					if (bitCount == 8)
						index = row[x];
					else if (bitCount == 4)
						index = (row[x / 2] >> ((x % 2) ? 0 : 4)) & 0x0F;
					else if (bitCount == 1)
						index = (row[x / 8] >> (7 - x % 8)) & 1;
					r = palette[index].rgbRed;
					g = palette[index].rgbGreen;
					b = palette[index].rgbBlue;
				}
				out[x] = (a << 24) | (r << 16) | (g << 8) | b;
				if (hasAndMask && bitCount != 32 && ((mask[x / 8] >> (7 - x % 8)) & 1))
					out[x] = 0;
			}
		}
		// A 32-bit image without alpha relies on its AND mask.
		if (bitCount == 32 && !hasAlpha)
		{
			for (int y = 0; y < height; ++y)
			{
				const BYTE* mask = andBits + andStride * static_cast<size_t>(height - 1 - y);
				uint32_t* out = bitmap->rowPointer(y);
				for (int x = 0; x < width; ++x)
				{
					const bool transparent = hasAndMask && ((mask[x / 8] >> (7 - x % 8)) & 1);
					out[x] = transparent ? 0 : (out[x] | 0xFF000000);
				}
			}
		}
		for (uint32_t& pixel : bitmap->_ownedPixels)
		{
			const uint32_t a = pixel >> 24;
			if (a != 0xFF)
				pixel = (a << 24) | ((((pixel >> 16) & 0xFF) * a / 255) << 16) | ((((pixel >> 8) & 0xFF) * a / 255) << 8) | ((pixel & 0xFF) * a / 255);
		}
		bitmap->_hasAlpha = true;
		return bitmap;
	}

	HICON createIcon(std::shared_ptr<gdi::Bitmap> image, bool isIcon, POINT hotspot)
	{
		if (!image)
			return nullptr;
		auto* icon = new gdi::Icon;
		icon->_color = std::move(image);
		icon->_isIcon = isIcon;
		icon->_hotspot = hotspot;
		return reinterpret_cast<HICON>(gdi::trackObject(icon));
	}

	int pickGroupEntry(std::span<const BYTE> group, int wantedWidth)
	{
		if (group.size() < 6)
			return -1;
		WORD count = 0;
		std::memcpy(&count, group.data() + 4, 2);
		int best = -1;
		int bestScore = 0;
		for (WORD i = 0; i < count; ++i)
		{
			const size_t offset = 6 + static_cast<size_t>(i) * sizeof(GroupIconEntry);
			if (offset + sizeof(GroupIconEntry) > group.size())
				break;
			GroupIconEntry entry{};
			std::memcpy(&entry, group.data() + offset, sizeof(entry));
			const int score = sizeScore(entry._width ? entry._width : 256, wantedWidth, entry._bitCount);
			if (best < 0 || score > bestScore)
			{
				best = entry._id;
				bestScore = score;
			}
		}
		return best;
	}

	HICON loadIconResource(LPCWSTR name, int width, int height, bool cursor)
	{
		std::span<const BYTE> group = resourceBytes(name, cursor ? RT_GROUP_CURSOR : RT_GROUP_ICON);
		if (group.empty())
			return nullptr;
		if (width <= 0)
			width = cursor ? 32 : GetSystemMetrics(SM_CXICON);
		(void)height;
		const int id = pickGroupEntry(group, width);
		if (id < 0)
			return nullptr;
		std::span<const BYTE> image = resourceBytes(MAKEINTRESOURCEW(id), cursor ? RT_CURSOR : RT_ICON);
		if (image.size() < 4)
			return nullptr;
		POINT hotspot{};
		if (cursor)
		{
			// Cursor images start with their hotspot.
			WORD x = 0;
			WORD y = 0;
			std::memcpy(&x, image.data(), 2);
			std::memcpy(&y, image.data() + 2, 2);
			hotspot = {x, y};
			image = image.subspan(4);
		}
		std::shared_ptr<gdi::Bitmap> decoded = decodeIconImage(image.data(), image.size());
		// For Retina screens, the image nearest twice the size goes along, when the file has a larger one.
		const int hiResId = cursor ? -1 : pickGroupEntry(group, width * 2);
		if (decoded && hiResId >= 0 && hiResId != id)
		{
			std::span<const BYTE> hiResImage = resourceBytes(MAKEINTRESOURCEW(hiResId), RT_ICON);
			std::shared_ptr<gdi::Bitmap> hiRes = decodeIconImage(hiResImage.data(), hiResImage.size());
			if (hiRes && hiRes->_width > decoded->_width)
				decoded->_hiRes = std::move(hiRes);
		}
		return createIcon(std::move(decoded), !cursor, hotspot);
	}

	HBITMAP loadBitmapResource(LPCWSTR name)
	{
		std::span<const BYTE> data = resourceBytes(name, RT_BITMAP);
		if (data.size() < sizeof(BITMAPINFOHEADER))
			return nullptr;
		const auto* info = reinterpret_cast<const BITMAPINFO*>(data.data());
		const BITMAPINFOHEADER& header = info->bmiHeader;
		const int colors = header.biBitCount <= 8 ? (header.biClrUsed ? static_cast<int>(header.biClrUsed) : (1 << header.biBitCount)) : 0;
		const size_t masks = header.biCompression == BI_BITFIELDS ? 12 : 0;
		const BYTE* bits = data.data() + header.biSize + masks + static_cast<size_t>(colors) * 4;
		const int height = std::abs(header.biHeight);
		HBITMAP bitmap = CreateBitmap(header.biWidth, height, 1, 32, nullptr);
		SetDIBits(nullptr, bitmap, 0, static_cast<UINT>(height), bits, info, DIB_RGB_COLORS);
		return bitmap;
	}

	// LR_LOADTRANSPARENT: Windows paints the pixels of the top-left pixel's color in the window color, so they
	// look transparent on a default background; here they are made transparent, which they look on any background.
	void makeBackgroundTransparent(HBITMAP handle)
	{
		auto* bitmap = nppmac::gdi::objectAs<nppmac::gdi::Bitmap>(handle, nppmac::gdi::ObjectType::bitmap);
		if (!bitmap || bitmap->_width <= 0 || bitmap->_height <= 0)
			return;
		const uint32_t key = bitmap->rowPointer(0)[0] & 0xFFFFFF;
		for (int y = 0; y < bitmap->_height; ++y)
		{
			uint32_t* row = bitmap->rowPointer(y);
			for (int x = 0; x < bitmap->_width; ++x)
			{
				row[x] = (row[x] & 0xFFFFFF) == key ? 0 : (row[x] | 0xFF000000);
			}
		}
		bitmap->_hasAlpha = true;
	}

	// An .ico, .cur, .bmp or .png file (LoadImage with LR_LOADFROMFILE).
	HANDLE loadImageFile(LPCWSTR path, UINT type, int width)
	{
		std::ifstream in{nativePath(std::u16string_view{path}), std::ios::binary};
		if (!in)
			return nullptr;
		const std::vector<BYTE> data{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
		if (data.size() < 8)
			return nullptr;
		if (std::memcmp(data.data(), "\x89PNG", 4) == 0)
		{
			auto image = decodePng(data.data(), data.size());
			if (!image)
				return nullptr;
			if (type == IMAGE_BITMAP)
				return CreateBitmap(image->_width, image->_height, 1, 32, image->_pixels);
			return createIcon(image, type != IMAGE_CURSOR, {});
		}
		if (data[0] == 'B' && data[1] == 'M')
		{
			const auto* info = reinterpret_cast<const BITMAPINFO*>(data.data() + sizeof(BITMAPFILEHEADER));
			DWORD bitsOffset = 0;
			std::memcpy(&bitsOffset, data.data() + 10, 4);
			const int height = std::abs(info->bmiHeader.biHeight);
			HBITMAP bitmap = CreateBitmap(info->bmiHeader.biWidth, height, 1, 32, nullptr);
			SetDIBits(nullptr, bitmap, 0, static_cast<UINT>(height), data.data() + bitsOffset, info, DIB_RGB_COLORS);
			return bitmap;
		}
		// ICONDIR: entries like a resource group's, ending with a file offset instead of an id.
		WORD count = 0;
		std::memcpy(&count, data.data() + 4, 2);
		struct Choice
		{
			int _offset = -1;
			int _size = 0;
			int _score = 0;
			POINT _hotspot{};
		};
		auto choose = [&](int wanted)
		{
			Choice best;
			for (WORD i = 0; i < count; ++i)
			{
				const BYTE* entry = data.data() + 6 + static_cast<size_t>(i) * 16;
				if (entry + 16 > data.data() + data.size())
					break;
				WORD fourth = 0;
				WORD sixth = 0;
				DWORD size = 0;
				DWORD offset = 0;
				std::memcpy(&fourth, entry + 4, 2);
				std::memcpy(&sixth, entry + 6, 2);
				std::memcpy(&size, entry + 8, 4);
				std::memcpy(&offset, entry + 12, 4);
				const int score = sizeScore(entry[0] ? entry[0] : 256, wanted, type == IMAGE_CURSOR ? 0 : sixth);
				if (best._offset < 0 || score > best._score)
					best = {static_cast<int>(offset), static_cast<int>(size), score, {fourth, sixth}}; // cursors keep the hotspot where icons keep planes and depth
			}
			if (best._offset >= 0 && static_cast<size_t>(best._offset) + static_cast<size_t>(best._size) > data.size())
				best._offset = -1;
			return best;
		};
		const int wanted = width > 0 ? width : 32;
		const Choice best = choose(wanted);
		if (best._offset < 0)
			return nullptr;
		std::shared_ptr<gdi::Bitmap> decoded = decodeIconImage(data.data() + best._offset, static_cast<size_t>(best._size));
		// For Retina screens, the image nearest twice the size goes along, when the file has a larger one.
		if (const Choice hiRes = type == IMAGE_CURSOR ? Choice{} : choose(wanted * 2); decoded && hiRes._offset >= 0 && hiRes._offset != best._offset)
		{
			std::shared_ptr<gdi::Bitmap> image = decodeIconImage(data.data() + hiRes._offset, static_cast<size_t>(hiRes._size));
			if (image && image->_width > decoded->_width)
				decoded->_hiRes = std::move(image);
		}
		return createIcon(std::move(decoded), type != IMAGE_CURSOR, type == IMAGE_CURSOR ? best._hotspot : POINT{});
	}
}

using namespace nppmac::user;

// --- Generic resource access ----------------------------------------------------------------------

HRSRC WINAPI FindResourceW(HMODULE, LPCWSTR name, LPCWSTR type)
{
	const ResourceData* resource = findResource(name, type);
	if (!resource)
		SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
	return reinterpret_cast<HRSRC>(const_cast<ResourceData*>(resource));
}

HRSRC WINAPI FindResourceExW(HMODULE module, LPCWSTR type, LPCWSTR name, WORD)
{
	return FindResourceW(module, name, type);
}

HGLOBAL WINAPI LoadResource(HMODULE, HRSRC resource)
{
	std::span<const BYTE> bytes = resourceBytes(reinterpret_cast<const ResourceData*>(resource));
	return bytes.empty() ? nullptr : reinterpret_cast<HGLOBAL>(const_cast<BYTE*>(bytes.data()));
}

LPVOID WINAPI LockResource(HGLOBAL data)
{
	return data;
}

DWORD WINAPI SizeofResource(HMODULE, HRSRC resource)
{
	return static_cast<DWORD>(resourceBytes(reinterpret_cast<const ResourceData*>(resource)).size());
}

BOOL WINAPI FreeResource(HGLOBAL)
{
	return FALSE;
}

// --- Strings --------------------------------------------------------------------------------------

int WINAPI LoadStringW(HINSTANCE, UINT id, LPWSTR buffer, int size)
{
	// Strings come in blocks of sixteen: each a length, then that many characters, not terminated.
	std::span<const BYTE> block = resourceBytes(MAKEINTRESOURCEW((id >> 4) + 1), RT_STRING);
	if (block.empty())
	{
		if (buffer && size > 0)
			buffer[0] = u'\0';
		return 0;
	}
	const BYTE* p = block.data();
	for (UINT i = 0; i < (id & 15); ++i)
	{
		WORD length = 0;
		std::memcpy(&length, p, 2);
		p += 2 + static_cast<size_t>(length) * 2;
	}
	WORD length = 0;
	std::memcpy(&length, p, 2);
	const auto* text = reinterpret_cast<const char16_t*>(p + 2);
	if (size == 0)
	{
		// With no size, the caller gets a read-only pointer to the string itself.
		*reinterpret_cast<const char16_t**>(buffer) = text;
		return length;
	}
	const int count = std::min<int>(length, size - 1);
	std::memcpy(buffer, text, static_cast<size_t>(count) * 2);
	buffer[count] = u'\0';
	return count;
}

// --- Icons, cursors, bitmaps ----------------------------------------------------------------------

HCURSOR WINAPI LoadCursorW(HINSTANCE instance, LPCWSTR name)
{
	if (!instance)
		return loadSystemCursor(name);
	HCURSOR cursor = loadIconResource(name, 32, 32, true);
	return cursor ? cursor : loadSystemCursor(IDC_ARROW);
}

HICON WINAPI LoadIconW(HINSTANCE instance, LPCWSTR name)
{
	if (!instance)
		return loadSystemIcon(name);
	HICON icon = loadIconResource(name, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), false);
	if (auto* object = nppmac::gdi::objectAs<nppmac::gdi::Icon>(icon, nppmac::gdi::ObjectType::icon))
		object->_shared = true; // LoadIcon's icons are shared and never destroyed
	return icon;
}

HRESULT WINAPI LoadIconWithScaleDown(HINSTANCE instance, PCWSTR name, int width, int height, HICON* icon)
{
	*icon = instance ? loadIconResource(name, width, height, false) : loadSystemIcon(name);
	return *icon ? S_OK : HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
}

HRESULT WINAPI LoadIconMetric(HINSTANCE instance, PCWSTR name, int metric, HICON* icon)
{
	const int size = metric == LIM_SMALL ? GetSystemMetrics(SM_CXSMICON) : GetSystemMetrics(SM_CXICON);
	return LoadIconWithScaleDown(instance, name, size, size, icon);
}

HBITMAP WINAPI LoadBitmapW(HINSTANCE instance, LPCWSTR name)
{
	return instance ? loadBitmapResource(name) : nullptr;
}

HANDLE WINAPI LoadImageW(HINSTANCE instance, LPCWSTR name, UINT type, int width, int height, UINT flags)
{
	if (flags & LR_LOADFROMFILE)
		return loadImageFile(name, type, width);
	switch (type)
	{
		case IMAGE_BITMAP:
		{
			HBITMAP bitmap = instance ? loadBitmapResource(name) : nullptr;
			if (bitmap && (flags & LR_LOADTRANSPARENT))
				makeBackgroundTransparent(bitmap);
			return bitmap;
		}
		case IMAGE_CURSOR:
			return instance ? loadIconResource(name, width, height, true) : loadSystemCursor(name);
		case IMAGE_ICON:
		{
			if (!instance)
				return loadSystemIcon(name);
			if ((flags & LR_DEFAULTSIZE) && width == 0)
				width = height = GetSystemMetrics(SM_CXICON);
			HICON icon = loadIconResource(name, width, height, false);
			auto* object = nppmac::gdi::objectAs<nppmac::gdi::Icon>(icon, nppmac::gdi::ObjectType::icon);
			if (object && (flags & LR_SHARED))
				object->_shared = true;
			return icon;
		}
		default:
			return nullptr;
	}
}

HANDLE WINAPI CopyImage(HANDLE image, UINT type, int, int, UINT)
{
	if (type == IMAGE_ICON || type == IMAGE_CURSOR)
		return CopyIcon(static_cast<HICON>(image));
	auto* bitmap = nppmac::gdi::objectAs<nppmac::gdi::Bitmap>(image, nppmac::gdi::ObjectType::bitmap);
	if (!bitmap)
		return nullptr;
	HBITMAP copy = CreateBitmap(bitmap->_width, bitmap->_height, 1, 32, nullptr);
	auto* target = nppmac::gdi::objectAs<nppmac::gdi::Bitmap>(copy, nppmac::gdi::ObjectType::bitmap);
	for (int y = 0; y < bitmap->_height; ++y)
		std::copy_n(bitmap->rowPointer(y), bitmap->_width, target->rowPointer(y));
	return copy;
}

HICON WINAPI CreateIconFromResourceEx(PBYTE data, DWORD size, BOOL isIcon, DWORD, int, int, UINT)
{
	if (!isIcon && size > 4)
	{
		WORD x = 0;
		WORD y = 0;
		std::memcpy(&x, data, 2);
		std::memcpy(&y, data + 2, 2);
		return createIcon(decodeIconImage(data + 4, size - 4), false, POINT{x, y});
	}
	return createIcon(decodeIconImage(data, size), isIcon != FALSE, {});
}

// --- Version information --------------------------------------------------------------------------
// The program's own VERSIONINFO; other files have none.

DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR fileName, LPDWORD handle)
{
	if (handle)
		*handle = 0;
	const std::u16string name{fileName ? fileName : u""};
	if (name.size() < 13 || _wcsicmp(name.substr(name.size() - 13).c_str(), u"notepad++.exe") != 0)
	{
		SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
		return 0;
	}
	return static_cast<DWORD>(resourceBytes(MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION).size());
}

BOOL WINAPI GetFileVersionInfoW(LPCWSTR fileName, DWORD, DWORD size, LPVOID data)
{
	if (!GetFileVersionInfoSizeW(fileName, nullptr))
		return FALSE;
	std::span<const BYTE> bytes = resourceBytes(MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION);
	std::memcpy(data, bytes.data(), std::min<size_t>(size, bytes.size()));
	return TRUE;
}

BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID* buffer, PUINT length)
{
	// The VS_VERSIONINFO tree: each node is wLength, wValueLength, wType, szKey, padding, Value,
	// padding, Children. A path such as "\\StringFileInfo\\040904b0\\FileVersion" names a node.
	struct Node
	{
		const BYTE* _start = nullptr;
		WORD _length = 0;
		WORD _valueLength = 0;
		WORD _type = 0;
		std::u16string _key;
		const BYTE* _value = nullptr;
		const BYTE* _children = nullptr;
	};
	auto readNode = [](const BYTE* p)
	{
		Node node;
		node._start = p;
		std::memcpy(&node._length, p, 2);
		std::memcpy(&node._valueLength, p + 2, 2);
		std::memcpy(&node._type, p + 4, 2);
		const BYTE* q = p + 6;
		for (;;)
		{
			char16_t ch = 0;
			std::memcpy(&ch, q, 2);
			q += 2;
			if (ch == 0)
				break;
			node._key.push_back(ch);
		}
		auto align = [p](const BYTE* x) { return p + ((static_cast<size_t>(x - p) + 3) & ~static_cast<size_t>(3)); };
		node._value = align(q);
		const size_t valueBytes = node._type == 1 ? static_cast<size_t>(node._valueLength) * 2 : node._valueLength;
		node._children = align(node._value + valueBytes);
		return node;
	};

	Node node = readNode(static_cast<const BYTE*>(block));
	std::u16string_view path{subBlock ? subBlock : u"\\"};
	for (;;)
	{
		while (!path.empty() && path.front() == u'\\')
			path.remove_prefix(1);
		if (path.empty())
			break;
		const size_t separator = path.find(u'\\');
		const std::u16string part{path.substr(0, separator)};
		path = separator == std::u16string_view::npos ? std::u16string_view{} : path.substr(separator);
		bool found = false;
		for (const BYTE* child = node._children; child < node._start + node._length;)
		{
			const Node candidate = readNode(child);
			if (candidate._length == 0)
				break;
			if (_wcsicmp(candidate._key.c_str(), part.c_str()) == 0)
			{
				node = candidate;
				found = true;
				break;
			}
			child += (static_cast<size_t>(candidate._length) + 3) & ~static_cast<size_t>(3);
		}
		if (!found)
			return FALSE;
	}
	*buffer = const_cast<BYTE*>(node._value);
	*length = node._valueLength;
	return node._valueLength ? TRUE : FALSE;
}
