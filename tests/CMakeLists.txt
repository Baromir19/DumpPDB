# ============================================================================
# Tests CMakeLists.txt
# ============================================================================

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# ============================================================================
# Unit tests — no DIA dependency, pure algorithm tests
# ============================================================================

add_executable(DumpPDB_UnitTests
    Unit/ScopeContextTests.cpp
    Unit/TypeBuilderTests.cpp
)

target_include_directories(DumpPDB_UnitTests
    PRIVATE
        "${DUMPPDB_SOURCE_DIR}"
        "${DIA_INCLUDE_DIR}"
)

target_link_libraries(DumpPDB_UnitTests
    PRIVATE
        GTest::gtest_main
)

# ============================================================================
# Integration tests — require TestCompiland PDB
# ============================================================================

# Find TestCompiland PDB — built separately (MSBuild project)
set(TEST_COMPILAND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TestCompiland")
set(TEST_COMPILAND_PDB "${TEST_COMPILAND_DIR}/x64/Debug/TestCompiland.pdb")

add_executable(DumpPDB_IntegrationTests
    Integration/PdbDumpTests.cpp
)

target_include_directories(DumpPDB_IntegrationTests
    PRIVATE
        "${DUMPPDB_SOURCE_DIR}"
        "${DIA_INCLUDE_DIR}"
)

target_link_libraries(DumpPDB_IntegrationTests
    PRIVATE
        GTest::gtest_main
        "${DIA_LIBRARY}"
        ole32
        shell32
)

target_compile_definitions(DumpPDB_IntegrationTests
    PRIVATE
        UNICODE
        _UNICODE
        TEST_COMPILAND_PDB="${TEST_COMPILAND_PDB}"
)

# ============================================================================
# Register with CTest
# ============================================================================

include(GoogleTest)
gtest_discover_tests(DumpPDB_UnitTests)
gtest_discover_tests(DumpPDB_IntegrationTests)