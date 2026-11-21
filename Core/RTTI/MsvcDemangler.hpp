#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// MsvcDemangler — demangles MSVC RTTI TypeDescriptor names.
///
/// A TypeDescriptor name looks like ".?AV<qualified-name>@" (class),
/// ".?AU..." (struct), ".?AT..." (union) or ".?AW4..." (enum). The qualified
/// name is a list of fragments, innermost scope first, terminated by '@':
///
///     .?AVBlockStreamer@NavMeshPrivate@AI@@   ->  AI::NavMeshPrivate::BlockStreamer
///
/// On top of plain nested names this handles everything MSVC emits in practice:
///
///   * Class templates with arbitrary (nested) type and non-type arguments:
///       .?AV?$CCallback@VSteamEnvironment@@UDlcInstalled_t@@$0A@@@
///         -> CCallback<SteamEnvironment,DlcInstalled_t,0>
///   * Name back-references (digits 0-9) — one table per template scope.
///   * Anonymous namespaces:  ?A0x0a9a5557@  ->  `anonymous namespace'
///   * Classes local to a function:
///       ?1??Func@Scope@@QAEXXZ@  ->  Scope::Func
///   * Primitive types, pointers, references, rvalue references, arrays and
///     function pointers used as template arguments.
///
/// The output follows the style MSVC itself writes into PDB type names (what
/// DIA reports): no `class`/`struct` keywords, ',' without a following space and
/// a space between consecutive closing angle brackets ("A<B<int> >").
///
/// Anything that cannot be parsed makes demangle() return an empty string, so a
/// caller can fall back to the raw mangled text instead of printing garbage.
/// ============================================================================
class MsvcDemangler
{
public:

    struct Options
    {
        /// Emit "> >" between consecutive closing brackets (MSVC debug-info style).
        bool angleBracketSpace = true;
        /// Prefix template arguments with class/struct/union/enum (undname style).
        bool classKeywords = false;
    };

    enum class Kind : uint8_t
    {
        Unknown,
        Class,
        Struct,
        Union,
        Enum,
    };

    struct Result
    {
        bool ok = false;
        Kind kind = Kind::Unknown;
        std::string name;       ///< Fully qualified, e.g. "Ns::Foo<int>".
        bool isTemplate = false; ///< Leaf fragment is a template instantiation.
        bool isAnonymous = false; ///< Lives (partly) in an anonymous namespace.
        bool isLocal = false;     ///< Declared inside a function body.
    };

    /// Demangle a TypeDescriptor name such as ".?AVFoo@@".
    [[nodiscard]] static Result parse(const std::string& a_mangled)
    {
        return parse(a_mangled, Options{});
    }

    [[nodiscard]] static Result parse(const std::string& a_mangled, const Options& a_opt)
    {
        Result r;
        if (a_mangled.size() < 5 || a_mangled[0] != '.' || a_mangled[1] != '?'
            || a_mangled[2] != 'A')
        {
            return r;
        }

        Parser p(a_mangled, 4, a_opt);
        const char tag = a_mangled[3];
        switch (tag)
        {
        case 'V':
            r.kind = Kind::Class;
            break;
        case 'U':
            r.kind = Kind::Struct;
            break;
        case 'T':
            r.kind = Kind::Union;
            break;
        case 'W':
            if (a_mangled.size() < 6 || a_mangled[4] != '4')
            {
                return r;
            }
            r.kind = Kind::Enum;
            p.skip(1);
            break;
        default:
            return r;
        }

        const std::string name = p.qualifiedName();
        if (p.failed() || name.empty() || !p.atEnd())
        {
            return Result{};
        }

        r.ok = true;
        r.name = name;
        r.isTemplate = p.sawTemplate();
        r.isAnonymous = p.sawAnonymous();
        r.isLocal = p.sawLocal();
        return r;
    }

    /// Convenience: the demangled name, or "" when parsing failed.
    [[nodiscard]] static std::string demangle(const std::string& a_mangled)
    {
        return parse(a_mangled, Options{}).name;
    }

    [[nodiscard]] static std::string demangle(const std::string& a_mangled, const Options& a_opt)
    {
        return parse(a_mangled, a_opt).name;
    }

