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
	cmake_parse_arguments(plugin "NPP_HEADERS" "SOURCE_DIR" "SOURCES;RC_FILES;RC_INCLUDES" ${ARGN})
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
			--plugin-text-rules "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plugin-text-rules.txt"
		DEPENDS npp-migrate ${pluginFiles} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plugin-text-rules.txt"
		COMMENT "Migrating plugin ${name}"
		VERBATIM
	)

	# Microsoft's linker defines __ImageBase, the start of the module's own image, which plugins take as
	# their module handle; any address in a plugin is one to the runtime (see GetModuleHandleEx).
	set(imageBase "${CMAKE_CURRENT_BINARY_DIR}/${name}/ImageBase.cpp")
	file(WRITE "${imageBase}" "#include <windows.h>\n\nextern \"C\" IMAGE_DOS_HEADER __ImageBase{};\n")
	add_library(${name} MODULE ${generatedSources} "${imageBase}" "${migrationReport}")
	add_dependencies(${name} nppMigrate)
	set_target_properties(${name} PROPERTIES
		PREFIX ""
		SUFFIX ".dll"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${name}"
	)
	target_include_directories(${name} PRIVATE "${generatedDir}")
	# Upstream's plugin headers, as migrated for Notepad++ itself; plugins of the catalog have their own
	# copies, and Scintilla's headers only if they lack them.
	if(plugin_NPP_HEADERS)
		target_include_directories(${name} PRIVATE "${NPPMAC_GENERATED_SRC}/MISC/PluginsManager" "${NPPMAC_GENERATED_SRC}" "${NPP_ROOT}/scintilla/include")
	endif()
	target_link_libraries(${name} PRIVATE nppRuntimeHeaders)
	# Plugins are written for Visual C++: __declspec. (Not -fms-extensions: it makes the Interlocked
	# functions Wine's headers define compiler built-ins.)
	target_compile_options(${name} PRIVATE -fdeclspec)
	target_link_options(${name} PRIVATE "SHELL:-bundle_loader \"$<TARGET_FILE:NotepadPlusPlus>\"")
	add_dependencies(${name} NotepadPlusPlus)

	# The plugin's resources (dialogs, icons, strings): its .rc files compiled into <name>.res beside
	# <name>.dll, where the runtime finds them for the plugin's module handle. Several .rc files are
	# concatenated, as a .res file is a sequence of resources.
	if(plugin_RC_FILES)
		set(resFiles "")
		set(rcIncludeFlags "")
		foreach(include IN LISTS plugin_RC_INCLUDES)
			list(APPEND rcIncludeFlags -I "${plugin_SOURCE_DIR}/${include}")
		endforeach()
		foreach(rcFile IN LISTS plugin_RC_FILES)
			get_filename_component(rcName "${rcFile}" NAME_WE)
			get_filename_component(rcDirectory "${plugin_SOURCE_DIR}/${rcFile}" DIRECTORY)
			set(partFile "${CMAKE_CURRENT_BINARY_DIR}/${name}/res/${rcName}.res")
			# The engine's copy (UTF-8, Windows separators fixed), compiled in the plugin's own folder with
			# its own headers and files; relative, as llvm-rc reads an argument starting with '/' as an option.
			file(RELATIVE_PATH rcText "${rcDirectory}" "${generatedDir}/${rcFile}")
			add_custom_command(
				OUTPUT "${partFile}"
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${name}/res"
				COMMAND "${NPPMAC_LLVM_RC}" -I "${rcDirectory}" ${rcIncludeFlags} ${nppPluginRcFlags} -c 65001 -D_WIN64 -DUNICODE -D_UNICODE
					-fo "${partFile}" "${rcText}"
				WORKING_DIRECTORY "${rcDirectory}"
				DEPENDS "${migrationReport}" "${plugin_SOURCE_DIR}/${rcFile}"
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

# A plugin from the official plugin list, built from its own sources: fetched at configure time at a
# pinned commit (or taken from NPPMAC_PLUGIN_SOURCES/<name>, a local checkout), then built as above.
# Paths are relative to its repository: SOURCES and RC_FILES as its project lists them, INCLUDES its
# project's include folders (relative to the repository; the migrated copies are used).
set(NPPMAC_PLUGIN_SOURCES "" CACHE PATH "Folder of local plugin checkouts, one per plugin name (instead of fetching them)")
option(NPPMAC_BUILD_PLUGINS "Build the plugins of the plugin catalog" ON)
function(nppmac_add_external_plugin name)
	cmake_parse_arguments(plugin "" "GIT;COMMIT;VERSION;DISPLAY_NAME" "SOURCES;RC_FILES;INCLUDES;DEFINES" ${ARGN})
	if(NPPMAC_PLUGIN_SOURCES AND EXISTS "${NPPMAC_PLUGIN_SOURCES}/${name}")
		set(sourceDir "${NPPMAC_PLUGIN_SOURCES}/${name}")
	else()
		set(sourceDir "${CMAKE_BINARY_DIR}/plugin-sources/${name}")
		if(NOT EXISTS "${sourceDir}/.nppmac-commit" OR NOT plugin_COMMIT STREQUAL "")
			set(fetched "")
			if(EXISTS "${sourceDir}/.nppmac-commit")
				file(READ "${sourceDir}/.nppmac-commit" fetched)
			endif()
			if(NOT fetched STREQUAL plugin_COMMIT)
				file(REMOVE_RECURSE "${sourceDir}")
				execute_process(COMMAND git clone --quiet --filter=blob:none --recurse-submodules "${plugin_GIT}" "${sourceDir}" RESULT_VARIABLE cloned)
				execute_process(COMMAND git -C "${sourceDir}" checkout --quiet "${plugin_COMMIT}" RESULT_VARIABLE checkedOut)
				execute_process(COMMAND git -C "${sourceDir}" submodule update --quiet --init --recursive)
				if(NOT cloned EQUAL 0 OR NOT checkedOut EQUAL 0)
					message(WARNING "Plugin ${name}: cannot fetch ${plugin_GIT} at ${plugin_COMMIT}; left out")
					return()
				endif()
				file(WRITE "${sourceDir}/.nppmac-commit" "${plugin_COMMIT}")
			endif()
		endif()
	endif()
	nppmac_add_plugin(${name} SOURCE_DIR "${sourceDir}" SOURCES ${plugin_SOURCES} RC_FILES ${plugin_RC_FILES} RC_INCLUDES ${plugin_INCLUDES})
	set(generatedDir "${CMAKE_CURRENT_BINARY_DIR}/${name}/generated")
	foreach(include IN LISTS plugin_INCLUDES)
		target_include_directories(${name} PRIVATE "${generatedDir}/${include}")
	endforeach()
	target_include_directories(${name} AFTER PRIVATE "${NPP_ROOT}/scintilla/include")
	target_compile_definitions(${name} PRIVATE ${plugin_DEFINES})
	# windows.h defines the min and max macros unless the project asks it not to; the runtime's headers
	# ask for Notepad++.
	if(NOT "NOMINMAX" IN_LIST plugin_DEFINES)
		target_compile_options(${name} PRIVATE -UNOMINMAX)
	endif()
	set_property(GLOBAL APPEND PROPERTY NPPMAC_CATALOG_PLUGINS ${name})
	set_property(GLOBAL PROPERTY NPPMAC_PLUGIN_${name}_VERSION "${plugin_VERSION}")
	set_property(GLOBAL PROPERTY NPPMAC_PLUGIN_${name}_DISPLAY_NAME "${plugin_DISPLAY_NAME}")
endfunction()

# The include paths llvm-rc needs for a plugin's .rc: the runtime's and Wine's headers.
set(nppPluginRcFlags -I "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include")
foreach(wineDir IN LISTS NPPMAC_WINE_INCLUDE_DIRS)
	list(APPEND nppPluginRcFlags -I "${wineDir}")
endforeach()

# A demonstration plugin, used by the tests.
nppmac_add_plugin(HelloMac NPP_HEADERS SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/HelloMac" SOURCES HelloMac.cpp RC_FILES HelloMac.rc)

if(NPPMAC_BUILD_PLUGINS)
	include(catalog.cmake)
	# A maintainer's plugins being ported, not committed.
	include(catalog-local.cmake OPTIONAL)
endif()
