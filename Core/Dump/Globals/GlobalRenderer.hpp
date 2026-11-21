#pragma once

#include <string>

#include <dia2.h>

#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/Constants/ConstantRenderer.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>
#include <Core/Dump/Functions/FunctionRenderer.hpp>
#include <Core/Dump/Globals/AddressInfo.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// One symbol flattened into plain strings/numbers. This is the machine
/// readable view (TSV) used by the restoration scripts and for mapping PDB
/// symbols onto an executable.
struct SymbolRecord
{
    std::wstring kind;       ///< function, thunk, global, file-static, static-member, static-local, constant, public-function, public-data
    std::wstring name;       ///< Qualified name as stored in the PDB.
    std::wstring undecorated; ///< Full undecorated signature (publics).
    std::wstring className;  ///< Owning class for members.
    std::wstring compiland;  ///< Object file / translation unit.
    std::wstring sourceFile; ///< Functions: file of the first line.
    std::wstring flags;      ///< Comma separated: virtual,static,inline,naked,...
    std::wstring typeText;   ///< Data: "type name". Functions: return type ("" for ctor/dtor/conversion).
    std::wstring params;     ///< Functions: parameter list without the parentheses.
    std::wstring qualifiers; ///< Functions: "const", "volatile", "noexcept" (space separated).
    bool hasRva = false;
    DWORD rva = 0;
    DWORD section = 0;
    DWORD offset = 0;
    DWORD line = 0;
    ULONGLONG length = 0;
};

/// Renders top-level (non-type) symbols: free functions, global / file-static /
/// static-local variables, constants, public symbols and thunks.
///
/// This is what makes functions and variables visible in a dump at all:
/// previously the dispatcher only knew about UDT / enum / typedef and silently
/// dropped everything else.
class GlobalRenderer
{
public:

    GlobalRenderer(DumpContext& a_ctx,
        DumpFormatter& a_fmt,
        ConstantRenderer& a_constants,
        FunctionRenderer& a_functions)
        : m_ctx(a_ctx)
        , m_fmt(a_fmt)
        , m_constants(a_constants)
        , m_functions(a_functions)
    {
    }

    // ─────────────────────────────────────────────────────────────────────
    // Classification
    // ─────────────────────────────────────────────────────────────────────

    /// True for symbols this renderer can print.
    static bool handles(DWORD a_symTag)
    {
        return a_symTag == SymTagFunction || a_symTag == SymTagData
               || a_symTag == SymTagPublicSymbol || a_symTag == SymTagThunk;
    }

    /// Function/data that belongs to a class (a method definition or static member).
    static bool isMember(IDiaSymbol* a_symbol)
    {
        return TypeWalker::hasClassParent(a_symbol);
    }

