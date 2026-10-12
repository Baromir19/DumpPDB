#pragma once

#include <string>

#include <dia2.h>

#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>
#include <Core/Dump/IDumpCoordinator.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Renders function and friend declarations, including return types,
/// parameters, cv/noexcept/virtual qualifiers and source registration.
class FunctionRenderer
{
public:

    FunctionRenderer(DumpContext& a_ctx, DumpFormatter& a_fmt, IDumpCoordinator& a_host)
        : m_ctx(a_ctx)
        , m_fmt(a_fmt)
        , m_host(a_host)
    {
    }

    std::wstring dumpFunction(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;
        ret += m_fmt.tab(a_nestingLevel);

        // Virtual/static qualifiers
        const wchar_t* names[] = {getVirtualName(a_symbol), getStaticName(a_symbol)};
        for (auto name : names)
        {
            if (name)
            {
                ret += name;
                ret += L" ";
            }
        }

        auto functionType = getTypeCom(a_symbol); // SymTagFunctionType

        // DIA reports the "..." marker as an extra argument, but it is not a named
        // parameter: it is excluded from the count and rendered separately.
        DWORD argCount = 0;
        bool isVariadic = false;
        if (functionType)
        {
            argCount = TypeWalker::countFunctionArgs(functionType.get());
            isVariadic = TypeWalker::isVariadicFunction(functionType.get());
        }

        // Return type
        std::wstring funcName = TypeWalker::getName(a_symbol, m_ctx.scope());
        BOOL isCtor = FALSE;
        a_symbol->get_constructor(&isCtor);
        bool isDtor = !funcName.empty() && funcName[0] == L'~';

        // A conversion function (`operator bool`, `operator int`, ...) carries its
        // "return type" as part of the operator name — rendering it separately
        // would produce ill-formed C++ (`bool operator bool()`).
        const bool isConversionOp = isConversionOperator(funcName);

        if (!isCtor && !m_ctx.scope().empty()
            && funcName == TypeWalker::leafName(m_ctx.scope().top()))
        {
            isCtor = TRUE;
        }

        // Return type — skipped for constructors/destructors and conversion
        // functions (their target type is already part of the operator name)
        if (!isCtor && !isDtor && !isConversionOp && functionType)
        {
            ComPtr<IDiaSymbol> retType;
            if (SUCCEEDED(functionType->get_type(&retType)) && retType)
            {
                std::wstring retTypeStr;
                try
                {
                    retTypeStr = TypeWalker::resolveType(retType.get(),
                        m_ctx.scope(),
                        m_ctx.config().m_showNonScoped,
                        m_ctx.config().m_intStyle)
                                     .build();
                }
                catch (...)
                {
                    retTypeStr = L"/* <error> */";
                }
                ret += retTypeStr;
                ret += L" ";
            }
        }

        ret += funcName;
        ret += L"(";

        // Named parameters (searched on SymTagFunction itself)
        auto namedArgCount = dumpFunctionArgsToString(a_symbol, ret);

        // If named arg count doesn't match the actual function type arg count,
        // fall back to the function type's args (which may have unnamed params).
        // This fixes constructors/copy-constructors where params are on FunctionType
        // but not directly on the Function symbol.
        if (functionType && namedArgCount != (int)argCount)
        {
            if (namedArgCount > 0)
            {
                ret += L", ";
            }
            ret += TypeWalker::getFuncArgsString(functionType.get(),
                m_ctx.scope(),
                m_ctx.config().m_showNonScoped,
                m_ctx.config().m_intStyle);
        }
        else if (isVariadic)
        {
            if (namedArgCount > 0)
            {
                ret += L", ";
            }

            ret += L"...";
        }

        ret += L")";

        // Const / volatile qualifiers of the member function
        if (functionType)
        {
            if (TypeWalker::isConstMemberFunction(functionType.get()))
            {
                ret += L" const";
            }

            if (TypeWalker::isVolatileMemberFunction(functionType.get()))
            {
                ret += L" volatile";
            }
        }

        // NOTE: noexcept support
        {
            IDiaSymbol4* symbol4 = nullptr;
            if (SUCCEEDED(a_symbol->QueryInterface(__uuidof(IDiaSymbol4), (void**)&symbol4))
                && symbol4)
            {
                BOOL isNoExcept = FALSE;
                if (SUCCEEDED(symbol4->get_noexcept(&isNoExcept)) && isNoExcept)
                {
                    ret += L" noexcept";
                }
                symbol4->Release();
            }
        }

        BOOL isVirtual = FALSE;
        a_symbol->get_virtual(&isVirtual);
        if (isVirtual)
        {
            BOOL isIntro = TRUE; // TRUE = new, FALSE = override
            a_symbol->get_intro(&isIntro);

            BOOL isSealed = FALSE;
            a_symbol->get_sealed(&isSealed);

            if (!isIntro)
            {
                ret += L" override";
            }

            if (isSealed)
            {
                ret += L" final";
            }

            // pure virtual
            BOOL isPure = FALSE;
            a_symbol->get_pure(&isPure);
            if (isPure)
            {
                ret += L" = 0";
            }
        }

        ret += L";";

        m_host.registerTypeSource(a_symbol);

        return ret;
    }

