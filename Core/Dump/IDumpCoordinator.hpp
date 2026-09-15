#pragma once

#include <string>
#include <unordered_set>

#include <dia2.h>

/// Internal dispatch interface between dump-domain renderers.
/// Renderers are mutually recursive, so instead of including each other they
/// communicate through this interface. SymbolDumper implements it privately.
struct IDumpCoordinator
{
    virtual ~IDumpCoordinator() = default;

    virtual void pushScope(const std::wstring& a_name) = 0;
    virtual void popScope() = 0;
    virtual void pushQualifiedScope(const std::wstring& a_qualifiedName) = 0;
    virtual void popQualifiedScope(size_t a_partCount) = 0;

    virtual std::wstring dumpTopLevelAny(IDiaSymbol* a_symbol) = 0;
    virtual void processType(IDiaSymbol* a_symbol, std::wstring& a_output) = 0;

    virtual std::wstring dumpClass(IDiaSymbol* a_symbol, int a_nestingLevel) = 0;
    virtual std::wstring dumpAnonymousUDT(IDiaSymbol* a_udtSymbol, int a_nestingLevel) = 0;
    virtual std::wstring dumpMembers(IDiaSymbol* a_symbol, int a_nestingLevel) = 0;
    virtual DWORD emitAccessLabel(
        std::wstring& a_output, IDiaSymbol* a_symbol, DWORD a_lastAccess, int a_nestingLevel)
        = 0;

    virtual std::wstring dumpEnum(IDiaSymbol* a_symbol, int a_nestingLevel) = 0;
    virtual std::wstring dumpTypedef(IDiaSymbol* a_symbol, int a_nestingLevel) = 0;
    virtual std::wstring dumpFunction(IDiaSymbol* a_symbol, int a_nestingLevel) = 0;
    virtual std::wstring dumpFriend(IDiaSymbol* a_symbol, int a_nestingLevel) = 0;

    virtual std::wstring getTypeSourceFiles(IDiaSymbol* a_symbol) = 0;
    virtual void registerTypeSource(IDiaSymbol* a_symbol) = 0;
    virtual std::wstring typeSources() = 0;
};
