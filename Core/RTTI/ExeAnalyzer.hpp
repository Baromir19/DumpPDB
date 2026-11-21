#pragma once

#include <Core/PE/PEDirectories.hpp>
#include <Core/PE/PEImage.hpp>
#include <Core/PE/PERelocations.hpp>
#include <Core/RTTI/RttiReader.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// ExeAnalyzer — whole-image analysis that needs NO disassembler.
///
/// Everything here is derived from three facts that a PE image gives away for
/// free:
///
///   1. Base relocations list every absolute pointer stored in code and data.
///      Reading the pointer value turns that list into an exact cross-reference
///      index: "this instruction/data word refers to that address".
///   2. MSVC pads functions with 0xCC and aligns them, so function boundaries
///      can be recovered with high accuracy from the bytes alone.
///   3. RTTI (via RttiReader) names the classes and lists their virtual
///      functions.
///
/// From these it builds: a function table, string table (with who references
/// each string), the imports each function uses, the static/global objects, the
/// source-file paths embedded by assert macros, and a mapping
/// function -> class -> source file. Names are produced for everything that can
/// be named with reasonable confidence.
///
/// Accuracy is documented per feature: function discovery is a heuristic
/// (reported with its recall against the vftable slots, which are known to be
/// function starts); class membership of non-virtual functions is inferred, and
/// every name carries a confidence level.
/// ============================================================================

/// Which evidence led to a function start being recorded (bit flags).
enum ExeFuncOrigin : uint32_t
{
    FO_Padding = 1u << 0,    ///< Follows 0xCC padding and is 16-byte aligned.
    FO_PostRet = 1u << 1,    ///< Follows a ret and begins with a standard prologue.
    FO_VtableSlot = 1u << 2, ///< Stored in a vftable.
    FO_CallTarget = 1u << 3, ///< Target of a direct call, right after a terminator.
    FO_EntryPoint = 1u << 4,
    FO_Export = 1u << 5,
    FO_DataPointer = 1u << 6, ///< Pointer stored in a data section (callback, table).
    FO_Tls = 1u << 7,
    FO_SafeSeh = 1u << 8,
    FO_Cfg = 1u << 9,
};

enum class NameConfidence : uint8_t
{
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3,
};

struct ExeFunction
{
    uint64_t rva = 0;
    uint32_t size = 0;     ///< Up to the next function start, trailing padding trimmed.
    uint32_t origin = 0;   ///< ExeFuncOrigin bits.
    uint32_t callers = 0;  ///< Direct-call (E8) sites that target it.
    uint32_t dataRefs = 0; ///< Relocated pointers to it (vftables, callback tables, push offset).

    std::string name;
    NameConfidence confidence = NameConfidence::None;
    std::string ownerClass;   ///< Class this function is believed to belong to.
    std::string sourceFile;   ///< Most frequent source path referenced by its body.

    std::vector<uint32_t> strings; ///< Indices into ExeAnalysis::strings (unique, capped).
    std::vector<uint32_t> imports; ///< Indices into ExeAnalysis::imports (unique, capped).
    uint32_t stringRefCount = 0;
    uint32_t importRefCount = 0;
};

struct ExeString
{
    uint64_t rva = 0;
    std::string text;      ///< UTF-8 (UTF-16 strings are narrowed/escaped).
    bool wide = false;
    uint32_t byteLength = 0; ///< Including the terminator.
    uint32_t refCount = 0;
    std::vector<uint32_t> funcs; ///< Referencing functions (unique, capped).
    bool dataRef = false;        ///< Referenced from a data section too.
    std::string sourcePath;      ///< Non-empty when the string embeds a source path.
};

struct ExeGlobal
{
    uint64_t rva = 0;
    std::string section;
    bool isBss = false;         ///< Not file-backed (zero-initialised at load).
    uint32_t refCount = 0;
    uint32_t maxSize = 0;       ///< Distance to the next referenced global (capped), 0 = unknown.
    std::string kind;           ///< "object", "pointer", "function-pointer", "string-pointer", "data".
    std::string className;      ///< For kind == "object": the class whose vptr it holds.
    uint64_t pointsTo = 0;      ///< Initial pointer value (RVA) for pointer kinds.
    std::vector<uint32_t> funcs; ///< Accessing functions (unique, capped).
};

struct ExeSourceFile
{
    std::string path;       ///< As embedded in the binary (first spelling seen).
    /// Every distinct spelling (the same file is often written with different
    /// capitalisation / slashes in different asserts). Useful to recover the
    /// real on-disk case.
    std::vector<std::string> spellings;
    uint32_t refCount = 0;
    std::vector<uint32_t> funcs;
    std::vector<std::string> classes; ///< Classes whose functions reference it.
};

struct ExeClass
{
    std::string name;
    uint64_t typeDescRva = 0;
    uint64_t primaryVftableRva = 0;
    uint32_t vftableCount = 0;
    uint32_t slotCount = 0;           ///< Slots of the primary vftable.
    uint32_t constructorCount = 0;
    std::vector<std::string> directBases;
    std::vector<uint32_t> ctorFuncs;  ///< Function indices that write the vptr.
    int32_t dtorFunc = -1;            ///< Function written/called by the deleting dtor.
    int32_t deletingDtorFunc = -1;
    std::vector<std::pair<std::string, uint32_t>> sources; ///< path -> score, best first.
};

struct ExeImportUse
{
    std::string dll;
    std::string name;   ///< Name or "#ordinal".
    uint64_t iatRva = 0;
    uint32_t refCount = 0;
    bool delayLoaded = false;
};

/// A relocated pointer: the word at `site` holds the address `target` (both RVAs).
struct ExeRef
{
    uint32_t site = 0;
    uint32_t target = 0;
};

struct ExeAnalysisStats
{
    size_t functions = 0;
    size_t functionsWithName = 0;
    size_t callSites = 0;
    size_t relocRefs = 0;
    size_t strings = 0;
    size_t referencedStrings = 0;
    size_t sourcePaths = 0;
    size_t globals = 0;
    size_t classesWithCtor = 0;
    size_t classesWithSource = 0;
    size_t vtableTargets = 0;
    size_t vtableTargetsFound = 0; ///< Targets that function discovery found *independently*.
    double vtableRecall = 0.0;
    size_t deletingDtors = 0;
    size_t deletingDtorCandidates = 0; ///< Slot-0 targets inspected.
};

