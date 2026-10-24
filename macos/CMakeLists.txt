# Notepad++ macOS build.
#
# Upstream source (PowerEditor/src, scintilla, lexilla, boostregex) is never edited. The build:
#   1. builds the migration engine (engine/),
#   2. runs it over PowerEditor/src into ${NPPMAC_GENERATED_DIR} (regenerated when upstream changes),
#   3. compiles the generated source against the Win32 runtime (runtime/).
#
#     cmake -S macos -B build/macos && cmake --build build/macos && ctest --test-dir build/macos

cmake_minimum_required(VERSION 3.24)
# The oldest macOS the app runs on (also the bundle's LSMinimumSystemVersion). Without it the SDK's own
# version is the target, and the app would need the newest macOS.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
project(NotepadPlusPlusMac LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(NPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(NPP_UPSTREAM_SRC "${NPP_ROOT}/PowerEditor/src")
set(NPPMAC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(NPPMAC_GENERATED_SRC "${NPPMAC_GENERATED_DIR}/PowerEditor/src")

enable_testing()

add_subdirectory(engine)

# --- 2. Migration -------------------------------------------------------------------------------

file(GLOB_RECURSE nppUpstreamFiles CONFIGURE_DEPENDS LIST_DIRECTORIES false "${NPP_UPSTREAM_SRC}/*")
set(nppGeneratedFiles "")
foreach(upstreamFile IN LISTS nppUpstreamFiles)
	file(RELATIVE_PATH relativePath "${NPP_UPSTREAM_SRC}" "${upstreamFile}")
	list(APPEND nppGeneratedFiles "${NPPMAC_GENERATED_SRC}/${relativePath}")
endforeach()

add_custom_command(
	OUTPUT "${NPPMAC_GENERATED_DIR}/migration-report.tsv"
	BYPRODUCTS ${nppGeneratedFiles}
	COMMAND npp-migrate "${NPP_UPSTREAM_SRC}" "${NPPMAC_GENERATED_SRC}" --report "${NPPMAC_GENERATED_DIR}/migration-report.tsv"
		--fixups "${CMAKE_CURRENT_SOURCE_DIR}/migration-fixups.txt"
	DEPENDS npp-migrate ${nppUpstreamFiles} "${CMAKE_CURRENT_SOURCE_DIR}/migration-fixups.txt"
	COMMENT "Migrating PowerEditor/src for macOS"
	VERBATIM
)
add_custom_target(nppMigrate DEPENDS "${NPPMAC_GENERATED_DIR}/migration-report.tsv")

# Include directories follow upstream's own list (projIncludDir in PowerEditor/src/CMakeLists.txt),
# read at configure time so a directory upstream adds is picked up without editing this file.
file(READ "${NPP_UPSTREAM_SRC}/CMakeLists.txt" nppUpstreamCMake)
string(REGEX MATCH "SET\\(projIncludDir([^)]*)\\)" nppIncludeMatch "${nppUpstreamCMake}")
string(REGEX REPLACE "[ \t\r\n]+" ";" nppUpstreamIncludes "${CMAKE_MATCH_1}")
set(nppIncludeDirs "")
foreach(includeDir IN LISTS nppUpstreamIncludes)
	if(includeDir STREQUAL "")
		continue()
	endif()
	if(includeDir MATCHES "^\\.\\./\\.\\./")
		# scintilla/ and lexilla/ are used as they are, not migrated
		string(REGEX REPLACE "^\\.\\./\\.\\./" "${NPP_ROOT}/" includeDir "${includeDir}")
	else()
		string(REGEX REPLACE "^\\./" "" includeDir "${includeDir}")
		set(includeDir "${NPPMAC_GENERATED_SRC}/${includeDir}")
	endif()
	list(APPEND nppIncludeDirs "${includeDir}")
endforeach()

# --- 3. Runtime and generated source ------------------------------------------------------------

include(runtime/wine/WineHeaders.cmake) # sets NPPMAC_WINE_INCLUDE_DIRS and NPPMAC_WINE_UUID_DIR
add_subdirectory(runtime)
add_subdirectory(core)
add_subdirectory(thirdparty)
add_subdirectory(app)
add_subdirectory(plugins)
