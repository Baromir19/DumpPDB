#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -execlass <name> <file.exe>
///
/// Describes a class from RTTI + analysis: hierarchy (with virtual-base data),
/// every vftable with named slots, constructor/destructor addresses, static
/// instances, source files and the strings/imports its functions use.
/// Unlike -exetype this needs no disassembler, so it is fast and has no
/// field-recovery guesswork.
class CommandExeClass : public ICommand
{
public:

    CommandExeClass()
        : ICommand(2, Type::COMMAND_EXE_EXECUTE)
    {
        m_names.push_back(L"-execlass");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"<typename> <file.exe>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"class details: bases, vtables, ctors, sources";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args)
            return false;

        const std::wstring& typeName = a_args[0];
        const std::wstring& exePath = a_args[1];
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

        ConsoleManager::printText(toolset.classInfo(typeName));
        return true;
    }
};
