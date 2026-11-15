# Scintilla (with Notepad++'s Boost regex engine, as boostregex/nppSpecifics.mak adds it on Windows)
# and Lexilla, built from upstream's vendored copies, unmodified. Scintilla uses its Cocoa platform layer.

file(GLOB scintillaSources CONFIGURE_DEPENDS "${NPP_ROOT}/scintilla/src/*.cxx")
set(scintillaCocoaSources
	"${NPP_ROOT}/scintilla/cocoa/PlatCocoa.mm"
	"${NPP_ROOT}/scintilla/cocoa/ScintillaCocoa.mm"
	"${NPP_ROOT}/scintilla/cocoa/ScintillaView.mm"
	"${NPP_ROOT}/scintilla/cocoa/InfoBar.mm"
)
set(boostRegexSources
	"${NPP_ROOT}/boostregex/BoostRegExSearch.cxx"
	"${NPP_ROOT}/boostregex/UTF8DocumentIterator.cxx"
)
set_source_files_properties(${scintillaCocoaSources} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

# Scintilla's platform layer: Cocoa on macOS, Qt on Linux (scintilla/qt/ScintillaEditBase, whose classes
# have Qt signals: the meta-object compiler runs over them).
if(NPPMAC_PLATFORM STREQUAL "macos")
	set(scintillaPlatformSources ${scintillaCocoaSources})
	set(scintillaPlatformInclude "${NPP_ROOT}/scintilla/cocoa")
else()
	set(scintillaQt "${NPP_ROOT}/scintilla/qt/ScintillaEditBase")
	set(scintillaPlatformSources "${scintillaQt}/PlatQt.cpp" "${scintillaQt}/ScintillaQt.cpp" "${scintillaQt}/ScintillaEditBase.cpp"
		"${scintillaQt}/PlatQt.h" "${scintillaQt}/ScintillaQt.h" "${scintillaQt}/ScintillaEditBase.h")
	set(scintillaPlatformInclude "${scintillaQt}")
endif()

add_library(nppScintilla STATIC ${scintillaSources} ${scintillaPlatformSources} ${boostRegexSources})
target_include_directories(nppScintilla
	PUBLIC "${NPP_ROOT}/scintilla/include" ${scintillaPlatformInclude} "${NPP_ROOT}/lexilla/include"
	PRIVATE "${NPP_ROOT}/scintilla/src" "${NPP_ROOT}/boostregex"
)
target_compile_definitions(nppScintilla PRIVATE SCI_OWNREGEX BOOST_REGEX_STANDALONE NDEBUG)
# Upstream code: its deprecation warnings are upstream's to fix.
target_compile_options(nppScintilla PRIVATE -Wno-deprecated-declarations -Wno-deprecated-this-capture)
if(NPPMAC_PLATFORM STREQUAL "macos")
	target_link_libraries(nppScintilla PUBLIC "-framework Cocoa" "-framework QuartzCore")
else()
	find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
	set_target_properties(nppScintilla PROPERTIES AUTOMOC ON)
	target_compile_definitions(nppScintilla PUBLIC SCINTILLA_QT=1 PRIVATE MAKING_LIBRARY=1)
	target_include_directories(nppScintilla PRIVATE "${NPP_ROOT}/scintilla/src")
	target_link_libraries(nppScintilla PUBLIC Qt6::Widgets Qt6::Core5Compat)
endif()

file(GLOB lexillaSources CONFIGURE_DEPENDS
	"${NPP_ROOT}/lexilla/src/Lexilla.cxx"
	"${NPP_ROOT}/lexilla/lexlib/*.cxx"
	"${NPP_ROOT}/lexilla/lexers/*.cxx"
)
# Notepad++'s user-defined-language lexer includes <windows.h>, so it builds against the runtime headers.
set(lexUserSource "${NPP_ROOT}/lexilla/lexers/LexUser.cxx")
list(REMOVE_ITEM lexillaSources "${lexUserSource}")
add_library(nppLexUser OBJECT "${lexUserSource}")
target_include_directories(nppLexUser PRIVATE "${NPP_ROOT}/lexilla/include" "${NPP_ROOT}/lexilla/lexlib" "${NPP_ROOT}/scintilla/include")
target_link_libraries(nppLexUser PRIVATE nppRuntimeHeaders)

add_library(nppLexilla STATIC ${lexillaSources} $<TARGET_OBJECTS:nppLexUser>)
target_include_directories(nppLexilla
	PUBLIC "${NPP_ROOT}/lexilla/include"
	PRIVATE "${NPP_ROOT}/lexilla/lexlib" "${NPP_ROOT}/scintilla/include"
)

# Scintilla's and Lexilla's headers use intptr_t without <cstdint>, which libc++ brings in through other
# headers and newer libstdc++ does not: a compiler-level fix, as upstream's gcc-fixes.h is.
if(NPPMAC_PLATFORM STREQUAL "linux")
	foreach(target IN ITEMS nppScintilla nppLexUser nppLexilla)
		target_compile_options(${target} PRIVATE -include cstdint)
	endforeach()
endif()