    /// Index of the last "::" that is not nested inside <>, () or []. npos if none.
    [[nodiscard]] static size_t lastScopeSeparator(const std::string& a_name)
    {
        size_t last = std::string::npos;
        int depth = 0;
        for (size_t i = 0; i < a_name.size(); ++i)
        {
            const char c = a_name[i];
            if (c == '<' || c == '(' || c == '[')
            {
                ++depth;
            }
            else if (c == '>' || c == ')' || c == ']')
            {
                if (depth > 0)
                {
                    --depth;
                }
            }
            else if (c == '`')
            {
                // `anonymous namespace' contains a space but never "::" — skip to the quote.
                const size_t close = a_name.find('\'', i + 1);
                if (close != std::string::npos)
                {
                    i = close;
                }
            }
            else if (c == ':' && depth == 0 && i + 1 < a_name.size() && a_name[i + 1] == ':')
            {
                last = i;
                ++i;
            }
        }
        return last;
    }

    /// "A::B::Foo<X::Y>" -> "Foo<X::Y>".
    [[nodiscard]] static std::string leafName(const std::string& a_name)
    {
        const size_t pos = lastScopeSeparator(a_name);
        return pos == std::string::npos ? a_name : a_name.substr(pos + 2);
    }

    /// "A::B::Foo<X::Y>" -> "A::B"  (empty for top-level names).
    [[nodiscard]] static std::string scopeOf(const std::string& a_name)
    {
        const size_t pos = lastScopeSeparator(a_name);
        return pos == std::string::npos ? std::string() : a_name.substr(0, pos);
    }

    /// Template-less name of the leaf: "A::Foo<int>" -> "Foo".
    [[nodiscard]] static std::string bareLeafName(const std::string& a_name)
    {
        std::string leaf = leafName(a_name);
        const size_t lt = leaf.find('<');
        if (lt != std::string::npos)
        {
            leaf.erase(lt);
        }
        return leaf;
    }

private:

    // ─────────────────────────────────────────────────────────────────────
    // Recursive-descent parser
    // ─────────────────────────────────────────────────────────────────────

    class Parser
    {
    public:

        Parser(const std::string& a_text, size_t a_pos, const Options& a_opt)
            : m_s(a_text)
            , m_pos(a_pos)
            , m_opt(a_opt)
        {
        }

        [[nodiscard]] bool failed() const noexcept { return m_err; }
        [[nodiscard]] bool atEnd() const noexcept { return m_pos == m_s.size(); }
        [[nodiscard]] bool sawTemplate() const noexcept { return m_leafIsTemplate; }
        [[nodiscard]] bool sawAnonymous() const noexcept { return m_sawAnon; }
        [[nodiscard]] bool sawLocal() const noexcept { return m_sawLocal; }
        void skip(size_t a_n) noexcept { m_pos += a_n; }

        /// name-fragment+ '@'  (innermost scope first) -> "Outer::Inner::Leaf"
        std::string qualifiedName()
        {
            if (++m_depth > kMaxDepth)
            {
                fail();
                --m_depth;
                return {};
            }

            std::vector<std::string> frags;
            bool leafTemplate = false;

            while (!m_err && m_pos < m_s.size() && m_s[m_pos] != '@')
            {
                bool wasTemplate = false;
                frags.push_back(fragment(wasTemplate));
                if (frags.size() == 1)
                {
                    leafTemplate = wasTemplate;
                }
            }

            if (m_pos >= m_s.size() || m_s[m_pos] != '@')
            {
                fail();
            }
            else
            {
                ++m_pos;
            }

            --m_depth;

            if (m_err || frags.empty())
            {
                fail();
                return {};
            }

            if (m_depth == 0)
            {
                m_leafIsTemplate = leafTemplate;
            }

            std::string out;
            for (size_t i = frags.size(); i-- > 0;)
            {
                if (!out.empty())
                {
                    out += "::";
                }
                out += frags[i];
            }
            return out;
        }

    private:

        static constexpr int kMaxDepth = 48;

        void fail() noexcept { m_err = true; }

        [[nodiscard]] bool startsWith(const char* a_prefix) const noexcept
        {
            size_t i = 0;
            while (a_prefix[i])
            {
                if (m_pos + i >= m_s.size() || m_s[m_pos + i] != a_prefix[i])
                {
                    return false;
                }
                ++i;
            }
            return true;
        }

        [[nodiscard]] char peek() const noexcept
        {
            return m_pos < m_s.size() ? m_s[m_pos] : '\0';
        }

        void memorize(const std::string& a_name)
        {
            if (m_names.size() >= 10)
            {
                return;
            }
            for (const auto& n : m_names)
            {
                if (n == a_name)
                {
                    return;
                }
            }
            m_names.push_back(a_name);
        }

