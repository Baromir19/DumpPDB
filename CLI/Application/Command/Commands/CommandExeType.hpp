#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -exetype <typename> <file.exe>
///
/// Reconstructs a C++ type declaration from RTTI data in the given EXE.
/// Finds all vftables for the type, recovers fields from the constructor,
/// and prints a C++ declaration with virtual functions and field offsets.
class CommandExeType : public ICommand
{
public:

    CommandExeType()
        : ICommand(2, Type::COMMAND_EXE_EXECUTE) // typename + exe path
    {
        m_names.push_back(L"-exetype");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override
    {
        return L"<typename> <file.exe>";
    }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"reconstruct type from exe RTTI";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args) return false;

        // a_args[0] = typename, a_args[1] = exe path
        const std::wstring& typeName = a_args[0];
        const std::wstring& exePath  = a_args[1];

        auto& toolset = DumpPDB::ExeToolset::instance();

        // Load (or reload if different file)
        if (!toolset.isLoaded() || toolset.exePath() != exePath)
        {
            ConsoleManager::print(L"// Loading %s ...\n", exePath.c_str());
            if (!toolset.load(exePath))
            {
                ConsoleManager::print(
                    L"// [EXE] Failed to load '%s' (not a valid x64 PE?).\n",
                    exePath.c_str());
                return false;
            }
            ConsoleManager::print(
                L"// Loaded. Found %zu vftables (%zu unique types).\n\n",
                toolset.vftableCount(), toolset.typeCount());
        }

        auto result = toolset.reconstructType(typeName);
        ConsoleManager::print(result.c_str());
        return true;
    }
};
