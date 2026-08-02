# ============================================================================
# Tests CMakeLists.txt
# ============================================================================

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

# ============================================================================
# Unit tests — no DIA dependency, pure algorithm tests
# ============================================================================

add_executable(DumpPDB_UnitTests
    Unit/ScopeContextTests.cpp
    Unit/TypeBuilderTests.cpp
    Unit/TypeWalkerTests.cpp
)

target_link_libraries(DumpPDB_UnitTests
    PRIVATE
        DumpPDBCore
        GTest::gtest_main
)

# ============================================================================
# Integration tests — require TestCompiland PDB
# ============================================================================

# Find TestCompiland PDB — built by the TestCompiland target
add_executable(DumpPDB_IntegrationTests
    Integration/PdbDumpTests.cpp
    Integration/PropertyTests.cpp
)

add_dependencies(
    DumpPDB_IntegrationTests
    TestCompiland
)

target_link_libraries(DumpPDB_IntegrationTests
    PRIVATE
        DumpPDBCore
        GTest::gtest_main
        "${DIA_LIBRARY}"
        ole32
        shell32
)

target_compile_definitions(
    DumpPDB_IntegrationTests
    PRIVATE
        TEST_COMPILAND_PDB="$<TARGET_PDB_FILE:TestCompiland>"
)

target_link_libraries(DumpPDB_IntegrationTests
    PRIVATE
        DumpPDBCore
        GTest::gtest_main
        "${DIA_LIBRARY}"
        ole32
        shell32
)

# ============================================================================
# Register with CTest
# ============================================================================

include(GoogleTest)
gtest_discover_tests(DumpPDB_UnitTests)
gtest_discover_tests(DumpPDB_IntegrationTests)