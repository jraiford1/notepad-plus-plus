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

add_library(nppScintilla STATIC ${scintillaSources} ${scintillaCocoaSources} ${boostRegexSources})
target_include_directories(nppScintilla
	PUBLIC "${NPP_ROOT}/scintilla/include" "${NPP_ROOT}/scintilla/cocoa" "${NPP_ROOT}/lexilla/include"
	PRIVATE "${NPP_ROOT}/scintilla/src" "${NPP_ROOT}/boostregex"
)
target_compile_definitions(nppScintilla PRIVATE SCI_OWNREGEX BOOST_REGEX_STANDALONE NDEBUG)
# Upstream code: its deprecation warnings are upstream's to fix.
target_compile_options(nppScintilla PRIVATE -Wno-deprecated-declarations -Wno-deprecated-this-capture)
target_link_libraries(nppScintilla PUBLIC "-framework Cocoa" "-framework QuartzCore")

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
