#pragma once 

#include <windows.h>
#include <string>

#define LANGUAGE_STRING_PREFIX STR_

#define GENERATE_STRING(id, val_c) StringLanguageSytnax _CONCAT(LANGUAGE_STRING_PREFIX, id) = StringLanguageSytnax(L ## # id, val_c);
#define GENERATE_STATIC_STRING(id, val_c) static inline GENERATE_STRING(id, val_c)
#define GET_STRING(id) StringManager::Table:: ## _CONCAT(LANGUAGE_STRING_PREFIX, id) ## .getValue()

class StringManager
{
public:
    static std::string convertWCharToChar(const wchar_t* a_string)
    {
        if (!a_string) return "";

        int _sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, a_string, -1, nullptr, 0, nullptr, nullptr);
        if (_sizeNeeded <= 0) return "";

        std::string _result(_sizeNeeded - 1, 0); // -1 to remove null terminator
        WideCharToMultiByte(CP_UTF8, 0, a_string, -1, &_result[0], _sizeNeeded, nullptr, nullptr);

        return _result;
    }

    static std::wstring convertCharToWChar(const char* a_string)
    {
        if (!a_string) return L"";

        int _sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, a_string, -1, nullptr, 0);
        if (_sizeNeeded <= 0) return L"";

        std::wstring _result(_sizeNeeded - 1, 0); // -1 to remove null terminator
        MultiByteToWideChar(CP_UTF8, 0, a_string, -1, &_result[0], _sizeNeeded);

        return _result;
    }

    template<typename T>
    static T convertWCharToInt(const wchar_t* a_string)
    {
        return static_cast<T>(wcstoll(a_string, nullptr, 10));
    }

public:
    class Table
    {
    protected:
        const class StringLanguageSytnax
        {
        private:
            const wchar_t* m_id;
            const wchar_t* m_valueC = L"";

        public:
            // StringLanguageSytnax() : m_id(0) {}
            StringLanguageSytnax(const wchar_t* a_id, const wchar_t* a_valC)
                : m_id(a_id), m_valueC(a_valC) { }

            const wchar_t* getId() const { return m_id; }

            const wchar_t* getValue() const { return *this; }

            operator const wchar_t*() const
            {
                switch (s_languageSyntax)
                {
                case LanguageSyntax::LANGUAGE_ID: return m_id;
                case LanguageSyntax::LANGUAGE_C: return m_valueC;
                default: break;
                }
                return L"";
            }

        private:
            StringLanguageSytnax(const StringLanguageSytnax&) = delete;
            StringLanguageSytnax& operator=(const StringLanguageSytnax&) = delete;
        };

    public:
        enum class LanguageSyntax : unsigned int
        {
            LANGUAGE_ID = 0, // dbg
            LANGUAGE_C
        };

        static constexpr auto s_languageSyntax = LanguageSyntax::LANGUAGE_C;
        
		/// TABLE OF STRINGS:

        // General syntax
		GENERATE_STATIC_STRING(STATEMENT_TERMINATOR,    L";")
		GENERATE_STATIC_STRING(REFERENCE,               L"&")
		GENERATE_STATIC_STRING(POINTER,                 L"*")

        GENERATE_STATIC_STRING(LINE_COMMENT,            L"//")

        GENERATE_STATIC_STRING(DOC_COMMENT,             L"///")

        GENERATE_STATIC_STRING(MULTILINE_COMMENT_BEGIN, L"/*")
        GENERATE_STATIC_STRING(MULTILINE_COMMENT_END,   L"*/")

        GENERATE_STATIC_STRING(SCOPE_BEGIN,             L"{")
        GENERATE_STATIC_STRING(SCOPE_END,               L"}")

        GENERATE_STATIC_STRING(PARAMETERS_BEGIN,        L"(")
        GENERATE_STATIC_STRING(PARAMETERS_END,          L")")

        // Names and types syntax
        GENERATE_STATIC_STRING(NAMESPACE,   L"namespace")
        GENERATE_STATIC_STRING(STRUCT,      L"struct")
        GENERATE_STATIC_STRING(CLASS,       L"class")
        GENERATE_STATIC_STRING(UNION,       L"union")
        GENERATE_STATIC_STRING(TYPEDEF,     L"typedef")

        GENERATE_STATIC_STRING(BOOL,        L"bool")

        GENERATE_STATIC_STRING(FLOAT,       L"float")
        GENERATE_STATIC_STRING(DOUBLE,      L"double")

        GENERATE_STATIC_STRING(CHAR,        L"char")

        GENERATE_STATIC_STRING(INT8,        L"__int8")
        GENERATE_STATIC_STRING(INT16,       L"__int16")
        GENERATE_STATIC_STRING(INT32,       L"__int32")
        GENERATE_STATIC_STRING(INT64,       L"__int64")

        GENERATE_STATIC_STRING(UINT8,       L"unsigned __int8")
        GENERATE_STATIC_STRING(UINT16,      L"unsigned __int16")
        GENERATE_STATIC_STRING(UINT32,      L"unsigned __int32")
        GENERATE_STATIC_STRING(UINT64,      L"unsigned __int64")

        // Mods and decltypes
        GENERATE_STATIC_STRING(STATIC,      L"static")
        GENERATE_STATIC_STRING(CONST,       L"const") // MACRO TROUBLE
        GENERATE_STATIC_STRING(VOLATILE,    L"volatile")

        GENERATE_STATIC_STRING(FASTCALL,    L"__fastcall")
        GENERATE_STATIC_STRING(CDECL,       L"__cdecl") // MACRO TROUBLE

        /// TABLE OF STRINGS END
    };
};