#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

/// Chain-of-modifiers type renderer.
/// Builds a C++ type declaration by collecting modifiers inner-to-outer,
/// then renders using the spiral/right-left rule.
enum class ModifierKind : uint8_t
{
    Pointer,
    Reference,
    RValueReference,
    Array,
    Function,
    BitField
};

struct TypeQualifier
{
    bool isConst = false;
    bool isVolatile = false;
};

struct Modifier
{
    ModifierKind kind;
    TypeQualifier qualifier;
    size_t arrayCount = 0;     ///< Array element count.
    std::wstring functionArgs; ///< Comma-separated arg string for Function.
    DWORD bitPosition = 0;     ///< Bit-field position.
    ULONGLONG bitLength = 0;   ///< Bit-field length.
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
        m_chain.push_back({ModifierKind::Pointer});
        return *this;
    }

    TypeBuilder& reference()
    {
        m_chain.push_back({ModifierKind::Reference});
        return *this;
    }

    TypeBuilder& rvalueReference()
    {
        m_chain.push_back({ModifierKind::RValueReference});
        return *this;
    }

    TypeBuilder& array(size_t acount)
    {
        m_chain.push_back({ModifierKind::Array, {}, acount});
        return *this;
    }

    TypeBuilder& function(std::wstring a_args)
    {
        m_chain.push_back({ModifierKind::Function, {}, 0, std::move(a_args)});
        return *this;
    }

    TypeBuilder& bitField(DWORD a_pos, ULONGLONG a_len)
    {
        m_chain.push_back({ModifierKind::BitField, {}, 0, L"", a_pos, a_len});
        return *this;
    }

    /// Set const qualifier on the base type (e.g. const int).
    TypeBuilder& constQual()
    {
        m_baseQualifier.isConst = true;
        return *this;
    }

    /// Set volatile qualifier on the base type (e.g. volatile int).
    TypeBuilder& volatileQual()
    {
        m_baseQualifier.isVolatile = true;
        return *this;
    }

    /// Set const qualifier on the last pointer modifier (int* const).
    TypeBuilder& constPointer()
    {
        if (!m_chain.empty())
            m_chain.back().qualifier.isConst = true;
        return *this;
    }

    TypeBuilder& volatilePointer()
    {
        if (!m_chain.empty())
            m_chain.back().qualifier.isVolatile = true;
        return *this;
    }

    /// Build the type string using the spiral/right-left rule.
    std::wstring build() const
    {
        std::wstring result;

        if (m_baseQualifier.isVolatile)
        {
            result += L"volatile ";
        }
        if (m_baseQualifier.isConst)
        {
            result += L"const ";
        }

        if (!mbaseType.empty())
        {
            result += mbaseType;
        }

        std::wstring prefix;
        std::wstring postfix;
        bool seenPostfix = false;
        bool needsParen = false;

        for (auto it = m_chain.begin(); it != m_chain.end(); ++it)
        {
            switch (it->kind)
            {
            case ModifierKind::Pointer:
                if (seenPostfix)
                {
                    needsParen = true;
                }
                prefix += L"*";
                if (it->qualifier.isConst)
                {
                    prefix += L" const";
                }
                if (it->qualifier.isVolatile)
                {
                    prefix += L" volatile";
                }
                break;

            case ModifierKind::Reference:
                if (seenPostfix)
                {
                    needsParen = true;
                }
                prefix += L"&";
                if (it->qualifier.isConst)
                {
                    prefix += L" const";
                }
                if (it->qualifier.isVolatile)
                {
                    prefix += L" volatile";
                }
                break;

            case ModifierKind::RValueReference:
                if (seenPostfix)
                {
                    needsParen = true;
                }
                prefix += L"&&";
                if (it->qualifier.isConst)
                {
                    prefix += L" const";
                }
                if (it->qualifier.isVolatile)
                {
                    prefix += L" volatile";
                }
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
                break;
            }
        }

        if (needsParen)
        {
            result += L" (";
            result += prefix;
            if (!m_name.empty())
            {
                result += L" ";
                result += m_name;
            }
            result += L")";
        }
        else
        {
            result += prefix;
            if (!m_name.empty())
            {
                result += L" ";
                result += m_name;
            }
        }

        result += postfix;

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

    void reset()
    {
        m_chain.clear();
        mbaseType.clear();
        m_name.clear();
        m_baseQualifier = TypeQualifier{};
    }

private:

    std::vector<Modifier> m_chain;
    std::wstring mbaseType;
    std::wstring m_name;
    TypeQualifier m_baseQualifier;
};
