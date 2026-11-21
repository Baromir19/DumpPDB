#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <dia2.h>

#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Globals/GlobalRenderer.hpp>
#include <Core/Dump/IDumpCoordinator.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Which global symbols to list and how to filter them.
struct GlobalQuery
{
    std::wstring filter;      ///< Substring of the (qualified) name; empty = everything.
    bool caseSensitive = false;
};

/// Enumerates top-level functions, variables, statics and public symbols and
/// renders them grouped by namespace.
///
/// WHERE THE SYMBOLS COME FROM
///   A PDB keeps some symbols in the global stream (reachable from the global
///   scope) and the rest per object file (compiland): `static` functions, file
///   statics and function-local statics only exist in the latter. To be complete
///   every enumeration therefore walks the global scope AND every compiland and
///   removes duplicates.
class GlobalDumper
{
public:

    GlobalDumper(DumpContext& a_ctx, GlobalRenderer& a_renderer, IDumpCoordinator& a_host)
        : m_ctx(a_ctx)
        , m_renderer(a_renderer)
        , m_host(a_host)
    {
    }

    // ─────────────────────────────────────────────────────────────────────
    // Declarations
    // ─────────────────────────────────────────────────────────────────────

    /// All free functions (and, with GlobalsIncludeMembers, member definitions).
    std::wstring dumpFunctions(IDiaSymbol* a_globalScope, const GlobalQuery& a_query)
    {
        auto entries = collect(a_globalScope, SymTagFunction, a_query);
        if (entries.empty())
            return L"";
        return renderGroupedEntries(entries, L"functions");
    }

    /// Global variables, file statics, constants (and optionally static
    /// members / function-local statics).
    std::wstring dumpVariables(IDiaSymbol* a_globalScope, const GlobalQuery& a_query)
    {
        auto entries = collect(a_globalScope, SymTagData, a_query);

        if (m_ctx.config().m_globalsIncludeLocalStatics)
        {
            std::set<std::wstring> seen;
            for (auto& e : entries)
                seen.insert(entryKey(e));

            auto fns = collect(a_globalScope, SymTagFunction, GlobalQuery{});
            for (auto& fn : fns)
                collectStaticLocals(fn.symbol.get(), a_query, entries, seen, 0);

            sortEntries(entries);
        }

        if (entries.empty())
            return L"";
        return renderGroupedEntries(entries, L"variables");
    }

    /// Public (export-style) symbols: decorated name + address only.
    std::wstring dumpPublics(IDiaSymbol* a_globalScope, const GlobalQuery& a_query)
    {
        auto entries = collect(a_globalScope, SymTagPublicSymbol, a_query);

        std::wstring out;
        out += L"// " + std::to_wstring(entries.size()) + L" public symbols\n";
        for (const auto& e : entries)
        {
            out += m_renderer.renderPublic(e.symbol.get(), 0);
            out += L"\n";
        }
        return out;
    }

    /// Everything the PDB knows about one name: functions, variables, publics.
    /// Falls back to the per-compiland symbols so `static` functions are found too.
    std::wstring dumpByName(IDiaSymbol* a_globalScope, const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_globalScope || !a_name)
            return out;

        const enum SymTagEnum tags[] = {SymTagFunction, SymTagData, SymTagThunk, SymTagPublicSymbol};
        std::vector<Entry> entries;
        std::set<std::wstring> seen;

        for (enum SymTagEnum tag : tags)
        {
            forEachScope(a_globalScope,
                [&](IDiaSymbol* a_scope, bool a_isGlobal)
                {
                    auto found = findAllNamed(a_scope, tag, a_name, a_caseSensitive);
                    for (auto& sym : found)
                        addEntry(sym, entries, seen, !a_isGlobal && tag == SymTagFunction);
                });
        }

        sortEntries(entries);
        for (auto& e : entries)
        {
            out += m_renderer.renderAny(e.symbol.get(), 0, e.fileLocal);
            out += L"\n";
        }
        return out;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Machine-readable table
    // ─────────────────────────────────────────────────────────────────────

    /// Tab-separated table of functions, variables, thunks and publics (header first).
    /// a_withSource also resolves file:line per function (slower on huge PDBs).
    std::wstring dumpSymbolTable(
        IDiaSymbol* a_globalScope, const GlobalQuery& a_query, bool a_withSource)
    {
        std::wstring out = GlobalRenderer::tsvHeader();

        const enum SymTagEnum tags[] = {SymTagFunction, SymTagData, SymTagThunk, SymTagPublicSymbol};
        for (enum SymTagEnum tag : tags)
        {
            auto entries = collect(a_globalScope, tag, a_query);

            if (tag == SymTagData && m_ctx.config().m_globalsIncludeLocalStatics)
            {
                std::set<std::wstring> seen;
                for (auto& e : entries)
                    seen.insert(entryKey(e));
                auto fns = collect(a_globalScope, SymTagFunction, GlobalQuery{});
                for (auto& fn : fns)
                    collectStaticLocals(fn.symbol.get(), a_query, entries, seen, 0);
                sortEntries(entries);
            }

            SymbolRecord rec;
            for (auto& e : entries)
            {
                if (m_renderer.readRecord(e.symbol.get(), rec, a_withSource, e.fileLocal))
                    out += GlobalRenderer::recordToTsv(rec);
            }
        }
        return out;
    }

private:

