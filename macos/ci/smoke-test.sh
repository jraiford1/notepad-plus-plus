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

# End-to-end smoke test: runs the app the way a user would, through the runtime's test aids, with a
# throwaway settings folder. Fails on a crash, a hang or a wrong result.
#
#     smoke-test.sh <path to Notepad++.app/Contents/MacOS/Notepad++> [<path to the HelloMac plugin>]
#
# With the plugin, it is installed in the app's plugins folder for the test and removed after.
# NPPMAC_TEST_ARCH=x86_64 runs that slice of a universal app (on Apple silicon, through Rosetta); the
# test is skipped (exit status 77) where it cannot run.

set -u

app="${1:?usage: smoke-test.sh <Notepad++ executable> [<HelloMac plugin>]}"
plugin="${2:-}"
work="$(mktemp -d "${TMPDIR:-/tmp}/npp-smoke.XXXXXX")"
pluginFolder="$(dirname "$app")/../Resources/plugins/HelloMac"
launcher=()
if [ -n "${NPPMAC_TEST_ARCH:-}" ]; then
	if ! /usr/bin/arch "-$NPPMAC_TEST_ARCH" /usr/bin/true 2> /dev/null; then
		echo "skipped: this Mac cannot run $NPPMAC_TEST_ARCH"
		exit 77
	fi
	launcher=(/usr/bin/arch "-$NPPMAC_TEST_ARCH")
fi
cleanup()
{
	rm -rf "$work"
	if [ -n "$plugin" ]; then
		rm -rf "$pluginFolder"
	fi
}
trap cleanup EXIT
settings="$work/settings"
mkdir -p "$settings"
failures=0

# Runs the app with the given environment and arguments; a snapshot after 5 seconds ends the run.
run()
{
	local name="$1"
	shift
	local log="$work/$name.log"
	# perl's alarm is the timeout macOS has without coreutils.
	env NPPMAC_TEST_RUN=1 NPPMAC_QUIET_STUBS=1 NPPMAC_SNAPSHOT="$work/$name.png" NPPMAC_SNAPSHOT_DELAY=5 NPPMAC_SNAPSHOT_EXIT=1 "$@" \
		perl -e 'alarm 60; exec @ARGV' ${launcher[@]+"${launcher[@]}"} "$app" -settingsDir="$settings" ${files[@]+"${files[@]}"} > "$log" 2>&1
	local status=$?
	if [ $status -ne 0 ]; then
		echo "FAIL $name: exit status $status"
		sed -n '1,40p' "$log"
		failures=$((failures + 1))
		return 1
	fi
	if [ ! -s "$work/$name.png" ]; then
		echo "FAIL $name: no snapshot"
		failures=$((failures + 1))
		return 1
	fi
	return 0
}

expect_file()
{
	local name="$1" path="$2" expected="$3"
	if [ "$(cat "$path" 2>/dev/null)" != "$expected" ]; then
		echo "FAIL $name: $path holds:"
		cat "$path" 2>/dev/null || echo "(no file)"
		failures=$((failures + 1))
		return 1
	fi
	echo "ok   $name"
}

expect_log()
{
	local name="$1" pattern="$2"
	if ! grep -q -- "$pattern" "$work/$name.log"; then
		echo "FAIL $name: the log has no line matching $pattern"
		grep -- 'Edit id=' "$work/$name.log" | head -5
		failures=$((failures + 1))
		return 1
	fi
	echo "ok   $name"
}

# Starts, shows the main window.
files=()
run startup && echo "ok   startup"

# Opens a file, types at the start, saves with Command-S.
printf 'first line\nsecond line\n' > "$work/edit.txt"
files=("$work/edit.txt")
run edit NPPMAC_TEST_TYPE='edited ^s' && expect_file edit "$work/edit.txt" $'edited first line\nsecond line'

# A new document saved through the Save As panel (answered by the test aid).
files=()
run save-as NPPMAC_TEST_TYPE='new document^s' NPPMAC_TEST_FILE_PANEL="$work/saved.txt" \
	&& expect_file save-as "$work/saved.txt" 'new document'

# Find: Command-F, a search twice, Escape; the document must be left as it was.
printf 'one two\ntwo three\n' > "$work/find.txt"
files=("$work/find.txt")
run find NPPMAC_TEST_TYPE=$'^ftwo\n\n\x1b' && expect_file find "$work/find.txt" $'one two\ntwo three'

# An input method in the Find box: typed text, then a composition committed as other text.
files=()
run ime NPPMAC_TEST_TYPE='^fab' NPPMAC_TEST_IME='かな|仮名' NPPMAC_DUMP_TREE=1 && expect_log ime 'Edit id=1001 .* text="ab仮名"'

# Printing (File > Print, IDM_FILE_PRINT): the print job's PDF, taken instead of printing, has the
# document's 90 lines on two pages.
for i in $(seq 1 90); do echo "Line $i: the quick brown fox"; done > "$work/print.txt"
files=("$work/print.txt")
if run print NPPMAC_TEST_COMMAND=41010 NPPMAC_TEST_PRINT_PDF="$work/print.pdf"; then
	pages="$(osascript -l JavaScript -e "ObjC.import('PDFKit'); var d = \$.PDFDocument.alloc.initWithURL(\$.NSURL.fileURLWithPath('$work/print.pdf')); d ? d.pageCount + ' ' + d.pageAtIndex(0).string.js.indexOf('Line 1: the quick brown fox') : 'none'" 2>&1)"
	if [ "${pages%% *}" = "2" ] && [ "${pages##* }" != "-1" ]; then
		echo "ok   print"
	else
		echo "FAIL print: the PDF reads $pages"
		failures=$((failures + 1))
	fi
fi

# Dialogs and panels open without crashing: About, Preferences, Shortcut Mapper, Function List.
files=("$work/find.txt")
for command in 47000 48011 48009 44084; do
	run "command-$command" NPPMAC_TEST_COMMAND=$command && echo "ok   command $command"
done

# A plugin: HelloMac's first command (the first plugin command id, 22000) inserts its greeting, which
# is then saved through the Save As panel.
if [ -n "$plugin" ]; then
	mkdir -p "$pluginFolder"
	cp "$plugin" "$pluginFolder/HelloMac.dll"
	files=()
	run plugin NPPMAC_TEST_COMMAND=22000 NPPMAC_TEST_TYPE='^s' NPPMAC_TEST_TYPE_DELAY=3 NPPMAC_TEST_FILE_PANEL="$work/plugin.txt" \
		&& expect_file plugin "$work/plugin.txt" 'Hello from a plugin on macOS'
	rm -rf "$pluginFolder"
fi

# Dark mode, following the system.
files=()
run dark NPPMAC_TEST_APPEARANCE=dark && echo "ok   dark"

if [ $failures -ne 0 ]; then
	echo "$failures failure(s)"
	exit 1
fi
echo "all passed"
