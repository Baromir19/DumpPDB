#pragma once

#include <algorithm>
#include <cstdint>
#include <dia2.h>
#include <string>
#include <utility>
#include <vector>

#include <Core/DIA/TypeBuilder.hpp>

#include <Core/Util/Com/ComPtr.hpp>

/// Integer style for base type names.
enum class IntStyle : std::uint8_t
{
    MsvcNative, ///< __int32, __int64, etc.
    Cstdint     ///< int32_t, int64_t, etc.
};

inline bool isValidIntStyle(long a_value) noexcept
{
    return a_value >= static_cast<long>(IntStyle::MsvcNative)
           && a_value <= static_cast<long>(IntStyle::Cstdint);
}

/// Tracks the current nested class/struct hierarchy for scope-prefix stripping.
struct ScopeContext
{
    std::vector<std::wstring> m_parts;

    void push(const std::wstring& a_name)
    {
        m_parts.push_back(a_name);
    }

    void pop()
    {
        if (!m_parts.empty())
        {
            m_parts.pop_back();
        }
    }

    [[nodiscard]] std::wstring top() const
    {
        return m_parts.empty() ? L"" : m_parts.back();
    }

    [[nodiscard]] std::wstring full() const
    {
        std::wstring result;

        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            if (i > 0)
            {
                result += L"::";
            }

            result += m_parts[i];
        }

        return result;
    }

    [[nodiscard]] bool empty() const
    {
        return m_parts.empty();
    }
};

/// A fully-qualified name split into namespace path + leaf name.
struct QualifiedName
{
    std::wstring ns;   ///< e.g. L"A::B" for L"A::B::Hello"; empty for top-level.
    std::wstring leaf; ///< e.g. L"Hello".
};

/// Walks IDiaSymbol trees and builds TypeBuilder chains.
class TypeWalker
{
public:

    static std::vector<std::wstring> splitQualifiedName(std::wstring_view a_name)
    {
        std::vector<std::wstring> result;

        size_t begin = 0;
        int templateDepth = 0;

        for (size_t i = 0; i < a_name.size(); ++i)
        {
            switch (a_name[i])
            {
            case L'<':
                ++templateDepth;
                break;

            case L'>':
                if (templateDepth > 0)
                    --templateDepth;
                break;

            case L':':
                if (templateDepth == 0 && i + 1 < a_name.size() && a_name[i + 1] == L':')
                {
                    result.emplace_back(a_name.substr(begin, i - begin));
                    ++i;
                    begin = i + 1;
                }
                break;
            }
        }

        result.emplace_back(a_name.substr(begin));
        return result;
    }

    static bool isTopLevelSymbol(IDiaSymbol* a_symbol)
    {
        if (!a_symbol)
            return false;

        BSTR rawName = nullptr;

        if (FAILED(a_symbol->get_name(&rawName)) || !rawName)
            return false;

        std::wstring name(rawName);
        SysFreeString(rawName);

        const auto parts = splitQualifiedName(name);

        if (parts.size() < 2)
            return true;

        std::wstring parentName;

        for (size_t i = 0; i + 1 < parts.size(); ++i)
        {
            if (!parentName.empty())
                parentName += L"::";

            parentName += parts[i];
        }

        ComPtr<IDiaSymbol> root;

        if (FAILED(a_symbol->get_lexicalParent(&root)) || !root)
            return false;

        ComPtr<IDiaEnumSymbols> children;

        if (FAILED(root->findChildren(SymTagUDT, parentName.c_str(), nsCaseSensitive, &children)))
        {
            return true;
        }

        ULONG count = 0;
        ComPtr<IDiaSymbol> parent;

        return FAILED(children->Next(1, &parent, &count)) || count == 0;
    }

    /// MSVC DIA name tag for an anonymous namespace.
    inline static const wchar_t* kAnonymousNamespace = L"`anonymous-namespace'";

    static bool isAnonymousNamespacePart(const std::wstring& a_part)
    {
        return a_part == kAnonymousNamespace;
    }

