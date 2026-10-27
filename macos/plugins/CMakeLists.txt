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
	cmake_parse_arguments(plugin "NPP_HEADERS" "SOURCE_DIR;ENCODING" "SOURCES;RC_FILES;RC_INCLUDES;RC_DEFINES" ${ARGN})
	set(generatedDir "${CMAKE_CURRENT_BINARY_DIR}/${name}/generated")
	set(generatedSources "")
	foreach(source IN LISTS plugin_SOURCES)
		list(APPEND generatedSources "${generatedDir}/${source}")
	endforeach()
	set(migrationReport "${CMAKE_CURRENT_BINARY_DIR}/${name}/migration-report.tsv")
	file(GLOB_RECURSE pluginFiles CONFIGURE_DEPENDS "${plugin_SOURCE_DIR}/*")
	list(FILTER pluginFiles EXCLUDE REGEX "/\\.git/")
	# Sources written in a code page (a plugin from a Chinese or Japanese Windows): ENCODING names it.
	set(encodingOption "")
	if(plugin_ENCODING)
		set(encodingOption --source-encoding "${plugin_ENCODING}")
	endif()
	add_custom_command(
		OUTPUT "${migrationReport}"
		BYPRODUCTS ${generatedSources}
		COMMAND npp-migrate "${plugin_SOURCE_DIR}" "${generatedDir}" --report "${migrationReport}"
			--plugin-text-rules "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plugin-text-rules.txt" --text-base "${name}" ${encodingOption}
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
	# Visual C++'s C compiler warns where clang's stops: pointers to functions or data whose types differ
	# only in what is the same here (WPARAM and LPARAM signedness...).
	target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-function-pointer-types;-Wno-error=incompatible-pointer-types;-Wno-error=int-conversion>")
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
			# It is preprocessed first as llvm-rc would, with the wide strings of the plugin's migrated
			# headers made L"..." again (npp-migrate --rc-wide-strings), then compiled as it is.
			file(RELATIVE_PATH rcText "${rcDirectory}" "${generatedDir}/${rcFile}")
			get_filename_component(rcGeneratedDirectory "${generatedDir}/${rcFile}" DIRECTORY)
			set(preprocessedFile "${rcGeneratedDirectory}/${rcName}.preprocessed.rc")
			set(wideFile "${rcGeneratedDirectory}/${rcName}.wide.rc")
			file(RELATIVE_PATH wideText "${rcDirectory}" "${wideFile}")
			set(rcDefines -DRC_INVOKED -D_WIN64 -DUNICODE -D_UNICODE)
			foreach(define IN LISTS plugin_RC_DEFINES)
				list(APPEND rcDefines "-D${define}")
			endforeach()
			add_custom_command(
				OUTPUT "${partFile}"
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${name}/res"
				COMMAND "${NPPMAC_RC_CLANG}" --driver-mode=gcc -target arm64-pc-windows-msvc-coff -E -xc ${rcDefines}
					-I "${rcDirectory}" ${rcIncludeFlags} ${nppPluginRcFlags} -o "${preprocessedFile}" "${rcText}"
				COMMAND npp-migrate --rc-wide-strings "${preprocessedFile}" "${wideFile}"
				COMMAND "${NPPMAC_LLVM_RC}" /no-preprocess -c 65001 -I "${rcDirectory}" ${rcIncludeFlags} -fo "${partFile}" "${wideText}"
				WORKING_DIRECTORY "${rcDirectory}"
				DEPENDS "${migrationReport}" "${plugin_SOURCE_DIR}/${rcFile}" npp-migrate
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
# project's include folders (relative to the repository; the migrated copies are used), FORCED_INCLUDES
# the headers its compiler includes before each source (ForcedIncludeFiles), CHARACTER_SET MultiByte
# for a project whose TCHAR is char (the Win32 A functions; Unicode is the default), CXX_STANDARD the
# C++ standard its project asks for when newer than the build's (C++20), RC_DEFINES the
# definitions its resource compiler is given (MSBuild properties such as a version, resolved), FILES
# what its Windows package has beside the DLL (a default configuration..., or a folder with what is in
# it), copied there as they are,
# PACKAGE_DIR the folder of its repository that is the rest of its package (copied beside the DLL,
# subfolders and all), CONCATENATE a file its build script makes before compiling by putting files of
# its repository end to end (the file, then the parts; from the migrated copies), ENCODING the code page
# its sources are written in when they are not Unicode (GBK, SHIFT_JIS...), FETCH the libraries its build
# fetches (CPM, vcpkg, a download script), each a folder of its checkout, a git repository and a
# commit, CONFIGURE the files its build configures (a template, then the file), with CMake's
# configure_file and the CONFIGURE_VARIABLES given (NAME=value), PROGRAMS the programs its package
# carries (gtags.exe...), built from their own sources by a script of programs/ (gnu-global for
# programs/gnu-global.cmake), and NPP_HEADERS for a project built against Notepad++'s own plugin headers
# (from its repository) rather than copies of its own.
set(NPPMAC_PLUGIN_SOURCES "" CACHE PATH "Folder of local plugin checkouts, one per plugin name (instead of fetching them)")
option(NPPMAC_BUILD_PLUGINS "Build the plugins of the plugin catalog" ON)
function(nppmac_add_external_plugin name)
	cmake_parse_arguments(plugin "NPP_HEADERS" "GIT;COMMIT;VERSION;DISPLAY_NAME;ENCODING;CHARACTER_SET;CXX_STANDARD;PACKAGE_DIR;PROGRAMS" "SOURCES;RC_FILES;INCLUDES;FORCED_INCLUDES;DEFINES;RC_DEFINES;FILES;CONCATENATE;FETCH;CONFIGURE;CONFIGURE_VARIABLES" ${ARGN})
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
	# Its libraries, cloned into its checkout at their pinned commits, as submodules would be.
	set(fetches ${plugin_FETCH})
	while(fetches)
		list(POP_FRONT fetches folder repository commit)
		set(dependencyDir "${sourceDir}/${folder}")
		set(fetched "")
		if(EXISTS "${dependencyDir}/.nppmac-commit")
			file(READ "${dependencyDir}/.nppmac-commit" fetched)
		endif()
		if(NOT fetched STREQUAL commit)
			file(REMOVE_RECURSE "${dependencyDir}")
			execute_process(COMMAND git clone --quiet --filter=blob:none "${repository}" "${dependencyDir}" RESULT_VARIABLE cloned)
			execute_process(COMMAND git -C "${dependencyDir}" checkout --quiet "${commit}" RESULT_VARIABLE checkedOut)
			if(NOT cloned EQUAL 0 OR NOT checkedOut EQUAL 0)
				message(WARNING "Plugin ${name}: cannot fetch ${repository} at ${commit}; left out")
				return()
			endif()
			file(WRITE "${dependencyDir}/.nppmac-commit" "${commit}")
		endif()
	endwhile()
	# Configured files (a fetched C library's config.h...), written beside the migrated copies, which they
	# join unchanged: they are the build's, not the project's sources.
	foreach(variable IN LISTS plugin_CONFIGURE_VARIABLES)
		string(FIND "${variable}" "=" equals)
		string(SUBSTRING "${variable}" 0 ${equals} variableName)
		math(EXPR valueStart "${equals} + 1")
		string(SUBSTRING "${variable}" ${valueStart} -1 variableValue)
		set(${variableName} "${variableValue}")
	endforeach()
	set(configures ${plugin_CONFIGURE})
	while(configures)
		list(POP_FRONT configures template configured)
		configure_file("${sourceDir}/${template}" "${CMAKE_CURRENT_BINARY_DIR}/${name}/generated/${configured}" @ONLY)
	endwhile()
	set(nppHeaders "")
	if(plugin_NPP_HEADERS)
		set(nppHeaders NPP_HEADERS)
	endif()
	nppmac_add_plugin(${name} ${nppHeaders} SOURCE_DIR "${sourceDir}" SOURCES ${plugin_SOURCES} RC_FILES ${plugin_RC_FILES} RC_INCLUDES ${plugin_INCLUDES}
		RC_DEFINES ${plugin_RC_DEFINES} ENCODING "${plugin_ENCODING}")
	set(generatedDir "${CMAKE_CURRENT_BINARY_DIR}/${name}/generated")

	# version_git.h, which some plugins make before building with their version_git.sh (from git
	# describe): written in the same form, with the catalog's version and the script's copyright line.
	if(EXISTS "${sourceDir}/version_git.sh" AND NOT EXISTS "${sourceDir}/version_git.h")
		file(READ "${sourceDir}/version_git.sh" script)
		string(REGEX MATCH "COPYRIGHT_STR \\\\\"([^\\\\]*)" copyrightMatch "${script}")
		string(TIMESTAMP year "%Y")
		string(REPLACE "$YEAR" "${year}" copyright "${CMAKE_MATCH_1}")
		string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" version "${plugin_VERSION}")
		string(REPLACE "." "," versionNumbers "${version}")
		file(MAKE_DIRECTORY "${generatedDir}")
		file(WRITE "${generatedDir}/version_git.h" "#pragma once\n"
			"#define VERSION_NUMBER ${versionNumbers},0\n"
			"#define VERSION_NUMBER_STR \"${version}\"\n"
			"#define VERSION_NUMBER_WSTR u\"${version}\"\n"
			"#define COPYRIGHT_STR \"${copyright}\"\n"
			"#define VERSION_GIT_STR \"v${version}\"\n"
			"#define VERSION_GIT_WSTR u\"v${version}\"\n")
	endif()
	if(plugin_CONCATENATE)
		list(POP_FRONT plugin_CONCATENATE concatenated)
		set(concatenated "${generatedDir}/${concatenated}")
		list(TRANSFORM plugin_CONCATENATE PREPEND "${generatedDir}/")
		get_filename_component(concatenatedDir "${concatenated}" DIRECTORY)
		add_custom_command(OUTPUT "${concatenated}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${concatenatedDir}"
			COMMAND "${CMAKE_COMMAND}" -E cat ${plugin_CONCATENATE} > "${concatenated}"
			DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${name}/migration-report.tsv"
			VERBATIM)
		target_sources(${name} PRIVATE "${concatenated}")
	endif()
	foreach(include IN LISTS plugin_INCLUDES)
		target_include_directories(${name} PRIVATE "${generatedDir}/${include}")
	endforeach()
	target_include_directories(${name} AFTER PRIVATE "${NPP_ROOT}/scintilla/include")
	target_compile_definitions(${name} PRIVATE ${plugin_DEFINES})
	if(plugin_CXX_STANDARD)
		set_target_properties(${name} PROPERTIES CXX_STANDARD ${plugin_CXX_STANDARD})
	endif()
	if(plugin_CHARACTER_SET STREQUAL "MultiByte")
		target_compile_options(${name} PRIVATE -UUNICODE -U_UNICODE)
	endif()
	foreach(header IN LISTS plugin_FORCED_INCLUDES)
		target_compile_options(${name} PRIVATE "SHELL:-include \"${generatedDir}/${header}\"")
	endforeach()
	# windows.h defines the min and max macros unless the project asks it not to; the runtime's headers
	# ask for Notepad++.
	if(NOT "NOMINMAX" IN_LIST plugin_DEFINES)
		target_compile_options(${name} PRIVATE -UNOMINMAX)
	endif()
	# The files beside the DLL, listed in <Name>.files for packing (MakePluginList.cmake) and the tests.
	set(fileNames "")
	foreach(dataFile IN LISTS plugin_FILES)
		get_filename_component(fileName "${dataFile}" NAME)
		if(IS_DIRECTORY "${sourceDir}/${dataFile}")
			# A folder goes with its name and everything in it.
			file(GLOB_RECURSE folderFiles RELATIVE "${sourceDir}/${dataFile}" "${sourceDir}/${dataFile}/*")
			foreach(folderFile IN LISTS folderFiles)
				get_filename_component(packageFolder "${CMAKE_CURRENT_BINARY_DIR}/${name}/${fileName}/${folderFile}" DIRECTORY)
				add_custom_command(TARGET ${name} POST_BUILD
					COMMAND "${CMAKE_COMMAND}" -E make_directory "${packageFolder}"
					COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sourceDir}/${dataFile}/${folderFile}" "${CMAKE_CURRENT_BINARY_DIR}/${name}/${fileName}/${folderFile}"
					VERBATIM)
				string(APPEND fileNames "${fileName}/${folderFile}\n")
			endforeach()
			continue()
		endif()
		add_custom_command(TARGET ${name} POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sourceDir}/${dataFile}" "${CMAKE_CURRENT_BINARY_DIR}/${name}/${fileName}"
			VERBATIM)
		string(APPEND fileNames "${fileName}\n")
	endforeach()
	if(plugin_PACKAGE_DIR)
		set(packageDir "${sourceDir}/${plugin_PACKAGE_DIR}")
		file(GLOB_RECURSE packageFiles RELATIVE "${packageDir}" "${packageDir}/*")
		foreach(packageFile IN LISTS packageFiles)
			get_filename_component(packageFolder "${CMAKE_CURRENT_BINARY_DIR}/${name}/${packageFile}" DIRECTORY)
			add_custom_command(TARGET ${name} POST_BUILD
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${packageFolder}"
				COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${packageDir}/${packageFile}" "${CMAKE_CURRENT_BINARY_DIR}/${name}/${packageFile}"
				VERBATIM)
			string(APPEND fileNames "${packageFile}\n")
		endforeach()
	endif()
	if(plugin_PROGRAMS)
		include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/programs/${plugin_PROGRAMS}.cmake")
	endif()
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}/${name}.files" "${fileNames}")
	set_property(GLOBAL APPEND PROPERTY NPPMAC_CATALOG_PLUGINS ${name})
	set_property(GLOBAL PROPERTY NPPMAC_PLUGIN_${name}_VERSION "${plugin_VERSION}")
	set_property(GLOBAL PROPERTY NPPMAC_PLUGIN_${name}_DISPLAY_NAME "${plugin_DISPLAY_NAME}")