    /// Short human-readable kind of a symbol ("function", "global", ...).
    static std::wstring kindOf(IDiaSymbol* a_symbol)
    {
        DWORD tag = SymTagNull;
        a_symbol->get_symTag(&tag);

        switch (tag)
        {
        case SymTagFunction:
            return L"function";
        case SymTagThunk:
            return L"thunk";
        case SymTagPublicSymbol:
        {
            BOOL isFunc = FALSE;
            BOOL isCode = FALSE;
            a_symbol->get_function(&isFunc);
            a_symbol->get_code(&isCode);
            return (isFunc || isCode) ? L"public-function" : L"public-data";
        }
        case SymTagData:
        {
            DWORD kind = 0;
            a_symbol->get_dataKind(&kind);
            switch (kind)
            {
            case DataIsGlobal:
                return L"global";
            case DataIsFileStatic:
                return L"file-static";
            case DataIsStaticMember:
                return L"static-member";
            case DataIsStaticLocal:
                return L"static-local";
            case DataIsConstant:
                return L"constant";
            default:
                return L"data";
            }
        }
        default:
            return L"symbol";
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Declarations
    // ─────────────────────────────────────────────────────────────────────

    /// Render any supported symbol, or "" for other tags.
    ///
    /// a_fileLocal: the symbol was only found in an object file's own symbol table
    /// and not in the global stream, i.e. it has internal linkage (`static`).
    std::wstring renderAny(IDiaSymbol* a_symbol, int a_nestingLevel, bool a_fileLocal = false)
    {
        if (!a_symbol)
            return L"";

        DWORD tag = SymTagNull;
        a_symbol->get_symTag(&tag);

        switch (tag)
        {
        case SymTagFunction:
            return renderFunction(a_symbol, a_nestingLevel, a_fileLocal);
        case SymTagData:
            return renderVariable(a_symbol, a_nestingLevel);
        case SymTagPublicSymbol:
            return renderPublic(a_symbol, a_nestingLevel);
        case SymTagThunk:
            return renderThunk(a_symbol, a_nestingLevel);
        default:
            return L"";
        }
    }

    /// Function declaration with return type, calling convention, name,
    /// parameters (with names when the PDB has them), qualifiers and a trailing
    /// "// RVA ... @ file:line" comment. No trailing newline.
    std::wstring renderFunction(IDiaSymbol* a_symbol, int a_nestingLevel, bool a_fileLocal = false)
    {
        std::wstring ret = m_fmt.tab(a_nestingLevel);
        const DumpConfig& cfg = m_ctx.config();

        ComPtr<IDiaSymbol> functionType = FunctionRenderer::getTypeCom(a_symbol);

        const bool member = isMember(a_symbol);
        const std::wstring name = TypeWalker::getName(a_symbol, m_ctx.scope());
        const QualifiedName qn = TypeWalker::parseQualifiedName(name);

        BOOL isCtor = FALSE;
        a_symbol->get_constructor(&isCtor);
        const bool isDtor = !qn.leaf.empty() && qn.leaf[0] == L'~';
        if (!isCtor && member && !qn.ns.empty() && qn.leaf == TypeWalker::leafName(qn.ns))
        {
            isCtor = TRUE;
        }
        const bool isConversion = FunctionRenderer::isConversionOperator(qn.leaf);

        std::wstring flagsComment;

        // Internal linkage (S_LPROC32 that is not reachable from the global scope).
        if (a_fileLocal && !member)
        {
            ret += L"static ";
        }

        // __declspec / inline
        if (cfg.m_showFuncAttrs)
        {
            ret += functionAttributes(a_symbol, flagsComment);
        }

        if (!functionType)
        {
            ret += L"/* signature unavailable: no type info */ ";
        }
        else if (!isCtor && !isDtor && !isConversion)
        {
            ComPtr<IDiaSymbol> retType;
            if (SUCCEEDED(functionType->get_type(&retType)) && retType)
            {
                try
                {
                    ret += TypeWalker::resolveType(retType.get(),
                        m_ctx.scope(),
                        cfg.m_showNonScoped,
                        cfg.m_intStyle)
                               .build();
                }
                catch (...)
                {
                    ret += L"/* <error> */";
                }
                ret += L" ";
            }
        }

        // Calling convention. Member functions use __thiscall by default, so it
        // would only be noise for them.
        if (cfg.m_showCallingConv && functionType && !m_ctx.hasSingleCallingConvention())
        {
            const auto cc = TypeWalker::getCallingConvention(functionType.get());
            const bool known = cc != TypeWalker::CallingConvention::Unknown;
            const bool implicitThiscall = member && cc == TypeWalker::CallingConvention::Thiscall;
            if (known && !implicitThiscall)
            {
                const wchar_t* text = TypeWalker::renderCallingConvention(cc);
                if (text && *text)
                {
                    ret += text;
                    ret += L" ";
                }
            }
        }

        ret += name;
        ret += L"(";
        ret += m_functions.renderParameterList(a_symbol, functionType.get());
        ret += L")";

        if (functionType)
        {
            if (TypeWalker::isConstMemberFunction(functionType.get()))
                ret += L" const";
            if (TypeWalker::isVolatileMemberFunction(functionType.get()))
                ret += L" volatile";
        }

        {
            IDiaSymbol4* symbol4 = nullptr;
            if (SUCCEEDED(a_symbol->QueryInterface(__uuidof(IDiaSymbol4), (void**)&symbol4))
                && symbol4)
            {
                BOOL isNoExcept = FALSE;
                if (SUCCEEDED(symbol4->get_noexcept(&isNoExcept)) && isNoExcept)
                    ret += L" noexcept";
                symbol4->Release();
            }
        }

        ret += L";";

        // Trailing information comment.
        std::wstring info;
        if (cfg.m_showAddress)
        {
            info = AddressInfo::comment(a_symbol);
        }
        if (cfg.m_showSourceLine)
        {
            std::wstring file;
            DWORD line = 0;
            if (AddressInfo::firstSourceLine(m_ctx.session(), a_symbol, file, line))
            {
                info += info.empty() ? L"// @ " : L"  @ ";
                info += file;
                if (line)
                {
                    info += L":";
                    info += std::to_wstring(line);
                }
            }
        }
        if (!flagsComment.empty())
        {
            if (info.empty())
                info = L"//";
            info += L"  [";
            info += flagsComment;
            info += L"]";
        }
        if (!info.empty())
        {
            ret += L"  ";
            ret += info;
        }

        return ret;
    }

    /// Variable / static / constant declaration. No trailing newline.
    std::wstring renderVariable(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret = m_fmt.tab(a_nestingLevel);
        const DumpConfig& cfg = m_ctx.config();

        DWORD kind = 0;
        a_symbol->get_dataKind(&kind);

        DWORD locType = 0;
        a_symbol->get_locationType(&locType);

        if (locType == LocIsTLS)
        {
            ret += L"__declspec(thread) ";
        }

        switch (kind)
        {
        case DataIsFileStatic:
        case DataIsStaticLocal:
            ret += L"static ";
            break;
        case DataIsConstant:
            ret += L"constexpr ";
            break;
        default:
            break;
        }

        try
        {
            ret += TypeWalker::resolveType(a_symbol, m_ctx.scope(), true, cfg.m_intStyle).build();
        }
        catch (...)
        {
            ret += L"/* <error> */";
        }

        if (m_constants.canHaveValue(a_symbol))
        {
            ret += m_constants.constantValueSuffix(a_symbol);
        }

        ret += L";";

        std::wstring info;
        if (cfg.m_showAddress)
        {
            info = AddressInfo::comment(a_symbol);
            if (info.empty() && locType == LocIsTLS)
                info = L"// thread-local";
        }

        const std::wstring kindText = kindOf(a_symbol);
        if (kind == DataIsStaticMember || kind == DataIsStaticLocal)
        {
            if (info.empty())
                info = L"//";
            info += L"  [";
            info += kindText;
            if (kind == DataIsStaticLocal)
            {
                const std::wstring owner = lexicalOwnerName(a_symbol);
                if (!owner.empty())
                {
                    info += L" in ";
                    info += owner;
                }
            }
            info += L"]";
        }

        if (!info.empty())
        {
            ret += L"  ";
            ret += info;
        }
        return ret;
    }

    /// Public symbol: only a (decorated) name and an address, no type info.
    /// Printed as the undecorated signature followed by an informational comment.
    std::wstring renderPublic(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret = m_fmt.tab(a_nestingLevel);

        BSTR rawDecorated = nullptr;
        a_symbol->get_name(&rawDecorated);
        const std::wstring decorated = AddressInfo::takeBstr(rawDecorated);

        BSTR rawUndecorated = nullptr;
        a_symbol->get_undecoratedName(&rawUndecorated);
        std::wstring undecorated = AddressInfo::takeBstr(rawUndecorated);

        ret += undecorated.empty() ? decorated : undecorated;
        ret += L";";

        std::wstring info = AddressInfo::comment(a_symbol, false);
        if (info.empty())
            info = L"//";
        info += L"  [";
        info += kindOf(a_symbol);
        info += L"]";
        if (!undecorated.empty() && undecorated != decorated)
        {
            info += L"  decorated: ";
            info += decorated;
        }

        ret += L"  ";
        ret += info;
        return ret;
    }

    /// Linker thunk (incremental-link jump stub, import thunk).
    std::wstring renderThunk(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret = m_fmt.tab(a_nestingLevel);
        ret += L"/* thunk */ ";
        ret += TypeWalker::getName(a_symbol, m_ctx.scope());
        ret += L"();";

        const std::wstring info = AddressInfo::comment(a_symbol);
        if (!info.empty())
        {
            ret += L"  ";
            ret += info;
        }
        return ret;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Machine-readable records (TSV)
    // ─────────────────────────────────────────────────────────────────────

    static const wchar_t* tsvHeader()
    {
        return L"kind\tname\tundecorated\trva\tsection\toffset\tlength\tclass\tcompiland\tsource\tline\tflags\ttype\tparams\tqualifiers\n";
    }

    /// Fills a_out from a symbol. a_withSource enables the (slower) line lookup.
    bool readRecord(
        IDiaSymbol* a_symbol, SymbolRecord& a_out, bool a_withSource, bool a_fileLocal = false)
    {
        if (!a_symbol)
            return false;

        a_out = SymbolRecord{};
        a_out.kind = kindOf(a_symbol);

        BSTR raw = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&raw)))
            a_out.name = AddressInfo::takeBstr(raw);

        DWORD tag = SymTagNull;
        a_symbol->get_symTag(&tag);

        if (tag == SymTagPublicSymbol)
        {
            BSTR und = nullptr;
            if (SUCCEEDED(a_symbol->get_undecoratedName(&und)))
                a_out.undecorated = AddressInfo::takeBstr(und);
        }

        const AddressInfo::Location loc = AddressInfo::locate(a_symbol);
        a_out.hasRva = loc.hasRva;
        a_out.rva = loc.rva;
        a_out.section = loc.section;
        a_out.offset = loc.offset;
        a_out.length = loc.length;

        // Owning class
        ComPtr<IDiaSymbol> classParent;
        if (SUCCEEDED(a_symbol->get_classParent(&classParent)) && classParent)
        {
            BSTR cn = nullptr;
            if (SUCCEEDED(classParent->get_name(&cn)))
                a_out.className = AddressInfo::takeBstr(cn);
        }

        a_out.compiland = compilandName(a_symbol);

        std::wstring flags;
        auto addFlag = [&](const wchar_t* f)
        {
            if (!flags.empty())
                flags += L",";
            flags += f;
        };

        if (tag == SymTagFunction)
        {
            BOOL b = FALSE;
            if (SUCCEEDED(a_symbol->get_virtual(&b)) && b)
                addFlag(L"virtual");
            if (TypeWalker::isStaticMemberFunction(a_symbol))
                addFlag(L"static");
            b = FALSE;
            if (SUCCEEDED(a_symbol->get_pure(&b)) && b)
                addFlag(L"pure");
            if (FunctionRenderer::isCompilerGenerated(a_symbol))
                addFlag(L"compiler-generated");
            if (a_fileLocal && !TypeWalker::hasClassParent(a_symbol))
                addFlag(L"file-local");
            std::wstring unused;
            const std::wstring attrs = functionAttributes(a_symbol, unused);
            if (!attrs.empty())
                addFlag(L"attrs");
            if (!unused.empty())
                addFlag(unused.c_str());

            ComPtr<IDiaSymbol> ft = FunctionRenderer::getTypeCom(a_symbol);
            if (ft)
            {
                const auto cc = TypeWalker::getCallingConvention(ft.get());
                const wchar_t* ccText = TypeWalker::renderCallingConvention(cc);
                if (ccText && *ccText)
                    addFlag(ccText);
            }

            if (a_withSource)
            {
                DWORD line = 0;
                std::wstring file;
                if (AddressInfo::firstSourceLine(m_ctx.session(), a_symbol, file, line))
                {
                    a_out.sourceFile = file;
                    a_out.line = line;
                }
            }
        }
        a_out.flags = flags;

        if (tag == SymTagData)
        {
            try
            {
                a_out.typeText
                    = TypeWalker::resolveType(a_symbol, m_ctx.scope(), false, m_ctx.config().m_intStyle)
                          .build();
            }
            catch (...)
            {
                a_out.typeText = L"";
            }
        }
        else if (tag == SymTagFunction)
        {
            ComPtr<IDiaSymbol> ft = FunctionRenderer::getTypeCom(a_symbol);
            if (ft)
            {
                // Return type — empty for constructors, destructors and conversion
                // operators, exactly like in renderFunction().
                const QualifiedName qn = TypeWalker::parseQualifiedName(a_out.name);
                BOOL isCtor = FALSE;
                a_symbol->get_constructor(&isCtor);
                if (!isCtor && !qn.ns.empty() && qn.leaf == TypeWalker::leafName(qn.ns)
                    && TypeWalker::hasClassParent(a_symbol))
                    isCtor = TRUE;
                const bool isDtor = !qn.leaf.empty() && qn.leaf[0] == L'~';
                const bool isConversion = FunctionRenderer::isConversionOperator(qn.leaf);

                ComPtr<IDiaSymbol> ret;
                if (!isCtor && !isDtor && !isConversion && SUCCEEDED(ft->get_type(&ret)) && ret)
                {
                    try
                    {
                        a_out.typeText
                            = TypeWalker::resolveType(ret.get(),
                                  m_ctx.scope(),
                                  false,
                                  m_ctx.config().m_intStyle)
                                  .build();
                    }
                    catch (...)
                    {
                    }
                }

                a_out.params = m_functions.renderParameterList(a_symbol, ft.get());

                std::wstring q;
                auto addQ = [&](const wchar_t* t)
                {
                    if (!q.empty())
                        q += L" ";
                    q += t;
                };
                if (TypeWalker::isConstMemberFunction(ft.get()))
                    addQ(L"const");
                if (TypeWalker::isVolatileMemberFunction(ft.get()))
                    addQ(L"volatile");
                IDiaSymbol4* symbol4 = nullptr;
                if (SUCCEEDED(a_symbol->QueryInterface(__uuidof(IDiaSymbol4), (void**)&symbol4))
                    && symbol4)
                {
                    BOOL isNoExcept = FALSE;
                    if (SUCCEEDED(symbol4->get_noexcept(&isNoExcept)) && isNoExcept)
                        addQ(L"noexcept");
                    symbol4->Release();
                }
                a_out.qualifiers = q;
            }
        }
        return true;
    }

