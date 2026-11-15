# Weak stubs for the Win32 functions the program references and the runtime does not implement yet,
# made from upstream's objects, the runtime, Scintilla and Lexilla (see GenerateStubs.cmake).

# --- Stubs for Win32 functions not implemented yet -----------------------------------------------
if(NPPMAC_PLATFORM STREQUAL "macos")
	set(nppSystemLibraries "-framework Cocoa" "-framework QuartzCore" "-framework IOSurface" "-framework CoreText" "-framework Security" "-framework UniformTypeIdentifiers"
		"-framework PDFKit" "-framework Metal")
	# The probe also gets what the runtime links itself (iconv).
	set(nppSystemLibraries -lc++ -liconv ${nppSystemLibraries})
else()
	# The C and C++ libraries, and Qt's (whose C names, such as qt_version_tag, the runtime's native side
	# refers to).
	set(nppSystemLibraries -lstdc++ -lm $<TARGET_FILE:Qt6::Core> $<TARGET_FILE:Qt6::Gui> $<TARGET_FILE:Qt6::Widgets>
		$<TARGET_FILE:Qt6::DBus> $<TARGET_FILE:Qt6::PrintSupport>)
endif()
string(REPLACE ";" "|" nppSystemLibrariesArgument "${nppSystemLibraries}") # a list cannot pass through -D
string(REPLACE " " "|" nppSystemLibrariesArgument "${nppSystemLibrariesArgument}")
set(stubInputs "${CMAKE_CURRENT_BINARY_DIR}/stub-inputs.txt")
set(stubProviders "${CMAKE_CURRENT_BINARY_DIR}/stub-providers.txt")
set(stubSource "${CMAKE_CURRENT_BINARY_DIR}/RuntimeStubs.c")
file(GENERATE OUTPUT "${stubInputs}" CONTENT
	"$<JOIN:$<TARGET_OBJECTS:nppUpstream>,\n>\n$<TARGET_FILE:nppRuntime>\n$<TARGET_FILE:nppScintilla>\n$<TARGET_FILE:nppLexilla>\n")
file(GENERATE OUTPUT "${stubProviders}" CONTENT
	"$<JOIN:$<TARGET_OBJECTS:nppUpstream>,\n>\n$<TARGET_FILE:nppRuntime>\n$<TARGET_FILE:nppScintilla>\n$<TARGET_FILE:nppLexilla>\n$<TARGET_FILE:nppUuid>\n")

add_custom_command(
	OUTPUT "${stubSource}"
	COMMAND "${CMAKE_COMMAND}" -DNM=${CMAKE_NM} -DCC=${CMAKE_C_COMPILER} -DPLATFORM=${NPPMAC_PLATFORM} "-DLINK_LIBRARIES=${nppSystemLibrariesArgument}"
		-DINPUTS_FILE=${stubInputs} -DPROVIDERS_FILE=${stubProviders} -DOUTPUT=${stubSource}
		-P "${CMAKE_CURRENT_SOURCE_DIR}/GenerateStubs.cmake"
	DEPENDS nppUpstream nppRuntime nppScintilla nppLexilla nppUuid
		$<TARGET_OBJECTS:nppUpstream> "${CMAKE_CURRENT_SOURCE_DIR}/GenerateStubs.cmake"
	COMMENT "Generating stubs for Win32 functions the runtime does not implement yet"
	VERBATIM
)
add_library(nppRuntimeStubs STATIC "${stubSource}")
# The stubs report through the runtime (StubReport.cpp), which the app links whole; programs that link
# only what they use take the two as a group (nppRuntimeWithStubs), which a one-pass linker (GNU ld)
# needs.
target_compile_options(nppRuntimeStubs PRIVATE -w)
