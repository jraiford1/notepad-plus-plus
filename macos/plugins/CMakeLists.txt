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
	cmake_parse_arguments(plugin "" "SOURCE_DIR" "SOURCES;RC_FILES" ${ARGN})
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

	# The plugin's resources (dialogs, icons, strings): its .rc files compiled into <name>.res beside
	# <name>.dll, where the runtime finds them for the plugin's module handle. Several .rc files are
	# concatenated, as a .res file is a sequence of resources.
	if(plugin_RC_FILES)
		set(resFiles "")
		foreach(rcFile IN LISTS plugin_RC_FILES)
			get_filename_component(rcName "${rcFile}" NAME_WE)
			get_filename_component(rcDirectory "${plugin_SOURCE_DIR}/${rcFile}" DIRECTORY)
			set(partFile "${CMAKE_CURRENT_BINARY_DIR}/${name}/res/${rcName}.res")
			add_custom_command(
				OUTPUT "${partFile}"
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${name}/res"
				COMMAND "${NPPMAC_LLVM_RC}" -I "${rcDirectory}" ${nppPluginRcFlags} -c 65001 -D_WIN64 -DUNICODE -D_UNICODE
					-fo "${partFile}" "${rcFile}"
				WORKING_DIRECTORY "${plugin_SOURCE_DIR}"
				DEPENDS "${plugin_SOURCE_DIR}/${rcFile}"
				COMMENT "Compiling resources of plugin ${name}: ${rcFile}"
				VERBATIM
			)
			list(APPEND resFiles "${partFile}")
		endforeach()
		set(resFile "${CMAKE_CURRENT_BINARY_DIR}/${name}/${name}.res")
		add_custom_command(
			OUTPUT "${resFile}"
			COMMAND "${CMAKE_COMMAND}" -E cat ${resFiles} > "${resFile}"
			DEPENDS ${resFiles}
			VERBATIM
		)
		target_sources(${name} PRIVATE "${resFile}")
	endif()
endfunction()

# The include paths llvm-rc needs for a plugin's .rc: the runtime's and Wine's headers.
set(nppPluginRcFlags -I "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include")
foreach(wineDir IN LISTS NPPMAC_WINE_INCLUDE_DIRS)
	list(APPEND nppPluginRcFlags -I "${wineDir}")
endforeach()

# A demonstration plugin, used by the tests.
nppmac_add_plugin(HelloMac SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/HelloMac" SOURCES HelloMac.cpp RC_FILES HelloMac.rc)
