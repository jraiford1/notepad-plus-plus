# Upstream's source, compiled from the generated (migrated) tree.
#
# nppCore is the non-UI slice that must always compile. nppUpstream compiles every source file
# upstream lists in PowerEditor/src/CMakeLists.txt (src_files), read at configure time.

set(nppCoreSources
	Utf8_16.cpp
	EncodingMapper.cpp
	MISC/Common/Common.cpp
	Parameters.cpp
)
list(TRANSFORM nppCoreSources PREPEND "${NPPMAC_GENERATED_SRC}/")
set_source_files_properties(${nppCoreSources} PROPERTIES GENERATED TRUE)

add_library(nppCore OBJECT ${nppCoreSources})
add_dependencies(nppCore nppMigrate)
target_include_directories(nppCore PRIVATE ${nppIncludeDirs})
target_link_libraries(nppCore PRIVATE nppRuntimeHeaders)

string(REGEX MATCH "SET\\(src_files([^)]*)\\)" nppSrcMatch "${nppUpstreamCMake}")
string(REGEX REPLACE "[ \t\r\n]+" ";" nppUpstreamSources "${CMAKE_MATCH_1}")
list(FILTER nppUpstreamSources INCLUDE REGEX "\\.(cpp|cxx|c)$")
list(TRANSFORM nppUpstreamSources REPLACE "^\\./" "")
list(TRANSFORM nppUpstreamSources PREPEND "${NPPMAC_GENERATED_SRC}/")
set_source_files_properties(${nppUpstreamSources} PROPERTIES GENERATED TRUE)

add_library(nppUpstream OBJECT ${nppUpstreamSources})
add_dependencies(nppUpstream nppMigrate)
target_include_directories(nppUpstream PRIVATE ${nppIncludeDirs})
target_link_libraries(nppUpstream PRIVATE nppRuntimeHeaders)

# NppLibsVersion.h, which upstream's NppLibsVersionH-generator.bat writes on Windows.
file(STRINGS "${NPP_ROOT}/scintilla/win32/ScintRes.rc" sciVersionLine REGEX "#define VERSION_SCINTILLA ")
file(STRINGS "${NPP_ROOT}/lexilla/src/LexillaVersion.rc" lexVersionLine REGEX "#define VERSION_LEXILLA ")
file(STRINGS "${NPP_ROOT}/boostregex/boost/version.hpp" boostVersionLine REGEX "#define BOOST_LIB_VERSION ")
string(REGEX MATCH "\"[^\"]*\"" sciVersion "${sciVersionLine}")
string(REGEX MATCH "\"[^\"]*\"" lexVersion "${lexVersionLine}")
string(REGEX MATCH "\"[^\"]*\"" boostVersion "${boostVersionLine}")
set(nppConfiguredIncludeDir "${CMAKE_BINARY_DIR}/configured-include")
file(CONFIGURE OUTPUT "${nppConfiguredIncludeDir}/NppLibsVersion.h" CONTENT
"// NppLibsVersion.h
// - maintained by macos/core/CMakeLists.txt (NppLibsVersionH-generator.bat on Windows)
#define NPP_SCINTILLA_VERSION ${sciVersion}
#define NPP_LEXILLA_VERSION ${lexVersion}
#define NPP_BOOST_REGEX_VERSION ${boostVersion}
")
foreach(target nppCore nppUpstream)
	target_include_directories(${target} PRIVATE "${nppConfiguredIncludeDir}")
	# Windows' LONG is long: braced initialisers of LONG fields from long values are not narrowing there.
	target_compile_options(${target} PRIVATE -Wno-c++11-narrowing)
endforeach()