struct ExeAnalysis
{
    std::vector<ExeFunction> functions;      ///< Sorted by rva.
    std::vector<ExeString> strings;          ///< Sorted by rva.
    std::vector<ExeGlobal> globals;          ///< Sorted by rva.
    std::vector<ExeSourceFile> sourceFiles;  ///< Sorted by path.
    std::vector<ExeClass> classes;           ///< Sorted by name.
    std::vector<ExeImportUse> imports;
    PEExtendedInfo pe;
    ExeAnalysisStats stats;
    std::vector<ExeRef> refsByTarget; ///< Every pointer reference, sorted by (target, site).

    /// All pointer references whose target is exactly a_target.
    [[nodiscard]] std::pair<const ExeRef*, const ExeRef*> refsTo(uint64_t a_target) const
    {
        const auto lo = std::lower_bound(refsByTarget.begin(),
            refsByTarget.end(),
            a_target,
            [](const ExeRef& r, uint64_t t) { return r.target < t; });
        auto hi = lo;
        while (hi != refsByTarget.end() && hi->target == a_target)
            ++hi;
        const ExeRef* base = refsByTarget.data();
        return {base + (lo - refsByTarget.begin()), base + (hi - refsByTarget.begin())};
    }

    /// Index of the function that contains a_rva, or -1.
    [[nodiscard]] int64_t functionIndexAt(uint64_t a_rva) const
    {
        auto it = std::upper_bound(functions.begin(),
            functions.end(),
            a_rva,
            [](uint64_t v, const ExeFunction& f) { return v < f.rva; });
        if (it == functions.begin())
            return -1;
        --it;
        const uint64_t span = std::max<uint32_t>(it->size, 1);
        if (a_rva >= it->rva + span && it->size != 0)
            return -1;
        return it - functions.begin();
    }

    /// Index of the function that STARTS at a_rva, or -1.
    [[nodiscard]] int64_t functionIndexStartingAt(uint64_t a_rva) const
    {
        auto it = std::lower_bound(functions.begin(),
            functions.end(),
            a_rva,
            [](const ExeFunction& f, uint64_t v) { return f.rva < v; });
        if (it == functions.end() || it->rva != a_rva)
            return -1;
        return it - functions.begin();
    }
};

class ExeAnalyzer
{
public:

    /// Run the full analysis. a_reader must already be loaded.
    [[nodiscard]] static ExeAnalysis analyze(const RttiReader& a_reader)
    {
        ExeAnalyzer an(a_reader);
        return an.run();
    }

    /// "e:\foo\bar.cpp" pulled out of arbitrary text, or "" if there is none.
    [[nodiscard]] static std::string extractSourcePath(const std::string& a_text)
    {
        static const char* const kExt[] = {".cpp", ".cxx", ".cc", ".hpp", ".hxx", ".inl", ".inc", ".c", ".h"};

        std::string lower;
        lower.reserve(a_text.size());
        for (char c : a_text)
            lower.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));

        for (const char* ext : kExt)
        {
            const size_t elen = std::strlen(ext);
            size_t pos = 0;
            while ((pos = lower.find(ext, pos)) != std::string::npos)
            {
                const size_t end = pos + elen;
                pos = end;

                // The extension must end the token (end of text, '(', ':', space, quote ...).
                if (end < lower.size() && isalnum(static_cast<unsigned char>(lower[end])))
                    continue;

                // Walk back to the start of the path.
                size_t begin = end - elen;
                bool hasSep = false;
                while (begin > 0)
                {
                    const char c = a_text[begin - 1];
                    if (c == '\\' || c == '/')
                        hasSep = true;
                    if (c == '"' || c == '\'' || c == '<' || c == '>' || c == '|' || c == '*'
                        || c == '?' || c == '\n' || c == '\r' || c == '\t')
                        break;
                    // A space only ends the path when it is not part of "C:\Program Files\...".
                    if (c == ' ' && !hasSep)
                        break;
                    --begin;
                }

                std::string path = a_text.substr(begin, end - begin);
                while (!path.empty() && path.front() == ' ')
                    path.erase(path.begin());

                path = cleanPath(path);
                if (path.size() >= 4 && hasSep)
                    return path;
            }
        }
        return {};
    }

    /// Drops binary junk that precedes the real path inside a string
    /// (",@e:\\dir\\a.cpp" -> "e:\\dir\\a.cpp").
    [[nodiscard]] static std::string cleanPath(std::string a_path)
    {
        // Prefer a drive-letter / UNC start if one occurs after the first byte.
        for (size_t i = 0; i + 2 < a_path.size(); ++i)
        {
            const bool drive = isalpha(static_cast<unsigned char>(a_path[i])) && a_path[i + 1] == ':'
                               && (a_path[i + 2] == '\\' || a_path[i + 2] == '/');
            const bool unc = a_path[i] == '\\' && a_path[i + 1] == '\\' && i + 2 < a_path.size()
                             && isalnum(static_cast<unsigned char>(a_path[i + 2]));
            // A drive letter normally follows a non-alphanumeric byte, but binary
            // junk right in front of the path is often a letter too ("Ae:\\dir"):
            // accept a drive start after at most two leading characters as well.
            if ((drive || unc)
                && (i == 0 || i <= 2 || !isalnum(static_cast<unsigned char>(a_path[i - 1]))))
            {
                a_path.erase(0, i);
                return a_path;
            }
        }

        size_t b = 0;
        while (b < a_path.size())
        {
            const unsigned char c = static_cast<unsigned char>(a_path[b]);
            if (isalnum(c) || c == '.' || c == '\\' || c == '/' || c == '_')
                break;
            ++b;
        }
        a_path.erase(0, b);
        return a_path;
    }

    /// Turn a demangled/arbitrary name into a safe identifier.
    [[nodiscard]] static std::string safeIdentifier(const std::string& a_name)
    {
        std::string out;
        out.reserve(a_name.size());
        for (char c : a_name)
        {
            if (isalnum(static_cast<unsigned char>(c)) || c == '_')
                out.push_back(c);
            else if (c == ':' && !out.empty() && out.back() != '_')
                out.push_back('_');
            else if (!out.empty() && out.back() != '_')
                out.push_back('_');
        }
        while (!out.empty() && out.back() == '_')
            out.pop_back();
        if (out.empty() || isdigit(static_cast<unsigned char>(out.front())))
            out.insert(out.begin(), '_');
        return out;
    }