        /// Plain identifier terminated by '@' (consumes the '@').
        std::string simpleName()
        {
            const size_t end = m_s.find('@', m_pos);
            if (end == std::string::npos || end == m_pos)
            {
                fail();
                return {};
            }
            std::string name = m_s.substr(m_pos, end - m_pos);
            m_pos = end + 1;
            return name;
        }

        std::string fragment(bool& a_isTemplate)
        {
            a_isTemplate = false;
            const char c = peek();

            // Back-reference to one of the (up to ten) previously seen fragments.
            if (c >= '0' && c <= '9')
            {
                const size_t idx = static_cast<size_t>(c - '0');
                ++m_pos;
                if (idx >= m_names.size())
                {
                    fail();
                    return {};
                }
                return m_names[idx];
            }

            if (startsWith("?$"))
            {
                a_isTemplate = true;
                m_sawTemplate = true;
                return templateInstance();
            }

            if (startsWith("?A"))
            {
                // Anonymous namespace: ?A0x<hash>@
                m_pos += 2;
                const size_t end = m_s.find('@', m_pos);
                if (end == std::string::npos)
                {
                    fail();
                    return {};
                }
                m_pos = end + 1;
                m_sawAnon = true;
                const std::string name = "`anonymous namespace'";
                memorize(name);
                return name;
            }

            if (c == '?')
            {
                return localScope();
            }

            std::string name = simpleName();
            if (!m_err)
            {
                memorize(name);
            }
            return name;
        }

        /// ?$name@ args... @   — own back-reference table, memorised in the outer one.
        std::string templateInstance()
        {
            m_pos += 2;

            std::vector<std::string> outer;
            outer.swap(m_names);

            std::string base = simpleName();
            if (!m_err)
            {
                memorize(base);
            }

            std::string args = templateArgs();

            m_names.swap(outer);
            if (m_err)
            {
                return {};
            }

            std::string full = base;
            full += '<';
            full += args;
            if (m_opt.angleBracketSpace && !args.empty() && args.back() == '>')
            {
                full += ' ';
            }
            full += '>';

            memorize(full);
            return full;
        }

        std::string templateArgs()
        {
            std::string out;
            bool first = true;

            while (!m_err && m_pos < m_s.size() && m_s[m_pos] != '@')
            {
                const std::string arg = templateArg();
                if (m_err)
                {
                    return {};
                }
                if (arg.empty())
                {
                    continue; // empty parameter pack marker
                }
                if (!first)
                {
                    out += ',';
                }
                out += arg;
                first = false;
            }

            if (m_pos >= m_s.size() || m_s[m_pos] != '@')
            {
                fail();
                return {};
            }
            ++m_pos; // closing '@' of the argument list
            return out;
        }

        std::string templateArg()
        {
            if (peek() != '$')
            {
                return type(true);
            }

            if (startsWith("$$V")) // end of parameter pack
            {
                m_pos += 3;
                return {};
            }
            if (startsWith("$$Z"))
            {
                m_pos += 3;
                return {};
            }
            if (startsWith("$$T"))
            {
                m_pos += 3;
                return "std::nullptr_t";
            }
            if (startsWith("$$C") || startsWith("$$Q") || startsWith("$$R"))
            {
                return type(true); // cv-qualified type / rvalue reference
            }

            if (startsWith("$0")) // integral constant
            {
                m_pos += 2;
                int64_t v = number();
                return std::to_string(v);
            }

            if (startsWith("$1") || startsWith("$H") || startsWith("$I") || startsWith("$J"))
            {
                // Address of a symbol / member pointer — consumed as a mangled symbol.
                const char kind = m_s[m_pos + 1];
                m_pos += 2;
                if (kind == 'H' || kind == 'I' || kind == 'J')
                {
                    // Pointer-to-member with displacement(s): rare; give up cleanly.
                    fail();
                    return {};
                }
                return "&" + symbolReference();
            }

            if (startsWith("$2")) // floating-point constant: mantissa, exponent
            {
                m_pos += 2;
                const int64_t mant = number();
                const int64_t exp = number();
                return std::to_string(mant) + "e" + std::to_string(exp);
            }

            if (startsWith("$D")) // template parameter index
            {
                m_pos += 2;
                const int64_t idx = number();
                return "`template-parameter-" + std::to_string(idx + 1) + "'";
            }

            if (startsWith("$S")) // empty non-type argument
            {
                m_pos += 2;
                return {};
            }

            fail();
            return {};
        }