endfunction()

# The include paths llvm-rc needs for a plugin's .rc: the runtime's and Wine's headers. Its preprocessor
# is the clang beside it.
get_filename_component(nppLlvmBin "${NPPMAC_LLVM_RC}" DIRECTORY)
find_program(NPPMAC_RC_CLANG clang HINTS "${nppLlvmBin}" NO_DEFAULT_PATH REQUIRED)
set(nppPluginRcFlags -I "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include")
foreach(wineDir IN LISTS NPPMAC_WINE_INCLUDE_DIRS)
	list(APPEND nppPluginRcFlags -I "${wineDir}")
endforeach()

# A demonstration plugin, used by the tests.
nppmac_add_plugin(HelloMac NPP_HEADERS SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/HelloMac" SOURCES HelloMac.cpp RC_FILES HelloMac.rc)

if(NPPMAC_BUILD_PLUGINS)
	include(catalog.cmake)
	get_property(committedPlugins GLOBAL PROPERTY NPPMAC_CATALOG_PLUGINS)
	# A maintainer's plugins being ported, not committed.
	include(catalog-local.cmake OPTIONAL)

	# The plugins Plugins Admin offers: packed into the app with their list (MakePluginList.cmake).
	get_property(catalogPlugins GLOBAL PROPERTY NPPMAC_CATALOG_PLUGINS)
	if(catalogPlugins)
		string(REPLACE ";" "|" catalogNames "${catalogPlugins}")
		add_custom_target(nppPluginCatalog ALL
			COMMAND "${CMAKE_COMMAND}" "-DNAMES=${catalogNames}" "-DPLUGIN_DIR=${CMAKE_CURRENT_BINARY_DIR}"
				"-DINFO=${CMAKE_CURRENT_SOURCE_DIR}/catalog-info.json" "-DRESOURCES=$<TARGET_BUNDLE_CONTENT_DIR:NotepadPlusPlus>/Resources"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/MakePluginList.cmake"
			COMMENT "Packing the plugins for Plugins Admin"
			VERBATIM
		)
		# Plugins being ported may not build: only the catalog's committed plugins are waited for.
		add_dependencies(nppPluginCatalog NotepadPlusPlus ${committedPlugins})
		add_test(NAME pluginList COMMAND "${CMAKE_COMMAND}" "-DRESOURCES=$<TARGET_BUNDLE_CONTENT_DIR:NotepadPlusPlus>/Resources"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/CheckPluginList.cmake")
	endif()
endif()
