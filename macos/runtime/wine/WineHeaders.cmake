# Win32 declarations for the runtime come from Wine's headers (LGPL-2.1-or-later), which are made to
# compile on Unix hosts. Built as C++ on macOS they already match the model the migration engine targets:
# WCHAR is char16_t, LONG and DWORD are 32-bit, pointers are 64-bit.
#
# Nothing from Wine is committed to this repository. At configure time this script downloads a pinned,
# hash-checked Wine source release, extracts include/ and the IDL compiler, builds widl, and generates
# the COM headers (wtypes.h, objidl.h, shobjidl.h, ...) that Wine produces from .idl files.
#
# Output: NPPMAC_WINE_INCLUDE_DIRS, the directories to put on the include path (generated first).
# Needs: bison >= 3 (brew install bison) and flex (part of the Xcode command line tools).

set(NPPMAC_WINE_VERSION "11.20")
set(NPPMAC_WINE_SHA256 "eac8ca448f9c0087f963026b4dc276d1e2b7fbb64056edc093e59bbb7e56fe28")
set(NPPMAC_WINE_URL "https://dl.winehq.org/wine/source/11.x/wine-${NPPMAC_WINE_VERSION}.tar.xz")

set(wineRoot "${CMAKE_BINARY_DIR}/wine")
set(wineSource "${wineRoot}/wine-${NPPMAC_WINE_VERSION}")
set(wineGenerated "${wineRoot}/generated-include")
set(wineStamp "${wineRoot}/headers-${NPPMAC_WINE_VERSION}.stamp")
set(NPPMAC_WINE_INCLUDE_DIRS "${wineGenerated}" "${wineSource}/include")

if(EXISTS "${wineStamp}")
	return()
endif()

# --- Download and extract only what is needed --------------------------------------------------

set(wineArchive "${wineRoot}/wine-${NPPMAC_WINE_VERSION}.tar.xz")
if(NOT EXISTS "${wineArchive}")
	message(STATUS "Downloading Wine ${NPPMAC_WINE_VERSION} source for its Win32 headers")
	file(DOWNLOAD "${NPPMAC_WINE_URL}" "${wineArchive}" EXPECTED_HASH SHA256=${NPPMAC_WINE_SHA256} SHOW_PROGRESS)
endif()

execute_process(
	COMMAND tar -xf "${wineArchive}"
		"wine-${NPPMAC_WINE_VERSION}/include"
		"wine-${NPPMAC_WINE_VERSION}/tools/tools.h"
		"wine-${NPPMAC_WINE_VERSION}/tools/widl"
		"wine-${NPPMAC_WINE_VERSION}/tools/wrc"
	WORKING_DIRECTORY "${wineRoot}"
	RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Extracting ${wineArchive} failed")
endif()

# --- Build widl ----------------------------------------------------------------------------------

find_program(NPPMAC_BISON bison HINTS /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin NO_DEFAULT_PATH)
if(NOT NPPMAC_BISON)
	find_program(NPPMAC_BISON bison)
endif()
find_program(NPPMAC_FLEX flex REQUIRED)
execute_process(COMMAND "${NPPMAC_BISON}" --version OUTPUT_VARIABLE bisonVersion)
if(NOT bisonVersion MATCHES "[Bb]ison\\) ([3-9])\\.")
	message(FATAL_ERROR "widl needs bison 3 or later (found ${NPPMAC_BISON}); run: brew install bison")
endif()

set(widlBuild "${wineRoot}/widl-build")
file(MAKE_DIRECTORY "${widlBuild}")
file(WRITE "${widlBuild}/config.h"
	"#define __WINE_CONFIG_H\n#define PACKAGE_VERSION \"${NPPMAC_WINE_VERSION}\"\n#define HAVE_UNISTD_H 1\n#define HAVE_SYS_WAIT_H 1\n#define HAVE_GETOPT_LONG 1\n")

set(widlTools "${wineSource}/tools")
foreach(step
		"${NPPMAC_BISON};-o;parser.tab.c;--defines=parser.tab.h;${widlTools}/widl/parser.y"
		"${NPPMAC_FLEX};-o;parser.yy.c;${widlTools}/widl/parser.l"
		"${NPPMAC_BISON};-o;ppy.tab.c;--defines=ppy.tab.h;${widlTools}/wrc/ppy.y"
		"${NPPMAC_FLEX};-o;ppl.yy.c;${widlTools}/wrc/ppl.l")
	execute_process(COMMAND ${step} WORKING_DIRECTORY "${widlBuild}" RESULT_VARIABLE result)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "Generating the widl parser failed: ${step}")
	endif()
endforeach()

set(widlSources "")
foreach(name attribute client expr hash header metadata proxy register server typegen typelib typetree utils widl write_msft write_sltg)
	list(APPEND widlSources "${widlTools}/widl/${name}.c")
endforeach()
list(APPEND widlSources "${widlTools}/wrc/wpp.c" parser.tab.c parser.yy.c ppy.tab.c ppl.yy.c)

execute_process(
	COMMAND "${CMAKE_C_COMPILER}" -O1 -w
		-I. "-I${widlTools}/widl" "-I${widlTools}/wrc" "-I${widlTools}" "-I${wineSource}/include"
		"-DINCLUDEDIR=\"/nonexistent\"" "-DBINDIR=\"/nonexistent\"" "-DLIBDIR=\"/nonexistent\""
		-o widl ${widlSources}
	WORKING_DIRECTORY "${widlBuild}"
	RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Building widl failed")
endif()

# --- Generate the COM headers --------------------------------------------------------------------

# Some .idl files (mostly OLE DB) do not compile on their own; Notepad++ does not use them.
file(MAKE_DIRECTORY "${wineGenerated}")
file(GLOB wineIdlFiles "${wineSource}/include/*.idl")
set(generatedCount 0)
foreach(idlFile IN LISTS wineIdlFiles)
	get_filename_component(idlName "${idlFile}" NAME_WE)
	if(EXISTS "${wineSource}/include/${idlName}.h")
		continue() # e.g. windows.idl (WinRT) must not shadow the real windows.h
	endif()
	execute_process(
		COMMAND "${widlBuild}/widl" --nostdinc "-I${wineSource}/include" -h -o "${wineGenerated}/${idlName}.h" "${idlFile}"
		RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET
	)
	if(result EQUAL 0)
		math(EXPR generatedCount "${generatedCount} + 1")
	else()
		file(REMOVE "${wineGenerated}/${idlName}.h")
	endif()
endforeach()
message(STATUS "Generated ${generatedCount} Wine COM headers")

file(WRITE "${wineStamp}" "${NPPMAC_WINE_VERSION}\n")
