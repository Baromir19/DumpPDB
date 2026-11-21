#pragma once

#include <Core/PE/RcExporter.hpp>
#include <Core/RTTI/ExeAnalyzer.hpp>
#include <Core/RTTI/RttiReader.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace DumpPDB
{

/// One thing worth naming in a disassembler.
struct ExeNameEntry
{
    uint64_t rva = 0;
    std::string name;     ///< Human readable ("Foo::vfunc_3").
    std::string safeName; ///< Valid identifier, unique ("Foo_vfunc_3").
    std::string kind;     ///< "function", "vftable", "rtti", "global".
    std::string comment;
    uint32_t size = 0;
    NameConfidence confidence = NameConfidence::None;
};

/// Result of ExeExporter::exportAll.
struct ExeExportResult
{
    bool ok = true;
    std::vector<std::string> files;
    std::vector<std::string> errors;
};

/// ============================================================================
/// ExeReport — renders an ExeAnalysis as text, CSV, JSON and helper scripts.
/// Pure string producers (no I/O) so they can be unit-tested; ExeExporter below
/// writes them to disk.
/// ============================================================================
class ExeReport
{
public:

    // ── Small helpers ────────────────────────────────────────────────────

    [[nodiscard]] static std::string hex(uint64_t v, int width = 0)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%0*llX", width, static_cast<unsigned long long>(v));
        return buf;
    }

    [[nodiscard]] static std::string jsonEscape(const std::string& s)
    {
        std::string o;
        o.reserve(s.size() + 8);
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':
                o += "\\\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\r':
                o += "\\r";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                }
                else
                    o.push_back(static_cast<char>(c));
            }
        }
        return o;
    }

    [[nodiscard]] static std::string csvEscape(const std::string& s)
    {
        bool need = false;
        for (char c : s)
            if (c == ',' || c == '"' || c == '\n' || c == '\r')
            {
                need = true;
                break;
            }
        if (!need)
            return s;
        std::string o = "\"";
        for (char c : s)
        {
            if (c == '"')
                o += "\"\"";
            else if (c == '\n' || c == '\r')
                o += ' ';
            else
                o.push_back(c);
        }
        o += '"';
        return o;
    }

    [[nodiscard]] static std::string timestampText(uint32_t a_ts)
    {
        const std::time_t t = static_cast<std::time_t>(a_ts);
        std::tm tmv{};
#if defined(_WIN32)
        gmtime_s(&tmv, &t);
#else
        gmtime_r(&t, &tmv);
#endif
        char buf[48];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tmv);
        return buf;
    }

    [[nodiscard]] static std::string characteristicsText(uint32_t c)
    {
        std::string s;
        auto add = [&](uint32_t bit, const char* n)
        {
            if (c & bit)
            {
                if (!s.empty())
                    s += '|';
                s += n;
            }
        };
        add(0x20000000, "X");
        add(0x40000000, "R");
        add(0x80000000u, "W");
        add(0x00000020, "CODE");
        add(0x00000040, "IDATA");
        add(0x00000080, "UDATA");
        add(0x02000000, "DISCARDABLE");
        return s;
    }

    // ── Summary ──────────────────────────────────────────────────────────

    [[nodiscard]] static std::string summary(const RttiReader& a_reader, const ExeAnalysis& a)
    {
        const PEImage& pe = a_reader.pe();
        std::ostringstream o;

        o << "==================== PE IMAGE ====================\n";
        o << "Machine         : " << PEDirectories::machineName(pe.machine()) << "\n";
        o << "Image base      : 0x" << hex(pe.imageBase()) << "\n";
        o << "Entry point     : RVA 0x" << hex(pe.entryPointRva()) << "\n";
        o << "Size of image   : 0x" << hex(pe.sizeOfImage()) << " (" << pe.sizeOfImage() << " bytes)\n";
        o << "File size       : " << pe.fileSize() << " bytes\n";
        o << "Subsystem       : " << PEDirectories::subsystemName(pe.subsystem()) << "\n";
        o << "Linker version  : " << static_cast<int>(pe.linkerMajor()) << "."
          << static_cast<int>(pe.linkerMinor()) << "\n";
        o << "Timestamp       : " << timestampText(pe.timeDateStamp()) << " (0x" << hex(pe.timeDateStamp())
          << ")\n";
        o << "Kind            : " << (pe.isDll() ? "DLL" : "EXE") << "\n";
        o << "ASLR / DEP      : " << ((pe.dllCharacteristics() & 0x40) ? "ASLR " : "no-ASLR ")
          << ((pe.dllCharacteristics() & 0x100) ? "DEP" : "no-DEP") << "\n";
        o << "Relocations     : " << a_reader.relocations().size() << " entries\n";

        if (a.pe.codeView.valid)
        {
            o << "\n-------------------- DEBUG INFO --------------------\n";
            o << "PDB path        : " << a.pe.codeView.pdbPath << "\n";
            o << "PDB GUID        : " << a.pe.codeView.guid << "\n";
            o << "PDB age         : " << a.pe.codeView.age << "\n";
            o << "Symbol server   : <name>.pdb\\" << a.pe.codeView.guidNoDashes << "\\<name>.pdb\n";
        }
        else
        {
            o << "\nNo CodeView (PDB) debug directory.\n";
        }

        if (a.pe.version.present)
        {
            o << "\n-------------------- VERSION INFO --------------------\n";
            for (const auto& [k, v] : a.pe.version.strings)
                o << k << " : " << v << "\n";
        }

        if (!pe.richHeader().empty())
        {
            o << "\n-------------------- RICH HEADER (build tools) --------------------\n";
            std::map<uint16_t, std::string> best; // build -> guess
            for (const auto& r : pe.richHeader())
            {
                o << "  product 0x" << hex(r.productId, 4) << "  build " << r.buildNumber << "  x" << r.count;
                const std::string g = PEDirectories::toolchainGuess(r.buildNumber);
                if (!g.empty())
                    o << "   " << g;
                o << "\n";
            }
        }

        o << "\n-------------------- SECTIONS --------------------\n";
        o << "name      virtual addr  virt size   raw size   entropy  flags\n";
        for (const auto& s : a.pe.sections)
        {
            char line[200];
            std::snprintf(line,
                sizeof(line),
                "%-8s  %08X      %08X    %08X   %5.2f    %s%s\n",
                s.name.c_str(),
                s.virtualAddress,
                s.virtualSize,
                s.rawSize,
                s.entropy,
                characteristicsText(s.characteristics).c_str(),
                s.standardName ? "" : "   <non-standard name>");
            o << line;
        }

        // Protection / packing indicators
        std::vector<std::string> notes;
        for (const auto& s : a.pe.sections)
        {
            if (s.entropy > 7.5 && s.rawSize > 0x1000)
                notes.push_back("section " + s.name + " has entropy "
                                + std::to_string(s.entropy).substr(0, 4)
                                + " — compressed or encrypted data");
            else if (!s.standardName)
                notes.push_back("non-standard section name " + s.name);
        }
        if (a.pe.hasAuthenticode)
            notes.push_back("Authenticode signature present (" + std::to_string(a.pe.authenticodeSize) + " bytes)");
        if (pe.overlaySize() > 0)
            notes.push_back("overlay of " + std::to_string(pe.overlaySize()) + " bytes after the last section");
        if (a.pe.tls.present)
            notes.push_back("TLS directory present (" + std::to_string(a.pe.tls.callbackRvas.size())
                            + " callbacks)");
        if (!notes.empty())
        {
            o << "\nNotes:\n";
            for (const auto& n : notes)
                o << "  * " << n << "\n";
        }

        // Imports by DLL
        std::map<std::string, size_t> perDll;
        for (const auto& i : a.pe.imports)
            ++perDll[i.dll];
        o << "\n-------------------- IMPORTS --------------------\n";
        o << a.pe.imports.size() << " imports from " << perDll.size() << " DLLs\n";
        for (const auto& [d, n] : perDll)
            o << "  " << d << " : " << n << "\n";
        if (!a.pe.exports.empty())
            o << a.pe.exports.size() << " exports\n";

        o << "\n==================== RTTI ====================\n";
        o << "vftables                : " << a_reader.vftables().size() << "\n";
        o << "classes (unique)        : " << a_reader.allTypeNames().size() << "\n";
        o << "type descriptors total  : " << a_reader.typeDescriptors().size() << "\n";
        size_t noVft = 0, tmpl = 0, anon = 0, local = 0;
        for (const auto& t : a_reader.typeDescriptors())
            noVft += t.hasVftable ? 0 : 1;
        for (const auto& v : a_reader.vftables())
        {
            tmpl += v.isTemplateInstance ? 1 : 0;
            anon += v.isInAnonymousNamespace ? 1 : 0;
            local += v.isLocalToFunction ? 1 : 0;
        }
        o << "  without a vftable     : " << noVft << " (interfaces / base-only types)\n";
        o << "  template instances    : " << tmpl << "\n";
        o << "  in anonymous namespace: " << anon << "\n";
        o << "  local to a function   : " << local << "\n";

        o << "\n==================== ANALYSIS ====================\n";
        const auto& s = a.stats;
        o << "functions found         : " << s.functions << "\n";
        o << "  named                 : " << s.functionsWithName << "\n";
        char rec[96];
        std::snprintf(rec,
            sizeof(rec),
            "%zu/%zu (%.1f%%)",
            s.vtableTargetsFound,
            s.vtableTargets,
            s.vtableRecall);
        o << "  discovery recall      : " << rec << " of vftable targets found without RTTI help\n";
        o << "direct call sites       : " << s.callSites << "\n";
        o << "pointer references      : " << s.relocRefs << "\n";
        o << "strings                 : " << s.strings << " (" << s.referencedStrings << " referenced)\n";
        o << "source paths embedded   : " << s.sourcePaths << "\n";
        o << "global/static objects   : " << s.globals << "\n";
        o << "classes with ctor       : " << s.classesWithCtor << "\n";
        o << "classes with source     : " << s.classesWithSource << "\n";
        o << "deleting destructors    : " << s.deletingDtors << "/" << s.deletingDtorCandidates << "\n";
        return o.str();
    }

    // ── Imports / exports ────────────────────────────────────────────────

    [[nodiscard]] static std::string importsText(const ExeAnalysis& a)
    {
        std::ostringstream o;
        std::string lastDll;
        for (size_t i = 0; i < a.pe.imports.size(); ++i)
        {
            const auto& e = a.pe.imports[i];
            if (e.dll != lastDll)
            {
                o << "\n[" << e.dll << "]" << (e.delayLoaded ? "  (delay-loaded)" : "") << "\n";
                lastDll = e.dll;
            }
            o << "  IAT 0x" << hex(e.iatRva, 8) << "  ";
            if (e.byOrdinal)
                o << "#" << e.ordinal;
            else
                o << e.name;
            const uint32_t uses = i < a.imports.size() ? a.imports[i].refCount : 0;
            o << "   (" << uses << " refs)\n";
        }
        if (!a.pe.exports.empty())
        {
            o << "\n[EXPORTS of " << a.pe.exportDllName << "]\n";
            for (const auto& e : a.pe.exports)
            {
                o << "  0x" << hex(e.rva, 8) << "  #" << e.ordinal << "  " << e.name;
                if (!e.forwarder.empty())
                    o << "  -> " << e.forwarder;
                o << "\n";
            }
        }
        return o.str();
    }

    // ── Classes ──────────────────────────────────────────────────────────

    [[nodiscard]] static std::string classesText(const RttiReader& a_reader, const ExeAnalysis& a)
    {
        std::ostringstream o;
        for (const auto& c : a.classes)
        {
            o << (isStruct(a_reader, c) ? "struct " : "class ") << c.name;
            bool first = true;
            for (const auto& b : c.directBases)
            {
                o << (first ? " : public " : ", public ") << b;
                first = false;
            }
            o << "\n";

            const auto vfts = a_reader.vftablesOfType(c.typeDescRva);
            for (const auto* v : vfts)
            {
                o << "    vftable RVA 0x" << hex(v->vftableRva, 8) << "  offset +0x" << hex(v->objectOffset)
                  << "  " << v->vfuncs.size() << " slots";
                if (!v->bases.empty() && v->objectOffset == 0)
                    o << "  hierarchy: " << v->bases.size() - 1 << " base(s)";
                o << "\n";
            }

            auto fnLine = [&](const char* label, int32_t idx)
            {
                if (idx < 0)
                    return;
                const auto& f = a.functions[static_cast<size_t>(idx)];
                o << "    " << label << " RVA 0x" << hex(f.rva, 8) << "  size " << f.size << "\n";
            };
            fnLine("deleting dtor", c.deletingDtorFunc);
            fnLine("dtor         ", c.dtorFunc);
            for (uint32_t ci : c.ctorFuncs)
                fnLine("ctor?        ", static_cast<int32_t>(ci));

            for (size_t i = 0; i < c.sources.size() && i < 3; ++i)
                o << "    source: " << c.sources[i].first << "\n";
            o << "\n";
        }
        return o.str();
    }

    [[nodiscard]] static std::string classesCsv(const RttiReader& a_reader, const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "class,type_descriptor_rva,primary_vftable_rva,vftable_count,slot_count,bases,"
             "ctor_rvas,dtor_rva,deleting_dtor_rva,source\n";
        for (const auto& c : a.classes)
        {
            (void)a_reader;
            std::string bases;
            for (const auto& b : c.directBases)
                bases += (bases.empty() ? "" : ";") + b;
            std::string ctors;
            for (uint32_t ci : c.ctorFuncs)
                ctors += (ctors.empty() ? "" : ";") + std::string("0x") + hex(a.functions[ci].rva, 8);
            auto rvaOf = [&](int32_t i) -> std::string
            { return i < 0 ? std::string() : "0x" + hex(a.functions[static_cast<size_t>(i)].rva, 8); };

            o << csvEscape(c.name) << ",0x" << hex(c.typeDescRva, 8) << ",0x" << hex(c.primaryVftableRva, 8)
              << "," << c.vftableCount << "," << c.slotCount << "," << csvEscape(bases) << "," << ctors
              << "," << rvaOf(c.dtorFunc) << "," << rvaOf(c.deletingDtorFunc) << ","
              << csvEscape(c.sources.empty() ? std::string() : c.sources[0].first) << "\n";
        }
        return o.str();
    }

    [[nodiscard]] static std::string vtablesCsv(const RttiReader& a_reader, const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "class,vftable_rva,object_offset,slot,function_rva,function_name,pure_virtual\n";
        for (const auto& v : a_reader.vftables())
        {
            for (const auto& s : v.vfuncs)
            {
                const int64_t fi = a.functionIndexStartingAt(s.funcRva);
                const std::string fname = fi >= 0 ? a.functions[static_cast<size_t>(fi)].name : std::string();
                o << csvEscape(v.fullName) << ",0x" << hex(v.vftableRva, 8) << "," << v.objectOffset << ","
                  << s.slotIndex << ",0x" << hex(s.funcRva, 8) << "," << csvEscape(fname) << ","
                  << (s.isPureVirtual ? 1 : 0) << "\n";
            }
        }
        return o.str();
    }

    [[nodiscard]] static std::string hierarchyText(const ExeAnalysis& a)
    {
        std::map<std::string, std::vector<std::string>> children;
        std::set<std::string> hasParent;
        std::set<std::string> known;
        for (const auto& c : a.classes)
            known.insert(c.name);
        for (const auto& c : a.classes)
            for (const auto& b : c.directBases)
            {
                children[b].push_back(c.name);
                hasParent.insert(c.name);
            }

        std::ostringstream o;
        std::set<std::string> roots;
        for (const auto& c : a.classes)
            if (!hasParent.count(c.name))
                roots.insert(c.name);
        // Bases that have no vftable of their own are not in `classes` but still anchor trees.
        for (const auto& kv : children)
            if (!known.count(kv.first) && !hasParent.count(kv.first))
                roots.insert(kv.first);

        std::set<std::string> visited;
        for (const auto& r : roots)
            printTree(o, children, r, 0, visited);
        return o.str();
    }

    [[nodiscard]] static std::string hierarchyDot(const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "digraph classes {\n  rankdir=BT;\n  node [shape=box,fontname=\"Consolas\"];\n";
        for (const auto& c : a.classes)
            for (const auto& b : c.directBases)
                o << "  \"" << jsonEscape(c.name) << "\" -> \"" << jsonEscape(b) << "\";\n";
        o << "}\n";
        return o.str();
    }

    // ── Functions / strings / globals ────────────────────────────────────

    [[nodiscard]] static std::string functionsCsv(const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "rva,size,origin,callers,data_refs,name,confidence,class,source,imports_used,strings_used\n";
        for (const auto& f : a.functions)
        {
            o << "0x" << hex(f.rva, 8) << "," << f.size << "," << f.origin << "," << f.callers << ","
              << f.dataRefs << "," << csvEscape(f.name) << "," << static_cast<int>(f.confidence) << ","
              << csvEscape(f.ownerClass) << "," << csvEscape(f.sourceFile) << "," << f.importRefCount
              << "," << f.stringRefCount << "\n";
        }
        return o.str();
    }

    [[nodiscard]] static std::string stringsCsv(const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "rva,encoding,length,refs,first_referencing_function,source_path,text\n";
        for (const auto& s : a.strings)
        {
            std::string fn;
            if (!s.funcs.empty())
                fn = "0x" + hex(a.functions[s.funcs[0]].rva, 8);
            o << "0x" << hex(s.rva, 8) << "," << (s.wide ? "utf16" : "ascii") << "," << s.text.size() << ","
              << s.refCount << "," << fn << "," << csvEscape(s.sourcePath) << "," << csvEscape(s.text) << "\n";
        }
        return o.str();
    }

    [[nodiscard]] static std::string globalsCsv(const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "rva,section,bss,kind,class,refs,max_size,points_to,accessing_functions\n";
        for (const auto& g : a.globals)
        {
            std::string fns;
            for (size_t i = 0; i < g.funcs.size() && i < 8; ++i)
                fns += (fns.empty() ? "" : ";") + std::string("0x") + hex(a.functions[g.funcs[i]].rva, 8);
            o << "0x" << hex(g.rva, 8) << "," << g.section << "," << (g.isBss ? 1 : 0) << "," << g.kind << ","
              << csvEscape(g.className) << "," << g.refCount << "," << g.maxSize << ","
              << (g.pointsTo ? "0x" + hex(g.pointsTo, 8) : std::string()) << "," << fns << "\n";
        }
        return o.str();
    }

    [[nodiscard]] static std::string sourcesText(const ExeAnalysis& a)
    {
        std::ostringstream o;
        for (const auto& f : a.sourceFiles)
        {
            o << f.path << "\n";
        }
        return o.str();
    }

    /// Every spelling of every source path (for case-recovery tools), one per line.
    [[nodiscard]] static std::string sourceSpellingsText(const ExeAnalysis& a)
    {
        std::ostringstream o;
        for (const auto& f : a.sourceFiles)
            for (const auto& sp : f.spellings)
                o << sp << "\n";
        return o.str();
    }

    [[nodiscard]] static std::string sourceMapCsv(const ExeAnalysis& a)
    {
        std::ostringstream o;
        o << "source,refs,function_count,classes\n";
        for (const auto& f : a.sourceFiles)
        {
            std::string cls;
            for (size_t i = 0; i < f.classes.size() && i < 20; ++i)
                cls += (cls.empty() ? "" : ";") + f.classes[i];
            o << csvEscape(f.path) << "," << f.refCount << "," << f.funcs.size() << "," << csvEscape(cls) << "\n";
        }
        return o.str();
    }

    // ── Names for disassemblers ──────────────────────────────────────────

    /// Collects every nameable address. a_includeAllGlobals adds untyped data
    /// globals (g_data_XXXXXXXX); otherwise only class instances are named.
    [[nodiscard]] static std::vector<ExeNameEntry> collectNames(
        const RttiReader& a_reader, const ExeAnalysis& a, bool a_includeAllGlobals = false)
    {
        std::vector<ExeNameEntry> names;

        // Functions
        for (const auto& f : a.functions)
        {
            if (f.name.empty())
                continue;
            ExeNameEntry e;
            e.rva = f.rva;
            e.name = f.name;
            e.kind = "function";
            e.size = f.size;
            e.confidence = f.confidence;
            e.comment = confidenceText(f.confidence);
            if (!f.sourceFile.empty())
                e.comment += "; source: " + f.sourceFile;
            names.push_back(std::move(e));
        }

        // vftables + RTTI structures
        std::set<uint64_t> tdNamed;
        for (const auto& v : a_reader.vftables())
        {
            const std::string suffix = v.objectOffset ? "_at_" + hex(v.objectOffset) : std::string();
            ExeNameEntry e;
            e.rva = v.vftableRva;
            e.name = v.fullName + "::vftable" + suffix;
            e.kind = "vftable";
            e.size = v.vftableSizeBytes;
            e.confidence = NameConfidence::High;
            e.comment = "RTTI vftable, " + std::to_string(v.vfuncs.size()) + " slots";
            names.push_back(e);

            ExeNameEntry col;
            col.rva = v.colRva;
            col.name = v.fullName + "::RTTI_CompleteObjectLocator" + suffix;
            col.kind = "rtti";
            col.confidence = NameConfidence::High;
            names.push_back(col);

            if (tdNamed.insert(v.typeDescriptorRva).second)
            {
                ExeNameEntry td;
                td.rva = v.typeDescriptorRva;
                td.name = v.fullName + "::RTTI_TypeDescriptor";
                td.kind = "rtti";
                td.confidence = NameConfidence::High;
                names.push_back(td);
            }
        }

        // Globals
        std::unordered_map<std::string, int> perClass;
        for (const auto& g : a.globals)
        {
            ExeNameEntry e;
            e.rva = g.rva;
            e.kind = "global";
            e.size = g.maxSize;
            if (g.kind == "object")
            {
                const int n = perClass[g.className]++;
                e.name = "g_" + g.className + (n ? "_" + std::to_string(n) : std::string());
                e.confidence = NameConfidence::Medium;
                e.comment = "static instance of " + g.className;
            }
            else if (a_includeAllGlobals)
            {
                e.name = "g_" + g.kind + "_" + hex(g.rva, 8);
                e.confidence = NameConfidence::Low;
                e.comment = g.kind + ", " + std::to_string(g.refCount) + " refs";
            }
            else
                continue;
            names.push_back(std::move(e));
        }

        // Unique, identifier-safe names.
        std::sort(names.begin(), names.end(), [](const ExeNameEntry& x, const ExeNameEntry& y) { return x.rva < y.rva; });
        std::unordered_map<std::string, int> used;
        for (auto& e : names)
        {
            std::string safe = ExeAnalyzer::safeIdentifier(e.name);
            const int n = used[safe]++;
            if (n)
                safe += "_" + hex(e.rva);
            e.safeName = std::move(safe);
        }
        return names;
    }

    [[nodiscard]] static std::string namesJson(const std::vector<ExeNameEntry>& a_names)
    {
        std::ostringstream o;
        o << "[\n";
        for (size_t i = 0; i < a_names.size(); ++i)
        {
            const auto& e = a_names[i];
            o << "  {\"rva\":" << e.rva << ",\"name\":\"" << jsonEscape(e.name) << "\",\"safe\":\""
              << jsonEscape(e.safeName) << "\",\"kind\":\"" << e.kind << "\",\"size\":" << e.size
              << ",\"confidence\":" << static_cast<int>(e.confidence) << ",\"comment\":\""
              << jsonEscape(e.comment) << "\"}" << (i + 1 < a_names.size() ? "," : "") << "\n";
        }
        o << "]\n";
        return o.str();
    }

    [[nodiscard]] static std::string idaScript(const std::vector<ExeNameEntry>& a_names, const std::string& a_module)
    {
        std::ostringstream o;
        o << "# Generated by DumpPDB — apply names to the IDA database of " << a_module << ".\n"
          << "# Run with File > Script file... The image base is taken from the database,\n"
          << "# so the script works after rebasing.\n"
          << "import idaapi, idc, ida_name, ida_bytes\n\n"
          << "BASE = idaapi.get_imagebase()\n"
          << "# (rva, safe_name, kind, comment, min_confidence_level)\n"
          << "NAMES = [\n";
        for (const auto& e : a_names)
            o << "    (0x" << hex(e.rva) << ", \"" << jsonEscape(e.safeName) << "\", \"" << e.kind << "\", \""
              << jsonEscape(e.name + (e.comment.empty() ? "" : "  // " + e.comment)) << "\", "
              << static_cast<int>(e.confidence) << "),\n";
        o << "]\n\n"
          << "MIN_CONFIDENCE = 1  # 1 = low (ctor guesses) ... 3 = high\n\n"
          << "applied = 0\n"
          << "for rva, name, kind, comment, conf in NAMES:\n"
          << "    if conf < MIN_CONFIDENCE:\n"
          << "        continue\n"
          << "    ea = BASE + rva\n"
          << "    if kind == 'function':\n"
          << "        idc.add_func(ea)\n"
          << "    if ida_name.set_name(ea, name, ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE):\n"
          << "        applied += 1\n"
          << "    idc.set_cmt(ea, comment, 1)\n"
          << "print('DumpPDB: applied %d of %d names' % (applied, len(NAMES)))\n";
        return o.str();
    }

    [[nodiscard]] static std::string ghidraScript(const std::vector<ExeNameEntry>& a_names, const std::string& a_module)
    {
        std::ostringstream o;
        o << "# Generated by DumpPDB — apply names to the Ghidra program of " << a_module << ".\n"
          << "# Run from the Script Manager (Jython).\n"
          << "#@category DumpPDB\n"
          << "from ghidra.program.model.symbol import SourceType\n\n"
          << "NAMES = [\n";
        for (const auto& e : a_names)
            o << "    (0x" << hex(e.rva) << ", \"" << jsonEscape(e.safeName) << "\", \"" << e.kind << "\", \""
              << jsonEscape(e.name) << "\", " << static_cast<int>(e.confidence) << "),\n";
        o << "]\n\n"
          << "base = currentProgram.getImageBase()\n"
          << "applied = 0\n"
          << "for rva, name, kind, comment, conf in NAMES:\n"
          << "    addr = base.add(rva)\n"
          << "    try:\n"
          << "        if kind == 'function' and getFunctionAt(addr) is None:\n"
          << "            createFunction(addr, name)\n"
          << "        createLabel(addr, name, True, SourceType.USER_DEFINED)\n"
          << "        setEOLComment(addr, comment)\n"
          << "        applied += 1\n"
          << "    except Exception as e:\n"
          << "        pass\n"
          << "print('DumpPDB: applied %d of %d names' % (applied, len(NAMES)))\n";
        return o.str();
    }

    [[nodiscard]] static std::string x64dbgJson(const std::vector<ExeNameEntry>& a_names, const std::string& a_module)
    {
        std::string mod;
        for (char c : a_module)
            mod.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));

        std::ostringstream o;
        o << "{\n \"labels\": [\n";
        bool first = true;
        for (const auto& e : a_names)
        {
            if (!first)
                o << ",\n";
            first = false;
            o << "  {\"module\":\"" << jsonEscape(mod) << "\",\"address\":\"0x" << hex(e.rva) << "\",\"manual\":false,\"text\":\""
              << jsonEscape(e.safeName) << "\"}";
        }
        o << "\n ]\n}\n";
        return o.str();
    }

    // ── Detailed views ───────────────────────────────────────────────────

    [[nodiscard]] static std::string functionLabel(const ExeAnalysis& a, uint64_t a_rva)
    {
        const int64_t i = a.functionIndexAt(a_rva);
        if (i < 0)
            return "0x" + hex(a_rva, 8);
        const auto& f = a.functions[static_cast<size_t>(i)];
        std::string n = f.name.empty() ? "sub_" + hex(f.rva) : f.name;
        if (a_rva != f.rva)
            n += "+0x" + hex(a_rva - f.rva);
        return n;
    }

    /// Complete description of one class: hierarchy, vftables with named slots,
    /// constructors, static instances, source files and the strings its
    /// functions use.
    [[nodiscard]] static std::string classDetail(
        const RttiReader& a_reader, const ExeAnalysis& a, const ExeClass& c)
    {
        std::ostringstream o;
        const auto vfts = a_reader.vftablesOfType(c.typeDescRva);

        o << "// ============================================================\n";
        o << "// " << c.name << "\n";
        o << "// ============================================================\n";
        o << "RTTI TypeDescriptor : RVA 0x" << hex(c.typeDescRva, 8) << "\n";

        if (!vfts.empty())
        {
            const VftableInfo& p = *vfts.front();
            o << "Mangled name        : " << p.mangledName << "\n";
            o << "Declared as         : " << (p.isStructTag ? "struct" : "class") << "\n";
            if (p.isTemplateInstance)
                o << "Template instance   : yes\n";
            if (p.isInAnonymousNamespace)
                o << "Anonymous namespace : yes\n";
            if (p.isLocalToFunction)
                o << "Local to a function : yes\n";
            o << "Inheritance         : ";
            switch (p.inheritance)
            {
            case InheritanceKind::None:
                o << "none";
                break;
            case InheritanceKind::Single:
                o << "single";
                break;
            case InheritanceKind::Multiple:
                o << "multiple";
                break;
            case InheritanceKind::Virtual:
                o << "virtual";
                break;
            }
            o << "\n";

            o << "\nHierarchy (CHD, " << p.bases.size() << " entries incl. itself):\n";
            for (const auto& b : p.bases)
            {
                o << "  " << (b.isDirect ? "direct     " : "transitive ") << "+0x" << hex(static_cast<uint32_t>(b.offset))
                  << "  " << b.demangledName;
                if (b.isVirtual)
                    o << "  [virtual base: vbptr@" << b.pdisp << ", vdisp " << b.vdisp << "]";
                if (b.attributes & BaseClassInfo::kAmbiguous)
                    o << "  [ambiguous]";
                if (b.attributes & BaseClassInfo::kPrivOrProtBase)
                    o << "  [private/protected]";
                o << "\n";
            }
        }

        for (const auto* v : vfts)
        {
            o << "\nvftable RVA 0x" << hex(v->vftableRva, 8) << "  (subobject +0x" << hex(v->objectOffset) << ", "
              << v->vfuncs.size() << " slots, COL 0x" << hex(v->colRva, 8) << ")\n";
            for (const auto& sl : v->vfuncs)
            {
                const int64_t fi = a.functionIndexStartingAt(sl.funcRva);
                o << "  [" << sl.slotIndex << "] 0x" << hex(sl.funcRva, 8) << "  ";
                if (fi >= 0)
                {
                    const auto& f = a.functions[static_cast<size_t>(fi)];
                    o << (f.name.empty() ? "sub_" + hex(f.rva) : f.name) << "  (" << f.size << " bytes)";
                }
                if (sl.isPureVirtual)
                    o << "  [pure virtual stub]";
                o << "\n";
            }
        }

        o << "\nConstructors / destructors:\n";
        auto fnLine = [&](const char* label, int32_t idx)
        {
            if (idx < 0)
                return;
            const auto& f = a.functions[static_cast<size_t>(idx)];
            o << "  " << label << " 0x" << hex(f.rva, 8) << "  " << f.size << " bytes\n";
        };
        fnLine("deleting destructor", c.deletingDtorFunc);
        fnLine("destructor         ", c.dtorFunc);
        for (uint32_t ci : c.ctorFuncs)
            fnLine("constructor (guess)", static_cast<int32_t>(ci));
        if (c.deletingDtorFunc < 0 && c.dtorFunc < 0 && c.ctorFuncs.empty())
            o << "  (none identified)\n";

        // Static instances
        bool anyInstance = false;
        for (const auto& g : a.globals)
            if (g.kind == "object" && g.className == c.name)
            {
                if (!anyInstance)
                    o << "\nStatic instances:\n";
                anyInstance = true;
                o << "  0x" << hex(g.rva, 8) << " (" << g.section << ")\n";
            }

        if (!c.sources.empty())
        {
            o << "\nSource files (best first):\n";
            for (size_t i = 0; i < c.sources.size() && i < 5; ++i)
                o << "  " << c.sources[i].first << "\n";
        }

        // Strings used by the class's functions
        std::set<std::string> texts;
        std::set<std::string> imports;
        for (const auto& f : a.functions)
        {
            if (f.ownerClass != c.name)
                continue;
            for (uint32_t si : f.strings)
            {
                const auto& st = a.strings[si];
                if (st.sourcePath.empty() && st.text.size() >= 4 && texts.size() < 25)
                    texts.insert(st.text.substr(0, 120));
            }
            for (uint32_t ii : f.imports)
                if (imports.size() < 25)
                    imports.insert(a.imports[ii].dll + "!" + a.imports[ii].name);
        }
        if (!texts.empty())
        {
            o << "\nStrings referenced by its functions:\n";
            for (const auto& t : texts)
                o << "  \"" << t << "\"\n";
        }
        if (!imports.empty())
        {
            o << "\nImports used by its functions:\n";
            for (const auto& t : imports)
                o << "  " << t << "\n";
        }
        return o.str();
    }

    /// Everything known about one address (RVA).
    [[nodiscard]] static std::string describeAddress(
        const RttiReader& a_reader, const ExeAnalysis& a, uint64_t a_rva)
    {
        const PEImage& pe = a_reader.pe();
        std::ostringstream o;

        o << "RVA 0x" << hex(a_rva, 8) << "   VA 0x" << hex(pe.imageBase() + a_rva, 8);
        size_t off = 0;
        if (pe.rvaToOffset(a_rva, off))
            o << "   file offset 0x" << hex(off, 8);
        o << "\n";

        const PESection* sec = pe.sectionForRva(a_rva);
        if (!sec)
        {
            o << "Not inside any section.\n";
            return o.str();
        }
        o << "Section: " << sec->name() << "  (" << characteristicsText(sec->characteristics()) << ")\n";

        // Function
        const int64_t fi = a.functionIndexAt(a_rva);
        if (fi >= 0)
        {
            const auto& f = a.functions[static_cast<size_t>(fi)];
            o << "Function: " << (f.name.empty() ? "sub_" + hex(f.rva) : f.name) << "  start 0x" << hex(f.rva, 8)
              << "  +0x" << hex(a_rva - f.rva) << "  size " << f.size << "\n";
            o << "  direct callers: " << f.callers << ", pointer references: " << f.dataRefs << "\n";
            if (!f.ownerClass.empty())
                o << "  belongs to: " << f.ownerClass << " (" << confidenceText(f.confidence) << ")\n";
            if (!f.sourceFile.empty())
                o << "  source: " << f.sourceFile << "\n";
            for (size_t i = 0; i < f.imports.size() && i < 10; ++i)
                o << "  imports: " << a.imports[f.imports[i]].dll << "!" << a.imports[f.imports[i]].name << "\n";
            for (size_t i = 0; i < f.strings.size() && i < 10; ++i)
            {
                const auto& st = a.strings[f.strings[i]];
                o << "  string 0x" << hex(st.rva, 8) << ": \"" << st.text.substr(0, 100) << "\"\n";
            }
        }

        // vftable / RTTI membership
        const uint64_t ps = pe.is64Bit() ? 8 : 4;
        for (const auto& v : a_reader.vftables())
        {
            const uint64_t begin = v.vftableRva;
            const uint64_t end = begin + v.vfuncs.size() * ps;
            if (a_rva + ps == begin)
                o << "RTTI: pointer to the Complete Object Locator of " << v.fullName << " (just before its vftable)\n";
            else if (a_rva >= begin && a_rva < std::max(end, begin + ps))
                o << "vftable of " << v.fullName << "  slot " << (a_rva - begin) / ps << "  (subobject +0x"
                  << hex(v.objectOffset) << ")\n";
            if (a_rva == v.colRva)
                o << "RTTI Complete Object Locator of " << v.fullName << "\n";
            if (a_rva == v.typeDescriptorRva)
                o << "RTTI Type Descriptor of " << v.fullName << "\n";
        }

        // String
        for (const auto& st : a.strings)
        {
            if (a_rva >= st.rva && a_rva < st.rva + st.byteLength)
            {
                o << (st.wide ? "UTF-16 string" : "String") << " at 0x" << hex(st.rva, 8) << ": \""
                  << st.text.substr(0, 200) << "\"\n";
                break;
            }
            if (st.rva > a_rva)
                break;
        }

        // Import slot
        for (size_t i = 0; i < a.imports.size(); ++i)
            if (a.imports[i].iatRva == a_rva)
                o << "Import address table slot: " << a.imports[i].dll << "!" << a.imports[i].name << "\n";

        // Global
        for (const auto& g : a.globals)
            if (a_rva >= g.rva && a_rva < g.rva + std::max<uint32_t>(g.maxSize, 1) && g.rva <= a_rva)
            {
                o << "Global (" << g.kind << ") at 0x" << hex(g.rva, 8) << (g.isBss ? " [bss]" : "")
                  << (g.className.empty() ? "" : "  instance of " + g.className) << "  " << g.refCount
                  << " refs\n";
                break;
            }

        // Cross references
        const auto range = a.refsTo(a_rva);
        const size_t n = static_cast<size_t>(range.second - range.first);
        o << "\nReferenced from " << n << " location(s)";
        if (n)
            o << ":\n";
        else
            o << " (pointer references only; direct calls are counted per function).\n";
        size_t shown = 0;
        for (const ExeRef* r = range.first; r != range.second && shown < 25; ++r, ++shown)
            o << "  0x" << hex(r->site, 8) << "  " << functionLabel(a, r->site) << "\n";
        if (n > shown)
            o << "  ... and " << (n - shown) << " more\n";
        return o.str();
    }

