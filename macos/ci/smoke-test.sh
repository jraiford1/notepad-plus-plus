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
# What the runs store per user (plugins' settings) goes here, not to the user's Application Support.
mkdir -p "$work/appdata"
pluginFolder="$(dirname "$app")/../Resources/plugins/HelloMac"
# The third argument: the folder of the catalog's built plugins (<Name>/<Name>.dll), all loaded together.
catalog="${3:-}"
pluginsRoot="$(dirname "$app")/../Resources/plugins"
catalogInstalled=()
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
	for name in ${catalogInstalled[@]+"${catalogInstalled[@]}"}; do
		rm -rf "${pluginsRoot:?}/$name"
	done
}
trap cleanup EXIT
settings="$work/settings"
mkdir -p "$settings"
# Notepad++'s own dark mode on, following the system ("Follow Windows"), as a user sets it.
darkSettings="$work/dark-settings"
mkdir -p "$darkSettings"
printf '<?xml version="1.0" encoding="UTF-8" ?>\n<NotepadPlus>\n<GUIConfigs>\n<GUIConfig name="DarkMode" enable="yes" enableWindowsMode="yes" />\n</GUIConfigs>\n</NotepadPlus>\n' > "$darkSettings/config.xml"
failures=0

# Runs the app with the given environment and arguments; a snapshot after 5 seconds ends the run.
run()
{
	local name="$1"
	shift
	local log="$work/$name.log"
	# perl's alarm is the timeout macOS has without coreutils.
	env NPPMAC_TEST_RUN=1 NPPMAC_TEST_APPDATA="$work/appdata" NPPMAC_QUIET_STUBS=1 NPPMAC_SNAPSHOT="$work/$name.png" NPPMAC_SNAPSHOT_DELAY=5 NPPMAC_SNAPSHOT_EXIT=1 "$@" \
		perl -e 'alarm 60; exec @ARGV' ${launcher[@]+"${launcher[@]}"} "$app" -settingsDir="${runSettings:-$settings}" ${files[@]+"${files[@]}"} > "$log" 2>&1
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

# Undo, select all, copy and paste with the Command keys. (Undo is a Scintilla key command, assigned
# with Windows' capital-letter key codes; Select All too.)
# Each starts from fresh settings, so that it types into an empty new document.
files=()
mkdir -p "$work/undo-settings" "$work/paste-settings" "$work/edit-undo-settings"
runSettings="$work/undo-settings" run undo NPPMAC_TEST_TYPE='abcd^zxy^s' NPPMAC_TEST_FILE_PANEL="$work/undo.txt" \
	&& expect_file undo "$work/undo.txt" 'xy'
runSettings="$work/paste-settings" run paste NPPMAC_TEST_TYPE='abc^a^c^v^v^s' NPPMAC_TEST_FILE_PANEL="$work/paste.txt" \
	&& expect_file paste "$work/paste.txt" 'abcabc'
# In a text box, Command-Z takes back the whole word typed, as in Windows' edit control.
runSettings="$work/edit-undo-settings" run edit-undo NPPMAC_TEST_TYPE='^fabcd^z' NPPMAC_DUMP_TREE=1 \
	&& expect_log edit-undo 'Edit id=1001 .* text=""'

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

# A file changed by another program while open: coming back to Notepad++ (WM_ACTIVATEAPP, played by
# the test aid) asks whether to reload it.
printf 'before\n' > "$work/switch.txt"
files=("$work/switch.txt")
mkdir -p "$work/switch-settings"
(sleep 2.5; printf 'after\n' > "$work/switch.txt") &
runSettings="$work/switch-settings" run switch NPPMAC_TEST_SWITCH_BACK=3.5 \
	&& expect_log switch 'NPPMAC alert: .*modified by another program'
wait

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
	# Its resources, beside it.
	if [ -f "${plugin%.dll}.res" ]; then
		cp "${plugin%.dll}.res" "$pluginFolder/HelloMac.res"
	fi
	files=()
	run plugin NPPMAC_TEST_COMMAND=22000 NPPMAC_TEST_TYPE='^s' NPPMAC_TEST_TYPE_DELAY=3 NPPMAC_TEST_FILE_PANEL="$work/plugin.txt" \
		&& expect_file plugin "$work/plugin.txt" 'Hello from a plugin on macOS'
	# Its third command (22002) inserts a string from its own resources and its folder's name: its
	# module handle (DllMain), LoadString from its .res and GetModuleFileName at work.
	files=()
	mkdir -p "$work/plugin-resources-settings"
	runSettings="$work/plugin-resources-settings" run plugin-resources NPPMAC_TEST_COMMAND=22002 NPPMAC_TEST_TYPE='^s' NPPMAC_TEST_TYPE_DELAY=3 \
		NPPMAC_TEST_FILE_PANEL="$work/plugin-resources.txt" \
		&& expect_file plugin-resources "$work/plugin-resources.txt" 'Hello from a plugin resource in HelloMac'
	rm -rf "$pluginFolder"
fi

# The catalog's plugins: installed together, each must appear in the Plugins menu (a submenu named by
# the plugin), so each loaded and answered Notepad++'s calls.
if [ -n "$catalog" ] && [ -d "$catalog" ]; then
	for dll in "$catalog"/*/*.dll; do
		name="$(basename "$(dirname "$dll")")"
		if [ "$name" = HelloMac ] || [ "$(basename "$dll" .dll)" != "$name" ]; then
			continue
		fi
		mkdir -p "$pluginsRoot/$name"
		cp "$dll" "$pluginsRoot/$name/"
		if [ -f "${dll%.dll}.res" ]; then
			cp "${dll%.dll}.res" "$pluginsRoot/$name/"
		fi
		# The files the plugin's package has beside it (a default configuration...).
		if [ -f "${dll%.dll}.files" ]; then
			while IFS= read -r dataFile; do
				if [ -n "$dataFile" ]; then
					mkdir -p "$(dirname "$pluginsRoot/$name/$dataFile")"
					cp "$(dirname "$dll")/$dataFile" "$pluginsRoot/$name/$dataFile"
				fi
			done < "${dll%.dll}.files"
		fi
		catalogInstalled+=("$name")
	done
	files=()
	mkdir -p "$work/plugins-load-settings"
	if runSettings="$work/plugins-load-settings" run plugins-load NPPMAC_DUMP_TREE=1; then
		# The Plugins menu's submenus: its entries one level in, but its own commands.
		# A submenu whose plugin gives it no name (SessionMgr names it only after Notepad++ asks) counts too.
		loaded="$(awk '/^Plugins$/ {in_menu=1; next} /^[^ ]/ {in_menu=0} in_menu && /^  ([^ ]|$)/ {print}' "$work/plugins-load.log" \
			| grep -v -e '^  Open Plugins Folder' -e '^  Plugins Admin' -e '^  -' | wc -l | tr -d ' ')"
		if [ "$loaded" -ge "${#catalogInstalled[@]}" ]; then
			echo "ok   plugins-load (${#catalogInstalled[@]} plugins)"
		else
			echo "FAIL plugins-load: $loaded of ${#catalogInstalled[@]} plugins in the Plugins menu"
			awk '/^Plugins$/ {in_menu=1; print; next} /^[^ ]/ {in_menu=0} in_menu && /^  [^ ]/ {print}' "$work/plugins-load.log"
			failures=$((failures + 1))
		fi
	fi
	for name in "${catalogInstalled[@]}"; do
		rm -rf "${pluginsRoot:?}/$name"
	done
	catalogInstalled=()
fi

# A rich edit in a docked panel: NppExec alone (its commands are 22000 on) runs the editor's text as
# a script (Select All, then Execute Selected Text), whose output its console (RichEdit20W) shows.
if [ -n "$catalog" ] && [ -f "$catalog/NppExec/NppExec.dll" ]; then
	mkdir -p "$pluginsRoot/NppExec"
	cp "$catalog/NppExec/NppExec.dll" "$pluginsRoot/NppExec/"
	if [ -f "$catalog/NppExec/NppExec.res" ]; then
		cp "$catalog/NppExec/NppExec.res" "$pluginsRoot/NppExec/"
	fi
	printf 'echo hello from nppexec\n' > "$work/nppexec.txt"
	files=("$work/nppexec.txt")
	mkdir -p "$work/rich-edit-settings"
	if runSettings="$work/rich-edit-settings" run rich-edit NPPMAC_TEST_COMMAND=42007,22002 NPPMAC_DUMP_TREE=1; then
		if grep -q 'RichEdit20W .* visible text="hello from nppexec' "$work/rich-edit.log"; then
			echo "ok   rich-edit"
		else
			echo "FAIL rich-edit: NppExec's console does not show the script's output"
			grep 'RichEdit20W' "$work/rich-edit.log" | head -3
			failures=$((failures + 1))
		fi
	fi
	rm -rf "${pluginsRoot:?}/NppExec"
fi

# Plugins Admin (IDM_SETTING_PLUGINADM) opens with the app's plugin list, without an error.
files=()
mkdir -p "$work/plugins-admin-settings"
if runSettings="$work/plugins-admin-settings" run plugins-admin NPPMAC_TEST_COMMAND=48015 NPPMAC_DUMP_TREE=1; then
	if grep -q 'text="Plugins Admin"' "$work/plugins-admin.log" && ! grep -q 'NPPMAC alert' "$work/plugins-admin.log"; then
		echo "ok   plugins-admin"
	else
		echo "FAIL plugins-admin: no Plugins Admin window, or an alert"
		grep 'NPPMAC alert\|Plugins Admin' "$work/plugins-admin.log" | head -5
		failures=$((failures + 1))
	fi
fi

# Dark mode, following the system.
files=()
runSettings="$darkSettings" run dark NPPMAC_TEST_APPEARANCE=dark && echo "ok   dark"

# Files from Finder: opened by LaunchServices (open -g: in the background, as tests must stay),
# when they launch the program and when it is already running.
bundle="$(cd "$(dirname "$app")/../.." && pwd)"
openArch=()
if [ -n "${NPPMAC_TEST_ARCH:-}" ]; then
	openArch=(--arch "$NPPMAC_TEST_ARCH")
fi
finder()
{
	local name="$1" delay="$2"
	shift 2
	mkdir -p "$work/$name-settings"
	perl -e 'alarm 60; exec @ARGV' open -g -n -W ${openArch[@]+"${openArch[@]}"} -a "$bundle" --env NPPMAC_TEST_RUN=1 --env NPPMAC_QUIET_STUBS=1 \
		--env NPPMAC_DUMP_TREE=1 --env NPPMAC_SNAPSHOT="$work/$name.png" --env NPPMAC_SNAPSHOT_DELAY="$delay" --env NPPMAC_SNAPSHOT_EXIT=1 \
		--stderr "$work/$name.log" "$@" --args -settingsDir="$work/$name-settings"
}
expect_title()
{
	local name="$1" file="$2"
	# By its name: Finder gives the resolved path (/private/var/... for $TMPDIR).
	if grep -q "Notepad++ id=.* text=\".*/$file - Notepad++\"" "$work/$name.log"; then
		echo "ok   $name"
	else
		echo "FAIL $name: the main window is not showing $file"
		grep -m1 'Notepad++ id=' "$work/$name.log"
		failures=$((failures + 1))
	fi
}
echo one > "$work/first.txt"
echo two > "$work/second.txt"
echo three > "$work/third.txt"
finder finder-launch 4 "$work/first.txt" "$work/second.txt"
expect_title finder-launch second.txt
finder finder-running 6 &
# Only to the test's own instance: with none running, open would start one with the user's settings.
running=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
	sleep 0.5
	if pgrep -f "finder-running-settings" > /dev/null; then
		running=1
		break
	fi
