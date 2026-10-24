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

# Plugins on macOS. A plugin's sources are the Windows ones: they go through the migration engine,
# as Notepad++'s do (wchar_t becomes char16_t, so the plugin interface matches the app's), compile
# against the runtime's headers, and link as a Mach-O bundle whose Win32 calls bind to the runtime in
# the app. The file keeps upstream's name and place: plugins/<Name>/<Name>.dll.
#
#     nppmac_add_plugin(<Name> SOURCE_DIR <dir> SOURCES <file>...)

function(nppmac_add_plugin name)
	cmake_parse_arguments(plugin "" "SOURCE_DIR" "SOURCES" ${ARGN})
	set(generatedDir "${CMAKE_CURRENT_BINARY_DIR}/${name}/generated")
	set(generatedSources "")
	foreach(source IN LISTS plugin_SOURCES)
		list(APPEND generatedSources "${generatedDir}/${source}")
	endforeach()
	set(migrationReport "${CMAKE_CURRENT_BINARY_DIR}/${name}/migration-report.tsv")
	file(GLOB_RECURSE pluginFiles CONFIGURE_DEPENDS "${plugin_SOURCE_DIR}/*")
	add_custom_command(
		OUTPUT "${migrationReport}"
		BYPRODUCTS ${generatedSources}
		COMMAND npp-migrate "${plugin_SOURCE_DIR}" "${generatedDir}" --report "${migrationReport}"
		DEPENDS npp-migrate ${pluginFiles}
		COMMENT "Migrating plugin ${name}"
		VERBATIM
	)

	add_library(${name} MODULE ${generatedSources} "${migrationReport}")
	add_dependencies(${name} nppMigrate)
	set_target_properties(${name} PROPERTIES
		PREFIX ""
		SUFFIX ".dll"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${name}"
	)
	# Upstream's plugin headers, as migrated for Notepad++ itself.
	target_include_directories(${name} PRIVATE
		"${generatedDir}"
		"${NPPMAC_GENERATED_SRC}/MISC/PluginsManager"
		"${NPPMAC_GENERATED_SRC}"
		"${NPP_ROOT}/scintilla/include"
	)
	target_link_libraries(${name} PRIVATE nppRuntimeHeaders)
	target_link_options(${name} PRIVATE "SHELL:-bundle_loader \"$<TARGET_FILE:NotepadPlusPlus>\"")
	add_dependencies(${name} NotepadPlusPlus)
endfunction()

# A demonstration plugin, used by the tests.
nppmac_add_plugin(HelloMac SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/HelloMac" SOURCES HelloMac.cpp)