    struct Entry
    {
        ComPtr<IDiaSymbol> symbol;
        std::wstring name;
        DWORD rva = 0;
        bool fileLocal = false; ///< Function seen only in a compiland (internal linkage).
    };

    static std::wstring entryKey(const Entry& a_e)
    {
        return a_e.name + L"@" + std::to_wstring(a_e.rva);
    }

    static std::wstring lower(std::wstring a_s)
    {
        for (auto& c : a_s)
            c = static_cast<wchar_t>(towlower(c));
        return a_s;
    }

    /// Thin wrapper so this header does not depend on SymbolFinder.hpp.
    static std::vector<ComPtr<IDiaSymbol>> findAllNamed(
        IDiaSymbol* a_scope, enum SymTagEnum a_tag, const wchar_t* a_name, bool a_caseSensitive)
    {
        std::vector<ComPtr<IDiaSymbol>> results;
        if (!a_scope || !a_name)
            return results;

        ComPtr<IDiaEnumSymbols> e;
        if (FAILED(a_scope->findChildren(
                a_tag, a_name, a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive, &e))
            || !e)
            return results;

        ComPtr<IDiaSymbol> symbol;
        ULONG celt = 0;
        while (SUCCEEDED(e->Next(1, &symbol, &celt)) && celt == 1)
        {
            results.push_back(symbol);
            symbol = nullptr;
        }
        return results;
    }

    /// Calls a_fn(scope, isGlobal) for the global scope and then for every compiland.
    template <typename Fn>
    static void forEachScope(IDiaSymbol* a_globalScope, Fn&& a_fn)
    {
        a_fn(a_globalScope, true);

        ComPtr<IDiaEnumSymbols> compilands;
        if (FAILED(a_globalScope->findChildren(SymTagCompiland, nullptr, nsNone, &compilands))
            || !compilands)
            return;

        ComPtr<IDiaSymbol> compiland;
        ULONG celt = 0;
        while (SUCCEEDED(compilands->Next(1, &compiland, &celt)) && celt == 1)
        {
            a_fn(compiland.get(), false);
            compiland = nullptr;
        }
    }

    bool addEntry(const ComPtr<IDiaSymbol>& a_sym,
        std::vector<Entry>& a_out,
        std::set<std::wstring>& a_seen,
        bool a_fileLocal = false)
    {
        Entry e;
        e.symbol = a_sym;
        e.fileLocal = a_fileLocal;

        BSTR raw = nullptr;
        if (SUCCEEDED(a_sym->get_name(&raw)))
            e.name = AddressInfo::takeBstr(raw);

        DWORD tag = SymTagNull;
        a_sym->get_symTag(&tag);
        DWORD rva = 0;
        a_sym->get_relativeVirtualAddress(&rva);
        e.rva = rva;

        // Same name + address + tag = the same symbol seen from two scopes. Data
        // without an address (constants) is keyed by name only.
        const std::wstring key = std::to_wstring(tag) + L"|" + entryKey(e);
        if (!a_seen.insert(key).second)
            return false;

        a_out.push_back(std::move(e));
        return true;
    }

    /// Does this data symbol belong in a variable listing?
    bool wantData(IDiaSymbol* a_sym) const
    {
        DWORD kind = 0;
        if (FAILED(a_sym->get_dataKind(&kind)))
            return false;

        switch (kind)
        {
        case DataIsGlobal:
        case DataIsFileStatic:
        case DataIsConstant:
            return true;
        case DataIsStaticMember:
            return m_ctx.config().m_globalsIncludeMembers;
        case DataIsStaticLocal:
            return m_ctx.config().m_globalsIncludeLocalStatics;
        default:
            return false; // locals, parameters, fields, this-pointers
        }
    }

    bool matchesFilter(const std::wstring& a_name, const GlobalQuery& a_q) const
    {
        if (a_q.filter.empty())
            return true;
        if (a_q.caseSensitive)
            return a_name.find(a_q.filter) != std::wstring::npos;
        return lower(a_name).find(lower(a_q.filter)) != std::wstring::npos;
    }