done
if [ $running -eq 1 ]; then
	sleep 2
	open -g -a "$bundle" "$work/third.txt"
	wait
	expect_title finder-running third.txt
else
	wait
	echo "FAIL finder-running: the test instance did not start"
	failures=$((failures + 1))
fi

# Quitting through File > Exit (IDM_FILE_EXIT), as a user does: the program's static objects are
# destroyed, and upstream's call into the runtime from theirs (dark mode's theme deletes its pens).
# The other runs end with _exit and skip that.
quit()
{
	local name="$1" folder="$2"
	shift 2
	env NPPMAC_TEST_RUN=1 NPPMAC_QUIET_STUBS=1 NPPMAC_TEST_COMMAND=41011 NPPMAC_TEST_COMMAND_DELAY=3 "$@" \
		perl -e 'alarm 60; exec @ARGV' ${launcher[@]+"${launcher[@]}"} "$app" -settingsDir="$folder" > "$work/$name.log" 2>&1
	local status=$?
	if [ $status -ne 0 ]; then
		echo "FAIL $name: exit status $status"
		grep -i 'terminat\|exception' "$work/$name.log" | head -5
		failures=$((failures + 1))
		return 1
	fi
	echo "ok   $name"
}
quit quit-light "$settings"
quit quit-dark "$darkSettings" NPPMAC_TEST_APPEARANCE=dark

