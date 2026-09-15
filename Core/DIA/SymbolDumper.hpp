#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <dia2.h>

#include <Core/Config/DumpConfig.hpp>
#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/Constants/ConstantRenderer.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>
#include <Core/Dump/Functions/FunctionRenderer.hpp>
#include <Core/Dump/IDumpCoordinator.hpp>
#include <Core/Dump/Members/MemberLayout.hpp>
#include <Core/Dump/Scope/ScopeTracker.hpp>
#include <Core/Dump/Sources/TypeSourceTracker.hpp>
#include <Core/Dump/TopLevel/TopLevelDispatcher.hpp>
#include <Core/Dump/Types/ClassRenderer.hpp>
#include <Core/Dump/Types/EnumRenderer.hpp>
#include <Core/Dump/Types/TypedefRenderer.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Produces formatted C++ declaration strings from DIA symbols.
///
/// SymbolDumper owns a DumpContext (shared mutable state) plus one renderer
/// object per dump subdomain (class, enum, typedef, function, members, top-level,
/// formatting, constants, sources, scope).
///
/// All renderers talk to each other through the IDumpCoordinator back-channel,
/// which SymbolDumper implements privately. The public API is unchanged.
class SymbolDumper : private IDumpCoordinator
{
public:

    // ── Lifecycle and configuration ────────────────────────────────────────────

    explicit SymbolDumper(const DumpConfig& a_config = DumpConfig())
        : m_ctx(a_config)
        , m_formatter(m_ctx)
        , m_constants(m_ctx)
        , m_sources(m_ctx)
        , m_scopeTracker(m_ctx)
        , m_classes(m_ctx, m_formatter, *this)
        , m_enums(m_ctx, m_formatter, m_constants)
        , m_typedefs(m_ctx, m_formatter)
        , m_functions(m_ctx, m_formatter, *this)
        , m_members(
              m_ctx, m_formatter, m_constants, m_classes, m_enums, m_typedefs, m_functions, *this)
        , m_topLevel(m_ctx, *this)
    {
    }

    void setConfig(const DumpConfig& a_config)
    {
        m_ctx.setConfig(a_config);
    }

    const DumpConfig& config() const
    {
        return m_ctx.config();
    }

    /// Set the user-requested template instantiation (computed from the query name).
    /// When active, a "template<...>" header is emitted above the dumped type and
    /// concrete arguments are substituted back with the generated parameter names.
    void setTemplateInstantiation(TypeWalker::TemplateInstantiation a_ti)
    {
        m_ctx.setTemplateInstantiation(std::move(a_ti));
    }

    const TypeWalker::TemplateInstantiation& templateInstantiation() const
    {
        return m_ctx.templateInstantiation();
    }

    void setSession(IDiaSession* a_session)
    {
        m_ctx.setSession(a_session);
    }

    // ── Scope control ──────────────────────────────────────────────────────────

    void pushScope(const std::wstring& a_name)
    {
        m_scopeTracker.pushScope(a_name);
    }

    void popScope()
    {
        m_scopeTracker.popScope();
    }

    /// Push a fully-qualified namespace onto the scope stack one part at a time.
    /// e.g. pushQualifiedScope(L"User::Math") pushes "User" then "Math".
    void pushQualifiedScope(const std::wstring& a_qualifiedName)
    {
        m_scopeTracker.pushQualifiedScope(a_qualifiedName);
    }

    void popQualifiedScope(size_t a_partCount)
    {
        m_scopeTracker.popQualifiedScope(a_partCount);
    }

    // ── Top-level dump ─────────────────────────────────────────────────────────

    /// Dump any top-level symbol (class/enum/typedef), wrapping it in its
    /// namespace block if it lives inside a namespace.
    /// Uses the compact C++17 form "namespace User::Math { ... }".
    std::wstring dumpTopLevelAny(IDiaSymbol* a_symbol)
    {
        return m_topLevel.dumpTopLevelAny(a_symbol);
    }