        // ── Numbers ───────────────────────────────────────────────────────

        /// MSVC number: [?] ( digit | [A-P]+ '@' ). digit d means d+1.
        int64_t number()
        {
            bool neg = false;
            if (peek() == '?')
            {
                neg = true;
                ++m_pos;
            }

            int64_t value = 0;
            const char c = peek();
            if (c >= '0' && c <= '9')
            {
                value = (c - '0') + 1;
                ++m_pos;
            }
            else
            {
                int digits = 0;
                while (m_pos < m_s.size() && m_s[m_pos] != '@')
                {
                    const char d = m_s[m_pos];
                    if (d < 'A' || d > 'P' || ++digits > 16)
                    {
                        fail();
                        return 0;
                    }
                    value = (value << 4) | (d - 'A');
                    ++m_pos;
                }
                if (m_pos >= m_s.size())
                {
                    fail();
                    return 0;
                }
                ++m_pos; // '@'
            }
            return neg ? -value : value;
        }

        // ── Types ─────────────────────────────────────────────────────────

        /// Reads an optional __ptr64/__unaligned/__restrict run and the cv-qualifier
        /// char (A..D) that follows a pointer/reference/array marker.
        std::string pointerCv()
        {
            while (m_pos < m_s.size()
                   && (m_s[m_pos] == 'E' || m_s[m_pos] == 'F' || m_s[m_pos] == 'I'))
            {
                ++m_pos;
            }

            const char c = peek();
            ++m_pos;
            switch (c)
            {
            case 'A':
                return {};
            case 'B':
                return "const ";
            case 'C':
                return "volatile ";
            case 'D':
                return "const volatile ";
            default:
                fail();
                return {};
            }
        }

        std::string udtName(const char* a_keyword, bool a_inTemplate)
        {
            std::string name = qualifiedName();
            if (m_err)
            {
                return {};
            }
            if (m_opt.classKeywords && a_inTemplate)
            {
                return std::string(a_keyword) + " " + name;
            }
            return name;
        }

        std::string type(bool a_inTemplate)
        {
            if (m_err || m_pos >= m_s.size())
            {
                fail();
                return {};
            }
            if (++m_depth > kMaxDepth)
            {
                --m_depth;
                fail();
                return {};
            }

            std::string result = typeInner(a_inTemplate);
            --m_depth;
            return result;
        }