    /// Find the index of the last "::" not nested inside template/function/array brackets.
    /// Returns npos if there is none.
    static size_t findLastTopLevelSeparator(const std::wstring& a_name)
    {
        size_t last = std::wstring::npos;
        int depth = 0;

        for (size_t i = 0; i < a_name.size(); ++i)
        {
            switch (a_name[i])
            {
            case L'<':
            case L'(':
            case L'[':
                ++depth;
                break;

            case L'>':
            case L')':
            case L']':
                if (depth > 0)
                    --depth;
                break;

            case L':':
                if (depth == 0 && i + 1 < a_name.size() && a_name[i + 1] == L':')
                {
                    last = i;
                    ++i;
                }
                break;

            default:
                break;
            }
        }
        return last;
    }

    /// Number of top-level namespace parts in a fully-qualified namespace path.
    static size_t namespacePartCount(const std::wstring& a_namespace)
    {
        return splitQualifiedName(a_namespace).size();
    }

    /// Emit the opening lines of a namespace block.
    /// Anonymous-namespace parts are emitted as "namespace { ... }".
    static std::wstring namespaceBlockOpen(const std::wstring& a_namespace)
    {
        std::wstring ret;
        for (const auto& part : splitQualifiedName(a_namespace))
        {
            ret += L"namespace";
            if (!isAnonymousNamespacePart(part))
            {
                ret += L" ";
                ret += part;
            }
            ret += L"\n{\n";
        }
        return ret;
    }

    /// Emit the matching closing braces for namespaceBlockOpen().
    static std::wstring namespaceBlockClose(const std::wstring& a_namespace)
    {
        std::wstring ret;
        for (size_t i = 0; i < namespacePartCount(a_namespace); ++i)
        {
            ret += L"}\n";
        }
        return ret;
    }

    /// User-defined template instantiation for a requested name, e.g.
    /// "Type<float, 11, TB::HighRes>" -> "template<typename T, size_t U, typename V>".
    struct TemplateInstantiation
    {
        bool active = false;
        std::wstring decl;
        std::vector<std::pair<std::wstring, std::wstring>>
            replacements; ///< { concrete arg, param name }
    };

    static bool isWhitespace(wchar_t a_ch)
    {
        return a_ch == L' ' || a_ch == L'\t' || a_ch == L'\r' || a_ch == L'\n';
    }

    static std::wstring trim(const std::wstring& a_text)
    {
        size_t begin = 0;
        while (begin < a_text.size() && isWhitespace(a_text[begin]))
            ++begin;
        size_t end = a_text.size();
        while (end > begin && isWhitespace(a_text[end - 1]))
            --end;
        return a_text.substr(begin, end - begin);
    }

    /// Split on a_delim, ignoring delimiters nested inside template/function/array brackets.
    static std::vector<std::wstring> splitTopLevel(const std::wstring& a_text, wchar_t a_delim)
    {
        std::vector<std::wstring> result;
        size_t begin = 0;
        int depth = 0;

        for (size_t i = 0; i < a_text.size(); ++i)
        {
            switch (a_text[i])
            {
            case L'<':
            case L'(':
            case L'[':
                ++depth;
                break;
            case L'>':
            case L')':
            case L']':
                if (depth > 0)
                    --depth;
                break;
            default:
                break;
            }

            if (a_text[i] == a_delim && depth == 0)
            {
                result.push_back(a_text.substr(begin, i - begin));
                begin = i + 1;
            }
        }

        result.push_back(a_text.substr(begin));
        return result;
    }

    static std::wstring templateParamName(size_t a_index)
    {
        const wchar_t* letters = L"TUVWXYZ";
        if (a_index < 7)
            return std::wstring(1, letters[a_index]);
        return L"_" + std::to_wstring(a_index + 1);
    }

    /// True if the text is a plain base-10 integer literal (optionally signed).
    static bool isIntegerLiteral(const std::wstring& a_text)
    {
        size_t i = 0;
        if (i < a_text.size() && (a_text[i] == L'-' || a_text[i] == L'+'))
            ++i;
        if (i >= a_text.size())
            return false;
        for (; i < a_text.size(); ++i)
        {
            if (a_text[i] < L'0' || a_text[i] > L'9')
                return false;
        }
        return true;
    }