    /// One TSV line (tabs/newlines inside fields are flattened to spaces).
    static std::wstring recordToTsv(const SymbolRecord& a_r)
    {
        auto clean = [](const std::wstring& s)
        {
            std::wstring o = s;
            for (auto& c : o)
                if (c == L'\t' || c == L'\n' || c == L'\r')
                    c = L' ';
            return o;
        };

        wchar_t num[48];
        std::wstring out;
        out += clean(a_r.kind);
        out += L"\t";
        out += clean(a_r.name);
        out += L"\t";
        out += clean(a_r.undecorated);
        out += L"\t";
        if (a_r.hasRva)
        {
            swprintf_s(num, L"0x%08X", a_r.rva);
            out += num;
        }
        out += L"\t";
        if (a_r.section)
        {
            swprintf_s(num, L"%u", a_r.section);
            out += num;
        }
        out += L"\t";
        if (a_r.section)
        {
            swprintf_s(num, L"0x%08X", a_r.offset);
            out += num;
        }
        out += L"\t";
        if (a_r.length)
        {
            swprintf_s(num, L"%llu", a_r.length);
            out += num;
        }
        out += L"\t";
        out += clean(a_r.className);
        out += L"\t";
        out += clean(a_r.compiland);
        out += L"\t";
        out += clean(a_r.sourceFile);
        out += L"\t";
        if (a_r.line)
        {
            swprintf_s(num, L"%u", a_r.line);
            out += num;
        }
        out += L"\t";
        out += clean(a_r.flags);
        out += L"\t";
        out += clean(a_r.typeText);
        out += L"\t";
        out += clean(a_r.params);
        out += L"\t";
        out += clean(a_r.qualifiers);
        out += L"\n";
        return out;
    }

private:

