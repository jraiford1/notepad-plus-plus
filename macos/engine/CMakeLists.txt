# npp-migrate: the source migration engine. A host tool, built with Homebrew's LLVM
# (brew install llvm), linked against libclang-cpp for clang's lexer.

find_package(Clang REQUIRED CONFIG HINTS /opt/homebrew/opt/llvm/lib/cmake/clang /usr/local/opt/llvm/lib/cmake/clang)

add_library(nppMigrationRules STATIC MigrationRules.cpp MigrationRules.h)
target_include_directories(nppMigrationRules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_link_libraries(nppMigrationRules PUBLIC clang-cpp LLVM)
target_compile_features(nppMigrationRules PUBLIC cxx_std_20)

add_executable(npp-migrate NppMigrate.cpp)
target_link_libraries(npp-migrate PRIVATE nppMigrationRules)

add_executable(nppMigrationRulesTest MigrationRulesTest.cpp)
target_link_libraries(nppMigrationRulesTest PRIVATE nppMigrationRules)
add_test(NAME migrationRules COMMAND nppMigrationRulesTest)

# The engine runs on the build machine only, and Homebrew's LLVM has the build machine's architecture.
set_target_properties(nppMigrationRules npp-migrate nppMigrationRulesTest PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
