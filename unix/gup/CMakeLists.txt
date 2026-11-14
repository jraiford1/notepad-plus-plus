# The updater Plugins Admin runs (updater/gup.exe in the app): plain macOS code, no Windows API.

add_executable(nppGup Gup.cpp)
set_target_properties(nppGup PROPERTIES OUTPUT_NAME "gup.exe")
target_compile_features(nppGup PRIVATE cxx_std_20)

add_test(NAME gup COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../ci/gup-test.sh" "$<TARGET_FILE:nppGup>" "$<TARGET_FILE:HelloMac>")
