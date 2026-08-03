#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ICommand
{
public:

    enum class Type : std::uint8_t
    {
        COMMAND_HELP = 0x0,
        COMMAND_OPTIONS = 0x1,

        COMMAND_EXECUTE = 0x10
    };

    static constexpr unsigned int s_executableMask = 0xF0;

    virtual ~ICommand() = default;

    [[nodiscard]]
    const std::vector<const wchar_t*>& getCommandNames()
    {
        return m_names;
    }

    [[nodiscard]]
    virtual const wchar_t* getArgHelp() const
        = 0;

    [[nodiscard]]
    virtual const wchar_t* getUsageHelp() const
        = 0;

    virtual bool execute(const std::wstring* a_commandArgs) = 0;

    [[nodiscard]]
    int getArgCount() const
    {
        return m_minArgCount;
    }

    [[nodiscard]] Type getType() const
    {
        return m_commandType;
    }

protected:

    int m_minArgCount;
    Type m_commandType;

    std::vector<const wchar_t*> m_names;

    ICommand(int a_argumentCount, Type a_type)
        : m_minArgCount(a_argumentCount)
        , m_commandType(a_type)
    {
    }
};
