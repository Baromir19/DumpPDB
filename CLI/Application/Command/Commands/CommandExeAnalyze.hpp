#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// -exeanalyze <outdir> <file.exe>
///
/// Full static analysis of the exe. Writes into <outdir>:
///   summary.txt, imports.txt, classes.txt/.csv, vtables.csv, hierarchy.txt/.dot,
///   functions.csv, strings.csv, globals.csv, source_files.txt,
///   source_spellings.txt, source_map.csv, names.json and the scripts
///   apply_names_ida.py, apply_names_ghidra.py, x64dbg_labels.dd32.json.
class CommandExeAnalyze : public ICommand
{
public:

    CommandExeAnalyze()
        : ICommand(2, Type::COMMAND_EXE_EXECUTE)
    {
        m_names.push_back(L"-exeanalyze");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"<outdir> <file.exe>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"full exe analysis -> reports + IDA/Ghidra/x64dbg scripts";
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
        const std::wstring log = toolset.exportAnalysis(outDir, ok);
        ConsoleManager::printText(log);
        return ok;
    }
};
