#pragma once

#include <Core/RTTI/ExeToolset.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

#include <cwchar>

/// -exeaddr [rva:|va:]<address> <file.exe>
///
/// Describes an address (hex; a virtual address as shown by a debugger, or an RVA —
/// prefix "rva:" / "va:" to be explicit, which matters when the value is valid as
/// both): the containing function
/// and its probable owner class, the vftable slot, string, global or import
/// slot at that location, and every place that references it.
class CommandExeAddr : public ICommand
{
public:

    CommandExeAddr()
        : ICommand(2, Type::COMMAND_EXE_EXECUTE)
    {
        m_names.push_back(L"-exeaddr");
    }

    [[nodiscard]] const wchar_t* getArgHelp() const override { return L"[rva:|va:]<hex address> <file.exe>"; }

    [[nodiscard]] const wchar_t* getUsageHelp() const override
    {
        return L"what is at this RVA/VA and who references it";
    }

    bool execute(const std::wstring* a_args) override
    {
        if (!a_args)
            return false;

        std::wstring addrText = a_args[0];
        const std::wstring& exePath = a_args[1];

        // Optional "rva:" / "va:" prefix resolves the RVA/VA ambiguity explicitly.
        DumpPDB::ExeInspector::AddressKind kind = DumpPDB::ExeInspector::AddressKind::Auto;
        auto stripPrefix = [&](const wchar_t* a_prefix, DumpPDB::ExeInspector::AddressKind a_kind)
        {
            const size_t n = std::wcslen(a_prefix);
            if (addrText.size() > n && _wcsnicmp(addrText.c_str(), a_prefix, n) == 0)
            {
                addrText.erase(0, n);
                kind = a_kind;
            }
        };
        stripPrefix(L"rva:", DumpPDB::ExeInspector::AddressKind::Rva);
        stripPrefix(L"va:", DumpPDB::ExeInspector::AddressKind::Va);

        wchar_t* end = nullptr;
        const unsigned long long value = std::wcstoull(addrText.c_str(), &end, 16);
        if (end == addrText.c_str() || (end && *end != L'\0'))
        {
            ConsoleManager::print(L"// [EXE] '%s' is not a hexadecimal address.\n", a_args[0].c_str());
            return false;
        }
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

        ConsoleManager::printText(toolset.addressInfo(value, kind));
        return true;
    }
};