    std::vector<Entry> collect(IDiaSymbol* a_globalScope, enum SymTagEnum a_tag, const GlobalQuery& a_query)
    {
        std::vector<Entry> entries;
        if (!a_globalScope)
            return entries;

        std::set<std::wstring> seen;

        forEachScope(a_globalScope,
            [&](IDiaSymbol* a_scope, bool a_isGlobal)
            {
                ComPtr<IDiaEnumSymbols> e;
                if (FAILED(a_scope->findChildren(a_tag, nullptr, nsNone, &e)) || !e)
                    return;

                ComPtr<IDiaSymbol> sym;
                ULONG celt = 0;
                while (SUCCEEDED(e->Next(1, &sym, &celt)) && celt == 1)
                {
                    bool keep = true;

                    if (a_tag == SymTagData)
                        keep = wantData(sym.get());
                    else if (a_tag == SymTagFunction
                             && !m_ctx.config().m_globalsIncludeMembers
                             && GlobalRenderer::isMember(sym.get()))
                        keep = false;
                    else if (a_tag == SymTagFunction && m_ctx.config().m_hideCompilerGenerated
                             && FunctionRenderer::isCompilerGenerated(sym.get()))
                        keep = false;

                    if (keep && a_tag == SymTagData && !m_ctx.config().m_globalsIncludeMembers
                        && GlobalRenderer::isMember(sym.get()))
                        keep = false;

                    if (keep)
                    {
                        BSTR raw = nullptr;
                        std::wstring name;
                        if (SUCCEEDED(sym->get_name(&raw)))
                            name = AddressInfo::takeBstr(raw);

                        if (matchesFilter(name, a_query))
                            addEntry(sym, entries, seen, !a_isGlobal && a_tag == SymTagFunction);
                    }
                    sym = nullptr;
                }
            });

        sortEntries(entries);
        return entries;
    }

    /// Finds `static` locals inside a function and its nested blocks.
    void collectStaticLocals(IDiaSymbol* a_scope,
        const GlobalQuery& a_query,
        std::vector<Entry>& a_out,
        std::set<std::wstring>& a_seen,
        int a_depth)
    {
        if (!a_scope || a_depth > 12)
            return;

        ComPtr<IDiaEnumSymbols> e;
        if (FAILED(a_scope->findChildren(SymTagNull, nullptr, nsNone, &e)) || !e)
            return;

        ComPtr<IDiaSymbol> child;
        ULONG celt = 0;
        while (SUCCEEDED(e->Next(1, &child, &celt)) && celt == 1)
        {
            DWORD tag = SymTagNull;
            child->get_symTag(&tag);

            if (tag == SymTagBlock)
            {
                collectStaticLocals(child.get(), a_query, a_out, a_seen, a_depth + 1);
            }
            else if (tag == SymTagData)
            {
                DWORD kind = 0;
                if (SUCCEEDED(child->get_dataKind(&kind)) && kind == DataIsStaticLocal)
                {
                    BSTR raw = nullptr;
                    std::wstring name;
                    if (SUCCEEDED(child->get_name(&raw)))
                        name = AddressInfo::takeBstr(raw);
                    if (matchesFilter(name, a_query))
                        addEntry(child, a_out, a_seen);
                }
            }
            child = nullptr;
        }
    }

    void sortEntries(std::vector<Entry>& a_entries) const
    {
        const bool byAddress = m_ctx.config().m_globalsSortByAddress;
        std::stable_sort(a_entries.begin(),
            a_entries.end(),
            [byAddress](const Entry& a, const Entry& b)
            {
                if (byAddress && a.rva != b.rva)
                    return a.rva < b.rva;
                const std::wstring la = lower(a.name);
                const std::wstring lb = lower(b.name);
                if (la != lb)
                    return la < lb;
                return a.rva < b.rva;
            });
    }

    /// Group entries by namespace: free symbols go into "namespace X { }" blocks,
    /// class members are listed afterwards with their qualified names.
    std::wstring renderGroupedEntries(std::vector<Entry>& a_entries, const wchar_t* a_what)
    {
        std::map<std::wstring, std::vector<Entry*>> byNamespace;
        std::vector<Entry*> members;

        for (auto& e : a_entries)
        {
            if (GlobalRenderer::isMember(e.symbol.get()))
            {
                members.push_back(&e);
                continue;
            }
            const std::wstring ns = TypeWalker::parseQualifiedName(e.name).ns;
            byNamespace[ns].push_back(&e);
        }

        std::wstring out;
        out += L"// " + std::to_wstring(a_entries.size()) + L" " + a_what + L"\n";

        auto emitBlock = [&](const std::wstring& a_ns, const std::vector<Entry*>& a_list)
        {
            const bool hasNs = !a_ns.empty();
            if (hasNs)
            {
                out += TypeWalker::namespaceBlockOpen(a_ns);
                m_host.pushQualifiedScope(a_ns);
            }
            for (Entry* e : a_list)
            {
                out += m_renderer.renderAny(e->symbol.get(), hasNs ? 1 : 0, e->fileLocal);
                out += L"\n";
            }
            if (hasNs)
            {
                m_host.popQualifiedScope(TypeWalker::namespacePartCount(a_ns));
                out += TypeWalker::namespaceBlockClose(a_ns);
            }
        };

        // Global namespace first, then the named ones.
        const auto globalIt = byNamespace.find(L"");
        if (globalIt != byNamespace.end())
            emitBlock(L"", globalIt->second);
        for (const auto& [ns, list] : byNamespace)
        {
            if (ns.empty())
                continue;
            out += L"\n";
            emitBlock(ns, list);
        }

        if (!members.empty())
        {
            out += L"\n// ---- class members (definitions) ----\n";
            for (Entry* e : members)
            {
                out += m_renderer.renderAny(e->symbol.get(), 0, e->fileLocal);
                out += L"\n";
            }
        }
        return out;
    }

    DumpContext& m_ctx;
    GlobalRenderer& m_renderer;
    IDumpCoordinator& m_host;
};
