# cli - Command-line interface executable
# Depends on pdbcore library

add_executable(DumpPDB
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Main.cpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Application.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/ICommand.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/CommandManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/CommandType.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/CommandHelp.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/CommandCompiland.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/CommandSource.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Command/CommandSettings.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Console/ConsoleManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Debug/DebugManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Debug/DebugTools.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/DIA/DiaManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Hash/HashManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Save/SaveManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/Settings/GlobalSettings.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Application/String/StringManager.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Test/Tests.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Container/Singleton.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Types/Serialization/BoolSerializable.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Types/Serialization/IBaseSerializable.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Types/Serialization/IBaseSerializableBase.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Types/Serialization/SerializableRegistry.hpp
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source/Util/Types/Serialization/UInt32Serializable.hpp
)

target_include_directories(DumpPDB PRIVATE
    ${CMAKE_SOURCE_DIR}/DumpPDB/Source
    ${DIA_INCLUDE_DIR}
)

target_link_libraries(DumpPDB PRIVATE
    pdbcore
    ${DIA_LIBRARY}
    ole32.lib
    shell32.lib
)