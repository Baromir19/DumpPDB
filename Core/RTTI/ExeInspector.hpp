#pragma once

#include <Core/PE/RcExporter.hpp>
#include <Core/RTTI/ExeAnalyzer.hpp>
#include <Core/RTTI/ExeReport.hpp>
#include <Core/RTTI/RttiReader.hpp>

#include <memory>
#include <string>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// ExeInspector — text-level facade over RttiReader + ExeAnalyzer.
///
/// Holds no disassembler dependency, so everything it does can be (and is)
/// exercised against real executables on any platform. The analysis is computed
/// lazily on first use and cached.
/// ============================================================================
class ExeInspector
{
public:

    explicit ExeInspector(const RttiReader& a_reader)
        : m_reader(a_reader)
    {
    }

    /// Drop the cached analysis (call after the reader was reloaded).
    void reset() { m_analysis.reset(); }

    [[nodiscard]] const ExeAnalysis& analysis()
    {
        if (!m_analysis)
            m_analysis = std::make_unique<ExeAnalysis>(ExeAnalyzer::analyze(m_reader));
        return *m_analysis;
    }

    /// PE summary, debug info, build tools, sections, imports, RTTI + analysis statistics.
    [[nodiscard]] std::string infoText() { return ExeReport::summary(m_reader, analysis()); }

    /// Full description of the class named a_name (exact match first, then
    /// substring; at most a_maxClasses are expanded).
    [[nodiscard]] std::string classText(const std::string& a_name, size_t a_maxClasses = 5)
    {
        const ExeAnalysis& a = analysis();
        const std::string needle = normalize(a_name);

        std::vector<const ExeClass*> exact;
        std::vector<const ExeClass*> partial;
        for (const auto& c : a.classes)
        {
            const std::string n = normalize(c.name);
            if (n == needle || normalize(MsvcDemangler::leafName(c.name)) == needle)
                exact.push_back(&c);
            else if (n.find(needle) != std::string::npos)
                partial.push_back(&c);
        }

        const auto& pick = exact.empty() ? partial : exact;
        if (pick.empty())
            return "// [EXE] No class matches '" + a_name + "'.\n";

        std::string out;
        if (pick.size() > a_maxClasses)
        {
            out += "// " + std::to_string(pick.size()) + " classes match '" + a_name + "'; showing names only:\n";
            for (const auto* c : pick)
                out += "//   " + c->name + "\n";
            return out;
        }
        for (const auto* c : pick)
            out += ExeReport::classDetail(m_reader, a, *c) + "\n";
        return out;
    }

    /// How a numeric address is to be interpreted.
    enum class AddressKind : int
    {
        Auto = 0, ///< VA when it lies inside [imageBase, imageBase+sizeOfImage), else RVA.
        Rva = 1,
        Va = 2,
    };

    /// Describe an address.
    ///
    /// With AddressKind::Auto a value can be ambiguous: for an image based at
    /// 0x400000 and 0x1C78000 bytes long, 0x915020 is a valid RVA *and* a valid
    /// VA (RVA 0x515020). Auto then picks the VA (what debuggers show) and says
    /// so, so the caller can repeat the query with an explicit kind.
    [[nodiscard]] std::string addressText(uint64_t a_value, AddressKind a_kind = AddressKind::Auto)
    {
        const PEImage& pe = m_reader.pe();
        const uint64_t base = pe.imageBase();
        const uint64_t size = pe.sizeOfImage();

        const bool vaValid = a_value >= base && a_value - base < size;
        const bool rvaValid = a_value < size;

        uint64_t rva = a_value;
        std::string note;

        switch (a_kind)
        {
        case AddressKind::Rva:
            rva = a_value;
            break;
        case AddressKind::Va:
            if (a_value < base)
                return "// [EXE] 0x" + ExeReport::hex(a_value) + " is below the image base 0x"
                       + ExeReport::hex(base) + " and cannot be a virtual address.\n";
            rva = a_value - base;
            break;
        case AddressKind::Auto:
        default:
            if (vaValid)
            {
                rva = a_value - base;
                if (rvaValid)
                    note = "// Note: 0x" + ExeReport::hex(a_value) + " is also a valid RVA; it was read as a "
                           "VA. Use the 'rva:' prefix to read it as an RVA.\n";
            }
            break;
        }
        return note + ExeReport::describeAddress(m_reader, analysis(), rva);
    }

