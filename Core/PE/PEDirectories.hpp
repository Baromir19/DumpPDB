#pragma once

#include <Core/PE/PEImage.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace DumpPDB
{

// ─────────────────────────────────────────────────────────────────────────────
// Result structures
// ─────────────────────────────────────────────────────────────────────────────

struct PEImportEntry
{
    std::string dll;       ///< DLL name as written in the descriptor.
    std::string name;      ///< Function name (empty for ordinal imports).
    uint16_t ordinal = 0;  ///< Valid when byOrdinal.
    bool byOrdinal = false;
    bool delayLoaded = false;
    uint16_t hint = 0;
    uint64_t iatRva = 0;   ///< RVA of the IAT slot that receives the address.
};

struct PEExportEntry
{
    std::string name;       ///< Empty for ordinal-only exports.
    uint32_t ordinal = 0;
    uint64_t rva = 0;
    std::string forwarder;  ///< "DLL.Func" when the export is forwarded.
};

struct PEDebugEntry
{
    uint32_t type = 0;
    uint32_t size = 0;
    uint32_t timeDateStamp = 0;
    uint32_t rawOffset = 0;
    uint32_t rva = 0;
    std::string typeName;
};

struct PECodeViewInfo
{
    bool valid = false;
    std::string pdbPath;
    std::string guid;   ///< "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
    std::string guidNoDashes; ///< As used in symbol-server paths (GUID + age).
    uint32_t age = 0;
};

struct PETlsInfo
{
    bool present = false;
    uint64_t startRawData = 0;  ///< VA
    uint64_t endRawData = 0;    ///< VA
    uint64_t indexVa = 0;
    uint64_t callbacksVa = 0;
    std::vector<uint64_t> callbackRvas;
};

struct PELoadConfigInfo
{
    bool present = false;
    uint32_t size = 0;
    uint64_t securityCookieVa = 0;
    uint64_t seHandlerTableVa = 0;
    uint32_t seHandlerCount = 0;
    uint64_t guardCfCheckFunctionVa = 0;
    uint64_t guardCfFunctionTableVa = 0;
    uint32_t guardCfFunctionCount = 0;
    uint32_t guardFlags = 0;
    std::vector<uint64_t> seHandlerRvas; ///< SafeSEH handlers — all of them are functions.
    std::vector<uint64_t> guardCfFunctionRvas; ///< CFG targets — all of them are functions.
};

struct PEResourceEntry
{
    std::string type;  ///< "RT_ICON", "RT_VERSION", "#123", or a name.
    std::string name;  ///< "#1" or a name.
    uint32_t language = 0;
    uint32_t rva = 0;
    uint32_t size = 0;
};

struct PEVersionInfo
{
    bool present = false;
    uint16_t fileVersion[4] = {};
    uint16_t productVersion[4] = {};
    std::vector<std::pair<std::string, std::string>> strings; ///< CompanyName, FileVersion, ...
};

struct PESectionStats
{
    std::string name;
    uint32_t virtualAddress = 0;
    uint32_t virtualSize = 0;
    uint32_t rawOffset = 0;
    uint32_t rawSize = 0;
    uint32_t characteristics = 0;
    double entropy = 0.0;      ///< Shannon entropy of the raw bytes, 0..8.
    bool standardName = false; ///< Name produced by MSVC/LINK ( .text, .rdata, ... ).
};

/// Everything beyond the section list that a PE header can tell about an image.
struct PEExtendedInfo
{
    std::vector<PESectionStats> sections;
    std::vector<PEImportEntry> imports;
    std::vector<PEExportEntry> exports;
    std::string exportDllName;
    std::vector<PEDebugEntry> debugEntries;
    PECodeViewInfo codeView;
    PETlsInfo tls;
    PELoadConfigInfo loadConfig;
    std::vector<PEResourceEntry> resources;
    PEVersionInfo version;
    std::string manifest;
    bool hasAuthenticode = false;
    uint32_t authenticodeSize = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Parser
// ─────────────────────────────────────────────────────────────────────────────

class PEDirectories
{
public:

    /// Parse all directories of a_pe. Never throws; missing/corrupt directories
    /// simply leave the corresponding part of the result empty.
    [[nodiscard]] static PEExtendedInfo parse(const PEImage& a_pe)
    {
        PEExtendedInfo info;
        parseSections(a_pe, info);
        parseImports(a_pe, info);
        parseDelayImports(a_pe, info);
        parseExports(a_pe, info);
        parseDebug(a_pe, info);
        parseTls(a_pe, info);
        parseLoadConfig(a_pe, info);
        parseResources(a_pe, info);

        const auto& sec = a_pe.directory(PE_DIR_SECURITY);
        if (sec.rva != 0 && sec.size != 0)
        {
            info.hasAuthenticode = true;
            info.authenticodeSize = sec.size;
        }
        return info;
    }

    // ── Names ─────────────────────────────────────────────────────────────

    [[nodiscard]] static const char* machineName(uint16_t a_machine)
    {
        switch (a_machine)
        {
        case 0x014C:
            return "x86 (i386)";
        case 0x8664:
            return "x64 (AMD64)";
        case 0x01C0:
            return "ARM";
        case 0xAA64:
            return "ARM64";
        case 0x0200:
            return "IA-64";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] static const char* subsystemName(uint16_t a_subsystem)
    {
        switch (a_subsystem)
        {
        case 1:
            return "Native";
        case 2:
            return "Windows GUI";
        case 3:
            return "Windows CUI (console)";
        case 9:
            return "Windows CE GUI";
        case 10:
            return "EFI application";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] static const char* debugTypeName(uint32_t a_type)
    {
        switch (a_type)
        {
        case 1:
            return "COFF";
        case 2:
            return "CODEVIEW";
        case 3:
            return "FPO";
        case 4:
            return "MISC";
        case 5:
            return "EXCEPTION";
        case 6:
            return "FIXUP";
        case 9:
            return "BORLAND";
        case 10:
            return "RESERVED10";
        case 11:
            return "CLSID";
        case 12:
            return "VC_FEATURE";
        case 13:
            return "POGO";
        case 14:
            return "ILTCG";
        case 15:
            return "MPX";
        case 16:
            return "REPRO";
        case 20:
            return "EX_DLLCHARACTERISTICS";
        default:
            return "other";
        }
    }

    /// Best-effort MSVC toolchain name from a Rich-header build number. Build
    /// numbers overlap between products, so this is a hint, never a certainty.
    [[nodiscard]] static std::string toolchainGuess(uint16_t a_build)
    {
        struct Range
        {
            uint16_t lo;
            uint16_t hi;
            const char* name;
        };
        static const Range kRanges[] = {
            {9044, 9999, "VS2002 (7.0)"},
            {3052, 4035, "VS2003 (7.1)"},
            {50727, 50727, "VS2005 (8.0) / VS2012 RTM (11.0)"},
            {21022, 21022, "VS2008 (9.0)"},
            {30729, 30729, "VS2008 SP1 (9.0)"},
            {30319, 30319, "VS2010 (10.0)"},
            {40219, 40219, "VS2010 SP1 (10.0)"},
            {51025, 51025, "VS2012 (11.0)"},
            {60315, 61030, "VS2012 Update (11.0)"},
            {20617, 21005, "VS2013 (12.0)"},
            {30501, 31101, "VS2013 Update (12.0)"},
            {40629, 40629, "VS2013 Update 5 (12.0)"},
            {23026, 24245, "VS2015 (14.0)"},
            {25017, 26132, "VS2017 15.x (14.1x)"},
            {26206, 27999, "VS2017 15.x/VS2019 (14.1x/14.2x)"},
            {28000, 29999, "VS2019 (14.2x)"},
            {30000, 39999, "VS2022 (14.3x)"},
        };
        for (const auto& r : kRanges)
        {
            if (a_build >= r.lo && a_build <= r.hi)
            {
                return r.name;
            }
        }
        return {};
    }

private:

    // ── Sections ──────────────────────────────────────────────────────────

    static double entropyOf(const BinaryView& a_v)
    {
        if (a_v.empty())
        {
            return 0.0;
        }
        uint64_t counts[256] = {};
        // Sampling keeps this cheap on 20 MB code sections without changing the
        // answer meaningfully.
        const size_t step = a_v.size() > (8u << 20) ? 7 : 1;
        uint64_t total = 0;
        for (size_t i = 0; i < a_v.size(); i += step)
        {
            ++counts[a_v[i]];
            ++total;
        }
        double h = 0.0;
        for (uint64_t c : counts)
        {
            if (c == 0)
            {
                continue;
            }
            const double p = static_cast<double>(c) / static_cast<double>(total);
            h -= p * std::log2(p);
        }
        return h;
    }

    static void parseSections(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        static const char* const kStandard[] = {".text",
            ".rdata",
            ".data",
            ".bss",
            ".idata",
            ".edata",
            ".pdata",
            ".xdata",
            ".rsrc",
            ".reloc",
            ".tls",
            ".gfids",
            ".didat",
            ".CRT",
            ".00cfg",
            ".voltbl",
            ".textbss",
            ".debug"};

        for (const auto& s : a_pe.sections())
        {
            PESectionStats st;
            st.name = s.name();
            st.virtualAddress = s.virtualAddress();
            st.virtualSize = s.virtualSize();
            st.rawOffset = static_cast<uint32_t>(s.fileOffset());
            st.rawSize = static_cast<uint32_t>(s.size());
            st.characteristics = s.characteristics();
            st.entropy = entropyOf(s.view());
            for (const char* n : kStandard)
            {
                if (st.name == n)
                {
                    st.standardName = true;
                    break;
                }
            }
            a_info.sections.push_back(std::move(st));
        }
    }

    // ── Imports ───────────────────────────────────────────────────────────

    static void parseImports(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_IMPORT);
        if (dir.rva == 0)
        {
            return;
        }

        const uint32_t ptrSize = a_pe.is64Bit() ? 8u : 4u;
        const uint64_t ordinalFlag = a_pe.is64Bit() ? 0x8000000000000000ull : 0x80000000ull;

        for (uint32_t d = 0; d < 4096; ++d)
        {
            const uint64_t desc = dir.rva + static_cast<uint64_t>(d) * 20;
            uint32_t oft = 0, nameRva = 0, ft = 0;
            if (!a_pe.readU32(desc, oft) || !a_pe.readU32(desc + 12, nameRva)
                || !a_pe.readU32(desc + 16, ft))
            {
                break;
            }
            if (oft == 0 && nameRva == 0 && ft == 0)
            {
                break; // terminator
            }

            const std::string dll = a_pe.readCString(nameRva, 260);
            const uint64_t lookup = oft != 0 ? oft : ft;

            for (uint32_t i = 0; i < 65536; ++i)
            {
                uint64_t thunk = 0;
                if (!a_pe.readPointer(lookup + static_cast<uint64_t>(i) * ptrSize, thunk) || thunk == 0)
                {
                    break;
                }

                PEImportEntry e;
                e.dll = dll;
                e.iatRva = ft + static_cast<uint64_t>(i) * ptrSize;
                if (thunk & ordinalFlag)
                {
                    e.byOrdinal = true;
                    e.ordinal = static_cast<uint16_t>(thunk & 0xFFFF);
                }
                else
                {
                    const uint64_t hintRva = thunk & 0x7FFFFFFFull;
                    (void)a_pe.readU16(hintRva, e.hint);
                    e.name = a_pe.readCString(hintRva + 2, 512);
                }
                a_info.imports.push_back(std::move(e));
            }
        }
    }

    static void parseDelayImports(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_DELAY_IMPORT);
        if (dir.rva == 0)
        {
            return;
        }

        const uint32_t ptrSize = a_pe.is64Bit() ? 8u : 4u;
        const uint64_t ordinalFlag = a_pe.is64Bit() ? 0x8000000000000000ull : 0x80000000ull;

        for (uint32_t d = 0; d < 1024; ++d)
        {
            // IMAGE_DELAYLOAD_DESCRIPTOR: Attributes, DllNameRVA, ModuleHandleRVA,
            // ImportAddressTableRVA, ImportNameTableRVA, ...
            const uint64_t desc = dir.rva + static_cast<uint64_t>(d) * 32;
            uint32_t attrs = 0, nameRva = 0, iat = 0, intRva = 0;
            if (!a_pe.readU32(desc, attrs) || !a_pe.readU32(desc + 4, nameRva)
                || !a_pe.readU32(desc + 12, iat) || !a_pe.readU32(desc + 16, intRva))
            {
                break;
            }
            if (nameRva == 0 && iat == 0 && intRva == 0)
            {
                break;
            }

            // Attributes bit 0 set = RVAs (otherwise absolute VAs in old images).
            auto fix = [&](uint32_t v) -> uint64_t
            {
                if ((attrs & 1) == 0 && v >= a_pe.imageBase())
                {
                    return v - a_pe.imageBase();
                }
                return v;
            };

            const std::string dll = a_pe.readCString(fix(nameRva), 260);
            const uint64_t lookup = fix(intRva);
            const uint64_t iatBase = fix(iat);

            for (uint32_t i = 0; i < 65536; ++i)
            {
                uint64_t thunk = 0;
                if (!a_pe.readPointer(lookup + static_cast<uint64_t>(i) * ptrSize, thunk) || thunk == 0)
                {
                    break;
                }

                PEImportEntry e;
                e.dll = dll;
                e.delayLoaded = true;
                e.iatRva = iatBase + static_cast<uint64_t>(i) * ptrSize;
                if (thunk & ordinalFlag)
                {
                    e.byOrdinal = true;
                    e.ordinal = static_cast<uint16_t>(thunk & 0xFFFF);
                }
                else
                {
                    const uint64_t hintRva = fix(static_cast<uint32_t>(thunk & 0xFFFFFFFFull));
                    (void)a_pe.readU16(hintRva, e.hint);
                    e.name = a_pe.readCString(hintRva + 2, 512);
                }
                a_info.imports.push_back(std::move(e));
            }
        }
    }

    // ── Exports ───────────────────────────────────────────────────────────

    static void parseExports(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_EXPORT);
        if (dir.rva == 0)
        {
            return;
        }

        uint32_t nameRva = 0, base = 0, numFuncs = 0, numNames = 0, addrFuncs = 0, addrNames = 0,
                 addrOrdinals = 0;
        const uint64_t d = dir.rva;
        if (!a_pe.readU32(d + 12, nameRva) || !a_pe.readU32(d + 16, base)
            || !a_pe.readU32(d + 20, numFuncs) || !a_pe.readU32(d + 24, numNames)
            || !a_pe.readU32(d + 28, addrFuncs) || !a_pe.readU32(d + 32, addrNames)
            || !a_pe.readU32(d + 36, addrOrdinals))
        {
            return;
        }

        a_info.exportDllName = a_pe.readCString(nameRva, 260);
        numFuncs = std::min<uint32_t>(numFuncs, 65536);
        numNames = std::min<uint32_t>(numNames, 65536);

        std::map<uint32_t, std::string> nameByIndex;
        for (uint32_t i = 0; i < numNames; ++i)
        {
            uint32_t nrva = 0;
            uint16_t ord = 0;
            if (!a_pe.readU32(addrNames + static_cast<uint64_t>(i) * 4, nrva)
                || !a_pe.readU16(addrOrdinals + static_cast<uint64_t>(i) * 2, ord))
            {
                break;
            }
            nameByIndex[ord] = a_pe.readCString(nrva, 512);
        }

        for (uint32_t i = 0; i < numFuncs; ++i)
        {
            uint32_t frva = 0;
            if (!a_pe.readU32(addrFuncs + static_cast<uint64_t>(i) * 4, frva) || frva == 0)
            {
                continue;
            }
            PEExportEntry e;
            e.ordinal = base + i;
            e.rva = frva;
            const auto it = nameByIndex.find(i);
            if (it != nameByIndex.end())
            {
                e.name = it->second;
            }
            // A forwarder RVA points back inside the export directory.
            if (frva >= dir.rva && frva < dir.rva + dir.size)
            {
                e.forwarder = a_pe.readCString(frva, 256);
            }
            a_info.exports.push_back(std::move(e));
        }
    }

    // ── Debug directory / CodeView ────────────────────────────────────────

    static std::string hex(uint64_t v, int width)
    {
        static const char* d = "0123456789ABCDEF";
        std::string s(static_cast<size_t>(width), '0');
        for (int i = width - 1; i >= 0; --i)
        {
            s[static_cast<size_t>(i)] = d[v & 0xF];
            v >>= 4;
        }
        return s;
    }

    static void parseDebug(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_DEBUG);
        if (dir.rva == 0)
        {
            return;
        }

        const uint32_t count = std::min<uint32_t>(dir.size / 28, 64);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint64_t e = dir.rva + static_cast<uint64_t>(i) * 28;
            PEDebugEntry de;
            if (!a_pe.readU32(e + 4, de.timeDateStamp) || !a_pe.readU32(e + 12, de.type)
                || !a_pe.readU32(e + 16, de.size) || !a_pe.readU32(e + 20, de.rva)
                || !a_pe.readU32(e + 24, de.rawOffset))
            {
                break;
            }
            de.typeName = debugTypeName(de.type);
            a_info.debugEntries.push_back(de);

            if (de.type == 2 && de.rawOffset != 0) // CODEVIEW — raw file offset
            {
                parseCodeView(a_pe, de, a_info.codeView);
            }
        }
    }

    static void parseCodeView(const PEImage& a_pe, const PEDebugEntry& a_de, PECodeViewInfo& a_out)
    {
        const BinaryView file = a_pe.view();
        const size_t off = a_de.rawOffset;
        if (off + 24 > file.size() || file[off] != 'R' || file[off + 1] != 'S'
            || file[off + 2] != 'D' || file[off + 3] != 'S')
        {
            return;
        }

        uint32_t d1 = 0, age = 0;
        uint16_t d2 = 0, d3 = 0;
        (void)file.readLE<uint32_t>(off + 4, d1);
        (void)file.readLE<uint16_t>(off + 8, d2);
        (void)file.readLE<uint16_t>(off + 10, d3);
        (void)file.readLE<uint32_t>(off + 20, age);

        std::string guid = hex(d1, 8) + "-" + hex(d2, 4) + "-" + hex(d3, 4) + "-";
        std::string noDash = hex(d1, 8) + hex(d2, 4) + hex(d3, 4);
        for (int i = 0; i < 8; ++i)
        {
            const std::string b = hex(file[off + 12 + static_cast<size_t>(i)], 2);
            if (i == 2)
            {
                guid += "-";
            }
            guid += b;
            noDash += b;
        }

        std::string path;
        for (size_t i = off + 24; i < file.size() && i < off + a_de.size && file[i] != 0; ++i)
        {
            path.push_back(static_cast<char>(file[i]));
        }

        a_out.valid = true;
        a_out.pdbPath = path;
        a_out.guid = guid;
        a_out.guidNoDashes = noDash + std::to_string(age);
        a_out.age = age;
    }

    // ── TLS ───────────────────────────────────────────────────────────────

    static void parseTls(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_TLS);
        if (dir.rva == 0)
        {
            return;
        }

        PETlsInfo& t = a_info.tls;
        const uint64_t d = dir.rva;
        const uint32_t ps = a_pe.is64Bit() ? 8u : 4u;

        if (!a_pe.readPointer(d, t.startRawData) || !a_pe.readPointer(d + ps, t.endRawData)
            || !a_pe.readPointer(d + 2 * ps, t.indexVa)
            || !a_pe.readPointer(d + 3 * ps, t.callbacksVa))
        {
            return;
        }
        t.present = true;

        if (t.callbacksVa >= a_pe.imageBase())
        {
            const uint64_t arr = t.callbacksVa - a_pe.imageBase();
            for (uint32_t i = 0; i < 64; ++i)
            {
                uint64_t cb = 0;
                if (!a_pe.readPointer(arr + static_cast<uint64_t>(i) * ps, cb) || cb == 0)
                {
                    break;
                }
                if (cb >= a_pe.imageBase())
                {
                    t.callbackRvas.push_back(cb - a_pe.imageBase());
                }
            }
        }
    }

    // ── Load config ───────────────────────────────────────────────────────

    static void parseLoadConfig(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_LOAD_CONFIG);
        if (dir.rva == 0)
        {
            return;
        }

        PELoadConfigInfo& lc = a_info.loadConfig;
        const uint64_t d = dir.rva;
        if (!a_pe.readU32(d, lc.size))
        {
            return;
        }
        lc.present = true;

        const bool is64 = a_pe.is64Bit();
        const uint32_t ps = is64 ? 8u : 4u;

        // Offsets differ between PE32 and PE32+.
        const uint64_t offCookie = is64 ? 0x58 : 0x3C;
        const uint64_t offSeTable = is64 ? 0x60 : 0x40;
        const uint64_t offSeCount = is64 ? 0x68 : 0x44;
        const uint64_t offGuardCheck = is64 ? 0x70 : 0x48;
        const uint64_t offGuardTable = is64 ? 0x80 : 0x50;
        const uint64_t offGuardCount = is64 ? 0x88 : 0x54;
        const uint64_t offGuardFlags = is64 ? 0x90 : 0x58;

        if (lc.size > offCookie + ps)
        {
            (void)a_pe.readPointer(d + offCookie, lc.securityCookieVa);
        }

        if (!is64 && lc.size >= offSeCount + 4)
        {
            (void)a_pe.readPointer(d + offSeTable, lc.seHandlerTableVa);
            (void)a_pe.readU32(d + offSeCount, lc.seHandlerCount);

            if (lc.seHandlerTableVa >= a_pe.imageBase() && lc.seHandlerCount <= 1000000)
            {
                const uint64_t table = lc.seHandlerTableVa - a_pe.imageBase();
                for (uint32_t i = 0; i < lc.seHandlerCount; ++i)
                {
                    uint32_t h = 0;
                    if (!a_pe.readU32(table + static_cast<uint64_t>(i) * 4, h))
                    {
                        break;
                    }
                    lc.seHandlerRvas.push_back(h); // SafeSEH table stores RVAs
                }
            }
        }

        if (lc.size >= offGuardFlags + 4)
        {
            (void)a_pe.readPointer(d + offGuardCheck, lc.guardCfCheckFunctionVa);
            (void)a_pe.readPointer(d + offGuardTable, lc.guardCfFunctionTableVa);
            (void)a_pe.readU32(d + offGuardCount, lc.guardCfFunctionCount);
            (void)a_pe.readU32(d + offGuardFlags, lc.guardFlags);

            if (lc.guardCfFunctionTableVa >= a_pe.imageBase() && lc.guardCfFunctionCount <= 4000000)
            {
                // Entry stride = 4 bytes + (flags >> 28) extra bytes.
                const uint32_t extra = (lc.guardFlags >> 28) & 0xF;
                const uint32_t stride = 4 + extra;
                const uint64_t table = lc.guardCfFunctionTableVa - a_pe.imageBase();
                for (uint32_t i = 0; i < lc.guardCfFunctionCount; ++i)
                {
                    uint32_t h = 0;
                    if (!a_pe.readU32(table + static_cast<uint64_t>(i) * stride, h))
                    {
                        break;
                    }
                    lc.guardCfFunctionRvas.push_back(h);
                }
            }
        }
    }

    // ── Resources ─────────────────────────────────────────────────────────

    static std::string resTypeName(uint32_t a_id)
    {
        switch (a_id)
        {
        case 1:
            return "RT_CURSOR";
        case 2:
            return "RT_BITMAP";
        case 3:
            return "RT_ICON";
        case 4:
            return "RT_MENU";
        case 5:
            return "RT_DIALOG";
        case 6:
            return "RT_STRING";
        case 7:
            return "RT_FONTDIR";
        case 8:
            return "RT_FONT";
        case 9:
            return "RT_ACCELERATOR";
        case 10:
            return "RT_RCDATA";
        case 11:
            return "RT_MESSAGETABLE";
        case 12:
            return "RT_GROUP_CURSOR";
        case 14:
            return "RT_GROUP_ICON";
        case 16:
            return "RT_VERSION";
        case 17:
            return "RT_DLGINCLUDE";
        case 19:
            return "RT_PLUGPLAY";
        case 20:
            return "RT_VXD";
        case 21:
            return "RT_ANICURSOR";
        case 22:
            return "RT_ANIICON";
        case 23:
            return "RT_HTML";
        case 24:
            return "RT_MANIFEST";
        default:
            return "#" + std::to_string(a_id);
        }
    }

    static std::string resEntryName(const PEImage& a_pe, uint64_t a_base, uint32_t a_nameField)
    {
        if ((a_nameField & 0x80000000u) == 0)
        {
            return {};
        }
        const uint64_t strRva = a_base + (a_nameField & 0x7FFFFFFFu);
        uint16_t len = 0;
        if (!a_pe.readU16(strRva, len) || len > 256)
        {
            return {};
        }
        std::string out;
        for (uint16_t i = 0; i < len; ++i)
        {
            uint16_t c = 0;
            if (!a_pe.readU16(strRva + 2 + static_cast<uint64_t>(i) * 2, c))
            {
                break;
            }
            out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        return out;
    }

    static void walkResources(const PEImage& a_pe,
        uint64_t a_base,
        uint64_t a_dirRva,
        int a_level,
        std::string a_type,
        std::string a_name,
        PEExtendedInfo& a_info)
    {
        if (a_level > 2)
        {
            return;
        }
        uint16_t numNamed = 0, numId = 0;
        if (!a_pe.readU16(a_dirRva + 12, numNamed) || !a_pe.readU16(a_dirRva + 14, numId))
        {
            return;
        }
        const uint32_t total = std::min<uint32_t>(static_cast<uint32_t>(numNamed) + numId, 1024);

        for (uint32_t i = 0; i < total; ++i)
        {
            const uint64_t e = a_dirRva + 16 + static_cast<uint64_t>(i) * 8;
            uint32_t nameField = 0, offField = 0;
            if (!a_pe.readU32(e, nameField) || !a_pe.readU32(e + 4, offField))
            {
                break;
            }

            std::string label = resEntryName(a_pe, a_base, nameField);
            const uint32_t id = nameField & 0xFFFF;
            if (label.empty())
            {
                label = (a_level == 0) ? resTypeName(id) : "#" + std::to_string(id);
            }

            if (offField & 0x80000000u)
            {
                const uint64_t sub = a_base + (offField & 0x7FFFFFFFu);
                if (a_level == 0)
                {
                    walkResources(a_pe, a_base, sub, 1, label, a_name, a_info);
                }
                else if (a_level == 1)
                {
                    walkResources(a_pe, a_base, sub, 2, a_type, label, a_info);
                }
            }
            else
            {
                // Leaf: IMAGE_RESOURCE_DATA_ENTRY { OffsetToData(RVA), Size, ... }
                uint32_t dataRva = 0, size = 0;
                if (!a_pe.readU32(a_base + offField, dataRva)
                    || !a_pe.readU32(a_base + offField + 4, size))
                {
                    continue;
                }
                PEResourceEntry r;
                r.type = a_type;
                r.name = a_name;
                r.language = id;
                r.rva = dataRva;
                r.size = size;
                a_info.resources.push_back(std::move(r));
            }
        }
    }

    static std::string utf16Z(const BinaryView& a_v, size_t& a_pos)
    {
        std::string out;
        while (a_pos + 2 <= a_v.size())
        {
            uint16_t c = 0;
            (void)a_v.readLE<uint16_t>(a_pos, c);
            a_pos += 2;
            if (c == 0)
            {
                break;
            }
            // UTF-8 encode (BMP only is enough for version strings).
            if (c < 0x80)
            {
                out.push_back(static_cast<char>(c));
            }
            else if (c < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    /// Reads the "String" entries of a VS_VERSIONINFO block.
    static void parseVersionBlock(const BinaryView& a_v, PEVersionInfo& a_out)
    {
        if (a_v.size() < 0x40)
        {
            return;
        }

        // VS_FIXEDFILEINFO follows the "VS_VERSION_INFO" key (32-bit aligned).
        for (size_t p = 0; p + 52 <= a_v.size(); p += 4)
        {
            uint32_t sig = 0;
            if (a_v.readLE<uint32_t>(p, sig) && sig == 0xFEEF04BDu)
            {
                uint32_t fvMs = 0, fvLs = 0, pvMs = 0, pvLs = 0;
                (void)a_v.readLE<uint32_t>(p + 8, fvMs);
                (void)a_v.readLE<uint32_t>(p + 12, fvLs);
                (void)a_v.readLE<uint32_t>(p + 16, pvMs);
                (void)a_v.readLE<uint32_t>(p + 20, pvLs);
                a_out.fileVersion[0] = static_cast<uint16_t>(fvMs >> 16);
                a_out.fileVersion[1] = static_cast<uint16_t>(fvMs & 0xFFFF);
                a_out.fileVersion[2] = static_cast<uint16_t>(fvLs >> 16);
                a_out.fileVersion[3] = static_cast<uint16_t>(fvLs & 0xFFFF);
                a_out.productVersion[0] = static_cast<uint16_t>(pvMs >> 16);
                a_out.productVersion[1] = static_cast<uint16_t>(pvMs & 0xFFFF);
                a_out.productVersion[2] = static_cast<uint16_t>(pvLs >> 16);
                a_out.productVersion[3] = static_cast<uint16_t>(pvLs & 0xFFFF);
                break;
            }
        }

        // String table entries: wLength, wValueLength, wType(1=text), key\0, pad, value\0.
        // Walk the whole block and accept well-formed text entries.
        for (size_t p = 0; p + 8 <= a_v.size(); p += 2)
        {
            uint16_t len = 0, valLen = 0, type = 0;
            (void)a_v.readLE<uint16_t>(p, len);
            (void)a_v.readLE<uint16_t>(p + 2, valLen);
            (void)a_v.readLE<uint16_t>(p + 4, type);
            if (type != 1 || len < 8 || valLen == 0 || p + len > a_v.size())
            {
                continue;
            }

            size_t kp = p + 6;
            const std::string key = utf16Z(a_v, kp);
            if (key.empty() || key.size() > 40
                || !std::all_of(key.begin(),
                    key.end(),
                    [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }))
            {
                continue;
            }
            kp = (kp + 3) & ~static_cast<size_t>(3);
            if (kp >= p + len)
            {
                continue;
            }
            const std::string value = utf16Z(a_v, kp);
            if (!value.empty())
            {
                a_out.strings.emplace_back(key, value);
                p += (len > 2 ? len - 2 : 0); // skip over this entry
            }
        }
        a_out.present = true;
    }

    static void parseResources(const PEImage& a_pe, PEExtendedInfo& a_info)
    {
        const auto& dir = a_pe.directory(PE_DIR_RESOURCE);
        if (dir.rva == 0)
        {
            return;
        }

        walkResources(a_pe, dir.rva, dir.rva, 0, {}, {}, a_info);

        for (const auto& r : a_info.resources)
        {
            if (r.type == "RT_VERSION" && !a_info.version.present)
            {
                parseVersionBlock(a_pe.bytesAtRva(r.rva, r.size), a_info.version);
            }
            else if (r.type == "RT_MANIFEST" && a_info.manifest.empty())
            {
                const BinaryView v = a_pe.bytesAtRva(r.rva, std::min<uint32_t>(r.size, 16384));
                a_info.manifest.assign(reinterpret_cast<const char*>(v.data()), v.size());
            }
        }
    }
};

} // namespace DumpPDB
