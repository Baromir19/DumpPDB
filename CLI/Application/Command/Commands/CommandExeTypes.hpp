#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -exetypes <file.exe>
///
/// Enumerates all types found via RTTI in the given EXE.
/// Prints one fully-qualified demangled name per line, sorted alphabetically.
/// Also prints a summary: total vftable count and unique type count.
class CommandExeTypes : public ICommand
{
public:

    CommandExeTypes()
        : ICommand(1, Type::COMMAND_EXE_EXECUTE) // exe path only
    {
        m_names.push_back(L"-exetypes");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override
    {
        return L"<file.exe>";
    }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"enumerate all RTTI types in exe";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args) return false;

        // a_args[0] = exe path
        const std::wstring& exePath = a_args[0];

        auto& toolset = DumpPDB::ExeToolset::instance();

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
        }

        ConsoleManager::print(
            L"// Found %zu vftables across %zu unique types.\n\n",
            toolset.vftableCount(), toolset.typeCount());

        auto names = toolset.enumerateVftableTypes();
        ConsoleManager::print(names.c_str());
        return true;
    }
};
