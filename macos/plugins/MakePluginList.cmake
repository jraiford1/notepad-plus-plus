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

# Packs the catalog's built plugins for Plugins Admin: each into Resources/plugins-catalog/<Name>.zip
# (<Name>.dll, <Name>.res and the files listed in <Name>.files, as a plugin's zip holds them on
# Windows), and the plugin list Plugins Admin reads, Resources/plugins/Config/nppPluginList.json: the
# official list's entries for them (catalog-info.json) with each zip as its package and the zip's
# SHA-256 as its id.
#
# Run by cmake -P with:
#   NAMES       |-separated plugin names
#   PLUGIN_DIR  the folder of the built plugins (<Name>/<Name>.dll)
#   INFO        catalog-info.json
#   RESOURCES   the app's Contents/Resources

string(REPLACE "|" ";" names "${NAMES}")
file(READ "${INFO}" info)
file(MAKE_DIRECTORY "${RESOURCES}/plugins-catalog" "${RESOURCES}/plugins/Config")

set(entries "")
foreach(name IN LISTS names)
	set(dll "${PLUGIN_DIR}/${name}/${name}.dll")
	string(JSON entry ERROR_VARIABLE missing GET "${info}" "${name}")
	if(NOT EXISTS "${dll}" OR missing)
		continue() # not built (being ported) or not in the list
	endif()
	set(files "${name}.dll")
	if(EXISTS "${PLUGIN_DIR}/${name}/${name}.res")
		list(APPEND files "${name}.res")
	endif()
	if(EXISTS "${PLUGIN_DIR}/${name}/${name}.files")
		file(STRINGS "${PLUGIN_DIR}/${name}/${name}.files" dataFiles)
		list(APPEND files ${dataFiles})
	endif()
	set(zip "${RESOURCES}/plugins-catalog/${name}.zip")
	file(REMOVE "${zip}")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip}" --format=zip ${files}
		WORKING_DIRECTORY "${PLUGIN_DIR}/${name}" RESULT_VARIABLE packed)
	if(NOT packed EQUAL 0)
		message(FATAL_ERROR "Cannot pack plugin ${name}")
	endif()
	file(SHA256 "${zip}" sha256)
	string(JSON entry SET "${entry}" id "\"${sha256}\"")
	string(JSON entry SET "${entry}" repository "\"plugins-catalog/${name}.zip\"")
	list(APPEND entries "${entry}")
endforeach()

list(JOIN entries ",\n" joined)
file(WRITE "${RESOURCES}/plugins/Config/nppPluginList.json"
	"{\n\"name\": \"Notepad++ plugin list (macOS)\",\n\"version\": \"1.0\",\n\"npp-plugins\": [\n${joined}\n]\n}\n")