private:

    [[nodiscard]] static const char* confidenceText(NameConfidence c)
    {
        switch (c)
        {
        case NameConfidence::High:
            return "confidence: high";
        case NameConfidence::Medium:
            return "confidence: medium";
        case NameConfidence::Low:
            return "confidence: low";
        default:
            return "";
        }
    }

    [[nodiscard]] static bool isStruct(const RttiReader& a_reader, const ExeClass& c)
    {
        const auto v = a_reader.vftablesOfType(c.typeDescRva);
        return !v.empty() && v.front()->isStructTag;
    }

    static void printTree(std::ostringstream& o,
        const std::map<std::string, std::vector<std::string>>& a_children,
        const std::string& a_name,
        int a_depth,
        std::set<std::string>& a_visited)
    {
        for (int i = 0; i < a_depth; ++i)
            o << "  ";
        o << a_name;
        if (!a_visited.insert(a_name).second)
        {
            o << "  (...)\n";
            return;
        }
        o << "\n";
        const auto it = a_children.find(a_name);
        if (it == a_children.end())
            return;
        std::vector<std::string> kids = it->second;
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
        for (const auto& k : kids)
            printTree(o, a_children, k, a_depth + 1, a_visited);
    }
};

/// ============================================================================
/// ExeExporter — writes the complete analysis to a directory.
/// ============================================================================
class ExeExporter
{
public:

