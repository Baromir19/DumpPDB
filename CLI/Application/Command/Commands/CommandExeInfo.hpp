#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -exeinfo <file.exe>
///
/// Prints everything the PE headers and RTTI reveal without a disassembler:
/// build timestamp, PDB path/GUID/age (to fetch the matching PDB), toolchain
/// (Rich header), sections with entropy, imports per DLL, RTTI statistics and
/// the function/string/global discovery statistics.
class CommandExeInfo : public ICommand
{
public:

    CommandExeInfo()
        : ICommand(1, Type::COMMAND_EXE_EXECUTE)
    {
        m_names.push_back(L"-exeinfo");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"<file.exe>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"PE/RTTI/PDB-identity summary of an exe";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args)
            return false;

        const std::wstring& exePath = a_args[0];
        auto& toolset = DumpPDB::ExeToolset::instance();

        if (!toolset.isLoaded() || toolset.exePath() != exePath)
        {
            ConsoleManager::print(L"// Loading %s ...\\n", exePath.c_str());
            if (!toolset.load(exePath))
            {
                ConsoleManager::print(L"// [EXE] Failed to load '%s' (not a valid x86/x64 PE?).\n", exePath.c_str());
                return false;
            }
        }

        ConsoleManager::printText(toolset.infoText());
        return true;
    }
};
