# pdbcore - Static library with DIA session management, type walking, and symbol dumping
# No dependency on ConsoleManager, CommandManager, or any CLI-specific code.

add_library(pdbcore STATIC
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/DiaSession.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/TypeBuilder.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/TypeWalker.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/SymbolDumper.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/SymbolFinder.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/PdbToolset.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Com/ComPtr.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Error/DumpError.hpp
)

target_include_directories(pdbcore PUBLIC
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source
    ${DIA_INCLUDE_DIR}
)

target_link_libraries(pdbcore PUBLIC
    ${DIA_LIBRARY}
    ole32.lib
)

# Mark headers as PUBLIC so consumers get the include path
target_sources(pdbcore PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_SOURCE_DIR}/DumpPDB/Source
    FILES
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/DiaSession.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/TypeBuilder.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/TypeWalker.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/SymbolDumper.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/SymbolFinder.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Core/PdbToolset.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Com/ComPtr.hpp
        ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Error/DumpError.hpp
)