    std::wstring dumpFriend(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;
        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.modPrefix(a_symbol);
        ret += L"friend ";

        std::wstring typeText;
        try
        {
            typeText
                = TypeWalker::resolveType(a_symbol, m_ctx.scope(), true, m_ctx.config().m_intStyle)
                      .build();
        }
        catch (...)
        {
            typeText = L"/* <error resolving type> */";
        }
        ret += typeText;
        ret += L";\n";
        return ret;
    }

    /// Appends named parameter types to aout; returns count of named params found.
    int dumpFunctionArgsToString(IDiaSymbol* a_symbol, std::wstring& aout)
    {
        int count = 0;
        bool isFirst = true;

        ComPtr<IDiaEnumSymbols> enum_symbolsParams;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, nullptr, nsNone, &enum_symbolsParams)))
        {
            ComPtr<IDiaSymbol> param;
            ULONG fetched = 0;
            while (SUCCEEDED(enum_symbolsParams->Next(1, &param, &fetched)) && fetched == 1)
            {
                DWORD _kind = 0;
                if (SUCCEEDED(param->get_dataKind(&_kind)) && _kind == DataIsParam)
                {
                    ++count;
                    if (!isFirst)
                    {
                        aout += L", ";
                    }

                    try
                    {
                        aout += TypeWalker::resolveType(
                            param.get(), m_ctx.scope(), true, m_ctx.config().m_intStyle)
                                    .build();
                    }
                    catch (...)
                    {
                        aout += L"/* <error> */";
                    }

                    isFirst = false;
                }
            }
        }

        return count;
    }

    /// Check if a function symbol is compiler-generated.
    static bool isCompilerGenerated(IDiaSymbol* a_symbol)
    {
        BOOL isGen = FALSE;
        if (SUCCEEDED(a_symbol->get_compilerGenerated(&isGen)) && isGen)
            return true;
        return false;
    }

    /// Returns true when a function name denotes a C++ conversion function
    /// (`operator bool`, `operator int`, `operator const char *`, ...).
    /// Symbolic operators (`operator+`, `operator[]`, ...) and the allocation
    /// operators `new`/`delete` are NOT conversions — they keep their return type.
    static bool isConversionOperator(const std::wstring& a_name)
    {
        static const std::wstring keyword = L"operator";

        // The keyword is either at the very start of the name or preceded by a
        // scope separator (member functions may arrive scope-qualified).
        size_t pos = 0;
        while ((pos = a_name.find(keyword, pos)) != std::wstring::npos)
        {
            const bool atStart = (pos == 0);
            const bool afterScope = pos >= 2 && a_name[pos - 1] == L':' && a_name[pos - 2] == L':';
            if (atStart || afterScope)
            {
                break;
            }
            pos += keyword.size();
        }

        if (pos == std::wstring::npos)
        {
            return false;
        }

        size_t i = pos + keyword.size();
        while (i < a_name.size() && iswspace(a_name[i]))
        {
            ++i;
        }

        if (i >= a_name.size())
        {
            return false;
        }

        // A symbolic token (`+`, `[]`, `()`, `->`, `""`, ...) — a real operator.
        if (a_name[i] == L'+' || a_name[i] == L'-' || a_name[i] == L'*' || a_name[i] == L'/'
            || a_name[i] == L'%' || a_name[i] == L'^' || a_name[i] == L'&' || a_name[i] == L'|'
            || a_name[i] == L'~' || a_name[i] == L'!' || a_name[i] == L'=' || a_name[i] == L'<'
            || a_name[i] == L'>' || a_name[i] == L'[' || a_name[i] == L'(' || a_name[i] == L','
            || a_name[i] == L'"' || a_name[i] == L'?')
        {
            return false;
        }

        // Identifier-starting operators: allocation and coroutine keywords.
        static const wchar_t* kNonConversionKeywords[]
            = {L"new", L"delete", L"co_await", L"co_return", L"co_yield"};
        for (const wchar_t* keyWord : kNonConversionKeywords)
        {
            const size_t length = wcslen(keyWord);
            if (a_name.compare(i, length, keyWord) != 0)
            {
                continue;
            }

            const size_t after = i + length;
            if (after >= a_name.size())
            {
                return false; // plain "new" / "delete"
            }

            // "new[]" / "delete[]" or the parameter list — but not "newtype".
            if (a_name[after] == L'[' || a_name[after] == L'(' || iswspace(a_name[after]))
            {
                return false;
            }
        }

        return true;
    }

    static const wchar_t* getVirtualName(IDiaSymbol* a_symbol)
    {
        BOOL isVirt;
        return SUCCEEDED(a_symbol->get_virtual(&isVirt)) && isVirt ? L"virtual" : nullptr;
    }

    static const wchar_t* getStaticName(IDiaSymbol* a_symbol)
    {
        return TypeWalker::isStaticMemberFunction(a_symbol) ? L"static" : nullptr;
    }

    static ComPtr<IDiaSymbol> getTypeCom(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> type;
        if (SUCCEEDED(a_symbol->get_type(&type)))
            return type;
        return ComPtr<IDiaSymbol>();
    }

private:

    DumpContext& m_ctx;
    DumpFormatter& m_fmt;
    IDumpCoordinator& m_host;
};
