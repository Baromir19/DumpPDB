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
#   build/bin/<Config>/DumpPDB.exe
#   build/bin/<Config>/PdbAPI.dll
# ============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(CMAKE_CONFIGURATION_TYPES)
    foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${CONFIG} CONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${CONFIG}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${CONFIG}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${CONFIG}")
    endforeach()
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}")
endif()

# ============================================================================
# DIA SDK
# ============================================================================

# Primary: use VSINSTALLDIR set by vcvarsall.bat / ilammy/msvc-dev-cmd.
# This works with any VS version (2022, 2026, ...) without hardcoding year.
set(DIA_SEARCH_ROOTS "$ENV{VSINSTALLDIR}DIA SDK")

# Fallback: ask vswhere for the latest VS installation path.
# vswhere ships with every VS2017+ and is always at the fixed location below.
find_program(_VSWHERE
    NAMES vswhere
    PATHS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
    NO_DEFAULT_PATH
)
if(_VSWHERE)
    execute_process(
        COMMAND "${_VSWHERE}" -latest -property installationPath
        OUTPUT_VARIABLE _VS_INSTALL_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_VS_INSTALL_PATH)
        list(APPEND DIA_SEARCH_ROOTS "${_VS_INSTALL_PATH}/DIA SDK")
    endif()
endif()

find_path(DIA_INCLUDE_DIR
    NAMES dia2.h
    PATHS ${DIA_SEARCH_ROOTS}
    PATH_SUFFIXES include
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DIA_LIB_PATH lib/amd64)
else()
    set(DIA_LIB_PATH lib)
endif()

find_library(DIA_LIBRARY
    NAMES diaguids
    PATHS ${DIA_SEARCH_ROOTS}
    PATH_SUFFIXES
        ${DIA_LIB_PATH}
)

message(STATUS "DIA architecture path: ${DIA_LIB_PATH}")
message(STATUS "DIA library: ${DIA_LIBRARY}")

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
# Target 1: DumpPDBCore — header-only library (INTERFACE)
#   Contains: Core/DIA/*, Core/Config/*, Core/System/*, Core/Util/*
#   No .cpp files — pure header-only, so INTERFACE is correct.
# ============================================================================

add_library(DumpPDBCore INTERFACE)

target_include_directories(DumpPDBCore
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${DIA_INCLUDE_DIR}"
)

target_compile_definitions(DumpPDBCore
    INTERFACE
        UNICODE
        _UNICODE
        NOMINMAX
)

# ============================================================================
# Target 2: DumpPDB — CLI executable
# ============================================================================

add_executable(DumpPDB
    CLI/Main.cpp
)

target_link_libraries(DumpPDB
    PRIVATE
        DumpPDBCore
        "${DIA_LIBRARY}"
        ole32
        shell32
)

target_compile_options(DumpPDB PRIVATE /Zi)
target_link_options(DumpPDB PRIVATE /DEBUG)

set_target_properties(DumpPDB PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# ============================================================================
# Target 3: PdbAPI — shared library (DLL)
# ============================================================================

add_library(PdbAPI SHARED
    API/dllmain.cpp
    API/PdbApi.cpp
)

target_include_directories(PdbAPI
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${DIA_INCLUDE_DIR}"
)

target_link_libraries(PdbAPI
    PRIVATE
        DumpPDBCore
        "${DIA_LIBRARY}"
        ole32
        shell32
)

target_compile_definitions(PdbAPI
    PRIVATE
        PDBAPI_EXPORTS
)

target_compile_options(PdbAPI PRIVATE /Zi)
target_link_options(PdbAPI PRIVATE /DEBUG)

set_target_properties(PdbAPI PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# ============================================================================
# Target 4: TestCompiland — generates test PDB for integration tests
# ============================================================================

add_executable(TestCompiland
    TestCompiland/Main.cpp
    TestCompiland/Test/Tests.cpp
)

target_include_directories(TestCompiland
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/TestCompiland"
)

target_compile_options(TestCompiland PRIVATE /Zi)
target_link_options(TestCompiland PRIVATE /DEBUG)

set_target_properties(TestCompiland PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# ============================================================================
# Tests (optional, requires GoogleTest)
# ============================================================================

option(BUILD_TESTS "Build unit and integration tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()