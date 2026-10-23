# The Win32 runtime: macOS implementations of the Windows API subset Notepad++ uses.
#
# Declarations come from Wine's headers (see wine/WineHeaders.cmake); include/ comes first so the
# runtime can supply or replace a header where Wine's does not fit macOS.

include(wine/WineHeaders.cmake)

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
