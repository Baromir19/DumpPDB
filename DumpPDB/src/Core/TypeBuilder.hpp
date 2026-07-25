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
        m_baseType = a_type;
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

    TypeBuilder& array(size_t a_count)
    {
        m_chain.push_back({ ModifierKind::Array, a_count });
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
        m_isConst = true;
        return *this;
    }

    TypeBuilder& volatileQual()
    {
        m_isVolatile = true;
        return *this;
    }

    TypeBuilder& constPointed()
    {
        m_isConstPointed = true;
        return *this;
    }

    /// Build the type string using spiral/right-left rule.
    /// The chain is traversed from outer to inner (back to front),
    /// applying modifiers in the correct C++ declaration order.
    std::wstring build() const
    {
        std::wstring _result;

        // 1. Leading qualifiers (const, volatile) for the base type.
        //    m_isConstPointed means the pointed-to type is const (e.g. const int*),
        //    which goes before the base type, not after the pointer.
        if (m_isVolatile) { _result += L"volatile "; }
        if (m_isConst || m_isConstPointed) { _result += L"const "; }

        // 2. Base type
        if (!m_baseType.empty()) { _result += m_baseType; }

        // 3. Build prefix (before name) and postfix (after name) from the modifier chain.
        //    Walk from inner (begin) to outer (end) to correctly handle C++ declarators.
        //    When a postfix modifier (Function/Array) wraps a prefix modifier (Pointer/Ref),
        //    we need parentheses around the prefix: e.g. int (*)(float) not int*(float).
        std::wstring _prefix;
        std::wstring _postfix;
        bool _seenPostfix = false;
        bool _needsParen = false;

        for (auto it = m_chain.begin(); it != m_chain.end(); ++it)
        {
            switch (it->kind)
            {
            case ModifierKind::Pointer:
                if (_seenPostfix) { _needsParen = true; }
                _prefix += L"*";
                break;

            case ModifierKind::Reference:
                if (_seenPostfix) { _needsParen = true; }
                _prefix += L"&";
                break;

            case ModifierKind::RValueReference:
                if (_seenPostfix) { _needsParen = true; }
                _prefix += L"&&";
                break;

            case ModifierKind::Array:
                _postfix += L"[";
                if (it->arrayCount > 0)
                {
                    _postfix += std::to_wstring(it->arrayCount);
                }
                _postfix += L"]";
                _seenPostfix = true;
                break;

            case ModifierKind::Function:
                _postfix += L"(";
                _postfix += it->functionArgs;
                _postfix += L")";
                _seenPostfix = true;
                break;

            case ModifierKind::BitField:
                break; // handled after name
            }
        }

        // 4. Emit prefix with parentheses if needed for correct C++ declarator syntax.
        //    The name is placed inside the parentheses (or right after prefix if no parens)
        //    to correctly handle the spiral rule for pointers to arrays/functions.
        //    e.g. int (*arr)[10] not int (*)[10] arr
        if (_needsParen)
        {
            _result += L" (";
            _result += _prefix;
            if (!m_name.empty()) { _result += L" "; _result += m_name; }
            _result += L")";
        }
        else
        {
            _result += _prefix;
            if (!m_name.empty()) { _result += L" "; _result += m_name; }
        }

        // 5. Postfix (function args, array dimensions)
        _result += _postfix;

        // 7. Bitfield
        for (auto it = m_chain.begin(); it != m_chain.end(); ++it)
        {
            if (it->kind == ModifierKind::BitField && it->bitLength > 0)
            {
                wchar_t _buf[64];
                swprintf_s(_buf, L" : %llu", it->bitLength);
                _result += _buf;
            }
        }

        return _result;
    }

    /// Reset builder state for reuse.
    void reset()
    {
        m_chain.clear();
        m_baseType.clear();
        m_name.clear();
        m_isConst = false;
        m_isVolatile = false;
        m_isConstPointed = false;
    }

private:
    std::vector<Modifier> m_chain;  // inner (closest to base) to outer
    std::wstring          m_baseType;
    std::wstring          m_name;
    bool                  m_isConst = false;
    bool                  m_isVolatile = false;
    bool                  m_isConstPointed = false;
};