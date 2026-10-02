#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -exetype <typename> <file.exe> [expanded]
///
/// Reconstructs a C++ type declaration from RTTI data in the given EXE.
/// Optional third argument "expanded" switches padding from arrays to
/// individual fields (one declaration per 4/2/1 bytes).
class CommandExeType : public ICommand
{
public:

    CommandExeType()
        : ICommand(2, Type::COMMAND_EXE_EXECUTE) // typename + exe path (+ optional "expanded")
    {
        m_names.push_back(L"-exetype");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override
    {
        return L"<typename> <file.exe> [expanded]";
    }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"reconstruct type from exe RTTI";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args) return false;

        // a_args[0] = typename, a_args[1] = exe path, a_args[2] = optional "expanded"
        const std::wstring& typeName = a_args[0];
        const std::wstring& exePath  = a_args[1];

        DumpPDB::PaddingStyle paddingStyle = DumpPDB::PaddingStyle::Array;
        // Check if a third argument exists and equals "expanded"
        // getCommandArguments() returns pointer to argv[2], so args[2] is the third CLI arg
        // We check via ConsoleManager arg count
        {
            const auto argCount = ConsoleManager::instance().getCommandArgumentCount();
            if (argCount >= 3 && a_args[2] == L"expanded")
                paddingStyle = DumpPDB::PaddingStyle::Expanded;
        }

        auto& toolset = DumpPDB::ExeToolset::instance();

        if (!toolset.isLoaded() || toolset.exePath() != exePath)
        {
            ConsoleManager::print(L"// Loading %s ...\n", exePath.c_str());
            if (!toolset.load(exePath))
            {
                ConsoleManager::print(
                    L"// [EXE] Failed to load '%s' (not a valid x64/x86 MSVC PE?).\n",
                    exePath.c_str());
                return false;
            }
            ConsoleManager::print(
                L"// Loaded. Found %zu vftables (%zu unique types).\n\n",
                toolset.vftableCount(), toolset.typeCount());
        }

        auto result = toolset.reconstructType(typeName, paddingStyle);
        ConsoleManager::print(result.c_str());
        return true;
    }
};
