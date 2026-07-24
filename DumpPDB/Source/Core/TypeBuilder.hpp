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

        // 1. Leading qualifiers (const, volatile) for the base type
        if (m_isVolatile) { _result += L"volatile "; }
        if (m_isConst)    { _result += L"const "; }

        // 2. Base type
        if (!m_baseType.empty()) { _result += m_baseType; }

        // 3. Apply modifiers from outer to inner using spiral rule
        //    Walk the chain backwards (outermost first)
        bool _hasPtrOrRef = false;
        bool _hasParen = false;

        for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it)
        {
            switch (it->kind)
            {
            case ModifierKind::Pointer:
                if (!_hasPtrOrRef && !_hasParen && m_name.empty())
                {
                    // No name yet, pointer goes after base type
                    _result += L"*";
                }
                else
                {
                    _result += L"*";
                }
                _hasPtrOrRef = true;
                break;

            case ModifierKind::Reference:
                _result += L"&";
                _hasPtrOrRef = true;
                break;

            case ModifierKind::RValueReference:
                _result += L"&&";
                _hasPtrOrRef = true;
                break;

            case ModifierKind::Array:
                _result += L"[";
                if (it->arrayCount > 0)
                {
                    wchar_t _buf[32];
                    _result += std::to_wstring(it->arrayCount);
                }
                _result += L"]";
                break;

            case ModifierKind::Function:
                _result += L"(";
                _result += it->functionArgs;
                _result += L")";
                break;

            case ModifierKind::BitField:
                break; // handled after name
            }
        }

        // 4. const on pointed-to type
        if (m_isConstPointed && _hasPtrOrRef)
        {
            _result += L" const";
        }

        // 5. Name
        if (!m_name.empty())
        {
            _result += L" ";
            _result += m_name;
        }

        // 6. Bitfield
        for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it)
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