# The session: a file open when Notepad++ quits is open again when it starts.
printf 'kept\n' > "$work/session.txt"
mkdir -p "$work/session-settings"
env NPPMAC_TEST_RUN=1 NPPMAC_QUIET_STUBS=1 NPPMAC_TEST_COMMAND=41011 NPPMAC_TEST_COMMAND_DELAY=3 \
	perl -e 'alarm 60; exec @ARGV' ${launcher[@]+"${launcher[@]}"} "$app" -settingsDir="$work/session-settings" "$work/session.txt" \
	> "$work/session-quit.log" 2>&1
files=()
if runSettings="$work/session-settings" run session NPPMAC_DUMP_TREE=1; then
	if grep -q 'Notepad++ id=.* text=".*/session.txt - Notepad++"' "$work/session.log"; then
		echo "ok   session"
	else
		echo "FAIL session: the file was not open again"
		grep -m1 'Notepad++ id=' "$work/session.log"
		failures=$((failures + 1))
	fi
fi

# Starting again with the window position that quit saved: the editor's tabs sit below the toolbar,
# not over it (the window must be visible when its saved placement lays it out).
files=()
if runSettings="$darkSettings" run restart NPPMAC_TEST_APPEARANCE=dark NPPMAC_DUMP_TREE=1; then
	if grep -q 'SysTabControl32 id=0 rect=(0,0,' "$work/restart.log" || ! grep -q 'SysTabControl32 id=0 rect=' "$work/restart.log"; then
		echo "FAIL restart: the tabs are not below the toolbar"
		grep 'SysTabControl32 id=0\|ReBarWindow32' "$work/restart.log" | head -3
		failures=$((failures + 1))
	else
		echo "ok   restart"
	fi
fi

if [ $failures -ne 0 ]; then
	echo "$failures failure(s)"
	exit 1
fi
echo "all passed"
