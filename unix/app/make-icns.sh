#!/bin/bash
# This file is part of Notepad++ project (macOS build)
# Copyright (C)2026 Notepad++ macOS contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Makes the app's .icns from an image sips can read (a .ico, .png...), with the sizes macOS uses up
# to the image's own size: nothing is scaled up.
#
#     make-icns.sh <image> <output.icns>

set -euo pipefail

source="$1"
output="$2"
work="$(mktemp -d "${TMPDIR:-/tmp}/npp-icns.XXXXXX")"
trap 'rm -rf "$work"' EXIT

sips -s format png "$source" --out "$work/source.png" > /dev/null
largest=$(sips -g pixelWidth "$work/source.png" | awk '/pixelWidth/ { print $2 }')
mkdir "$work/icon.iconset"
for size in 16 32 128 256 512; do
	if [ "$size" -le "$largest" ]; then
		sips -z "$size" "$size" "$work/source.png" --out "$work/icon.iconset/icon_${size}x${size}.png" > /dev/null
	fi
	if [ $((size * 2)) -le "$largest" ]; then
		sips -z $((size * 2)) $((size * 2)) "$work/source.png" --out "$work/icon.iconset/icon_${size}x${size}@2x.png" > /dev/null
	fi
done
iconutil -c icns "$work/icon.iconset" -o "$output"
