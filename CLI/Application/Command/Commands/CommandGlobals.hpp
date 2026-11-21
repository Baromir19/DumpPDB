#pragma once

#include <Core/PdbToolset.hpp>
#include <Core/System/ClipboardManager.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

/// Shared behaviour of the commands that list global symbols.
///
/// The optional <filter> argument is a substring of the qualified name. Because
/// the .pdb path is always the LAST argument, a filter is present exactly when the
/// user supplied two arguments instead of one.
class CommandGlobalsBase : public ICommand
{
protected:

    CommandGlobalsBase(const DumpConfig& a_config, bool a_useClipboard)
        : ICommand(0, Type::COMMAND_EXECUTE)
        , m_dumpConfig(a_config)
        , m_useClipboard(a_useClipboard)
    {
    }

    /// Returns the filter text or nullptr when the user gave none.
    static const wchar_t* filterArg(const std::wstring* a_args)
    {
        const int count = ConsoleManager::instance().getCommandArgumentCount();
        return (a_args && count >= 2) ? a_args[0].c_str() : nullptr;
    }

    bool output(const std::wstring& a_text) const
    {
        if (a_text.empty())
        {
            ConsoleManager::print(L"// Nothing found.\n");
            return false;
        }

        if (m_useClipboard && !ClipboardManager::copy(a_text))
        {
            ConsoleManager::printError(L"Failed to copy result to clipboard.\n\n");
        }

        ConsoleManager::printText(a_text);
        return true;
    }

    DumpConfig m_dumpConfig;
    bool m_useClipboard = false;
};

/// -functions [filter]
///
/// Free functions of the PDB grouped by namespace, with calling convention,
/// attributes, RVA and source file:line. With DumpConfig.GlobalsIncludeMembers
/// the out-of-line member definitions are listed too.
class CommandFunctions : public CommandGlobalsBase
{
public:

    CommandFunctions(const DumpConfig& a_config, bool a_useClipboard)
        : CommandGlobalsBase(a_config, a_useClipboard)
    {
        m_names.push_back(L"-functions");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"[filter]"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"list global functions (with RVA, source line)";
    }

    bool execute(const std::wstring* a_args) override
    {
        auto& toolset = PdbToolset::instance();
        toolset.dumper().setConfig(m_dumpConfig);
        return output(toolset.dumpGlobalFunctions(filterArg(a_args), false));
    }
};

/// -globals [filter]
///
/// Global variables, file statics and constants (and, depending on the settings,
/// static class members and function-local statics) with their RVA and size.
class CommandGlobals : public CommandGlobalsBase
{
public:

    CommandGlobals(const DumpConfig& a_config, bool a_useClipboard)
        : CommandGlobalsBase(a_config, a_useClipboard)
    {
        m_names.push_back(L"-globals");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"[filter]"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"list global variables / statics (with RVA)";
    }

    bool execute(const std::wstring* a_args) override
    {
        auto& toolset = PdbToolset::instance();
        toolset.dumper().setConfig(m_dumpConfig);
        return output(toolset.dumpGlobalVariables(filterArg(a_args), false));
    }
};

/// -publics [filter]
///
/// Public symbols: decorated + undecorated name and address. These exist even in
/// stripped ("public only") PDBs that have no type information at all.
class CommandPublics : public CommandGlobalsBase
{
public:

    CommandPublics(const DumpConfig& a_config, bool a_useClipboard)
        : CommandGlobalsBase(a_config, a_useClipboard)
    {
        m_names.push_back(L"-publics");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"[filter]"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"list public symbols (works on stripped PDBs)";
    }

    bool execute(const std::wstring* a_args) override
    {
        auto& toolset = PdbToolset::instance();
        toolset.dumper().setConfig(m_dumpConfig);
        return output(toolset.dumpPublicSymbols(filterArg(a_args), false));
    }
};

/// -symbols <out.tsv>
///
/// Writes a tab-separated table of every function, variable, thunk and public
/// symbol (kind, name, undecorated, rva, section, offset, length, class,
/// compiland, source, line, flags, type) to a UTF-8 file. This is the input of
/// scripts/restore_project.py and of the IDA/Ghidra name import.
class CommandSymbols : public ICommand
{
public:

    explicit CommandSymbols(const DumpConfig& a_config)
        : ICommand(1, Type::COMMAND_EXECUTE)
        , m_dumpConfig(a_config)
    {
        m_names.push_back(L"-symbols");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"<out.tsv>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"export all symbols as a TSV table";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args)
            return false;

        auto& toolset = PdbToolset::instance();
        toolset.dumper().setConfig(m_dumpConfig);

        std::wstring error;
        if (!toolset.exportToFile(PdbToolset::EXPORT_SYMBOL_TABLE_SOURCE,
                nullptr,
                false,
                a_args[0].c_str(),
                error))
        {
            ConsoleManager::printError(L"Export failed: %s\n", error.c_str());
            return false;
        }

        ConsoleManager::print(L"// Symbol table written to %s\n", a_args[0].c_str());
        return true;
    }

private:

    DumpConfig m_dumpConfig;
};

/// -dumpall <out.txt>
///
/// Writes every type plus every function, variable and public symbol as C++-like
/// text to a UTF-8 file. Can be very large on a real game PDB.
class CommandDumpAll : public ICommand
{
public:

    explicit CommandDumpAll(const DumpConfig& a_config)
        : ICommand(1, Type::COMMAND_EXECUTE)
        , m_dumpConfig(a_config)
    {
        m_names.push_back(L"-dumpall");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"<out.txt>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"dump all types, functions, variables to a file";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args)
            return false;

        auto& toolset = PdbToolset::instance();
        toolset.dumper().setConfig(m_dumpConfig);

        std::wstring error;
        if (!toolset.exportToFile(
                PdbToolset::EXPORT_EVERYTHING, nullptr, false, a_args[0].c_str(), error))
        {
            ConsoleManager::printError(L"Export failed: %s\n", error.c_str());
            return false;
        }

        ConsoleManager::print(L"// Dump written to %s\n", a_args[0].c_str());
        return true;
    }

private:

    DumpConfig m_dumpConfig;
};
