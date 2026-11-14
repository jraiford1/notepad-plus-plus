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

# Checks the plugin list the build writes for Plugins Admin (MakePluginList.cmake): it is valid JSON
# (Notepad++ cannot start otherwise), and each entry has its package, with the SHA-256 it gives.
#
# Run by cmake -P with RESOURCES, the app's Contents/Resources.

set(listFile "${RESOURCES}/plugins/Config/nppPluginList.json")
file(READ "${listFile}" content)
string(JSON count ERROR_VARIABLE invalid LENGTH "${content}" npp-plugins)
if(invalid)
	message(FATAL_ERROR "${listFile} is not a valid plugin list: ${invalid}")
endif()
if(count EQUAL 0)
	message(FATAL_ERROR "${listFile} lists no plugins")
endif()
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
	string(JSON name GET "${content}" npp-plugins ${index} folder-name)
	string(JSON package GET "${content}" npp-plugins ${index} repository)
	string(JSON id GET "${content}" npp-plugins ${index} id)
	if(NOT EXISTS "${RESOURCES}/${package}")
		message(FATAL_ERROR "${name}: no package ${package}")
	endif()
	file(SHA256 "${RESOURCES}/${package}" sha256)
	if(NOT sha256 STREQUAL id)
		message(FATAL_ERROR "${name}: the package's SHA-256 is not the list's")
	endif()
endforeach()
message(STATUS "${count} plugins in the list, each with its package")
