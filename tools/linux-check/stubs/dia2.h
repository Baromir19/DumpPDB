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
enum NameSearchOptions { nsNone_unused };
struct IUnknownStub { virtual ~IUnknownStub() {} template<class... A> ULONG AddRef(A&&...) { return 1; } template<class... A> ULONG Release(A&&...) { return 0; } template<class... A> HRESULT QueryInterface(A&&...) { return 0; } };
struct IDiaSymbol : IUnknownStub {
    template<class... A> HRESULT get_guid(A&&...) { return 0; }
    template<class... A> HRESULT get_age(A&&...) { return 0; }
    template<class... A> HRESULT get_signature(A&&...) { return 0; }
    template<class... A> HRESULT get_RValueReference(A&&...) { return 0; }
    template<class... A> HRESULT get_access(A&&...) { return 0; }
    template<class... A> HRESULT get_addressOffset(A&&...) { return 0; }
    template<class... A> HRESULT get_addressSection(A&&...) { return 0; }
    template<class... A> HRESULT get_baseType(A&&...) { return 0; }
    template<class... A> HRESULT get_bitPosition(A&&...) { return 0; }
    template<class... A> HRESULT get_callingConvention(A&&...) { return 0; }
    template<class... A> HRESULT get_classParent(A&&...) { return 0; }
    template<class... A> HRESULT get_code(A&&...) { return 0; }
    template<class... A> HRESULT get_compilands(A&&...) { return 0; }
    template<class... A> HRESULT get_compilerGenerated(A&&...) { return 0; }
    template<class... A> HRESULT get_compilerName(A&&...) { return 0; }
    template<class... A> HRESULT get_constType(A&&...) { return 0; }
    template<class... A> HRESULT get_constructor(A&&...) { return 0; }
    template<class... A> HRESULT get_count(A&&...) { return 0; }
    template<class... A> HRESULT get_dataKind(A&&...) { return 0; }
    template<class... A> HRESULT get_fileName(A&&...) { return 0; }
    template<class... A> HRESULT get_function(A&&...) { return 0; }
    template<class... A> HRESULT get_globalScope(A&&...) { return 0; }
    template<class... A> HRESULT get_hasDebugInfo(A&&...) { return 0; }
    template<class... A> HRESULT get_hasEH(A&&...) { return 0; }
    template<class... A> HRESULT get_hasInlAsm(A&&...) { return 0; }
    template<class... A> HRESULT get_hasSEH(A&&...) { return 0; }
    template<class... A> HRESULT get_imagebase(A&&...) { return 0; }
    template<class... A> HRESULT get_inlSpec(A&&...) { return 0; }
    template<class... A> HRESULT get_intro(A&&...) { return 0; }
    template<class... A> HRESULT get_isNaked(A&&...) { return 0; }
    template<class... A> HRESULT get_isStatic(A&&...) { return 0; }
    template<class... A> HRESULT get_language(A&&...) { return 0; }
    template<class... A> HRESULT get_length(A&&...) { return 0; }
    template<class... A> HRESULT get_lexicalParent(A&&...) { return 0; }
    template<class... A> HRESULT get_lineNumber(A&&...) { return 0; }
    template<class... A> HRESULT get_locationType(A&&...) { return 0; }
    template<class... A> HRESULT get_name(A&&...) { return 0; }
    template<class... A> HRESULT get_nested(A&&...) { return 0; }
    template<class... A> HRESULT get_noInline(A&&...) { return 0; }
    template<class... A> HRESULT get_noReturn(A&&...) { return 0; }
    template<class... A> HRESULT get_noexcept(A&&...) { return 0; }
    template<class... A> HRESULT get_objectPointerType(A&&...) { return 0; }
    template<class... A> HRESULT get_offset(A&&...) { return 0; }
    template<class... A> HRESULT get_platform(A&&...) { return 0; }
    template<class... A> HRESULT get_pure(A&&...) { return 0; }
    template<class... A> HRESULT get_rank(A&&...) { return 0; }
    template<class... A> HRESULT get_reference(A&&...) { return 0; }
    template<class... A> HRESULT get_relativeVirtualAddress(A&&...) { return 0; }
    template<class... A> HRESULT get_sealed(A&&...) { return 0; }
    template<class... A> HRESULT get_sourceFile(A&&...) { return 0; }
    template<class... A> HRESULT get_stride(A&&...) { return 0; }
    template<class... A> HRESULT get_symIndexId(A&&...) { return 0; }
    template<class... A> HRESULT get_symTag(A&&...) { return 0; }
    template<class... A> HRESULT get_type(A&&...) { return 0; }
    template<class... A> HRESULT get_udtKind(A&&...) { return 0; }
    template<class... A> HRESULT get_undecoratedName(A&&...) { return 0; }
    template<class... A> HRESULT get_value(A&&...) { return 0; }
    template<class... A> HRESULT get_virtual(A&&...) { return 0; }
    template<class... A> HRESULT get_virtualBaseClass(A&&...) { return 0; }
    template<class... A> HRESULT get_virtualBaseOffset(A&&...) { return 0; }
    template<class... A> HRESULT get_volatileType(A&&...) { return 0; }
    template<class... A> HRESULT findChildren(A&&...) { return 0; }
    template<class... A> HRESULT findFile(A&&...) { return 0; }
    template<class... A> HRESULT findLinesByAddr(A&&...) { return 0; }
    template<class... A> HRESULT loadDataFromPdb(A&&...) { return 0; }
    template<class... A> HRESULT openSession(A&&...) { return 0; }
};
struct IDiaSymbol4 : IDiaSymbol { template<class... A> HRESULT get_noexcept(A&&...) { return 0; } };
struct IDiaLineNumber : IUnknownStub { template<class... A> HRESULT get_sourceFile(A&&...) { return 0; } template<class... A> HRESULT get_lineNumber(A&&...) { return 0; } template<class... A> HRESULT get_lineNumberEnd(A&&...) { return 0; } };
struct IDiaSourceFile : IUnknownStub { template<class... A> HRESULT get_fileName(A&&...) { return 0; } template<class... A> HRESULT get_compilands(A&&...) { return 0; } };
struct IDiaEnumSymbols : IUnknownStub { template<class... A> HRESULT Next(A&&...) { return 0; } };
struct IDiaEnumSourceFiles : IUnknownStub { template<class... A> HRESULT Next(A&&...) { return 0; } };
struct IDiaEnumLineNumbers : IUnknownStub { template<class... A> HRESULT Next(A&&...) { return 0; } };
struct IDiaSession : IUnknownStub {
    template<class... A> HRESULT findLinesByAddr(A&&...) { return 0; }
    template<class... A> HRESULT findFile(A&&...) { return 0; }
    template<class... A> HRESULT get_globalScope(A&&...) { return 0; }
    template<class... A> HRESULT findSymbolByRVA(A&&...) { return 0; }
    template<class... A> HRESULT put_loadAddress(A&&...) { return 0; }
};
struct IDiaDataSource : IUnknownStub { template<class... A> HRESULT loadDataFromPdb(A&&...) { return 0; } template<class... A> HRESULT openSession(A&&...) { return 0; } template<class... A> HRESULT loadAndValidateDataFromPdb(A&&...) { return 0; } };
