cmake_minimum_required(VERSION 3.16)
project(DumpPDB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find DIA SDK (part of Visual Studio)
if(DEFINED ENV{VSInstallDir})
    set(DIA_INCLUDE_DIR "$ENV{VSInstallDir}DIA SDK/include")
    set(DIA_LIBRARY_DIR "$ENV{VSInstallDir}DIA SDK/lib")
endif()

find_path(DIA_INCLUDE_DIR NAMES dia2.h
    PATHS "$ENV{VSInstallDir}DIA SDK/include"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/DIA SDK/include"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/DIA SDK/include"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/DIA SDK/include"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/DIA SDK/include"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/DIA SDK/include"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/DIA SDK/include"
)

find_library(DIA_LIBRARY NAMES diaguids.lib
    PATHS "$ENV{VSInstallDir}DIA SDK/lib"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/DIA SDK/lib"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/DIA SDK/lib"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/DIA SDK/lib"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/DIA SDK/lib"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/DIA SDK/lib"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/DIA SDK/lib"
)

if(NOT DIA_INCLUDE_DIR)
    message(FATAL_ERROR "DIA SDK not found. Install Visual Studio with 'Desktop development with C++' workload.")
endif()

message(STATUS "DIA SDK include: ${DIA_INCLUDE_DIR}")
message(STATUS "DIA SDK library: ${DIA_LIBRARY}")

# Add subdirectories
add_subdirectory(pdbcore)
add_subdirectory(cli)