    [[nodiscard]] static ExeExportResult exportAll(const RttiReader& a_reader,
        const ExeAnalysis& a,
        const std::filesystem::path& a_dir,
        const std::string& a_moduleName)
    {
        ExeExportResult res;
        std::error_code ec;
        std::filesystem::create_directories(a_dir, ec);
        if (ec)
        {
            res.ok = false;
            res.errors.push_back("cannot create " + a_dir.string() + ": " + ec.message());
            return res;
        }

        const auto names = ExeReport::collectNames(a_reader, a);

        write(res, a_dir, "summary.txt", ExeReport::summary(a_reader, a));
        write(res, a_dir, "imports.txt", ExeReport::importsText(a));
        write(res, a_dir, "classes.txt", ExeReport::classesText(a_reader, a));
        write(res, a_dir, "classes.csv", ExeReport::classesCsv(a_reader, a));
        write(res, a_dir, "vtables.csv", ExeReport::vtablesCsv(a_reader, a));
        write(res, a_dir, "hierarchy.txt", ExeReport::hierarchyText(a));
        write(res, a_dir, "hierarchy.dot", ExeReport::hierarchyDot(a));
        write(res, a_dir, "functions.csv", ExeReport::functionsCsv(a));
        write(res, a_dir, "strings.csv", ExeReport::stringsCsv(a));
        write(res, a_dir, "globals.csv", ExeReport::globalsCsv(a));
        write(res, a_dir, "source_files.txt", ExeReport::sourcesText(a));
        write(res, a_dir, "source_spellings.txt", ExeReport::sourceSpellingsText(a));
        write(res, a_dir, "source_map.csv", ExeReport::sourceMapCsv(a));
        write(res, a_dir, "names.json", ExeReport::namesJson(names));
        write(res, a_dir, "apply_names_ida.py", ExeReport::idaScript(names, a_moduleName));
        write(res, a_dir, "apply_names_ghidra.py", ExeReport::ghidraScript(names, a_moduleName));
        write(res, a_dir, "x64dbg_labels.dd32.json", ExeReport::x64dbgJson(names, a_moduleName));

        // Resources: .rc + icons + manifest + ... in resources/
        const auto items = PEResources::parse(a_reader.pe());
        if (!items.empty())
        {
            const RcExportResult rr = RcExporter::build(items, "resources");
            std::vector<std::string> errors;
            for (const auto& f : RcExporter::writeTo(rr, a_dir / "resources", "resources", errors))
                res.files.push_back("resources/" + f);
            for (const auto& e : errors)
            {
                res.ok = false;
                res.errors.push_back(e);
            }
        }
        return res;
    }

private:

    static void write(ExeExportResult& a_res,
        const std::filesystem::path& a_dir,
        const std::string& a_file,
        const std::string& a_content)
    {
        const std::filesystem::path p = a_dir / a_file;
        std::ofstream f(p, std::ios::binary);
        if (!f)
        {
            a_res.ok = false;
            a_res.errors.push_back("cannot write " + p.string());
            return;
        }
        f.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
        a_res.files.push_back(a_file);
    }
};

} // namespace DumpPDB
