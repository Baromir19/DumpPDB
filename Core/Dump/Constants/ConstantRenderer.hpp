#pragma once

#include <string>

#include <dia2.h>

#include <Core/Dump/DumpContext.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Renders DIA constant values (enum members, static const data) as
/// C++ value suffixes like " = 42".
class ConstantRenderer
{
public:

    explicit ConstantRenderer(DumpContext& a_ctx)
        : m_ctx(a_ctx)
    {
    }

    std::wstring constantValueSuffix(IDiaSymbol* a_symbol) const
    {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(a_symbol->get_value(&v)))
        {
            std::wstring ret;
            if (m_ctx.config().m_showEnumHex)
            {
                wchar_t buf[32];
                swprintf_s(buf, L" = 0x%llX", v.llVal);
                ret = buf;
            }
            else
            {
                switch (v.vt)
                {
                case VT_I4:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %d", v.lVal);
                    ret = buf;
                    break;
                }
                case VT_UI4:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %u", v.ulVal);
                    ret = buf;
                    break;
                }
                case VT_I2:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %d", v.iVal);
                    ret = buf;
                    break;
                }
                case VT_UI2:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %u", v.uiVal);
                    ret = buf;
                    break;
                }
                case VT_I1:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %d", (int)v.cVal);
                    ret = buf;
                    break;
                }
                case VT_UI1:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %u", v.bVal);
                    ret = buf;
                    break;
                }
                case VT_R4:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %ff", v.fltVal);
                    ret = buf;
                    break;
                }
                case VT_R8:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" = %f", v.dblVal);
                    ret = buf;
                    break;
                }
                case VT_BSTR:
                    if (v.bstrVal)
                    {
                        ret = L" = L\"";
                        ret += v.bstrVal;
                        ret += L"\"";
                    }
                    break;
                default: /*printf("VALUE: Undefined type : %d", v.vt);*/
                    break;
                }
            }
            VariantClear(&v);
            return ret;
        }
        return L"";
    }

    bool canHaveValue(IDiaSymbol* a_field)
    {
        DWORD kind = 0;
        if (FAILED(a_field->get_dataKind(&kind)))
            return false;

        if (kind == DataIsConstant)
            return true;

        if (kind == DataIsStaticMember)
        {
            ComPtr<IDiaSymbol> subType;
            if (SUCCEEDED(a_field->get_type(&subType)))
            {
                BOOL isConst = FALSE;
                if (SUCCEEDED(subType->get_constType(&isConst)) && isConst)
                    return true;
            }
        }

        return false;
    }

private:

    DumpContext& m_ctx;
};