    /// "__declspec(naked) noinline ..." prefix; a_comment receives body flags
    /// (seh, eh, inline-asm) for the trailing comment.
    ///
    /// NOTE: this is the only place that uses the IDiaSymbol attribute getters
    /// (get_isNaked, get_noInline, get_noReturn, get_inlSpec, get_hasSEH,
    /// get_hasEH, get_hasInlAsm). If a getter is missing from your dia2.h,
    /// remove its block here.
    static std::wstring functionAttributes(IDiaSymbol* a_symbol, std::wstring& a_comment)
    {
        std::wstring out;
        BOOL b = FALSE;

        if (SUCCEEDED(a_symbol->get_isNaked(&b)) && b)
            out += L"__declspec(naked) ";
        b = FALSE;
        if (SUCCEEDED(a_symbol->get_noInline(&b)) && b)
            out += L"__declspec(noinline) ";
        b = FALSE;
        if (SUCCEEDED(a_symbol->get_noReturn(&b)) && b)
            out += L"__declspec(noreturn) ";
        b = FALSE;
        if (SUCCEEDED(a_symbol->get_inlSpec(&b)) && b)
            out += L"inline ";

        auto addComment = [&](const wchar_t* f)
        {
            if (!a_comment.empty())
                a_comment += L",";
            a_comment += f;
        };
        b = FALSE;
        if (SUCCEEDED(a_symbol->get_hasSEH(&b)) && b)
            addComment(L"seh");
        b = FALSE;
        if (SUCCEEDED(a_symbol->get_hasEH(&b)) && b)
            addComment(L"eh");
        b = FALSE;
        if (SUCCEEDED(a_symbol->get_hasInlAsm(&b)) && b)
            addComment(L"inline-asm");

        return out;
    }

