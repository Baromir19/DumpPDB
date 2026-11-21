#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -exeresources <outdir> <file.exe>
///
/// Extracts the executable's resources into <outdir>: resources.rc (icons, cursors,
/// bitmaps, manifest, VERSIONINFO, STRINGTABLE, dialogs, menus, accelerators, user
/// data), resource.h, a byte-exact resources.res and the payload files
/// (icons/*.ico, manifest/*.manifest, ...).
class CommandExeResources : public ICommand
{
public:

    CommandExeResources()
        : ICommand(2, Type::COMMAND_EXE_EXECUTE)
    {
        m_names.push_back(L"-exeresources");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"<outdir> <file.exe>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"extract resources: .rc + .ico + manifest + .res";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args)
            return false;

        const std::wstring& outDir = a_args[0];
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

        bool ok = false;
        const std::wstring log = toolset.exportResources(outDir, ok);
        ConsoleManager::printText(log);
        return ok;
    }
};