    /// Build a TemplateInstantiation from a requested template-instantiation name.
    /// Returns an inactive struct when a_name has no template argument list.
    static TemplateInstantiation makeTemplateInstantiation(const std::wstring& a_name)
    {
        TemplateInstantiation ti;

        auto lt = a_name.find(L'<');
        if (lt == std::wstring::npos)
            return ti;

        int depth = 0;
        size_t gt = std::wstring::npos;
        for (size_t i = lt; i < a_name.size(); ++i)
        {
            if (a_name[i] == L'<')
            {
                ++depth;
                continue;
            }
            if (a_name[i] == L'>')
            {
                --depth;
                if (depth == 0)
                {
                    gt = i;
                    break;
                }
            }
        }
        if (gt == std::wstring::npos)
            return ti;

        const std::wstring inner = a_name.substr(lt + 1, gt - lt - 1);
        auto args = splitTopLevel(inner, L',');
        if (args.empty())
            return ti;

        ti.decl = L"template<";
        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::wstring arg = trim(args[i]);
            if (i > 0)
                ti.decl += L", ";
            const std::wstring param = templateParamName(i);
            if (isIntegerLiteral(arg))
                ti.decl += (arg[0] == L'-') ? L"int " : L"size_t ";
            else
                ti.decl += L"typename ";
            ti.decl += param;

            if (!arg.empty())
                ti.replacements.emplace_back(arg, param);
        }
        ti.decl += L">";

