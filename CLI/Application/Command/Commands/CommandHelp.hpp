#pragma once

#include <CLI/Application/Command/ICommand.hpp>

class CommandHelp : public ICommand
{
public:

    CommandHelp()
        : ICommand(0, Type::COMMAND_HELP)
    {
        m_names.push_back(L"-help");
        m_names.push_back(L"--h");
    }

    // virtual const wchar_t* getCommandName() const override { return L"-help"; }

    [[nodiscard]]
    const wchar_t* getArgHelp() const override
    {
        return L"";
    }

    [[nodiscard]]
    const wchar_t* getUsageHelp() const override
    {
        return L"print this table";
    }

    virtual bool execute(const std::wstring* a_commandArgs = nullptr) override
    {
        return true;
    }
};