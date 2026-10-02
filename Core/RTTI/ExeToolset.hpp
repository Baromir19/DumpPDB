#pragma once

#include <Core/RTTI/RttiReader.hpp>
#include <Core/RTTI/TypeReconstructor.hpp>
#include <Core/Util/Container/Singleton.hpp>

#include <string>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// ExeToolset
///
/// High-level singleton facade for EXE-based type reconstruction.
///
/// Usage:
///   ExeToolset::instance().load(L"Dev.exe");
///   auto text = ExeToolset::instance().reconstructType("ActorInstance");
///   auto names = ExeToolset::instance().enumerateVftableTypes();
/// ============================================================================
class ExeToolset : public Singleton<ExeToolset>
{
    SET_SINGLETON_FRIEND(ExeToolset)

protected:

    ExeToolset() = default;

public:

    /// Load and parse a PE executable. Must be called before any other method.
    /// Returns false if the file cannot be opened or is not a valid x64 MSVC PE.
    bool load(const std::wstring& a_exePath)
    {
        m_loaded = false;
        m_exePath = a_exePath;

        if (!m_reader.load(a_exePath))
            return false;

        m_loaded = true;
        return true;
    }

    [[nodiscard]] bool isLoaded() const noexcept { return m_loaded; }

    [[nodiscard]] const std::wstring& exePath() const noexcept { return m_exePath; }

    // ─────────────────────────────────────────────────────────────────────────
    // Type reconstruction
    // ─────────────────────────────────────────────────────────────────────────

    /// Reconstruct a single type by name and return a C++ declaration string.
    /// Searches by exact name first, falls back to substring match.
    /// Returns an error string if not loaded or not found.
    /// a_paddingStyle: PaddingStyle::Array (default) or PaddingStyle::Expanded.
    [[nodiscard]] std::wstring reconstructType(const std::string& a_name,
        PaddingStyle a_paddingStyle = PaddingStyle::Array)
    {
        if (!m_loaded)
            return L"// [EXE] Not loaded. Call load() first.\n";

        TypeReconstructor rec;
        auto rt = rec.reconstruct(a_name, m_reader);

        if (!rt.hasRtti)
        {
            std::wstring err = L"// [EXE] Type not found: ";
            err += std::wstring(a_name.begin(), a_name.end());
            err += L"\n";
            return err;
        }

        rt.paddingStyle = a_paddingStyle;
        return TypeReconstructor::format(rt);
    }

    /// Reconstruct a single type by wide-string name.
    [[nodiscard]] std::wstring reconstructType(const std::wstring& a_name,
        PaddingStyle a_paddingStyle = PaddingStyle::Array)
    {
        std::string narrow;
        narrow.reserve(a_name.size());
        for (wchar_t c : a_name)
            narrow.push_back(static_cast<char>(c));
        return reconstructType(narrow, a_paddingStyle);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Enumeration
    // ─────────────────────────────────────────────────────────────────────────

    /// List all type names (demangled) found via RTTI in the executable.
    /// One name per line, sorted alphabetically.
    [[nodiscard]] std::wstring enumerateVftableTypes()
    {
        if (!m_loaded)
            return L"// [EXE] Not loaded.\n";

        const auto names = m_reader.allTypeNames();

        std::wstring out;
        for (const auto& n : names)
        {
            out += std::wstring(n.begin(), n.end());
            out += L'\n';
        }

        if (out.empty())
            out = L"// [EXE] No RTTI types found.\n";

        return out;
    }

    /// Total number of vftable entries found.
    [[nodiscard]] size_t vftableCount() const noexcept
    {
        return m_reader.vftables().size();
    }

    /// Total number of unique types (one type may have multiple vftables for MI).
    [[nodiscard]] size_t typeCount() const noexcept
    {
        return m_reader.allTypeNames().size();
    }

    /// Direct access to the underlying reader (for advanced use).
    [[nodiscard]] const RttiReader& reader() const noexcept { return m_reader; }

private:

    RttiReader   m_reader;
    std::wstring m_exePath;
    bool         m_loaded = false;
};

} // namespace DumpPDB
