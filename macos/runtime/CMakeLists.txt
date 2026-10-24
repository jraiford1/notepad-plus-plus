# The Win32 runtime: macOS implementations of the Windows API subset Notepad++ uses.
#
# Declarations come from Wine's headers (see wine/WineHeaders.cmake); include/ comes first so the
# runtime can supply or replace a header where Wine's does not fit macOS.

add_library(nppRuntimeHeaders INTERFACE)
target_include_directories(nppRuntimeHeaders INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${NPPMAC_WINE_INCLUDE_DIRS}
)
target_compile_definitions(nppRuntimeHeaders INTERFACE
	UNICODE _UNICODE NOMINMAX OEMRESOURCE _USE_64BIT_TIME_T
	_WIN32_WINNT=0x0601 NTDDI_VERSION=0x06010000
	NPPMAC=1
)
target_compile_options(nppRuntimeHeaders INTERFACE
	"SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/include/nppmac/ForcedInclude.h\""
)

file(GLOB_RECURSE nppRuntimeSources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
add_library(nppRuntime STATIC ${nppRuntimeSources})
target_include_directories(nppRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# AppKit code (.mm) and Win32 code (.cpp) never share a translation unit: Cocoa's headers and the
# Windows headers both define BOOL, Polygon, GetCurrentProcess and more.
set_source_files_properties(${nppRuntimeSources} PROPERTIES COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(nppRuntime PUBLIC nppRuntimeHeaders "-framework Cocoa" "-framework QuartzCore" "-framework CoreText" "-framework PDFKit" "-framework Metal" iconv)
# The Scintilla window class talks to Scintilla's Cocoa view.
target_include_directories(nppRuntime PRIVATE "${NPP_ROOT}/scintilla/include" "${NPP_ROOT}/scintilla/cocoa")

add_executable(nppRuntimeCrtTest test/CrtTest.cpp)
target_link_libraries(nppRuntimeCrtTest PRIVATE nppRuntime nppRuntimeStubs)
add_test(NAME runtimeCrt COMMAND nppRuntimeCrtTest)

add_executable(nppRuntimeDirectoryChangesTest test/DirectoryChangesTest.cpp)
target_link_libraries(nppRuntimeDirectoryChangesTest PRIVATE nppRuntime nppRuntimeStubs)
add_test(NAME runtimeDirectoryChanges COMMAND nppRuntimeDirectoryChangesTest)

add_executable(nppRuntimeSystemInfoTest test/SystemInfoTest.cpp)
# The user layer (DwmGetColorizationColor) brings in the Scintilla window class.
target_link_libraries(nppRuntimeSystemInfoTest PRIVATE nppRuntime nppRuntimeStubs nppScintilla nppLexilla)
add_test(NAME runtimeSystemInfo COMMAND nppRuntimeSystemInfoTest)

add_executable(nppRuntimeIconTest test/IconTest.cpp)
target_include_directories(nppRuntimeIconTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(nppRuntimeIconTest PRIVATE "NPP_ROOT=\"${NPP_ROOT}\"")
target_link_libraries(nppRuntimeIconTest PRIVATE nppRuntime nppRuntimeStubs nppScintilla nppLexilla)
add_test(NAME runtimeIcons COMMAND nppRuntimeIconTest)