        // Replace longer (more specific) arguments first to avoid partial overlaps.
        std::sort(ti.replacements.begin(),
            ti.replacements.end(),
            [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

        ti.active = true;
        return ti;
    }

    static bool isIdentifierContinuation(wchar_t ach)
    {
        return (ach >= L'a' && ach <= L'z') || (ach >= L'A' && ach <= L'Z')
               || (ach >= L'0' && ach <= L'9') || ach == L'_';
    }

    /// Replace concrete template arguments with their generated parameter names.
    /// Matches only whole tokens. Lines beginning with "// reconstructed by " are
    /// emitted verbatim so the original argument list stays visible in comments.
    static std::wstring substituteTemplateArgs(
        const std::wstring& a_text, const TemplateInstantiation& a_ti)
    {
        static const std::wstring kReconPrefix = L"// reconstructed by ";

        std::wstring result;
        size_t pos = 0;
        while (pos < a_text.size())
        {
            const size_t eol = a_text.find(L'\n', pos);
            const bool hasNewline = eol != std::wstring::npos;
            const size_t end = hasNewline ? eol : a_text.size();

            std::wstring line = a_text.substr(pos, end - pos);

            if (line.compare(0, kReconPrefix.size(), kReconPrefix) != 0)
            {
                for (const auto& [from, to] : a_ti.replacements)
                {
                    if (from.empty())
                        continue;

                    std::wstring next;
                    size_t lp = 0;
                    while (lp < line.size())
                    {
                        const size_t found = line.find(from, lp);
                        if (found == std::wstring::npos)
                        {
                            next += line.substr(lp);
                            break;
                        }

                        const size_t hitEnd = found + from.size();
                        const bool boundaryBefore
                            = (found == 0) || !isIdentifierContinuation(line[found - 1]);
                        const bool boundaryAfter
                            = (hitEnd >= line.size()) || !isIdentifierContinuation(line[hitEnd]);

                        if (boundaryBefore && boundaryAfter)
                        {
                            next += line.substr(lp, found - lp);
                            next += to;
                            lp = hitEnd;
                        }
                        else
                        {
                            next += line.substr(lp, found - lp + 1);
                            lp = found + 1;
                        }
                    }
                    line = std::move(next);
                }
            }

            result += line;
            if (!hasNewline)
                break;
            result += L'\n';
            pos = eol + 1;
        }

        return result;
    }

    /// Returns true if the symbol's lexical parent is a UDT (i.e. the symbol is nested).
    static bool isNestedType(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> parent;
        if (FAILED(a_symbol->get_lexicalParent(&parent)) || !parent)
            return false;

        DWORD tag = SymTagNull;
        parent->get_symTag((DWORD*)&tag);
        return tag == SymTagUDT;
    }

    /// Parses a fully-qualified name string into namespace path + leaf name.
    /// Template-aware: a "::" inside "<...>" is not treated as a scope separator.
    static QualifiedName parseQualifiedName(const std::wstring& a_fullyQualifiedName)
    {
        QualifiedName result;

        auto lastSep = findLastTopLevelSeparator(a_fullyQualifiedName);
        if (lastSep == std::wstring::npos)
        {
            result.leaf = a_fullyQualifiedName;
            return result;
        }

        result.ns = a_fullyQualifiedName.substr(0, lastSep);
        result.leaf = a_fullyQualifiedName.substr(lastSep + 2);
        return result;
    }

    /// Parses a fully-qualified name from a DIA symbol into namespace path + leaf name.
    /// Only call this when isTopLevelSymbol(a_symbol) is true.
    static QualifiedName parseQualifiedName(IDiaSymbol* a_symbol)
    {
        QualifiedName result;

        BSTR bstrName = nullptr;
        if (FAILED(a_symbol->get_name(&bstrName)) || !bstrName)
            return result;

        std::wstring fullName(bstrName);
        SysFreeString(bstrName);

        return parseQualifiedName(fullName);
    }

    /// Get the base type name for a SymTagBaseType symbol.
    static const wchar_t* getBaseTypeName(
        IDiaSymbol* a_symbol, IntStyle a_intStyle = IntStyle::MsvcNative)
    {
        DWORD baseType = 0;
        ULONGLONG length = 0;

        if (SUCCEEDED(a_symbol->get_baseType(&baseType)))
        {
            a_symbol->get_length(&length);

            switch (baseType)
            {
            case btCurrency:
                return L"CY";
            case btDate:
                return L"DATE";
            case btVariant:
                return L"VARIANT";
            case btComplex:
                return L"std::complex";
            case btBSTR:
                return L"BSTR";
            case btHresult:
                return L"HRESULT";

            case btChar16:
                return L"char16_t";
            case btChar32:
                return L"char32_t";
            case btChar8:
                return L"char8_t";

            case btVoid:
                return L"void";

            case btFloat:
                switch (length)
                {
                case 4:
                    return L"float";
                case 8:
                    return L"double";
                case 0x10:
                    return L"long double";
                default:
                    return L"float";
                }

            case btBool:
                return L"bool";
            case btChar:
                return L"char";
            case btWChar:
                return L"wchar_t";

            case btInt:
                switch (length)
                {
                case 1:
                    return a_intStyle == IntStyle::Cstdint ? L"int8_t" : L"__int8";
                case 2:
                    return a_intStyle == IntStyle::Cstdint ? L"int16_t" : L"__int16";
                case 4:
                    return a_intStyle == IntStyle::Cstdint ? L"int32_t" : L"__int32";
                case 8:
                    return a_intStyle == IntStyle::Cstdint ? L"int64_t" : L"__int64";
                default:
                    return L"int";
                }

            case btUInt:
                switch (length)
                {
                case 1:
                    return a_intStyle == IntStyle::Cstdint ? L"uint8_t" : L"unsigned __int8";
                case 2:
                    return a_intStyle == IntStyle::Cstdint ? L"uint16_t" : L"unsigned __int16";
                case 4:
                    return a_intStyle == IntStyle::Cstdint ? L"uint32_t" : L"unsigned __int32";
                case 8:
                    return a_intStyle == IntStyle::Cstdint ? L"uint64_t" : L"unsigned __int64";
                default:
                    return L"unsigned int";
                }

            case btLong:
                return L"long";
            case btULong:
                return L"unsigned long";

            case btBCD:
            case btBit:
            case btNoType:
            default:
                break;
            }
        }
        return nullptr;
    }

    static const wchar_t* getUDTKindName(IDiaSymbol* a_symbol)
    {
        DWORD _udt = 0;
        if (SUCCEEDED(a_symbol->get_udtKind(&_udt)))
        {
            switch (_udt)
            {
            case UdtStruct:
                return L"struct";
            case UdtClass:
                return L"class";
            case UdtUnion:
                return L"union";
            case UdtInterface:
                return L"interface";
            default:
                return nullptr;
            }
        }
        return nullptr;
    }

    /// Build a TypeBuilder chain by recursively walking the DIA type tree.
    /// @param a_stripScope  When true, strips the current scope prefix from names.
    static TypeBuilder resolveType(IDiaSymbol* a_symbol,
        const ScopeContext& a_scope = ScopeContext(),
        bool a_stripScope = true,
        IntStyle a_intStyle = IntStyle::MsvcNative)
    {
        TypeBuilder builder;

        if (!a_symbol)
            return builder;

        DWORD symTag = SymTagNull;
        a_symbol->get_symTag((DWORD*)&symTag);

        BOOL isConst = FALSE;
        BOOL isVolatile = FALSE;
        a_symbol->get_constType(&isConst);
        a_symbol->get_volatileType(&isVolatile);

        std::wstring name = getName(a_symbol, a_scope, a_stripScope);

        ComPtr<IDiaSymbol> subType;
        if (SUCCEEDED(a_symbol->get_type(&subType)))
        {
            TypeBuilder subBuilder = resolveType(subType.get(), a_scope, a_stripScope, a_intStyle);
            builder = std::move(subBuilder);
        }

        switch (symTag)
        {
        case SymTagBaseType:
        {
            if (auto baseName = getBaseTypeName(a_symbol, a_intStyle))
            {
                builder.base(baseName);
            }

            if (isConst)
                builder.constQual();
            if (isVolatile)
                builder.volatileQual();
            break;
        }

        case SymTagPointerType:
        {
            BOOL isRef = FALSE;
            BOOL isRVRef = FALSE;
            a_symbol->get_reference(&isRef);
            a_symbol->get_RValueReference(&isRVRef);

            if (isRVRef)
            {
                builder.rvalueReference();
            }
            else if (isRef)
            {
                builder.reference();
            }
            else
            {
                builder.pointer();
            }

            if (isConst)
            {
                builder.constPointer();
            }
            if (isVolatile)
            {
                builder.volatilePointer();
            }
            break;
        }

        case SymTagArrayType:
        {
            DWORD count = 0;
            if (SUCCEEDED(a_symbol->get_count(&count)) && count != 0xFFFFFFFC)
            {
                builder.array(count);
            }
            else
            {
                builder.array(0);
            }
            break;
        }

        case SymTagFunctionType:
        {
            std::wstring args = getFuncArgsString(a_symbol, a_scope, a_stripScope);
            builder.function(std::move(args));
            break;
        }

        case SymTagData:
        {
            if (!name.empty())
            {
                builder.name(name);
            }

            DWORD bitPos = 0;
            ULONGLONG bitLen = 0;
            if (SUCCEEDED(a_symbol->get_bitPosition(&bitPos))
                && SUCCEEDED(a_symbol->get_length(&bitLen)) && bitLen > 0 && bitLen != MAXULONGLONG)
            {
                builder.bitField(bitPos, bitLen);
            }
            break;
        }

        case SymTagUDT:
        case SymTagEnum:
        {
            std::wstring typeBase = (symTag == SymTagEnum)
                                        ? TypeWalker::prettyTypeName(name, L"Enum")
                                        : TypeWalker::prettyTypeName(name);

            if (!typeBase.empty())
            {
                builder.base(typeBase);
            }

            if (isConst)
            {
                builder.constQual();
            }
            if (isVolatile)
            {
                builder.volatileQual();
            }
            break;
        }

        case SymTagTypedef:
        {
            if (!name.empty())
            {
                builder.base(name);
            }
            if (isConst)
            {
                builder.constQual();
            }
            if (isVolatile)
            {
                builder.volatileQual();
            }
            break;
        }

        default:
            break;
        }

        return builder;
    }

    /// True when a function-type argument symbol is the "..." (ellipsis) marker.
    ///
    /// MSVC encodes a variadic function by appending one extra argument to the
    /// function type whose type is a base type with `btNoType` (a "no type" record).
    /// DIA reports it as an ordinary SymTagFunctionArgType child whose type has no
    /// name and no length, so it must be detected explicitly to be rendered as "...".
    static bool isVariadicMarker(IDiaSymbol* a_argSymbol)
    {
        if (!a_argSymbol)
            return false;

        ComPtr<IDiaSymbol> argType;
        if (FAILED(a_argSymbol->get_type(&argType)) || !argType)
            return false;

        DWORD tag = SymTagNull;
        if (FAILED(argType->get_symTag(&tag)) || tag != SymTagBaseType)
            return false;

        DWORD baseType = 0;
        if (FAILED(argType->get_baseType(&baseType)) || baseType != btNoType)
            return false;

        ULONGLONG length = 0;
        if (SUCCEEDED(argType->get_length(&length)) && length != 0)
            return false;

        return true;
    }

    /// True when the given function type carries a variadic ("...") argument.
    static bool isVariadicFunction(IDiaSymbol* a_functionType)
    {
        if (!a_functionType)
            return false;

        ComPtr<IDiaEnumSymbols> enum_args;
        if (FAILED(a_functionType->findChildren(SymTagFunctionArgType, nullptr, nsNone, &enum_args))
            || !enum_args)
        {
            return false;
        }

        ComPtr<IDiaSymbol> arg;
        ULONG celt = 0;
        while (SUCCEEDED(enum_args->Next(1, &arg, &celt)) && celt == 1)
        {
            if (isVariadicMarker(arg.get()))
            {
                return true;
            }
        }

        return false;
    }

    /// Number of declared arguments of a function type, ignoring the "..." marker.
    static DWORD countFunctionArgs(IDiaSymbol* a_functionType)
    {
        if (!a_functionType)
            return 0;

        DWORD count = 0;

        ComPtr<IDiaEnumSymbols> enum_args;
        if (FAILED(a_functionType->findChildren(SymTagFunctionArgType, nullptr, nsNone, &enum_args))
            || !enum_args)
        {
            return count;
        }

        ComPtr<IDiaSymbol> arg;
        ULONG celt = 0;
        while (SUCCEEDED(enum_args->Next(1, &arg, &celt)) && celt == 1)
        {
            if (!isVariadicMarker(arg.get()))
            {
                ++count;
            }
        }

        return count;
    }

    /// True when the symbol belongs to a class/struct (i.e. has a named class parent).
    static bool hasClassParent(IDiaSymbol* a_symbol)
    {
        if (!a_symbol)
            return false;

        ComPtr<IDiaSymbol> classParent;
        if (FAILED(a_symbol->get_classParent(&classParent)) || !classParent)
            return false;

        BSTR rawName = nullptr;
        const bool hasName
            = SUCCEEDED(classParent->get_name(&rawName)) && rawName && rawName[0] != L'\0';

        if (rawName)
        {
            SysFreeString(rawName);
        }

        return hasName;
    }

    /// True when a function type declares the implicit object ("this") pointer.
    /// Non-static member functions have one; free and static member functions do not.
    static bool hasObjectPointer(IDiaSymbol* a_functionType)
    {
        if (!a_functionType)
            return false;

        ComPtr<IDiaSymbol> objectPointer;
        return SUCCEEDED(a_functionType->get_objectPointerType(&objectPointer)) && objectPointer;
    }

    /// The class the object ("this") pointer of a member function points to.
    /// The returned symbol carries the cv-qualifiers of the member function.
    /// Empty for free and static member functions.
    static ComPtr<IDiaSymbol> objectPointerClass(IDiaSymbol* a_functionType)
    {
        ComPtr<IDiaSymbol> result;

        if (!a_functionType)
            return result;

        ComPtr<IDiaSymbol> objectPointer;
        if (FAILED(a_functionType->get_objectPointerType(&objectPointer)) || !objectPointer)
            return result;

        ComPtr<IDiaSymbol> target;
        if (SUCCEEDED(objectPointer->get_type(&target)))
        {
            result = std::move(target);
        }

        return result;
    }

    /// True for a member function that is const-qualified.
    ///
    /// MSVC stores the cv-qualifiers of a member function on the implicit object
    /// pointer type, so IDiaSymbol::get_constType() must be queried on the class the
    /// object pointer refers to (the function type itself always reports false).
    static bool isConstMemberFunction(IDiaSymbol* a_functionType)
    {
        auto objectClass = objectPointerClass(a_functionType);

        BOOL isConst = FALSE;
        return objectClass && SUCCEEDED(objectClass->get_constType(&isConst)) && isConst;
    }

    /// True for a member function that is volatile-qualified.
    static bool isVolatileMemberFunction(IDiaSymbol* a_functionType)
    {
        auto objectClass = objectPointerClass(a_functionType);

        BOOL isVolatile = FALSE;
        return objectClass && SUCCEEDED(objectClass->get_volatileType(&isVolatile)) && isVolatile;
    }

    /// True for a static member function.
    ///
    /// IDiaSymbol::get_isStatic() is not set for C++ static member functions, but such
    /// a function is still a class member while its function type has no implicit
    /// object pointer — that combination identifies it unambiguously.
    static bool isStaticMemberFunction(IDiaSymbol* a_functionSymbol)
    {
        if (!a_functionSymbol)
            return false;

        BOOL isStatic = FALSE;
        if (SUCCEEDED(a_functionSymbol->get_isStatic(&isStatic)) && isStatic)
        {
            return true;
        }

        if (!hasClassParent(a_functionSymbol))
        {
            return false;
        }

        ComPtr<IDiaSymbol> functionType;
        if (FAILED(a_functionSymbol->get_type(&functionType)) || !functionType)
        {
            return false;
        }

        return !hasObjectPointer(functionType.get());
    }

    /// Get function arguments as a comma-separated string.
    static std::wstring getFuncArgsString(IDiaSymbol* a_symbol,
        const ScopeContext& a_scope = ScopeContext(),
        bool a_stripScope = true,
        IntStyle a_intStyle = IntStyle::MsvcNative)
    {
        std::wstring result;
        bool isFirst = true;

        ComPtr<IDiaEnumSymbols> enum_symbolsParams;
        if (SUCCEEDED(
                a_symbol->findChildren(SymTagFunctionArgType, nullptr, nsNone, &enum_symbolsParams))
            && enum_symbolsParams)
        {
            ComPtr<IDiaSymbol> child;
            ULONG celt = 0;
            while (SUCCEEDED(enum_symbolsParams->Next(1, &child, &celt)) && celt == 1)
            {
                if (isVariadicMarker(child.get()))
                {
                    if (!isFirst)
                    {
                        result += L", ";
                    }

                    result += L"...";
                    isFirst = false;
                    continue;
                }

                ComPtr<IDiaSymbol> _argType;
                if (SUCCEEDED(child->get_type(&_argType)) && _argType)
                {
                    if (!isFirst)
                    {
                        result += L", ";
                    }

                    result
                        += resolveType(_argType.get(), a_scope, a_stripScope, a_intStyle).build();
                    isFirst = false;
                }
            }
        }

        return result;
    }

    /// Returns true if a_name is a compiler-generated synthetic name.
    static bool isSyntheticName(const std::wstring& a_name)
    {
        return a_name.empty() || a_name == L"<unnamed-tag>"
               || (!a_name.empty() && a_name.front() == L'$');
    }

    /// Convert an MSVC-generated anonymous/synthetic type name to a friendly C++ identifier.
    ///
    /// - L"<undefined-type>" / L"<unnamed-tag>" -> empty (caller drops the name).
    /// - L"<unnamed-type-m_Member>"             -> strips wrapper and Hungarian prefix (m_/s_/g_);
    ///   appends a_kindSuffix when supplied (e.g. L"Enum").
    /// - L"$..."-prefixed                        -> empty (compiler hash).
    /// - Anything else                           -> returned unchanged.
    static std::wstring prettyTypeName(
        const std::wstring& a_name, const wchar_t* a_kindSuffix = nullptr)
    {
        if (a_name.empty())
            return a_name;

        if (a_name == L"<unnamed-tag>" || a_name == L"<undefined-type>")
            return L"";

        if (a_name.front() == L'$')
            return L"";

        const wchar_t kAnonPrefix[] = L"<unnamed-type-";
        constexpr size_t kAnonPrefixLen = (sizeof(kAnonPrefix) / sizeof(kAnonPrefix[0])) - 1;
        if (a_name.compare(0, kAnonPrefixLen, kAnonPrefix) == 0)
        {
            std::wstring inner = a_name.substr(kAnonPrefixLen);
            if (!inner.empty() && inner.back() == L'>')
                inner.pop_back();

            static const wchar_t* memberPrefixes[] = {L"m_", L"s_", L"g_"};

            for (const auto* pfx : memberPrefixes)
            {
                if (inner.compare(0, 2, pfx) == 0)
                {
                    inner.erase(0, 2);
                    break;
                }
            }

            if (a_kindSuffix != nullptr)
                inner += a_kindSuffix;

            return inner;
        }

        return a_name;
    }

    /// Get the name of a symbol, optionally stripping the current scope prefix.
    static std::wstring getName(IDiaSymbol* a_symbol,
        const ScopeContext& a_scope = ScopeContext(),
        bool a_stripScope = true)
    {
        BSTR bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName)
        {
            std::wstring name(bstrName);
            SysFreeString(bstrName);

            if (a_stripScope && !a_scope.empty())
            {
                const std::wstring scope = a_scope.full();
                const std::wstring prefix = scope + L"::";

                if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
                {
                    return name.substr(prefix.size());
                }
            }

            return name;
        }
        return L"";
    }

