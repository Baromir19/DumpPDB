#pragma once

#include <stdexcept>
#include <string>

/// Exception type for fatal errors in DumpPDB.
/// Caught once in main() to ensure clean shutdown with destructors called.
class DumpError : public std::runtime_error
{
public:

    explicit DumpError(const std::wstring& a_message)
        : std::runtime_error("DumpPDB error")
        , m_wideMessage(a_message)
    {
    }

    const std::wstring& wideMessage() const noexcept
    {
        return m_wideMessage;
    }

private:

    std::wstring m_wideMessage;
};