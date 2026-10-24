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

# Builds the macOS app and runs its tests: what CI does, and what anyone can run locally.
#
#     macos/ci/build-and-test.sh [build directory]     (default: macos/build)
#
# NPPMAC_SKIP_GUI_TESTS=1 leaves out the tests that need a window server.

set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/macos/build}"

for formula in llvm bison; do
	if ! brew list --versions "$formula" > /dev/null 2>&1; then
		echo "Installing $formula"
		brew install "$formula"
	fi
done

cmake -S "$root/macos" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel "$(sysctl -n hw.ncpu)"

# Upstream's sources must be as upstream has them: the port lives in macos/ only.
if git -C "$root" rev-parse --verify --quiet upstream/master > /dev/null; then
	if ! git -C "$root" diff --quiet upstream/master -- PowerEditor scintilla lexilla boostregex; then
		echo "Upstream sources differ from upstream/master:"
		git -C "$root" diff --stat upstream/master -- PowerEditor scintilla lexilla boostregex
		exit 1
	fi
fi

if [ "${NPPMAC_SKIP_GUI_TESTS:-0}" = "1" ]; then
	ctest --test-dir "$build" --output-on-failure -LE gui
else
	ctest --test-dir "$build" --output-on-failure
fi
