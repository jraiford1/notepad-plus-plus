#!/bin/bash
# This file is part of Notepad++ project (macOS and Linux build)
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
# zip and shasum where there are (macOS), Python's zipfile and sha256sum where not (Linux).
if command -v zip > /dev/null; then
	(cd "$work" && zip -q "$resources/plugins-catalog/Sample.zip" Sample.dll)
else
	(cd "$work" && python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1], "w").write("Sample.dll")' "$resources/plugins-catalog/Sample.zip")
fi
if command -v sha256sum > /dev/null; then
	sha="$(sha256sum "$resources/plugins-catalog/Sample.zip" | cut -d' ' -f1)"
else
	sha="$(shasum -a 256 "$resources/plugins-catalog/Sample.zip" | cut -d' ' -f1)"
fi
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

# The plugins folder as Notepad++ gives it: "<program folder>\plugins", joined with '\' as on Windows.
"$gup" -unzipTo "$resources/notepad++.exe" "$resources\\plugins" "Sample plugins-catalog/Sample.zip $sha"
expect install '[ -f "$resources/plugins/Sample/Sample.dll" ] && [ ! -e "$resources\\plugins" ]'

"$gup" -unzipTo "$resources/notepad++.exe" "$resources/plugins" "Other plugins-catalog/Sample.zip 0000" 2> "$work/wrong.log"
expect wrong-hash '[ ! -e "$resources/plugins/Other/Sample.dll" ] && grep -q "SHA-256" "$work/wrong.log"'

"$gup" -moveFolder "$resources/notepad++.exe" "$resources\\plugins" "$resources\\plugins\\disabled" "Sample"
expect disable '[ -f "$resources/plugins/disabled/Sample/Sample.dll" ] && [ ! -e "$resources/plugins/Sample" ]'

"$gup" -clean "$resources/notepad++.exe" "$resources\\plugins\\disabled" "Sample"
expect remove '[ ! -e "$resources/plugins/disabled/Sample" ]'

# On the Mac the plugins are kept out of the app (W73): what Notepad++ names its plugins folder is
# Documents/Notepad++/plugins, here the test's stand-in for it.
if [ "$(uname)" = Darwin ]; then
	NPPMAC_TEST_PLUGINS_FOLDER="$work/Documents-plugins" "$gup" -unzipTo "$resources/notepad++.exe" "$resources\\plugins" "Sample plugins-catalog/Sample.zip $sha"
	expect install-outside-app '[ -f "$work/Documents-plugins/Sample/Sample.dll" ] && [ ! -e "$resources/plugins/Sample" ]'
fi

if [ $failures -ne 0 ]; then
	echo "$failures failure(s)"
	exit 1
fi
echo "all passed"
