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

# Tests of the updater Plugins Admin runs (gup.exe), on a stand-in app: installing a plugin from the
# app's catalog (checked against its SHA-256), refusing a package that is not the one expected,
# removing a plugin, and moving one to plugins/disabled.
#
#     gup-test.sh <gup.exe> <a plugin's .dll>

set -u

gup="${1:?usage: gup-test.sh <gup.exe> <plugin .dll>}"
plugin="${2:?usage: gup-test.sh <gup.exe> <plugin .dll>}"
work="$(mktemp -d "${TMPDIR:-/tmp}/npp-gup.XXXXXX")"
trap 'rm -rf "$work"' EXIT
export NPPMAC_TEST_RUN=1

resources="$work/Test.app/Contents/Resources"
mkdir -p "$resources/plugins-catalog" "$resources/plugins"
touch "$resources/notepad++.exe"
cp "$plugin" "$work/Sample.dll"
(cd "$work" && zip -q "$resources/plugins-catalog/Sample.zip" Sample.dll)
sha="$(shasum -a 256 "$resources/plugins-catalog/Sample.zip" | cut -d' ' -f1)"
failures=0
expect()
{
	if eval "$2"; then
		echo "ok   $1"
	else
		echo "FAIL $1"
		failures=$((failures + 1))
	fi
}

"$gup" -unzipTo "$resources/notepad++.exe" "$resources/plugins" "Sample plugins-catalog/Sample.zip $sha"
expect install '[ -f "$resources/plugins/Sample/Sample.dll" ]'

"$gup" -unzipTo "$resources/notepad++.exe" "$resources/plugins" "Other plugins-catalog/Sample.zip 0000" 2> "$work/wrong.log"
expect wrong-hash '[ ! -e "$resources/plugins/Other/Sample.dll" ] && grep -q "SHA-256" "$work/wrong.log"'

"$gup" -moveFolder "$resources/notepad++.exe" "$resources/plugins" "$resources/plugins/disabled" "Sample"
expect disable '[ -f "$resources/plugins/disabled/Sample/Sample.dll" ] && [ ! -e "$resources/plugins/Sample" ]'

"$gup" -clean "$resources/notepad++.exe" "$resources/plugins/disabled" "Sample"
expect remove '[ ! -e "$resources/plugins/disabled/Sample" ]'

if [ $failures -ne 0 ]; then
	echo "$failures failure(s)"
	exit 1
fi
echo "all passed"