        std::string typeInner(bool a_inTemplate)
        {
            const char c = m_s[m_pos++];
            switch (c)
            {
            case 'X':
                return "void";
            case 'D':
                return "char";
            case 'C':
                return "signed char";
            case 'E':
                return "unsigned char";
            case 'F':
                return "short";
            case 'G':
                return "unsigned short";
            case 'H':
                return "int";
            case 'I':
                return "unsigned int";
            case 'J':
                return "long";
            case 'K':
                return "unsigned long";
            case 'M':
                return "float";
            case 'N':
                return "double";
            case 'O':
                return "long double";

            case '_':
            {
                const char d = peek();
                ++m_pos;
                switch (d)
                {
                case 'N':
                    return "bool";
                case 'J':
                    return "__int64";
                case 'K':
                    return "unsigned __int64";
                case 'W':
                    return "wchar_t";
                case 'S':
                    return "char16_t";
                case 'U':
                    return "char32_t";
                case 'Q':
                    return "char8_t";
                case 'D':
                    return "__int8";
                case 'E':
                    return "unsigned __int8";
                case 'F':
                    return "__int16";
                case 'G':
                    return "unsigned __int16";
                case 'H':
                    return "__int32";
                case 'I':
                    return "unsigned __int32";
                case 'L':
                    return "__int128";
                case 'M':
                    return "unsigned __int128";
                default:
                    fail();
                    return {};
                }
            }

            case 'T':
                return udtName("union", a_inTemplate);
            case 'U':
                return udtName("struct", a_inTemplate);
            case 'V':
                return udtName("class", a_inTemplate);

            case 'W':
                if (peek() != '4')
                {
                    fail();
                    return {};
                }
                ++m_pos;
                return udtName("enum", a_inTemplate);

            case 'P':
            case 'Q':
            case 'R':
            case 'S':
                return pointerType(c);

            case 'A':
            case 'B':
            {
                const std::string cv = pointerCv();
                const std::string inner = type(a_inTemplate);
                return m_err ? std::string() : cv + inner + "&";
            }

            case '$':
                if (peek() == '$')
                {
                    ++m_pos;
                    const char k = peek();
                    ++m_pos;
                    if (k == 'Q' || k == 'R')
                    {
                        const std::string cv = pointerCv();
                        const std::string inner = type(a_inTemplate);
                        return m_err ? std::string() : cv + inner + "&&";
                    }
                    if (k == 'T')
                    {
                        return "std::nullptr_t";
                    }
                    if (k == 'C')
                    {
                        // $$C<cv> type — cv-qualified type argument.
                        const char q = peek();
                        ++m_pos;
                        std::string prefix;
                        if (q == 'B')
                        {
                            prefix = "const ";
                        }
                        else if (q == 'C')
                        {
                            prefix = "volatile ";
                        }
                        else if (q == 'D')
                        {
                            prefix = "const volatile ";
                        }
                        else if (q != 'A')
                        {
                            fail();
                            return {};
                        }
                        const std::string inner = type(a_inTemplate);
                        return m_err ? std::string() : prefix + inner;
                    }
                }
                fail();
                return {};

            case '?':
            {
                // cv-qualified by-value type: ?A (none) ?B (const) ?C (volatile) ?D (both)
                const char q = peek();
                ++m_pos;
                std::string prefix;
                if (q == 'B')
                {
                    prefix = "const ";
                }
                else if (q == 'C')
                {
                    prefix = "volatile ";
                }
                else if (q == 'D')
                {
                    prefix = "const volatile ";
                }
                else if (q != 'A')
                {
                    fail();
                    return {};
                }
                const std::string inner = type(a_inTemplate);
                return m_err ? std::string() : prefix + inner;
            }

            case 'Y':
            {
                const int64_t dims = number();
                if (m_err || dims <= 0 || dims > 8)
                {
                    fail();
                    return {};
                }
                std::string suffix;
                for (int64_t i = 0; i < dims; ++i)
                {
                    const int64_t d = number();
                    suffix += "[" + std::to_string(d) + "]";
                }
                const std::string inner = type(a_inTemplate);
                return m_err ? std::string() : inner + suffix;
            }

            default:
                fail();
                return {};
            }
        }

        std::string pointerType(char a_kind)
        {
            // P = pointer, Q = const pointer, R = volatile pointer, S = const volatile pointer.
            std::string ptrQual;
            if (a_kind == 'Q')
            {
                ptrQual = " const";
            }
            else if (a_kind == 'R')
            {
                ptrQual = " volatile";
            }
            else if (a_kind == 'S')
            {
                ptrQual = " const volatile";
            }

            // Function pointer: P6 <callconv> <ret> <params> @ Z
            // (an optional __ptr64 marker precedes the 6 on x64).
            size_t save = m_pos;
            if (peek() == 'E' || peek() == 'F' || peek() == 'I')
            {
                ++m_pos;
            }
            if (peek() == '6')
            {
                ++m_pos;
                return functionPointer(ptrQual);
            }
            m_pos = save;

            const std::string cv = pointerCv();
            const std::string inner = type(true);
            if (m_err)
            {
                return {};
            }
            return cv + inner + "*" + ptrQual;
        }

        std::string functionPointer(const std::string& a_ptrQual)
        {
            // Calling convention (A/B cdecl, C/D pascal, E/F thiscall, G/H stdcall, I/J fastcall ...)
            const char cc = peek();
            ++m_pos;
            const char* ccName = "";
            switch (cc)
            {
            case 'A':
            case 'B':
                ccName = "__cdecl";
                break;
            case 'E':
            case 'F':
                ccName = "__thiscall";
                break;
            case 'G':
            case 'H':
                ccName = "__stdcall";
                break;
            case 'I':
            case 'J':
                ccName = "__fastcall";
                break;
            case 'C':
            case 'D':
                ccName = "__pascal";
                break;
            default:
                fail();
                return {};
            }

            const std::string ret = type(true);
            const std::string params = parameterList();
            if (m_err)
            {
                return {};
            }

            // throw specification
            if (peek() == 'Z')
            {
                ++m_pos;
            }

            return ret + " (" + ccName + "*" + a_ptrQual + ")(" + params + ")";
        }