private:

    explicit ExeAnalyzer(const RttiReader& a_reader)
        : m_reader(a_reader)
        , m_pe(a_reader.pe())
    {
    }

    using Ref = ExeRef;

    ExeAnalysis run()
    {
        m_out.pe = PEDirectories::parse(m_pe);
        m_imageBase = m_pe.imageBase();
        m_ptrSize = m_pe.is64Bit() ? 8u : 4u;

        collectSections();
        buildRefs();
        discoverFunctions();
        scanCalls();
        collectStrings();
        classify();
        buildClasses();
        nameEverything();
        assignSources();
        finalize();
        m_out.refsByTarget = std::move(m_refsByTarget);
        return std::move(m_out);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Sections
    // ─────────────────────────────────────────────────────────────────────

    struct Range
    {
        uint64_t begin;
        uint64_t end;
        const PESection* sec;
    };

    void collectSections()
    {
        for (const auto& s : m_pe.sections())
        {
            const uint64_t span = std::max<uint64_t>(s.virtualSize(), s.size());
            if (span == 0)
                continue;
            Range r{s.virtualAddress(), s.virtualAddress() + span, &s};
            if (s.isExecutable() || s.hasCode())
                m_code.push_back(r);
            else if (s.name() != ".reloc" && s.name() != ".rsrc")
            {
                m_data.push_back(r);
                if (s.isWritable())
                    m_writable.push_back(r);
            }
        }
    }

    [[nodiscard]] static bool inRanges(const std::vector<Range>& a_r, uint64_t a_rva)
    {
        for (const auto& r : a_r)
            if (a_rva >= r.begin && a_rva < r.end)
                return true;
        return false;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Reference index built from relocations
    // ─────────────────────────────────────────────────────────────────────

    void buildRefs()
    {
        const auto& relocs = m_reader.relocations().rvas();
        m_refsBySite.reserve(relocs.size());

        for (uint32_t site : relocs)
        {
            uint64_t va = 0;
            if (!m_pe.readPointer(site, va))
                continue;
            if (va < m_imageBase || va - m_imageBase >= m_pe.sizeOfImage())
                continue;
            m_refsBySite.push_back({site, static_cast<uint32_t>(va - m_imageBase)});
        }
        // Already sorted by site because the relocation table is.
        m_refsByTarget = m_refsBySite;
        std::sort(m_refsByTarget.begin(),
            m_refsByTarget.end(),
            [](const Ref& a, const Ref& b)
            { return a.target != b.target ? a.target < b.target : a.site < b.site; });
        m_out.stats.relocRefs = m_refsBySite.size();
    }

    /// All references whose target is exactly a_target.
    [[nodiscard]] std::pair<std::vector<Ref>::const_iterator, std::vector<Ref>::const_iterator>
    refsTo(uint64_t a_target) const
    {
        const auto lo = std::lower_bound(m_refsByTarget.begin(),
            m_refsByTarget.end(),
            a_target,
            [](const Ref& r, uint64_t t) { return r.target < t; });
        auto hi = lo;
        while (hi != m_refsByTarget.end() && hi->target == a_target)
            ++hi;
        return {lo, hi};
    }

    // ─────────────────────────────────────────────────────────────────────
    // Function discovery
    // ─────────────────────────────────────────────────────────────────────

    void addStart(uint64_t a_rva, uint32_t a_origin)
    {
        if (!inRanges(m_code, a_rva))
            return;
        m_starts[a_rva] |= a_origin;
    }

    [[nodiscard]] static bool isTerminatorBefore(const BinaryView& v, size_t i)
    {
        if (i >= 1 && v[i - 1] == 0xC3)
            return true; // ret
        if (i >= 3 && v[i - 3] == 0xC2)
            return true; // ret imm16
        if (i >= 5 && v[i - 5] == 0xE9)
            return true; // jmp rel32
        if (i >= 2 && v[i - 2] == 0xEB)
            return true; // jmp rel8
        if (i >= 6 && v[i - 6] == 0xFF && v[i - 5] == 0x25)
            return true; // jmp [mem]
        return false;
    }

    [[nodiscard]] static bool hasStrongPrologue(const BinaryView& v, size_t i)
    {
        if (i + 5 > v.size())
            return false;
        // push ebp; mov ebp, esp
        if (v[i] == 0x55 && v[i + 1] == 0x8B && v[i + 2] == 0xEC)
            return true;
        // mov edi, edi; push ebp; mov ebp, esp   (hot-patch prologue)
        if (v[i] == 0x8B && v[i + 1] == 0xFF && v[i + 2] == 0x55)
            return true;
        // push -1; push handler  (SEH prolog)
        if (v[i] == 0x6A && v[i + 1] == 0xFF && v[i + 2] == 0x68)
            return true;
        return false;
    }

    void discoverFunctions()
    {
        // (1) Padding boundaries and (2) "ret followed by a prologue".
        for (const auto& cr : m_code)
        {
            const BinaryView v = cr.sec->view();
            const uint64_t va0 = cr.sec->virtualAddress();
            size_t run = 0;

            for (size_t i = 0; i < v.size(); ++i)
            {
                if (v[i] == 0xCC)
                {
                    ++run;
                    continue;
                }

                const uint64_t rva = va0 + i;
                if (run >= 1 && (rva & 0xF) == 0)
                    m_starts[rva] |= FO_Padding;
                else if (run == 0 && i >= 4 && hasStrongPrologue(v, i)
                         && ((v[i - 1] == 0xC3) || (v[i - 3] == 0xC2)))
                    m_starts[rva] |= FO_PostRet;
                run = 0;
            }
        }

        // (3) Independent evidence from the headers and from RTTI.
        addStart(m_pe.entryPointRva(), FO_EntryPoint);
        for (const auto& e : m_out.pe.exports)
            if (e.forwarder.empty())
                addStart(e.rva, FO_Export);
        for (uint64_t cb : m_out.pe.tls.callbackRvas)
            addStart(cb, FO_Tls);
        for (uint64_t h : m_out.pe.loadConfig.seHandlerRvas)
            addStart(h, FO_SafeSeh);
        for (uint64_t h : m_out.pe.loadConfig.guardCfFunctionRvas)
            addStart(h, FO_Cfg);

        // Recall of the *padding/prologue* heuristic is measured against vftable
        // targets BEFORE they are added (they are known function starts).
        std::set<uint64_t> vtableTargets;
        for (const auto& v : m_reader.vftables())
            for (const auto& s : v.vfuncs)
                vtableTargets.insert(s.funcRva);

        size_t found = 0;
        for (uint64_t t : vtableTargets)
            if (m_starts.count(t))
                ++found;
        m_out.stats.vtableTargets = vtableTargets.size();
        m_out.stats.vtableTargetsFound = found;
        m_out.stats.vtableRecall = vtableTargets.empty()
                                       ? 0.0
                                       : 100.0 * static_cast<double>(found)
                                             / static_cast<double>(vtableTargets.size());

        for (uint64_t t : vtableTargets)
            addStart(t, FO_VtableSlot);

        // Pointers stored in DATA that point at an existing start (callbacks, tables).
        for (const auto& r : m_refsBySite)
        {
            if (!inRanges(m_data, r.site))
                continue;
            auto it = m_starts.find(r.target);
            if (it != m_starts.end())
                it->second |= FO_DataPointer;
        }
    }

    /// Scans for direct calls; adds well-supported targets and counts callers.
    void scanCalls()
    {
        struct Site
        {
            uint64_t from;
            uint64_t to;
        };
        std::vector<Site> sites;

        for (const auto& cr : m_code)
        {
            const BinaryView v = cr.sec->view();
            const uint64_t va0 = cr.sec->virtualAddress();
            for (size_t i = 0; i + 5 <= v.size(); ++i)
            {
                if (v[i] != 0xE8)
                    continue;
                int32_t rel = 0;
                std::memcpy(&rel, &v[i + 1], 4);
                const int64_t tgt = static_cast<int64_t>(va0 + i + 5) + rel;
                if (tgt < 0 || !inRanges(m_code, static_cast<uint64_t>(tgt)))
                    continue;

                sites.push_back({va0 + i, static_cast<uint64_t>(tgt)});

                // A call target directly after a terminator is a function start even
                // without padding (optimised leaf functions packed back to back).
                const Range* rr = rangeOf(m_code, static_cast<uint64_t>(tgt));
                if (rr && !m_starts.count(static_cast<uint64_t>(tgt)))
                {
                    const BinaryView tv = rr->sec->view();
                    const size_t off = static_cast<size_t>(static_cast<uint64_t>(tgt) - rr->begin);
                    if (off < tv.size() && isTerminatorBefore(tv, off))
                        m_starts[static_cast<uint64_t>(tgt)] |= FO_CallTarget;
                }
            }
        }

        // Materialise the function table.
        m_out.functions.reserve(m_starts.size());
        for (const auto& [rva, origin] : m_starts)
        {
            ExeFunction f;
            f.rva = rva;
            f.origin = origin;
            m_out.functions.push_back(std::move(f));
        }
        computeSizes();

        for (const auto& s : sites)
        {
            const int64_t idx = m_out.functionIndexStartingAt(s.to);
            if (idx >= 0)
                ++m_out.functions[static_cast<size_t>(idx)].callers;
        }
        m_out.stats.callSites = sites.size();
        m_callSites.reserve(sites.size());
        for (const auto& s : sites)
            m_callSites.push_back({static_cast<uint32_t>(s.from), static_cast<uint32_t>(s.to)});
    }

    [[nodiscard]] static const Range* rangeOf(const std::vector<Range>& a_r, uint64_t a_rva)
    {
        for (const auto& r : a_r)
            if (a_rva >= r.begin && a_rva < r.end)
                return &r;
        return nullptr;
    }

    void computeSizes()
    {
        auto& fs = m_out.functions;
        for (size_t i = 0; i < fs.size(); ++i)
        {
            const Range* r = rangeOf(m_code, fs[i].rva);
            if (!r)
                continue;
            uint64_t end = r->begin + r->sec->size();
            if (i + 1 < fs.size() && fs[i + 1].rva < end)
                end = fs[i + 1].rva;

            // Trim trailing 0xCC padding.
            const BinaryView v = r->sec->view();
            uint64_t off = end - r->begin;
            const uint64_t start = fs[i].rva - r->begin;
            while (off > start && off - 1 < v.size() && v[static_cast<size_t>(off - 1)] == 0xCC)
                --off;
            fs[i].size = static_cast<uint32_t>(off > start ? off - start : 0);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Strings
    // ─────────────────────────────────────────────────────────────────────

    static bool isPrintableAscii(uint8_t c)
    {
        return (c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\r' || c == '\n';
    }

    void collectStrings()
    {
        constexpr size_t kMinLen = 4;

        for (const auto& dr : m_data)
        {
            const BinaryView v = dr.sec->view();
            const uint64_t va0 = dr.sec->virtualAddress();

            // ASCII
            size_t i = 0;
            while (i < v.size())
            {
                if (!isPrintableAscii(v[i]))
                {
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j < v.size() && isPrintableAscii(v[j]))
                    ++j;
                const bool terminated = j < v.size() && v[j] == 0;
                if (terminated && j - i >= kMinLen)
                {
                    ExeString s;
                    s.rva = va0 + i;
                    s.text.assign(reinterpret_cast<const char*>(&v[i]), j - i);
                    s.byteLength = static_cast<uint32_t>(j - i + 1);
                    m_out.strings.push_back(std::move(s));
                }
                i = j + 1;
            }

            // UTF-16LE (2-byte aligned)
            i = 0;
            while (i + 1 < v.size())
            {
                if (!(isPrintableAscii(v[i]) && v[i + 1] == 0))
                {
                    i += 2;
                    continue;
                }
                size_t j = i;
                std::string text;
                while (j + 1 < v.size() && isPrintableAscii(v[j]) && v[j + 1] == 0)
                {
                    text.push_back(static_cast<char>(v[j]));
                    j += 2;
                }
                const bool terminated = j + 1 < v.size() && v[j] == 0 && v[j + 1] == 0;
                if (terminated && text.size() >= kMinLen)
                {
                    ExeString s;
                    s.rva = va0 + i;
                    s.text = std::move(text);
                    s.wide = true;
                    s.byteLength = static_cast<uint32_t>(j - i + 2);
                    m_out.strings.push_back(std::move(s));
                }
                i = j + 2;
            }
        }

        std::sort(m_out.strings.begin(),
            m_out.strings.end(),
            [](const ExeString& a, const ExeString& b) { return a.rva < b.rva; });
    }

    /// Index of the string containing a_rva (suffix-merged strings included), or -1.
    [[nodiscard]] int64_t stringIndexAt(uint64_t a_rva) const
    {
        const auto& ss = m_out.strings;
        auto it = std::upper_bound(ss.begin(),
            ss.end(),
            a_rva,
            [](uint64_t v, const ExeString& s) { return v < s.rva; });
        if (it == ss.begin())
            return -1;
        --it;
        if (a_rva < it->rva + it->byteLength)
            return it - ss.begin();
        return -1;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Classification of every reference
    // ─────────────────────────────────────────────────────────────────────

    static void addUnique(std::vector<uint32_t>& a_v, uint32_t a_value, size_t a_cap)
    {
        if (a_v.size() >= a_cap)
            return;
        if (std::find(a_v.begin(), a_v.end(), a_value) == a_v.end())
            a_v.push_back(a_value);
    }

    void classify()
    {
        // IAT slot -> import index
        std::unordered_map<uint64_t, uint32_t> iat;
        for (const auto& e : m_out.pe.imports)
        {
            ExeImportUse u;
            u.dll = e.dll;
            u.name = e.byOrdinal ? "#" + std::to_string(e.ordinal) : e.name;
            u.iatRva = e.iatRva;
            u.delayLoaded = e.delayLoaded;
            iat[e.iatRva] = static_cast<uint32_t>(m_out.imports.size());
            m_out.imports.push_back(std::move(u));
        }

        for (const auto& t : m_reader.typeDescriptors())
            m_tdRvas.insert(t.rva);

        // vftable start -> index in reader.vftables()
        std::unordered_map<uint64_t, size_t> vftByRva;
        for (size_t i = 0; i < m_reader.vftables().size(); ++i)
            vftByRva.emplace(m_reader.vftables()[i].vftableRva, i);

        // Global candidates: target RVA -> accumulating record.
        std::map<uint64_t, ExeGlobal> globals;

        constexpr size_t kCap = 24;

        for (const auto& r : m_refsBySite)
        {
            const bool siteInCode = inRanges(m_code, r.site);
            const int64_t fn = siteInCode ? m_out.functionIndexAt(r.site) : -1;

            // IAT call/load
            const auto ii = iat.find(r.target);
            if (ii != iat.end())
            {
                ++m_out.imports[ii->second].refCount;
                if (fn >= 0)
                {
                    auto& f = m_out.functions[static_cast<size_t>(fn)];
                    ++f.importRefCount;
                    addUnique(f.imports, ii->second, kCap);
                }
                continue;
            }

            // String reference
            const int64_t si = stringIndexAt(r.target);
            if (si >= 0)
            {
                auto& s = m_out.strings[static_cast<size_t>(si)];
                ++s.refCount;
                if (fn >= 0)
                {
                    addUnique(s.funcs, static_cast<uint32_t>(fn), kCap);
                    auto& f = m_out.functions[static_cast<size_t>(fn)];
                    ++f.stringRefCount;
                    addUnique(f.strings, static_cast<uint32_t>(si), kCap);
                }
                else
                {
                    s.dataRef = true;
                }
                continue;
            }

            // vftable address written/loaded by code => constructor / destructor candidate
            const auto vi = vftByRva.find(r.target);
            if (vi != vftByRva.end())
            {
                if (fn >= 0)
                    m_fnWrites[static_cast<uint32_t>(fn)].push_back(
                        m_reader.vftables()[vi->second].typeDescriptorRva);
                else if (inRanges(m_writable, r.site) && !m_tdRvas.count(r.site))
                    m_staticObjects.push_back({r.site, r.target}); // not a TypeDescriptor
                continue;
            }

            // Function address stored somewhere (callback, push offset)
            const int64_t target = m_out.functionIndexStartingAt(r.target);
            if (target >= 0)
            {
                ++m_out.functions[static_cast<size_t>(target)].dataRefs;
                continue;
            }

            // RTTI TypeDescriptors live in .data but are metadata, not program globals.
            if (m_tdRvas.count(r.target))
                continue;

            // Writable data: a global/static object or field
            if (inRanges(m_writable, r.target))
            {
                auto& g = globals[r.target];
                g.rva = r.target;
                ++g.refCount;
                if (fn >= 0)
                    addUnique(g.funcs, static_cast<uint32_t>(fn), kCap);
            }
        }

        // Static objects (vptr stored in a data word) are globals too.
        for (const auto& so : m_staticObjects)
        {
            auto& g = globals[so.site];
            g.rva = so.site;
            const auto& v = m_reader.vftables()[vftByRva[so.target]];
            g.kind = "object";
            g.className = v.fullName;
        }

        // Describe + size every global.
        std::vector<uint64_t> keys;
        keys.reserve(globals.size());
        for (const auto& kv : globals)
            keys.push_back(kv.first);

        for (size_t k = 0; k < keys.size(); ++k)
        {
            ExeGlobal& g = globals[keys[k]];
            const Range* rr = rangeOf(m_writable, g.rva);
            if (rr)
            {
                g.section = rr->sec->name();
                g.isBss = (g.rva - rr->begin) >= rr->sec->size();
                const uint64_t secEnd = rr->end;
                uint64_t next = (k + 1 < keys.size()) ? keys[k + 1] : secEnd;
                next = std::min(next, secEnd);
                const uint64_t gap = next - g.rva;
                g.maxSize = gap <= 0x1000 ? static_cast<uint32_t>(gap) : 0;
            }

            if (g.kind.empty())
                describeGlobal(g);
            m_out.globals.push_back(std::move(g));
        }
    }

    /// Fill kind/pointsTo from the initial (file-backed) contents of a global.
    void describeGlobal(ExeGlobal& a_g)
    {
        a_g.kind = "data";
        if (a_g.isBss)
            return;

        // Only a relocated word is a pointer; an arbitrary integer is just data.
        if (!m_reader.relocations().contains(a_g.rva))
            return;

        uint64_t va = 0;
        if (!m_pe.readPointer(a_g.rva, va) || va < m_imageBase)
            return;
        const uint64_t rva = va - m_imageBase;
        a_g.pointsTo = rva;

        if (inRanges(m_code, rva))
            a_g.kind = "function-pointer";
        else if (stringIndexAt(rva) >= 0)
            a_g.kind = "string-pointer";
        else
            a_g.kind = "pointer";
    }

    // ─────────────────────────────────────────────────────────────────────
    // Classes: constructors, destructors, ownership of virtual functions
    // ─────────────────────────────────────────────────────────────────────

    /// True for MSVC's "scalar deleting destructor" thunk: it tests bit 0 of its
    /// flag argument (`delete this` or not). Different optimisation levels emit
    /// different encodings, so several are accepted.
    [[nodiscard]] bool looksLikeDeletingDtor(uint64_t a_rva) const
    {
        const BinaryView c = m_pe.bytesAtRva(a_rva, 96);
        if (c.size() < 8)
            return false;

        for (size_t i = 0; i + 6 <= c.size(); ++i)
        {
            // test byte ptr [esp+N], 1  — N grows with the registers the thunk
            // saved first (push edi -> 0Ch, push esi/edi -> 10h ...).
            if (c[i] == 0xF6 && c[i + 1] == 0x44 && c[i + 2] == 0x24 && c[i + 4] == 0x01
                && c[i + 3] >= 0x04 && c[i + 3] <= 0x1C && (c[i + 3] & 3) == 0)
                return true;
            // test byte ptr [ebp+8], 1
            if (c[i] == 0xF6 && c[i + 1] == 0x45 && c[i + 2] == 0x08 && c[i + 3] == 0x01)
                return true;
            // test cl/dl/bl, 1 (flag was moved into a byte register first)
            if (c[i] == 0xF6 && (c[i + 1] == 0xC1 || c[i + 1] == 0xC2 || c[i + 1] == 0xC3)
                && c[i + 2] == 0x01)
                return true;
            // mov eax,[ebp+8] / mov eax,[esp+N] ; and eax,1
            if (c[i] == 0x8B && ((c[i + 1] == 0x45 && c[i + 2] == 0x08)
                                 || (c[i + 1] == 0x44 && c[i + 2] == 0x24)))
            {
                const size_t k = i + (c[i + 1] == 0x45 ? 3 : 4);
                if (k + 3 <= c.size() && c[k] == 0x83 && c[k + 1] == 0xE0 && c[k + 2] == 0x01)
                    return true;
            }
            // mov al,[esp+N] ; test al,1
            if (c[i] == 0x8A && c[i + 1] == 0x44 && c[i + 2] == 0x24 && c[i + 4] == 0xA8
                && c[i + 5] == 0x01)
                return true;
            // x64: test dl,1 / test sil,1
            if (m_pe.is64Bit() && ((c[i] == 0xF6 && c[i + 1] == 0xC2 && c[i + 2] == 0x01)
                                   || (c[i] == 0x40 && c[i + 1] == 0xF6 && c[i + 2] == 0xC6 && c[i + 3] == 0x01)))
                return true;
        }
        return false;
    }

    /// Direct call targets (E8) found inside a function body, in order.
    [[nodiscard]] std::vector<uint64_t> callTargetsIn(const ExeFunction& a_f) const
    {
        std::vector<uint64_t> out;
        const BinaryView c = m_pe.bytesAtRva(a_f.rva, std::min<uint32_t>(a_f.size, 512));
        for (size_t i = 0; i + 5 <= c.size(); ++i)
        {
            if (c[i] != 0xE8)
                continue;
            int32_t rel = 0;
            std::memcpy(&rel, &c[i + 1], 4);
            const int64_t tgt = static_cast<int64_t>(a_f.rva + i + 5) + rel;
            if (tgt > 0)
                out.push_back(static_cast<uint64_t>(tgt));
        }
        return out;
    }

    void buildClasses()
    {
        // Group vftables by class identity.
        std::map<uint64_t, std::vector<const VftableInfo*>> byTd;
        for (const auto& v : m_reader.vftables())
            byTd[v.typeDescriptorRva].push_back(&v);

        // Base-class sets for "who owns this virtual function".
        for (auto& [td, vs] : byTd)
        {
            std::unordered_set<uint64_t> bases;
            for (const auto* v : vs)
                for (const auto& b : v->bases)
                    if (b.typeDescriptorRva != td)
                        bases.insert(b.typeDescriptorRva);
            m_baseSet[td] = std::move(bases);
        }

        for (auto& [td, vs] : byTd)
        {
            std::sort(vs.begin(),
                vs.end(),
                [](const VftableInfo* a, const VftableInfo* b)
                { return a->objectOffset < b->objectOffset; });

            const VftableInfo* primary = vs.front();
            ExeClass c;
            c.name = primary->fullName;
            c.typeDescRva = td;
            c.primaryVftableRva = primary->vftableRva;
            c.vftableCount = static_cast<uint32_t>(vs.size());
            c.slotCount = static_cast<uint32_t>(primary->vfuncs.size());
            for (const auto& b : primary->bases)
                if (b.isDirect && b.typeDescriptorRva != td)
                    c.directBases.push_back(b.demangledName);
            m_out.classes.push_back(std::move(c));
            m_classIndexByTd[td] = m_out.classes.size() - 1;
        }

        // Ownership of every virtual function target.
        //
        // For each class and slot, walk up the hierarchy to the most basic class
        // whose own vftable holds the same function in the same slot — that is
        // where the function was introduced ("root"). A function shared through
        // inheritance is therefore named after its root, even when identical
        // code (ICF) also appears in unrelated classes.
        std::unordered_map<uint64_t, const VftableInfo*> primaryOf;
        for (auto& [td, vs] : byTd)
            primaryOf[td] = vs.front();

        struct RootVote
        {
            const VftableInfo* vftable; ///< vftable of the root where the function sits
            uint32_t slot;
            uint32_t votes;
            size_t depth; ///< number of bases of the root (smaller = more basic)
        };
        std::unordered_map<uint64_t, std::map<uint64_t, RootVote>> votes; // target -> root td -> vote

        for (const auto& v : m_reader.vftables())
        {
            for (const auto& sl : v.vfuncs)
            {
                const VftableInfo* rootVft = &v;
                uint64_t rootTd = v.typeDescriptorRva;
                size_t rootDepth = v.bases.size();

                for (const auto& b : v.bases)
                {
                    if (b.typeDescriptorRva == v.typeDescriptorRva)
                        continue;
                    // The base subobject that this vftable belongs to.
                    if (b.offset != static_cast<int32_t>(v.objectOffset))
                        continue;
                    const auto pi = primaryOf.find(b.typeDescriptorRva);
                    if (pi == primaryOf.end())
                        continue;
                    const VftableInfo* bp = pi->second;
                    if (sl.slotIndex >= bp->vfuncs.size()
                        || bp->vfuncs[sl.slotIndex].funcRva != sl.funcRva)
                        continue;
                    if (bp->bases.size() < rootDepth)
                    {
                        rootVft = bp;
                        rootTd = b.typeDescriptorRva;
                        rootDepth = bp->bases.size();
                    }
                }

                auto& rv = votes[sl.funcRva][rootTd];
                if (rv.votes == 0)
                {
                    rv.vftable = rootVft;
                    rv.slot = sl.slotIndex;
                    rv.depth = rootDepth;
                }
                ++rv.votes;
            }
        }

        for (const auto& [target, roots] : votes)
        {
            const RootVote* best = nullptr;
            uint64_t bestTd = 0;
            for (const auto& [td, rv] : roots)
            {
                if (!best || rv.votes > best->votes
                    || (rv.votes == best->votes && rv.depth < best->depth))
                {
                    best = &rv;
                    bestTd = td;
                }
            }

            OwnerInfo oi;
            oi.shareCount = static_cast<uint32_t>(roots.size());
            // Several unrelated roots, none of which stands out: identical code
            // folded by the linker (or a trivial stub) — do not guess an owner.
            oi.shared = roots.size() > 1 && best->votes < 2;
            if (!oi.shared)
            {
                oi.td = bestTd;
                oi.vftable = best->vftable;
                oi.slot = best->slot;
            }
            m_owner[target] = oi;
        }

        // A function that stores several classes' vptrs is the constructor of the
        // MOST DERIVED one (its inlined base constructors store the base vptrs
        // first and are overwritten afterwards). Unrelated combinations are
        // ambiguous and attributed to nobody.
        std::unordered_map<uint64_t, std::vector<uint32_t>> ctorsOfClass;
        for (auto& [fn, tds] : m_fnWrites)
        {
            std::sort(tds.begin(), tds.end());
            tds.erase(std::unique(tds.begin(), tds.end()), tds.end());

            uint64_t derived = 0;
            for (uint64_t cand : tds)
            {
                bool all = true;
                for (uint64_t other : tds)
                    if (other != cand && !m_baseSet[cand].count(other))
                    {
                        all = false;
                        break;
                    }
                if (all)
                {
                    derived = cand;
                    break;
                }
            }
            if (derived)
                ctorsOfClass[derived].push_back(fn);
        }

        // Constructors / destructors from the functions that write each vptr.
        std::unordered_set<uint64_t> slotTargets;
        for (const auto& kv : m_owner)
            slotTargets.insert(kv.first);

        for (auto& cls : m_out.classes)
        {
            auto& fs = m_out.functions;

            // Deleting destructor: a slot-0 target of the primary vftable that tests bit 0.
            const VftableInfo* pv = m_reader.findByVftableRva(cls.primaryVftableRva);
            if (pv && !pv->vfuncs.empty())
            {
                ++m_out.stats.deletingDtorCandidates;
                const uint64_t t = pv->vfuncs[0].funcRva;
                if (looksLikeDeletingDtor(t))
                {
                    const int64_t fi = m_out.functionIndexStartingAt(t);
                    if (fi >= 0)
                    {
                        cls.deletingDtorFunc = static_cast<int32_t>(fi);
                        ++m_out.stats.deletingDtors;
                    }
                }
            }

            const auto wi = ctorsOfClass.find(cls.typeDescRva);
            std::vector<uint32_t> writers;
            if (wi != ctorsOfClass.end())
            {
                writers = wi->second;
                std::sort(writers.begin(), writers.end());
            }

            // The destructor is the vptr-writing function the deleting dtor calls.
            if (cls.deletingDtorFunc >= 0)
            {
                const auto calls = callTargetsIn(fs[static_cast<size_t>(cls.deletingDtorFunc)]);
                for (uint64_t ct : calls)
                {
                    const int64_t ci = m_out.functionIndexStartingAt(ct);
                    if (ci >= 0 && std::find(writers.begin(), writers.end(), static_cast<uint32_t>(ci))
                                       != writers.end())
                    {
                        cls.dtorFunc = static_cast<int32_t>(ci);
                        break;
                    }
                }
            }

            for (uint32_t w : writers)
            {
                if (static_cast<int32_t>(w) == cls.dtorFunc
                    || static_cast<int32_t>(w) == cls.deletingDtorFunc)
                    continue;
                // Virtual functions that merely re-write the vptr are not constructors.
                if (slotTargets.count(fs[w].rva))
                    continue;
                cls.ctorFuncs.push_back(w);
            }
            cls.constructorCount = static_cast<uint32_t>(cls.ctorFuncs.size());
            if (!cls.ctorFuncs.empty())
                ++m_out.stats.classesWithCtor;
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Naming
    // ─────────────────────────────────────────────────────────────────────

    struct OwnerInfo
    {
        uint64_t td = 0;
        const VftableInfo* vftable = nullptr;
        uint32_t slot = 0;
        bool shared = false;
        uint32_t shareCount = 0;
    };

    void setName(ExeFunction& a_f, std::string a_name, NameConfidence a_conf, const std::string& a_owner)
    {
        if (static_cast<uint8_t>(a_conf) < static_cast<uint8_t>(a_f.confidence))
            return;
        a_f.name = std::move(a_name);
        a_f.confidence = a_conf;
        a_f.ownerClass = a_owner;
    }

    void nameEverything()
    {
        auto& fs = m_out.functions;

        // Entry point / exports / TLS first (lowest priority information).
        for (auto& f : fs)
        {
            if (f.origin & FO_EntryPoint)
                setName(f, "entry_point", NameConfidence::High, {});
            if (f.origin & FO_Tls)
                setName(f, "tls_callback_" + hex(f.rva), NameConfidence::High, {});
        }
        for (const auto& e : m_out.pe.exports)
        {
            const int64_t i = m_out.functionIndexStartingAt(e.rva);
            if (i >= 0 && !e.name.empty())
                setName(fs[static_cast<size_t>(i)], e.name, NameConfidence::High, {});
        }

        // Virtual functions.
        for (const auto& [target, oi] : m_owner)
        {
            const int64_t i = m_out.functionIndexStartingAt(target);
            if (i < 0)
                continue;
            ExeFunction& f = fs[static_cast<size_t>(i)];

            if (oi.shared)
            {
                setName(f, "shared_vfunc_" + hex(target), NameConfidence::Low, {});
                continue;
            }

            const auto ci = m_classIndexByTd.find(oi.td);
            if (ci == m_classIndexByTd.end())
                continue;
            const ExeClass& cls = m_out.classes[ci->second];

            std::string fname;
            if (oi.slot == 0 && oi.vftable && oi.vftable->objectOffset == 0
                && cls.deletingDtorFunc == i)
                fname = cls.name + "::scalar_deleting_dtor";
            else if (oi.vftable && oi.vftable->objectOffset != 0)
                fname = cls.name + "::vfunc_" + hex(oi.vftable->objectOffset) + "_"
                        + std::to_string(oi.slot);
            else
                fname = cls.name + "::vfunc_" + std::to_string(oi.slot);

            setName(f, fname, NameConfidence::Medium, cls.name);
        }

        // Destructors and constructors from vptr writers.
        for (auto& cls : m_out.classes)
        {
            if (cls.dtorFunc >= 0)
                setName(fs[static_cast<size_t>(cls.dtorFunc)], cls.name + "::dtor", NameConfidence::Medium, cls.name);

            for (size_t k = 0; k < cls.ctorFuncs.size(); ++k)
            {
                std::string n = cls.name + "::ctor";
                if (cls.ctorFuncs.size() > 1)
                    n += "_" + std::to_string(k);
                setName(fs[cls.ctorFuncs[k]], n, NameConfidence::Low, cls.name);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Source files
    // ─────────────────────────────────────────────────────────────────────

    void assignSources()
    {
        auto& fs = m_out.functions;

        // Source-path strings.
        std::map<std::string, ExeSourceFile> files; // key: lower-case path
        for (size_t si = 0; si < m_out.strings.size(); ++si)
        {
            auto& s = m_out.strings[si];
            s.sourcePath = extractSourcePath(s.text);
            if (s.sourcePath.empty())
                continue;

            std::string key;
            for (char c : s.sourcePath)
                key.push_back(c == '/' ? '\\' : static_cast<char>(tolower(static_cast<unsigned char>(c))));
            auto& sf = files[key];
            if (sf.path.empty())
                sf.path = s.sourcePath;
            if (std::find(sf.spellings.begin(), sf.spellings.end(), s.sourcePath)
                == sf.spellings.end())
                sf.spellings.push_back(s.sourcePath);
            sf.refCount += s.refCount;
            for (uint32_t fn : s.funcs)
                addUnique(sf.funcs, fn, 1u << 20);
        }

        // Each function's source = its most referenced source path.
        for (auto& f : fs)
        {
            std::map<std::string, uint32_t> score;
            for (uint32_t si : f.strings)
            {
                const auto& s = m_out.strings[si];
                if (!s.sourcePath.empty())
                    ++score[s.sourcePath];
            }
            uint32_t best = 0;
            for (const auto& [p, n] : score)
                if (n > best)
                {
                    best = n;
                    f.sourceFile = p;
                }
        }

        // Class -> source: from every function believed to belong to the class.
        //
        // Headers referenced by many classes (Singleton.h, Array.h ...) come from
        // inlined template code, not from where the class lives, so they are
        // down-weighted; .cpp/.c files are what a class is actually implemented in.
        std::unordered_map<std::string, std::map<std::string, uint32_t>> classScore;
        std::unordered_map<std::string, std::set<std::string>> classesPerPath;
        for (const auto& f : fs)
        {
            if (f.ownerClass.empty() || f.sourceFile.empty())
                continue;
            ++classScore[f.ownerClass][f.sourceFile];
            classesPerPath[f.sourceFile].insert(f.ownerClass);
        }

        auto isImplementationFile = [](const std::string& p)
        {
            std::string l;
            for (char c : p)
                l.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            auto ends = [&](const char* e)
            {
                const size_t n = std::strlen(e);
                return l.size() >= n && l.compare(l.size() - n, n, e) == 0;
            };
            return ends(".cpp") || ends(".cc") || ends(".cxx") || ends(".c");
        };

        for (auto& cls : m_out.classes)
        {
            const auto it = classScore.find(cls.name);
            if (it == classScore.end())
                continue;

            std::vector<std::pair<std::string, uint32_t>> ranked;
            for (const auto& [p, n] : it->second)
            {
                uint32_t score = n * 100;
                if (isImplementationFile(p))
                    score *= 4;
                const size_t shared = classesPerPath[p].size();
                if (shared > 12)
                    score = score / static_cast<uint32_t>(shared);
                ranked.push_back({p, score});
            }
            std::sort(ranked.begin(),
                ranked.end(),
                [](const auto& a, const auto& b)
                { return a.second != b.second ? a.second > b.second : a.first < b.first; });
            cls.sources = std::move(ranked);
            ++m_out.stats.classesWithSource;
        }

        for (auto& [k, sf] : files)
        {
            std::set<std::string> cls;
            for (uint32_t fi : sf.funcs)
                if (fi < fs.size() && !fs[fi].ownerClass.empty())
                    cls.insert(fs[fi].ownerClass);
            sf.classes.assign(cls.begin(), cls.end());
            m_out.sourceFiles.push_back(std::move(sf));
        }
        std::sort(m_out.sourceFiles.begin(),
            m_out.sourceFiles.end(),
            [](const ExeSourceFile& a, const ExeSourceFile& b) { return a.path < b.path; });
    }

    void finalize()
    {
        auto& st = m_out.stats;
        st.functions = m_out.functions.size();
        for (const auto& f : m_out.functions)
            if (!f.name.empty())
                ++st.functionsWithName;
        st.strings = m_out.strings.size();
        for (const auto& s : m_out.strings)
            if (s.refCount)
                ++st.referencedStrings;
        st.sourcePaths = m_out.sourceFiles.size();
        st.globals = m_out.globals.size();
    }

    [[nodiscard]] static std::string hex(uint64_t v)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
        return buf;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Members
    // ─────────────────────────────────────────────────────────────────────

    const RttiReader& m_reader;
    const PEImage& m_pe;
    ExeAnalysis m_out;

    uint64_t m_imageBase = 0;
    uint32_t m_ptrSize = 4;

    std::vector<Range> m_code;
    std::vector<Range> m_data;
    std::vector<Range> m_writable;

    std::vector<Ref> m_refsBySite;
    std::vector<Ref> m_refsByTarget;
    std::map<uint64_t, uint32_t> m_starts;
    std::vector<Ref> m_callSites;

    struct StaticObject
    {
        uint64_t site;
        uint64_t target;
    };
    std::vector<StaticObject> m_staticObjects;
    /// function index -> type descriptors of the vftables whose address it stores
    std::unordered_map<uint32_t, std::vector<uint64_t>> m_fnWrites;
    std::unordered_set<uint64_t> m_tdRvas;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_baseSet;
    std::unordered_map<uint64_t, size_t> m_classIndexByTd;
    std::unordered_map<uint64_t, OwnerInfo> m_owner;
};

} // namespace DumpPDB