    /// Name of the compiland (object file) a symbol belongs to, or "".
    static std::wstring compilandName(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> parent;
        if (FAILED(a_symbol->get_lexicalParent(&parent)) || !parent)
            return L"";

        DWORD tag = SymTagNull;
        parent->get_symTag(&tag);
        if (tag != SymTagCompiland)
            return L"";

        BSTR name = nullptr;
        if (SUCCEEDED(parent->get_name(&name)))
            return AddressInfo::takeBstr(name);
        return L"";
    }

    /// Name of the function/block that lexically owns a static local.
    static std::wstring lexicalOwnerName(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> parent;
        if (FAILED(a_symbol->get_lexicalParent(&parent)) || !parent)
            return L"";

        // Blocks are unnamed: climb to the enclosing function.
        for (int depth = 0; depth < 16 && parent; ++depth)
        {
            DWORD tag = SymTagNull;
            parent->get_symTag(&tag);
            if (tag == SymTagFunction)
            {
                BSTR name = nullptr;
                if (SUCCEEDED(parent->get_name(&name)))
                    return AddressInfo::takeBstr(name);
                return L"";
            }
            ComPtr<IDiaSymbol> next;
            if (FAILED(parent->get_lexicalParent(&next)))
                break;
            parent = next;
        }
        return L"";
    }

    DumpContext& m_ctx;
    DumpFormatter& m_fmt;
    ConstantRenderer& m_constants;
    FunctionRenderer& m_functions;
};
