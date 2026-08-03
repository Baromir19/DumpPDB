#pragma once

#include <Core/System/ConsoleManager.hpp>
#include <Core/System/ClipboardManager.hpp>
#include <Core/PdbToolset.hpp>

#include <CLI/Application/Command/ICommand.hpp>

class CommandType : public ICommand
{
private:

    bool m_useClipboard = false;
    DumpConfig m_dumpConfig;

public:

    CommandType(bool a_useClipboard, const DumpConfig& config)
        : ICommand(1, Type::COMMAND_EXECUTE)
        , m_useClipboard(a_useClipboard)
        , m_dumpConfig(config)
    {
        m_names.push_back(L"-type");
    }

    // virtual const wchar_t* getCommandName() const override { return L"-type"; }

    [[nodiscard]]
    const wchar_t* getArgHelp() const override
    {
        return L"<typename>";
    }

    [[nodiscard]]
    const wchar_t* getUsageHelp() const override
    {
        return L"print type info";
    }

    virtual bool execute(const std::wstring* a_commandArgs) override
    {
        if (a_commandArgs == nullptr)
        {
            return false;
        }

        auto& toolset = PdbToolset::instance();

        toolset.dumper().setConfig(m_dumpConfig);

        auto text = toolset.dumpTypeByName(a_commandArgs[0].c_str(), false);
        if (text.empty())
        {
            return false;
        }

        if (m_useClipboard && !ClipboardManager::copy(text))
        {
            ConsoleManager::printError(L"Failed to copy result to clipboard.\n\n");
        }

        ConsoleManager::print(text.c_str());
        return true;
    }
};