    static std::wstring leafName(const std::wstring& a_qualifiedName)
    {
        auto pos = findLastTopLevelSeparator(a_qualifiedName);
        return (pos == std::wstring::npos) ? a_qualifiedName : a_qualifiedName.substr(pos + 2);
    }

    /// Returns true if the symbol is an anonymous union/struct or has a synthetic name.
    static bool isAnonymousUDT(IDiaSymbol* a_symbol)
    {
        BSTR bstrName = nullptr;
        std::wstring name;
        bool gotName = SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName;
        if (gotName)
        {
            name = bstrName;
            SysFreeString(bstrName);
        }

        if (!gotName || isSyntheticName(name))
        {
            DWORD symTag = SymTagNull;
            a_symbol->get_symTag((DWORD*)&symTag);

            if (symTag != SymTagUDT)
                return false;

            DWORD udtKind = 0;
            a_symbol->get_udtKind(&udtKind);
            return (udtKind == UdtStruct || udtKind == UdtUnion);
        }

        return false;
    }

    static const wchar_t* getAccessName(IDiaSymbol* a_symbol, DWORD abaseAccessType = 0)
    {
        DWORD access = 0;
        if (SUCCEEDED(a_symbol->get_access(&access)))
        {
            if (abaseAccessType)
            {
                access = abaseAccessType;
            }

            switch (access)
            {
            case CV_private:
                return L"private";
            case CV_protected:
                return L"protected";
            case CV_public:
                return L"public";
            default:
                return nullptr;
            }
        }
        return nullptr;
    }

