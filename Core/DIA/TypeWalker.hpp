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
    MsvcNative, // __int32, __int64, etc.
    Cstdint     // int32_t, int64_t, etc.
};

inline bool isValidIntStyle(long a_value) noexcept
{
    return a_value >= static_cast<long>(IntStyle::MsvcNative)
           && a_value <= static_cast<long>(IntStyle::Cstdint);
}

/// Scope context that tracks the current nested class/struct hierarchy.
/// Used to strip the current scope prefix from DIA symbol names.
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
    std::wstring ns;   // e.g. L"A::B" for L"A::B::Hello"; empty for L"Hello"
    std::wstring leaf; // e.g. L"Hello" for L"A::B::Hello"
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

/// The MSVC DIA name tag for an anonymous (unnamed) namespace.
    inline static const wchar_t* kAnonymousNamespace = L"`anonymous-namespace'";

    /// True when a single namespace part is MSVC's anonymous-namespace marker.
    static bool isAnonymousNamespacePart(const std::wstring& a_part)
    {
        return a_part == kAnonymousNamespace;
    }

    /// Find the index of the last "::" that is NOT inside a template/function/array
    /// bracket list (i.e. at depth zero). Returns npos if there is none.
    /// This matters for template instantiations such as
    /// "TB::TList<int, TB::CustomAllocator<int>>" where a "::" lives inside the
    /// "<...>" and must not be treated as the scope separator.
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
    /// e.g. L"A::B" -> 2, the anonymous marker -> 1, empty -> 0.
    static size_t namespacePartCount(const std::wstring& a_namespace)
    {
        return splitQualifiedName(a_namespace).size();
    }

    /// Emit the opening lines of a namespace block given a fully-qualified path.
    /// Anonymous-namespace parts are emitted as nameless "namespace { ... }"
    /// nested blocks (MSVC stores them as "`anonymous-namespace'" in names).
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

    /// A user-defined template instantiation requested by name, e.g.
    /// "Type<float, 11, TB::HighRes>" -> "template<typename T, size_t U, typename V>".
    /// Concrete arguments are remembered so they can be substituted back with the
    /// generated parameter names everywhere in the dumped types and values.
    struct TemplateInstantiation
    {
        bool active = false;
        std::wstring decl;                                               // e.g. L"template<typename T, size_t U, size_t V>"
        std::vector<std::pair<std::wstring, std::wstring>> replacements; // { concrete arg, generated param name }
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
        return L"_" + std::to_wstring(a_index + 1); // _8, _9, ...
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
        std::sort(ti.replacements.begin(), ti.replacements.end(),
            [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

        ti.active = true;
        return ti;
    }

    static bool isIdentifierContinuation(wchar_t ach)
    {
        return (ach >= L'a' && ach <= L'z')
               || (ach >= L'A' && ach <= L'Z')
               || (ach >= L'0' && ach <= L'9')
               || ach == L'_';
    }

    /// Replace concrete template arguments with their generated parameter names,
    /// matching only whole tokens so "11" does not rewrite "x11" or "111".
    static std::wstring substituteTemplateArgs(
        const std::wstring& a_text, const TemplateInstantiation& a_ti)
    {
        std::wstring result = a_text;
        for (const auto& [from, to] : a_ti.replacements)
        {
            if (from.empty())
                continue;

            std::wstring next;
            size_t pos = 0;
            while (pos < result.size())
            {
                const size_t found = result.find(from, pos);
                if (found == std::wstring::npos)
                {
                    next += result.substr(pos);
                    break;
                }

                const size_t end = found + from.size();
                const bool boundaryBefore = (found == 0) || !isIdentifierContinuation(result[found - 1]);
                const bool boundaryAfter = (end >= result.size()) || !isIdentifierContinuation(result[end]);

                if (boundaryBefore && boundaryAfter)
                {
                    next += result.substr(pos, found - pos);
                    next += to;
                    pos = end;
                }
                else
                {
                    next += result.substr(pos, found - pos + 1);
                    pos = found + 1;
                }
            }
            result = std::move(next);
        }
        return result;
    }
    /// Returns true if a_symbol's lexical parent is a UDT (class/struct/union),
    /// i.e. the symbol is nested inside another type.
    /// E.g. for "Test::Actor::Weapon", the parent is "Test::Actor" (SymTagUDT),
    /// so this returns true. For "Test::Weapon", the parent is the global scope
    /// (SymTagExe), so this returns false.
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
    /// e.g. "User::Hello" -> ns="User",   leaf="Hello"
    ///      "A::B::Hello" -> ns="A::B",   leaf="Hello"
    ///      "Hello"       -> ns="",       leaf="Hello"
    /// The split is template-aware: a "::" inside a "<...>" argument list (as in
    /// "TB::TList<int, TB::CustomAllocator<int>>") is NOT treated as the scope
    /// separator, so the leaf name keeps its full template argument list intact.
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
    /// Only call this when isTopLevelSymbol(a_symbol) is true — for nested classes
    /// the "::" in the name refers to enclosing classes, not namespaces, and this
    /// function must NOT be used there (ScopeContext handles that case instead).
    /// Assumes that any qualifier of a top-level symbol is a namespace,
    /// since nested classes have lexicalParent != SymTagExe.
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
    /// Returns a TypeBuilder populated with the full type chain.
    /// @param a_stripScope Controls whether current scope prefix is stripped from names
    ///                     (corresponds to DumpConfig::m_showNonScoped).
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

        // Get qualifiers
        BOOL isConst = FALSE;
        BOOL isVolatile = FALSE;
        a_symbol->get_constType(&isConst);
        a_symbol->get_volatileType(&isVolatile);

        // Get name (strip scope based on a_stripScope parameter)
        std::wstring name = getName(a_symbol, a_scope, a_stripScope);

        // Recurse into sub-type first (inner types are built first)
        ComPtr<IDiaSymbol> subType;
        if (SUCCEEDED(a_symbol->get_type(&subType)))
        {
            TypeBuilder subBuilder = resolveType(subType.get(), a_scope, a_stripScope, a_intStyle);
            // Merge sub-builder into this one (inner type becomes the builder state)
            builder = std::move(subBuilder);
        }

        // Apply this symbol's modifier and qualifiers.
        // Since we recurse first, we build from inner to outer:
        //   recursion builds the inner type, then we add the outer modifier.
        switch (symTag)
        {
        case SymTagBaseType:
        {
            if (auto baseName = getBaseTypeName(a_symbol, a_intStyle))
            {
                builder.base(baseName);
            }

            // const/volatile on BaseType applies to the base type itself
            // e.g. const int, volatile int
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

            // Bit field
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

            // Derive the type name: anonymous/inplace types get a friendly, re-usable
            // identifier (enums get an "Enum" suffix, e.g. <unnamed-type-m_Member>
            // -> "MemberEnum"; <undefined-type> / <unnamed-tag> / $HASH -> empty).
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

    /// Check if a name is a compiler-generated synthetic name (anonymous or hash-based).
    static bool isSyntheticName(const std::wstring& a_name)
    {
        return a_name.empty() || a_name == L"<unnamed-tag>"
               || (!a_name.empty() && a_name.front() == L'$');
    }

    /// Convert an MSVC-generated synthetic/anonymous type name into a friendly C++ identifier
    /// suitable for re-emitting in reconstructed definitions.

    /// Handles:
    ///   - L"<undefined-type>"          -> empty: anonymous type,no usable name (caller drops it).
    ///   - L"<unnamed-tag>"             -> empty: anonymous type,no usable name.

    ///   - L"<unnamed-type-m_Member>"   -> inplace anonymous type that MSVC named after its bound
    ///     member variable. Strips the "<unnamed-type-" / ">" wrapper and the Hungarian-ish
    ///     member/static/global prefix (m_/s_/g_). When *a_kindSuffix* is supplied
    ///     (e.g. L"Enum" for enums), it is appended -> e.g. L"MemberEnum".
    ///   - "$"-prefixed name       -> empty: compiler-generated hash name (anonymous).
    ///   - Anything else            -> returned unchanged (normal named types are untouched).
    static std::wstring prettyTypeName(const std::wstring& a_name,
        const wchar_t* a_kindSuffix = nullptr)
    {
        if (a_name.empty())
            return a_name;

        if (a_name == L"<unnamed-tag>" || a_name == L"<undefined-type>")
            return L"";

        if (a_name.front() == L'$')
            return L"";

        // inplace anonymous types: "<unnamed-type-m_Member>"

        const wchar_t kAnonPrefix[] = L"<unnamed-type-";
        constexpr size_t kAnonPrefixLen = (sizeof(kAnonPrefix) / sizeof(kAnonPrefix[0])) - 1;
        if (a_name.compare(0, kAnonPrefixLen, kAnonPrefix) == 0)
        {
            std::wstring inner = a_name.substr(kAnonPrefixLen);
            if (!inner.empty() && inner.back() == L'>')
                inner.pop_back();

            // Strip Hungarian-ish member/static/global prefix (m_, s_, g_).
            static const wchar_t* memberPrefixes[] = { L"m_", L"s_", L"g_" };

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
    /// @param a_symbol        The DIA symbol to get the name from.
    /// @param a_scope         The current scope context (stack of enclosing class names).
    /// @param a_stripScope    If true (default), strips the current scope prefix from the name.
    ///                         Controls the "m_showNonScoped" behavior: when true, only the
    ///                         short/non-scoped name is returned. When false, the full scoped
    ///                         name (e.g. "ParentClass::Child") is preserved.
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

    /// Check if a symbol is an anonymous union/struct (empty name + UDT kind)
    /// or has a compiler-generated synthetic name.
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

        // Check for synthetic/anonymous names
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

    /// Get the access specifier as a string.
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

    /// C++ calling convention enum.
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

    /// Get the calling convention from a DIA symbol.
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

    /// Render a calling convention enum to its C++ keyword string.
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