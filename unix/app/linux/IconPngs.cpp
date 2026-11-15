// This file is part of Notepad++ project (macOS and Linux build)
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


// npp-icon-pngs <file.ico> <folder>: each size of an icon file as <folder>/<size>.png, for the Linux
// build's icon theme and program folder. Where a size comes in several depths, the deepest is kept.

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtGui/QImage>
#include <QtGui/QImageReader>

#include <cstdio>
#include <map>

int main(int argc, char* argv[])
{
	QCoreApplication application(argc, argv); // for the image format plugins
	if (argc != 3)
	{
		std::fprintf(stderr, "usage: npp-icon-pngs <file.ico> <folder>\n");
		return 2;
	}
	QImageReader reader(QString::fromLocal8Bit(argv[1]));
	std::map<int, QImage> bySize;
	for (int index = 0; index < std::max(1, reader.imageCount()); ++index)
	{
		if (index > 0 && !reader.jumpToImage(index))
			break;
		QImage image = reader.read();
		if (image.isNull() || image.width() != image.height())
			continue;
		image = image.convertToFormat(QImage::Format_ARGB32);
		QImage& kept = bySize[image.width()];
		if (kept.isNull() || image.depth() >= kept.depth())
			kept = image;
	}
	if (bySize.empty())
	{
		std::fprintf(stderr, "npp-icon-pngs: no image in %s\n", argv[1]);
		return 1;
	}
	const QDir folder(QString::fromLocal8Bit(argv[2]));
	folder.mkpath(".");
	for (const auto& [size, image] : bySize)
	{
		if (!image.save(folder.filePath(QString::number(size) + ".png"), "PNG"))
		{
			std::fprintf(stderr, "npp-icon-pngs: cannot write %d.png\n", size);
			return 1;
		}
	}
	return 0;
}