    enum class CallingConvention : std::uint8_t
    {
        Unknown,
        Cdecl,
        Fastcall,
        Stdcall,
        Thiscall,
        Syscall,
        Clrcall
    };

    static CallingConvention getCallingConvention(IDiaSymbol* a_symbol)
    {
        DWORD _cc = 0;
        if (SUCCEEDED(a_symbol->get_callingConvention(&_cc)))
        {
            switch (_cc)
            {
            case CV_CALL_NEAR_C:
                return CallingConvention::Cdecl;
            case CV_CALL_NEAR_FAST:
                return CallingConvention::Fastcall;
            case CV_CALL_NEAR_STD:
                return CallingConvention::Stdcall;
            case CV_CALL_NEAR_SYS:
                return CallingConvention::Syscall;
            case CV_CALL_THISCALL:
                return CallingConvention::Thiscall;
            case CV_CALL_CLRCALL:
                return CallingConvention::Clrcall;
            default:
                return CallingConvention::Unknown;
            }
        }
        return CallingConvention::Unknown;
    }

    static const wchar_t* renderCallingConvention(CallingConvention a_cc)
    {
        switch (a_cc)
        {
        case CallingConvention::Cdecl:
            return L"__cdecl";
        case CallingConvention::Fastcall:
            return L"__fastcall";
        case CallingConvention::Stdcall:
            return L"__stdcall";
        case CallingConvention::Syscall:
            return L"__syscall";
        case CallingConvention::Thiscall:
            return L"__thiscall";
        case CallingConvention::Clrcall:
            return L"__clrcall";
        default:
            return nullptr;
        }
    }
};