    /// Write every report + disassembler script into a_dir.
    [[nodiscard]] ExeExportResult exportAll(const std::filesystem::path& a_dir, const std::string& a_moduleName)
    {
        return ExeExporter::exportAll(m_reader, analysis(), a_dir, a_moduleName);
    }

    /// Extract every resource into a_dir: <name>.rc, resource.h, a byte-exact <name>.res,
    /// icons/*.ico, cursors/*.cur, bitmaps/*.bmp, manifest/*.manifest, binary/* and
    /// resources.csv. Returns a human-readable log; a_ok is false on a write error.
    [[nodiscard]] std::string exportResources(
        const std::filesystem::path& a_dir, bool& a_ok, const std::string& a_baseName = "resources")
    {
        a_ok = true;
        const auto items = PEResources::parse(m_reader.pe());
        if (items.empty())
            return "// The executable has no resources.\n";

        const RcExportResult result = RcExporter::build(items, a_baseName);
        std::vector<std::string> errors;
        const auto written = RcExporter::writeTo(result, a_dir, a_baseName, errors);
        a_ok = errors.empty();

        std::string log = "// " + std::to_string(items.size()) + " resources -> " + std::to_string(written.size())
                          + " files in " + a_dir.string() + "\n";
        for (const auto& w : written)
            log += "//   " + w + "\n";
        for (const auto& n : result.notes)
            log += "// NOTE: " + n + "\n";
        for (const auto& e : errors)
            log += "// ERROR: " + e + "\n";
        return log;
    }

    // ── String conversion helpers (UTF-8 <-> UTF-16/32 wchar_t) ──────────

    [[nodiscard]] static std::wstring widen(const std::string& a_utf8)
    {
        std::wstring out;
        out.reserve(a_utf8.size());
        for (size_t i = 0; i < a_utf8.size();)
        {
            const unsigned char c = static_cast<unsigned char>(a_utf8[i]);
            uint32_t cp = c;
            size_t len = 1;
            if (c >= 0xF0 && i + 3 < a_utf8.size())
            {
                cp = ((c & 0x07u) << 18) | ((a_utf8[i + 1] & 0x3Fu) << 12) | ((a_utf8[i + 2] & 0x3Fu) << 6)
                     | (a_utf8[i + 3] & 0x3Fu);
                len = 4;
            }
            else if (c >= 0xE0 && i + 2 < a_utf8.size())
            {
                cp = ((c & 0x0Fu) << 12) | ((a_utf8[i + 1] & 0x3Fu) << 6) | (a_utf8[i + 2] & 0x3Fu);
                len = 3;
            }
            else if (c >= 0xC0 && i + 1 < a_utf8.size())
            {
                cp = ((c & 0x1Fu) << 6) | (a_utf8[i + 1] & 0x3Fu);
                len = 2;
            }
            if (sizeof(wchar_t) == 2 && cp > 0xFFFF)
            {
                cp -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            }
            else
                out.push_back(static_cast<wchar_t>(cp));
            i += len;
        }
        return out;
    }

    [[nodiscard]] static std::string narrow(const std::wstring& a_wide)
    {
        std::string out;
        out.reserve(a_wide.size());
        for (size_t i = 0; i < a_wide.size(); ++i)
        {
            uint32_t cp = static_cast<uint32_t>(a_wide[i]);
            if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp < 0xDC00 && i + 1 < a_wide.size())
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(a_wide[i + 1]) - 0xDC00);
                ++i;
            }
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    /// File-name part of a path (handles both separators).
    [[nodiscard]] static std::string fileNameOf(const std::wstring& a_path)
    {
        const size_t p = a_path.find_last_of(L"\\/");
        return narrow(p == std::wstring::npos ? a_path : a_path.substr(p + 1));
    }

private:

    [[nodiscard]] static std::string normalize(const std::string& a_name)
    {
        std::string r;
        for (char c : a_name)
            if (c != ' ')
                r.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
        return r;
    }

    const RttiReader& m_reader;
    std::unique_ptr<ExeAnalysis> m_analysis;
};

} // namespace DumpPDB
