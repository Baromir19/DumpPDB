#pragma once

#include <cstdio>
#include <string>

#include <dia2.h>

#include <Core/Util/Com/ComPtr.hpp>

/// Stateless helpers that read where a symbol lives in the image: RVA,
/// section:offset, size, and (for code) the source file and line.
///
/// Used by the class dumper (member RVA annotations) and by the global
/// symbol renderer, so the "// RVA ..." comment looks the same everywhere.
class AddressInfo
{
public:

    struct Location
    {
        bool hasRva = false;
        DWORD rva = 0;
        DWORD section = 0;
        DWORD offset = 0;
        ULONGLONG length = 0; ///< Code length for functions, type size for data.
        DWORD locationType = 0; ///< LocationType (LocIsStatic, LocIsTLS, LocIsConstant, ...).
    };

    /// BSTR -> std::wstring, always frees the BSTR.
    static std::wstring takeBstr(BSTR a_bstr)
    {
        std::wstring out;
        if (a_bstr)
        {
            out = a_bstr;
            SysFreeString(a_bstr);
        }
        return out;
    }

    /// Reads the location of a function/data/public/thunk/label symbol.
    /// For data symbols length is the size of the variable's type.
    static Location locate(IDiaSymbol* a_symbol)
    {
        Location loc;
        if (!a_symbol)
            return loc;

        DWORD tag = SymTagNull;
        a_symbol->get_symTag(&tag);

        DWORD locType = 0;
        if (SUCCEEDED(a_symbol->get_locationType(&locType)))
            loc.locationType = locType;

        DWORD rva = 0;
        if (SUCCEEDED(a_symbol->get_relativeVirtualAddress(&rva)) && rva != 0)
        {
            loc.hasRva = true;
            loc.rva = rva;
        }

        a_symbol->get_addressSection(&loc.section);
        a_symbol->get_addressOffset(&loc.offset);

        if (tag == SymTagData)
        {
            ComPtr<IDiaSymbol> type;
            if (SUCCEEDED(a_symbol->get_type(&type)) && type)
            {
                ULONGLONG size = 0;
                if (SUCCEEDED(type->get_length(&size)))
                    loc.length = size;
            }
        }
        else
        {
            ULONGLONG len = 0;
            if (SUCCEEDED(a_symbol->get_length(&len)))
                loc.length = len;
        }

        return loc;
    }

    /// First source file/line that covers a function. Needs line info in the PDB.
    static bool firstSourceLine(
        IDiaSession* a_session, IDiaSymbol* a_function, std::wstring& a_file, DWORD& a_line)
    {
        a_file.clear();
        a_line = 0;
        if (!a_session || !a_function)
            return false;

        DWORD section = 0;
        DWORD offset = 0;
        ULONGLONG length = 0;
        if (FAILED(a_function->get_addressSection(&section))
            || FAILED(a_function->get_addressOffset(&offset)) || section == 0)
            return false;
        a_function->get_length(&length);

        ComPtr<IDiaEnumLineNumbers> lines;
        if (FAILED(a_session->findLinesByAddr(
                section, offset, static_cast<DWORD>(length ? length : 1), &lines))
            || !lines)
            return false;

        ComPtr<IDiaLineNumber> line;
        ULONG celt = 0;
        if (FAILED(lines->Next(1, &line, &celt)) || celt != 1 || !line)
            return false;

        ComPtr<IDiaSourceFile> file;
        if (SUCCEEDED(line->get_sourceFile(&file)) && file)
        {
            BSTR name = nullptr;
            if (SUCCEEDED(file->get_fileName(&name)))
                a_file = takeBstr(name);
        }

        DWORD number = 0;
        if (SUCCEEDED(line->get_lineNumber(&number)))
            a_line = number;

        return !a_file.empty();
    }

    /// "// RVA 0x00012340 [0001:00012340] len 0x40" (empty when there is no address).
    static std::wstring comment(const Location& a_loc, bool a_withLength = true)
    {
        if (!a_loc.hasRva)
            return L"";

        wchar_t buf[128];
        int n = swprintf_s(buf, L"// RVA 0x%08X [%04X:%08X]", a_loc.rva, a_loc.section, a_loc.offset);
        std::wstring out(buf, n > 0 ? static_cast<size_t>(n) : 0);

        if (a_withLength && a_loc.length != 0)
        {
            swprintf_s(buf, L" len 0x%llX", a_loc.length);
            out += buf;
        }
        return out;
    }

    /// Convenience: comment for a symbol, or "" when it has no address.
    static std::wstring comment(IDiaSymbol* a_symbol, bool a_withLength = true)
    {
        return comment(locate(a_symbol), a_withLength);
    }
};
