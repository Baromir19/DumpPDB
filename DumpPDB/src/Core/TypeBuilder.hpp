#pragma once

#include <string>
#include <vector>
#include <cstdint>

/// Chain-of-modifiers approach for C++ type rendering.
/// Builds a type declaration by collecting modifiers from inner (base) to outer,
/// then renders using the spiral/right-left rule.
/// This systematically fixes the &* vs *& bug that flat-string concatenation causes.

enum class ModifierKind : uint8_t
{
    Pointer,
    Reference,
    RValueReference,
    Array,
    Function,
    BitField
};

struct Modifier
{
    ModifierKind kind;
    size_t       arrayCount = 0;     // for Array
    std::wstring functionArgs;       // for Function
    DWORD        bitPosition = 0;    // for BitField
    ULONGLONG    bitLength = 0;      // for BitField
};

class TypeBuilder
{
public:
    TypeBuilder& base(std::wstring_view a_type)
    {
        mbaseType = a_type;
        return *this;
    }

    TypeBuilder& name(std::wstring_view a_name)
    {
        m_name = a_name;
        return *this;
    }

    TypeBuilder& pointer()
    {
        m_chain.push_back({ ModifierKind::Pointer });
        return *this;
    }

    TypeBuilder& reference()
    {
        m_chain.push_back({ ModifierKind::Reference });
        return *this;
    }

    TypeBuilder& rvalueReference()
    {
        m_chain.push_back({ ModifierKind::RValueReference });
        return *this;
    }

    TypeBuilder& array(size_t acount)
    {
        m_chain.push_back({ ModifierKind::Array, acount });
        return *this;
    }

    TypeBuilder& function(std::wstring a_args)
    {
        m_chain.push_back({ ModifierKind::Function, 0, std::move(a_args) });
        return *this;
    }

    TypeBuilder& bitField(DWORD a_pos, ULONGLONG a_len)
    {
        m_chain.push_back({ ModifierKind::BitField, 0, L"", a_pos, a_len });
        return *this;
    }

    TypeBuilder& constQual()
    {
        misConst = true;
        return *this;
    }

    TypeBuilder& volatileQual()
    {
        misVolatile = true;
        return *this;
    }

    TypeBuilder& constPointed()
    {
        misConstPointed = true;
        return *this;
    }

    /// Build the type string using spiral/right-left rule.
    /// The chain is traversed from outer to inner (back to front),
    /// applying modifiers in the correct C++ declaration order.
    std::wstring build() const
    {
        std::wstring result;

        // 1. Leading qualifiers (const, volatile) for the base type.
        //    misConstPointed means the pointed-to type is const (e.g. const int*),
        //    which goes before the base type, not after the pointer.
        if (misVolatile) { result += L"volatile "; }
        if (misConst || misConstPointed) { result += L"const "; }

        // 2. Base type
        if (!mbaseType.empty()) { result += mbaseType; }

        // 3. Build prefix (before name) and postfix (after name) from the modifier chain.
        //    Walk from inner (begin) to outer (end) to correctly handle C++ declarators.
        //    When a postfix modifier (Function/Array) wraps a prefix modifier (Pointer/Ref),
        //    we need parentheses around the prefix: e.g. int (*)(float) not int*(float).
        std::wstring prefix;
        std::wstring postfix;
        bool seenPostfix = false;
        bool needsParen = false;

        for (auto it = m_chain.begin(); it != m_chain.end(); ++it)
        {
            switch (it->kind)
            {
            case ModifierKind::Pointer:
                if (seenPostfix) { needsParen = true; }
                prefix += L"*";
                break;

            case ModifierKind::Reference:
                if (seenPostfix) { needsParen = true; }
                prefix += L"&";
                break;

            case ModifierKind::RValueReference:
                if (seenPostfix) { needsParen = true; }
                prefix += L"&&";
                break;

            case ModifierKind::Array:
                postfix += L"[";
                if (it->arrayCount > 0)
                {
                    postfix += std::to_wstring(it->arrayCount);
                }
                postfix += L"]";
                seenPostfix = true;
                break;

            case ModifierKind::Function:
                postfix += L"(";
                postfix += it->functionArgs;
                postfix += L")";
                seenPostfix = true;
                break;

            case ModifierKind::BitField:
                break; // handled after name
            }
        }

        // 4. Emit prefix with parentheses if needed for correct C++ declarator syntax.
        //    The name is placed inside the parentheses (or right after prefix if no parens)
        //    to correctly handle the spiral rule for pointers to arrays/functions.
        //    e.g. int (*arr)[10] not int (*)[10] arr
        if (needsParen)
        {
            result += L" (";
            result += prefix;
            if (!m_name.empty()) { result += L" "; result += m_name; }
            result += L")";
        }
        else
        {
            result += prefix;
            if (!m_name.empty()) { result += L" "; result += m_name; }
        }

        // 5. Postfix (function args, array dimensions)
        result += postfix;

        // 7. Bitfield
        for (auto it = m_chain.begin(); it != m_chain.end(); ++it)
        {
            if (it->kind == ModifierKind::BitField && it->bitLength > 0)
            {
                wchar_t buf[64];
                swprintf_s(buf, L" : %llu", it->bitLength);
                result += buf;
            }
        }

        return result;
    }

    /// Reset builder state for reuse.
    void reset()
    {
        m_chain.clear();
        mbaseType.clear();
        m_name.clear();
        misConst = false;
        misVolatile = false;
        misConstPointed = false;
    }

private:
    std::vector<Modifier> m_chain;  // inner (closest to base) to outer
    std::wstring          mbaseType;
    std::wstring          m_name;
    bool                  misConst = false;
    bool                  misVolatile = false;
    bool                  misConstPointed = false;
};