        /// 'X' (void) or a list of types terminated by '@'.
        std::string parameterList()
        {
            if (peek() == 'X')
            {
                ++m_pos;
                return "void";
            }

            std::vector<std::string> seen; // function-parameter type back-references
            std::string out;
            bool first = true;

            while (!m_err && m_pos < m_s.size() && m_s[m_pos] != '@' && m_s[m_pos] != 'Z')
            {
                const size_t start = m_pos;
                std::string t;

                const char c = peek();
                if (c >= '0' && c <= '9')
                {
                    const size_t idx = static_cast<size_t>(c - '0');
                    ++m_pos;
                    if (idx >= seen.size())
                    {
                        fail();
                        return {};
                    }
                    t = seen[idx];
                }
                else if (c == 'Z')
                {
                    // variadic marker "...Z" is handled by caller
                    break;
                }
                else
                {
                    t = type(true);
                    if (!m_err && m_pos - start > 1 && seen.size() < 10)
                    {
                        seen.push_back(t);
                    }
                }

                if (m_err)
                {
                    return {};
                }
                if (!first)
                {
                    out += ',';
                }
                out += t;
                first = false;
            }

            if (peek() == '@')
            {
                ++m_pos;
            }
            else if (peek() == 'Z')
            {
                // "...": variadic function terminator
                out += first ? "..." : ",...";
                ++m_pos;
            }
            return out;
        }

        // ── Symbols (used by local-scope names and $1 template arguments) ─

        /// Parses a full mangled symbol "?name@scope@@<encoding>" and returns the
        /// qualified name of the entity. The encoding is consumed, not rendered.
        std::string symbolReference()
        {
            if (peek() != '?')
            {
                fail();
                return {};
            }
            ++m_pos;

            // Special names (constructors, operators ...) are not expected here.
            if (peek() == '?')
            {
                fail();
                return {};
            }

            // The symbol shares the back-reference table of the enclosing name
            // (e.g. "ABV3@" inside a local scope refers to a name seen before it).
            std::string name = qualifiedName();
            if (!m_err)
            {
                symbolEncoding();
            }

            return m_err ? std::string() : name;
        }

            /// Skips the type/encoding that follows a symbol's qualified name.
        void symbolEncoding()
        {
            const char c = peek();

            // Variables: 3 <type> <storage>   /  4 (local static) <type> <storage>
            if (c == '3' || c == '4')
            {
                ++m_pos;
                (void)type(true);
                if (!m_err)
                {
                    // storage-class / cv char
                    ++m_pos;
                }
                return;
            }

            // Global (non-member) functions: Y (near) / Z (far)
            bool hasThis = false;
            if (c == 'Y' || c == 'Z')
            {
                ++m_pos;
            }
            else if (c >= 'A' && c <= 'V')
            {
                ++m_pos;
                // Static members (C/D, K/L, S/T) have no implicit this.
                const bool isStatic = (c == 'C' || c == 'D' || c == 'K' || c == 'L' || c == 'S'
                                       || c == 'T');
                hasThis = !isStatic;
            }
            else
            {
                fail();
                return;
            }

            if (hasThis)
            {
                // [E __ptr64] [I __restrict] [F __unaligned] cv
                (void)pointerCv();
                if (m_err)
                {
                    return;
                }
            }

            // Calling convention
            const char cc = peek();
            if (cc < 'A' || cc > 'X')
            {
                fail();
                return;
            }
            ++m_pos;

            // Return type (constructors/destructors use '@').
            if (peek() == '@')
            {
                ++m_pos;
            }
            else
            {
                (void)type(true);
            }

            (void)parameterList();

            if (!m_err && peek() == 'Z')
            {
                ++m_pos;
            }
        }

        /// ?<number>?<symbol>  — a class declared inside a function body.
        std::string localScope()
        {
            ++m_pos; // '?'
            (void)number();
            if (m_err || peek() != '?')
            {
                fail();
                return {};
            }
            ++m_pos; // '?' terminating the number

            const std::string scope = symbolReference();
            if (m_err)
            {
                return {};
            }

            m_sawLocal = true;
            memorize(scope);
            return scope;
        }

        const std::string& m_s;
        size_t m_pos;
        Options m_opt;
        bool m_err = false;
        bool m_sawTemplate = false;
        bool m_leafIsTemplate = false;
        bool m_sawAnon = false;
        bool m_sawLocal = false;
        int m_depth = 0;
        std::vector<std::string> m_names;
    };
};

} // namespace DumpPDB