    void processType(IDiaSymbol* a_symbol, std::wstring& a_output)
    {
        m_topLevel.processType(a_symbol, a_output);
    }

    // ── Class dumping ──────────────────────────────────────────────────────────

    std::wstring dumpClass(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        return m_classes.dumpClass(a_symbol, a_nestingLevel);
    }

    /// Dump an anonymous UDT (union/struct) as an inline block, without a name.
    std::wstring dumpAnonymousUDT(IDiaSymbol* a_udtSymbol, int a_nestingLevel)
    {
        return m_classes.dumpAnonymousUDT(a_udtSymbol, a_nestingLevel);
    }

    std::wstring dumpMembers(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        return m_members.dumpMembers(a_symbol, a_nestingLevel);
    }

    /// Emit access specifier label if access has changed.
    /// Returns the new lastAccess value.
    DWORD emitAccessLabel(
        std::wstring& a_out, IDiaSymbol* a_symbol, DWORD a_lastAccess, int a_nestingLevel) const
    {
        // const_cast is safe: DumpFormatter state is only the DumpContext reference
        return const_cast<DumpFormatter&>(m_formatter)
            .emitAccessLabel(a_out, a_symbol, a_lastAccess, a_nestingLevel);
    }

    /// Check if a function symbol is compiler-generated.
    static bool isCompilerGenerated(IDiaSymbol* a_symbol)
    {
        return FunctionRenderer::isCompilerGenerated(a_symbol);
    }

    // ── Enum dumping ───────────────────────────────────────────────────────────

    std::wstring dumpEnum(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        return m_enums.dumpEnum(a_symbol, a_nestingLevel);
    }

    // ── Typedef dumping ────────────────────────────────────────────────────────

    std::wstring dumpTypedef(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        return m_typedefs.dumpTypedef(a_symbol, a_nestingLevel);
    }

    // ── Function dumping ───────────────────────────────────────────────────────

    std::wstring dumpFunction(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        return m_functions.dumpFunction(a_symbol, a_nestingLevel);
    }

    std::wstring dumpFriend(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        return m_functions.dumpFriend(a_symbol, a_nestingLevel);
    }

    // ── Type source lookup ─────────────────────────────────────────────────────

    /// Get source file names for a symbol, newline-separated.
    std::wstring getTypeSourceFiles(IDiaSymbol* a_symbol)
    {
        return m_sources.getTypeSourceFiles(a_symbol);
    }

    /// Register source file info for a symbol (stores for later output).
    void registerTypeSource(IDiaSymbol* a_symbol)
    {
        m_sources.registerTypeSource(a_symbol);
    }

    std::wstring typeSources()
    {
        return m_sources.typeSources();
    }

    std::wstring getTypeSourceFilesRecursive(
        IDiaSymbol* a_symbol, std::unordered_set<DWORD>& a_visited, int depth = 0)
    {
        return m_sources.getTypeSourceFilesRecursive(a_symbol, a_visited, depth);
    }

private:

    // ── IDumpCoordinator back-channel (used by renderers to call each other) ───

    DWORD emitAccessLabel(std::wstring& a_output,
        IDiaSymbol* a_symbol,
        DWORD a_lastAccess,
        int a_nestingLevel) override
    {
        return m_formatter.emitAccessLabel(a_output, a_symbol, a_lastAccess, a_nestingLevel);
    }

    // ── Owned state and renderers ──────────────────────────────────────────────

    DumpContext m_ctx;
    DumpFormatter m_formatter;
    ConstantRenderer m_constants;
    TypeSourceTracker m_sources;
    ScopeTracker m_scopeTracker;
    ClassRenderer m_classes;
    EnumRenderer m_enums;
    TypedefRenderer m_typedefs;
    FunctionRenderer m_functions;
    MemberLayout m_members;
    TopLevelDispatcher m_topLevel;
};
