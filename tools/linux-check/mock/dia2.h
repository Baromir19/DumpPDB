#pragma once
#include <Windows.h>
#include <diacreate.h>
enum SymTagEnum : int {
    CV_CALL_CLRCALL,
    CV_CALL_NEAR_C,
    CV_CALL_NEAR_FAST,
    CV_CALL_NEAR_STD,
    CV_CALL_NEAR_SYS,
    CV_CALL_THISCALL,
    CV_CFL_AMD64,
    CV_private,
    CV_protected,
    CV_public,
    DataIsConstant,
    DataIsFileStatic,
    DataIsGlobal,
    DataIsLocal,
    DataIsMember,
    DataIsObjectPtr,
    DataIsParam,
    DataIsStaticLocal,
    DataIsStaticMember,
    DataIsUnknown,
    LocInMetaData,
    LocIsBitField,
    LocIsConstant,
    LocIsEnregistered,
    LocIsIlRel,
    LocIsNull,
    LocIsRegRel,
    LocIsSlot,
    LocIsStatic,
    LocIsTLS,
    LocIsThisRel,
    SymTagAnnotation,
    SymTagArrayType,
    SymTagBaseClass,
    SymTagBaseType,
    SymTagBlock,
    SymTagCompiland,
    SymTagCompilandDetails,
    SymTagCompilandEnv,
    SymTagCustom,
    SymTagCustomType,
    SymTagData,
    SymTagDimension,
    SymTagEnum,
    SymTagExe,
    SymTagFriend,
    SymTagFunction,
    SymTagFunctionArgType,
    SymTagFunctionType,
    SymTagLabel,
    SymTagManagedType,
    SymTagNull,
    SymTagPointerType,
    SymTagPublicSymbol,
    SymTagThunk,
    SymTagTypedef,
    SymTagUDT,
    SymTagVTable,
    SymTagVTableShape,
    UdtClass,
    UdtInterface,
    UdtStruct,
    UdtUnion,
    btBCD,
    btBSTR,
    btBit,
    btBool,
    btChar,
    btChar16,
    btChar32,
    btChar8,
    btComplex,
    btCurrency,
    btDate,
    btFloat,
    btHresult,
    btInt,
    btLong,
    btNoType,
    btUInt,
    btULong,
    btVariant,
    btVoid,
    btWChar,
    nsCaseInsensitive,
    nsCaseSensitive,
    nsNone,
};
struct IUnknownStub { virtual ~IUnknownStub() {} ULONG AddRef() { return 1; } ULONG Release() { return 1; } template<class... A> HRESULT QueryInterface(A&&...) { return E_FAIL; } };
struct IDiaSymbol; struct IDiaEnumSymbols; struct IDiaSourceFile; struct IDiaLineNumber; struct IDiaEnumLineNumbers;
struct IDiaSymbol : IUnknownStub {
    template<class... A> HRESULT get_guid(A&&...) { return 0; }
    template<class... A> HRESULT get_age(A&&...) { return 0; }
    template<class... A> HRESULT get_signature(A&&...) { return 0; }
    virtual HRESULT get_symTag(DWORD*) { return E_FAIL; }
    virtual HRESULT get_name(BSTR*) { return E_FAIL; }
    virtual HRESULT get_undecoratedName(BSTR*) { return E_FAIL; }
    virtual HRESULT get_type(IDiaSymbol**) { return E_FAIL; }
    virtual HRESULT get_length(ULONGLONG*) { return E_FAIL; }
    virtual HRESULT get_relativeVirtualAddress(DWORD*) { return E_FAIL; }
    virtual HRESULT get_addressSection(DWORD*) { return E_FAIL; }
    virtual HRESULT get_addressOffset(DWORD*) { return E_FAIL; }
    virtual HRESULT get_dataKind(DWORD*) { return E_FAIL; }
    virtual HRESULT get_locationType(DWORD*) { return E_FAIL; }
    virtual HRESULT get_classParent(IDiaSymbol**) { return E_FAIL; }
    virtual HRESULT get_lexicalParent(IDiaSymbol**) { return E_FAIL; }
    virtual HRESULT get_constType(BOOL*) { return E_FAIL; }
    virtual HRESULT get_volatileType(BOOL*) { return E_FAIL; }
    virtual HRESULT get_baseType(DWORD*) { return E_FAIL; }
    virtual HRESULT get_callingConvention(DWORD*) { return E_FAIL; }
    virtual HRESULT get_constructor(BOOL*) { return E_FAIL; }
    virtual HRESULT get_virtual(BOOL*) { return E_FAIL; }
    virtual HRESULT get_pure(BOOL*) { return E_FAIL; }
    virtual HRESULT get_function(BOOL*) { return E_FAIL; }
    virtual HRESULT get_code(BOOL*) { return E_FAIL; }
    virtual HRESULT get_isStatic(BOOL*) { return E_FAIL; }
    virtual HRESULT get_compilerGenerated(BOOL*) { return E_FAIL; }
    virtual HRESULT get_isNaked(BOOL*) { return E_FAIL; }
    virtual HRESULT get_hasSEH(BOOL*) { return E_FAIL; }
    virtual HRESULT get_noReturn(BOOL*) { return E_FAIL; }
    virtual HRESULT get_value(VARIANT*) { return E_FAIL; }
    virtual HRESULT get_bitPosition(DWORD*) { return E_FAIL; }
    virtual HRESULT findChildren(enum SymTagEnum, const wchar_t*, DWORD, IDiaEnumSymbols**) { return E_FAIL; }
    template<class... A> HRESULT get_RValueReference(A&&...) { return 0; }
    template<class... A> HRESULT get_access(A&&...) { return 0; }
    template<class... A> HRESULT get_compilands(A&&...) { return 0; }
    template<class... A> HRESULT get_compilerName(A&&...) { return 0; }
    template<class... A> HRESULT get_count(A&&...) { return 0; }
    template<class... A> HRESULT get_fileName(A&&...) { return 0; }
    template<class... A> HRESULT get_globalScope(A&&...) { return 0; }
    template<class... A> HRESULT get_hasDebugInfo(A&&...) { return 0; }
    template<class... A> HRESULT get_hasEH(A&&...) { return 0; }
    template<class... A> HRESULT get_hasInlAsm(A&&...) { return 0; }
    template<class... A> HRESULT get_imagebase(A&&...) { return 0; }
    template<class... A> HRESULT get_inlSpec(A&&...) { return 0; }
    template<class... A> HRESULT get_intro(A&&...) { return 0; }
    template<class... A> HRESULT get_language(A&&...) { return 0; }
    template<class... A> HRESULT get_lineNumber(A&&...) { return 0; }
    template<class... A> HRESULT get_nested(A&&...) { return 0; }
    template<class... A> HRESULT get_noInline(A&&...) { return 0; }
    template<class... A> HRESULT get_noexcept(A&&...) { return 0; }
    template<class... A> HRESULT get_objectPointerType(A&&...) { return 0; }
    template<class... A> HRESULT get_offset(A&&...) { return 0; }
    template<class... A> HRESULT get_platform(A&&...) { return 0; }
    template<class... A> HRESULT get_rank(A&&...) { return 0; }
    template<class... A> HRESULT get_reference(A&&...) { return 0; }
    template<class... A> HRESULT get_sealed(A&&...) { return 0; }
    template<class... A> HRESULT get_sourceFile(A&&...) { return 0; }
    template<class... A> HRESULT get_stride(A&&...) { return 0; }
    template<class... A> HRESULT get_symIndexId(A&&...) { return 0; }
    template<class... A> HRESULT get_udtKind(A&&...) { return 0; }
    template<class... A> HRESULT get_virtualBaseClass(A&&...) { return 0; }
    template<class... A> HRESULT get_virtualBaseOffset(A&&...) { return 0; }
};
struct IDiaSymbol4 : IDiaSymbol { template<class... A> HRESULT get_noexcept(A&&...) { return 0; } };
struct IDiaEnumSymbols : IUnknownStub { virtual HRESULT Next(ULONG, IDiaSymbol**, ULONG*) { return 1; } };
struct IDiaSourceFile : IUnknownStub { virtual HRESULT get_fileName(BSTR*) { return E_FAIL; } template<class... A> HRESULT get_compilands(A&&...) { return 0; } };
struct IDiaLineNumber : IUnknownStub { virtual HRESULT get_sourceFile(IDiaSourceFile**) { return E_FAIL; } virtual HRESULT get_lineNumber(DWORD*) { return E_FAIL; } template<class... A> HRESULT get_lineNumberEnd(A&&...) { return 0; } };
struct IDiaEnumLineNumbers : IUnknownStub { virtual HRESULT Next(ULONG, IDiaLineNumber**, ULONG*) { return 1; } };
struct IDiaEnumSourceFiles : IUnknownStub { template<class... A> HRESULT Next(A&&...) { return 1; } };
struct IDiaSession : IUnknownStub {
    virtual HRESULT findLinesByAddr(DWORD, DWORD, DWORD, IDiaEnumLineNumbers**) { return E_FAIL; }
    template<class... A> HRESULT findFile(A&&...) { return E_FAIL; }
    template<class... A> HRESULT get_globalScope(A&&...) { return 0; }
};
struct IDiaDataSource : IUnknownStub { template<class... A> HRESULT loadDataFromPdb(A&&...) { return 0; } template<class... A> HRESULT openSession(A&&...) { return 0; } template<class... A> HRESULT loadAndValidateDataFromPdb(A&&...) { return 0; } };
