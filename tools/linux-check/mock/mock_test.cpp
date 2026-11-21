#include <Core/DIA/SymbolDumper.hpp>
#include <Core/Util/String/Utf8.hpp>
#include <cstdio>
#include <memory>
#include <vector>

struct MS;
struct MEnum : IDiaEnumSymbols {
    std::vector<IDiaSymbol*> items; size_t pos = 0;
    HRESULT Next(ULONG, IDiaSymbol** out, ULONG* fetched) override {
        if (pos >= items.size()) { *fetched = 0; return 1; }
        *out = items[pos++]; *fetched = 1; return 0; }
};
static BSTR bs(const std::wstring& s) { return wcsdup(s.c_str()); }
struct MS : IDiaSymbol {
    DWORD tag = SymTagNull; std::wstring name, undec; MS* type = nullptr; ULONGLONG length = 0;
    DWORD rva = 0, section = 0, offset = 0, dataKind = 0, locType = 0, baseType = 0, cc = 0;
    bool hasRva = false, isFunc = false, isCode = false; MS* classParent = nullptr; MS* lexParent = nullptr;
    std::vector<MS*> children;
    HRESULT get_symTag(DWORD* v) override { *v = tag; return 0; }
    HRESULT get_name(BSTR* v) override { if (name.empty()) return E_FAIL; *v = bs(name); return 0; }
    HRESULT get_undecoratedName(BSTR* v) override { if (undec.empty()) return E_FAIL; *v = bs(undec); return 0; }
    HRESULT get_type(IDiaSymbol** v) override { if (!type) return E_FAIL; *v = type; return 0; }
    HRESULT get_length(ULONGLONG* v) override { *v = length; return 0; }
    HRESULT get_relativeVirtualAddress(DWORD* v) override { *v = hasRva ? rva : 0; return 0; }
    HRESULT get_addressSection(DWORD* v) override { *v = section; return 0; }
    HRESULT get_addressOffset(DWORD* v) override { *v = offset; return 0; }
    HRESULT get_dataKind(DWORD* v) override { if (tag != SymTagData) return E_FAIL; *v = dataKind; return 0; }
    HRESULT get_locationType(DWORD* v) override { *v = locType; return 0; }
    HRESULT get_classParent(IDiaSymbol** v) override { if (!classParent) return E_FAIL; *v = classParent; return 0; }
    HRESULT get_lexicalParent(IDiaSymbol** v) override { if (!lexParent) return E_FAIL; *v = lexParent; return 0; }
    HRESULT get_constType(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_volatileType(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_baseType(DWORD* v) override { *v = baseType; return 0; }
    HRESULT get_callingConvention(DWORD* v) override { if (tag != SymTagFunctionType) return E_FAIL; *v = cc; return 0; }
    HRESULT get_constructor(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_virtual(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_pure(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_function(BOOL* v) override { *v = isFunc; return 0; }
    HRESULT get_code(BOOL* v) override { *v = isCode; return 0; }
    HRESULT get_isStatic(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_compilerGenerated(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_isNaked(BOOL* v) override { *v = (name == L"naked_fn"); return 0; }
    HRESULT get_hasSEH(BOOL* v) override { *v = 0; return 0; }
    HRESULT get_noReturn(BOOL* v) override { *v = 0; return 0; }
    HRESULT findChildren(enum SymTagEnum t, const wchar_t* n, DWORD, IDiaEnumSymbols** out) override {
        auto* e = new MEnum();
        for (MS* c : children) { if (t != SymTagNull && c->tag != (DWORD)t) continue; if (n && c->name != n) continue; e->items.push_back(c); }
        *out = e; return 0; }
};
struct MLine : IDiaLineNumber { struct F : IDiaSourceFile { HRESULT get_fileName(BSTR* v) override { *v = bs(L"e:\\src\\game.cpp"); return 0; } } f;
    HRESULT get_sourceFile(IDiaSourceFile** v) override { *v = &f; return 0; } HRESULT get_lineNumber(DWORD* v) override { *v = 42; return 0; } };
struct MLines : IDiaEnumLineNumbers { MLine l; bool done = false; HRESULT Next(ULONG, IDiaLineNumber** o, ULONG* f) override { if (done) { *f = 0; return 1; } done = true; *o = &l; *f = 1; return 0; } };
struct MSession : IDiaSession { DWORD hitOffset = 0;
    HRESULT findLinesByAddr(DWORD sec, DWORD off, DWORD, IDiaEnumLineNumbers** o) override { if (sec == 1 && off == 0) { *o = new MLines(); return 0; } return E_FAIL; } };

static MS* mk(DWORD tag, const wchar_t* name) { auto* s = new MS(); s->tag = tag; s->name = name; return s; }
static void print(const char* title, const std::wstring& w) { printf("===== %s =====\n%s\n", title, wideToUtf8(w).c_str()); }

int main() {
    MS* tInt = mk(SymTagBaseType, L""); tInt->baseType = btInt; tInt->length = 4;
    MS* tVoid = mk(SymTagBaseType, L""); tVoid->baseType = btVoid; tVoid->length = 0;
    // function types
    MS* argInt = mk(SymTagFunctionArgType, L""); argInt->type = tInt;
    MS* ftUpdate = mk(SymTagFunctionType, L""); ftUpdate->type = tVoid; ftUpdate->cc = CV_CALL_NEAR_C; ftUpdate->children = {argInt};
    MS* ftMain = mk(SymTagFunctionType, L""); ftMain->type = tInt; ftMain->cc = CV_CALL_NEAR_STD;
    MS* compiland = mk(SymTagCompiland, L"e:\\obj\\game.obj");
    MS* foo = mk(SymTagUDT, L"Foo");

    MS* f1 = mk(SymTagFunction, L"Game::Update"); f1->type = ftUpdate; f1->hasRva = true; f1->rva = 0x1000; f1->section = 1; f1->offset = 0; f1->length = 0x40; f1->lexParent = compiland;
    MS* p1 = mk(SymTagData, L"dt"); p1->dataKind = DataIsParam; p1->type = tInt; f1->children = {p1};
    MS* f2 = mk(SymTagFunction, L"main"); f2->type = ftMain; f2->hasRva = true; f2->rva = 0x1100; f2->section = 1; f2->offset = 0x100; f2->length = 0x20;
    MS* blk = mk(SymTagBlock, L""); MS* sl = mk(SymTagData, L"counter"); sl->dataKind = DataIsStaticLocal; sl->type = tInt; sl->hasRva = true; sl->rva = 0x2100; sl->section = 2; sl->length = 4; sl->locType = LocIsStatic; sl->lexParent = blk; blk->lexParent = f2; blk->children = {sl}; f2->children = {blk};
    MS* f3 = mk(SymTagFunction, L"Foo::Bar"); f3->type = ftUpdate; f3->classParent = foo; f3->hasRva = true; f3->rva = 0x1200; f3->section = 1; f3->offset = 0x200; f3->length = 0x10;
    MS* f4 = mk(SymTagFunction, L"helper"); f4->type = ftUpdate; f4->hasRva = true; f4->rva = 0x1300; f4->section = 1; f4->offset = 0x300; f4->length = 8; f4->lexParent = compiland;
    MS* f5 = mk(SymTagFunction, L"naked_fn"); f5->type = ftUpdate; f5->hasRva = true; f5->rva = 0x1400; f5->section = 1; f5->offset = 0x400; f5->length = 8;

    MS* d1 = mk(SymTagData, L"g_count"); d1->dataKind = DataIsGlobal; d1->type = tInt; d1->hasRva = true; d1->rva = 0x2000; d1->section = 2; d1->locType = LocIsStatic;
    MS* d2 = mk(SymTagData, L"Game::s_instances"); d2->dataKind = DataIsGlobal; d2->type = tInt; d2->hasRva = true; d2->rva = 0x2010; d2->section = 2; d2->locType = LocIsStatic;
    MS* d3 = mk(SymTagData, L"s_cache"); d3->dataKind = DataIsFileStatic; d3->type = tInt; d3->hasRva = true; d3->rva = 0x2020; d3->section = 2; d3->locType = LocIsStatic; d3->lexParent = compiland;
    MS* d4 = mk(SymTagData, L"Foo::s_count"); d4->dataKind = DataIsStaticMember; d4->type = tInt; d4->classParent = foo; d4->hasRva = true; d4->rva = 0x2030; d4->section = 2; d4->locType = LocIsStatic;
    MS* d5 = mk(SymTagData, L"fld"); d5->dataKind = DataIsMember; d5->type = tInt;  // must never show up
    MS* pub = mk(SymTagPublicSymbol, L"?Baz@@YAXXZ"); pub->undec = L"void __cdecl Baz(void)"; pub->isFunc = true; pub->isCode = true; pub->hasRva = true; pub->rva = 0x3000; pub->section = 1; pub->offset = 0x2000;
    MS* pubd = mk(SymTagPublicSymbol, L"?g_hidden@@3HA"); pubd->undec = L"int g_hidden"; pubd->hasRva = true; pubd->rva = 0x2040; pubd->section = 2;

    compiland->children = {f1, f4, d3, f5};
    MS* global = mk(SymTagExe, L"game.exe");
    global->children = {f1, f2, f3, f5, d1, d2, d4, d5, pub, pubd, compiland};

    MSession session;
    DumpConfig cfg; // defaults
    SymbolDumper dumper(cfg);
    dumper.setSession(&session);

    GlobalQuery all;
    print("FUNCTIONS (default)", dumper.globals().dumpFunctions(global, all));
    print("VARIABLES (default)", dumper.globals().dumpVariables(global, all));
    print("PUBLICS", dumper.globals().dumpPublics(global, all));

    cfg.m_globalsIncludeMembers = true; cfg.m_globalsIncludeLocalStatics = true; cfg.m_globalsSortByAddress = true;
    dumper.setConfig(cfg);
    print("FUNCTIONS (members, by address)", dumper.globals().dumpFunctions(global, all));
    print("VARIABLES (members + local statics)", dumper.globals().dumpVariables(global, all));
    print("SYMBOL TABLE", dumper.globals().dumpSymbolTable(global, all, true));
    GlobalQuery q; q.filter = L"update";
    print("FILTER 'update'", dumper.globals().dumpFunctions(global, q));
    print("BY NAME helper (static fn, found via compiland)", dumper.globals().dumpByName(global, L"helper", false));
    print("BY NAME g_count", dumper.globals().dumpByName(global, L"g_count", false));

    cfg = DumpConfig(); cfg.m_showAddress = false; cfg.m_showSourceLine = false; cfg.m_showCallingConv = false; dumper.setConfig(cfg);
    print("FUNCTIONS (no extras)", dumper.globals().dumpFunctions(global, all));
    dumper.setPlatform(0xD0); cfg = DumpConfig(); dumper.setConfig(cfg);
    print("FUNCTIONS (x64 platform: no calling conv)", dumper.globals().dumpFunctions(global, all));
}
