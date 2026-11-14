# npp-migrate: the source migration engine. A host tool, built with Homebrew's LLVM on macOS (brew install
# llvm) or Ubuntu's on Linux (libclang-cpp22-dev, llvm-22-dev), linked against libclang-cpp for clang's lexer.

find_package(Clang REQUIRED CONFIG HINTS /opt/homebrew/opt/llvm/lib/cmake/clang /usr/local/opt/llvm/lib/cmake/clang
	/usr/lib/llvm-22/lib/cmake/clang)

add_library(nppMigrationRules STATIC MigrationRules.cpp MigrationRules.h)
target_include_directories(nppMigrationRules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_link_libraries(nppMigrationRules PUBLIC clang-cpp LLVM)
target_compile_features(nppMigrationRules PUBLIC cxx_std_20)

add_executable(npp-migrate NppMigrate.cpp)
target_link_libraries(npp-migrate PRIVATE nppMigrationRules)
if(APPLE)
	target_link_libraries(npp-migrate PRIVATE iconv) # part of the C library on Linux
endif()

add_executable(nppMigrationRulesTest MigrationRulesTest.cpp)
target_link_libraries(nppMigrationRulesTest PRIVATE nppMigrationRules)
add_test(NAME migrationRules COMMAND nppMigrationRulesTest)

# The engine runs on the build machine only, and Homebrew's LLVM has the build machine's architecture.
set_target_properties(nppMigrationRules npp-migrate nppMigrationRulesTest PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
