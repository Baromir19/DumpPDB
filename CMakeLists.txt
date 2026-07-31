cmake_minimum_required(VERSION 3.20)

project(DumpPDB
    VERSION 2.0.0
    LANGUAGES CXX
)

# ============================================================================
# C++ standard
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Output directories
# ============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# Source files
# ============================================================================

set(DUMPPDB_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/DumpPDB/src"
)

file(GLOB_RECURSE DUMPPDB_CPP_FILES
    CONFIGURE_DEPENDS
        "${DUMPPDB_SOURCE_DIR}/*.cpp"
)

file(GLOB_RECURSE DUMPPDB_HEADER_FILES
    CONFIGURE_DEPENDS
        "${DUMPPDB_SOURCE_DIR}/*.hpp"
        "${DUMPPDB_SOURCE_DIR}/*.h"
)

# ============================================================================
# DIA SDK
# ============================================================================

set(DIA_SEARCH_ROOTS
    "$ENV{VSINSTALLDIR}DIA SDK"

    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/DIA SDK"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/DIA SDK"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/DIA SDK"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/BuildTools/DIA SDK"

    "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2019/Community/DIA SDK"
    "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2019/Professional/DIA SDK"
    "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2019/Enterprise/DIA SDK"
)

find_path(DIA_INCLUDE_DIR
    NAMES dia2.h
    PATHS ${DIA_SEARCH_ROOTS}
    PATH_SUFFIXES include
)

find_library(DIA_LIBRARY
    NAMES diaguids
    PATHS ${DIA_SEARCH_ROOTS}
    PATH_SUFFIXES
        lib/amd64
        lib
)

if(NOT DIA_INCLUDE_DIR)
    message(FATAL_ERROR
        "DIA SDK headers not found.\n"
        "Could not find dia2.h."
    )
endif()

if(NOT DIA_LIBRARY)
    message(FATAL_ERROR
        "DIA SDK library not found.\n"
        "Could not find diaguids.lib."
    )
endif()

message(STATUS "DIA include directory: ${DIA_INCLUDE_DIR}")
message(STATUS "DIA library: ${DIA_LIBRARY}")

# ============================================================================
# Executable
# ============================================================================

add_executable(DumpPDB
    ${DUMPPDB_CPP_FILES}
    ${DUMPPDB_HEADER_FILES}
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(DumpPDB
    PRIVATE
        "${DUMPPDB_SOURCE_DIR}"
        "${DIA_INCLUDE_DIR}"
)

# ============================================================================
# Libraries
# ============================================================================

target_link_libraries(DumpPDB
    PRIVATE
        "${DIA_LIBRARY}"
        ole32
        shell32
)

# ============================================================================
# Compile definitions
# ============================================================================

target_compile_definitions(DumpPDB
    PRIVATE
        UNICODE
        _UNICODE
)

# ============================================================================
# Visual Studio
# ============================================================================

set_target_properties(DumpPDB PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

source_group(
    TREE "${DUMPPDB_SOURCE_DIR}"
    FILES
        ${DUMPPDB_CPP_FILES}
        ${DUMPPDB_HEADER_FILES}
)

# ============================================================================
# Tests (optional, requires GoogleTest)
# ============================================================================

option(BUILD_TESTS "Build unit and integration tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
