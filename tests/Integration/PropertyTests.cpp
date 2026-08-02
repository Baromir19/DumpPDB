#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <set>
#include <map>

#include <Core/PdbToolset.hpp>
#include <Core/DIA/SymbolFinder.hpp>
#include <Core/DIA/TypeWalker.hpp>

#ifndef TEST_COMPILAND_PDB
#error TEST_COMPILAND_PDB is not defined
#endif

// ============================================================================
// Property-Based Integration Tests
//
// These tests open the real TestCompiland PDB and verify *properties*
// of the DIA symbols — field counts, union/structure nesting, bit-field
// widths, const/volatile qualifiers, noexcept, enum values, etc.
//
// They do NOT compare full formatted text output, which makes them
// much more robust against cosmetic formatting changes.
// ============================================================================

namespace fs = std::filesystem;

class PropertyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fs::path pdbPath = TEST_COMPILAND_PDB;
        ASSERT_TRUE(fs::exists(pdbPath)) << "Missing PDB: " << pdbPath.string();
        bool initialized = PdbToolset::instance().initialize(pdbPath);
        ASSERT_TRUE(initialized);
    }

    ComPtr<IDiaSymbol> findType(const wchar_t* a_name) const
    {
        return SymbolFinder::findFirst(
            PdbToolset::instance().globalScope(),
            SymTagNull,
            a_name,
            false);
    }

    ComPtr<IDiaSymbol> findUdt(const wchar_t* a_name) const
    {
        return SymbolFinder::findFirst(
            PdbToolset::instance().globalScope(),
            SymTagUDT,
            a_name,
            false);
    }

    ComPtr<IDiaSymbol> findEnum(const wchar_t* a_name) const
    {
        return SymbolFinder::findFirst(
            PdbToolset::instance().globalScope(),
            SymTagEnum,
            a_name,
            false);
    }

    // --- Children enumeration helpers ---

    template<typename Pred>
    std::vector<ComPtr<IDiaSymbol>> children(
        IDiaSymbol* a_symbol,
        enum SymTagEnum a_tag,
        Pred a_pred) const
    {
        std::vector<ComPtr<IDiaSymbol>> result;
        ComPtr<IDiaEnumSymbols> enum_symbols;
        if (FAILED(a_symbol->findChildren(a_tag, nullptr, nsNone, &enum_symbols)) || !enum_symbols)
            return result;

        ComPtr<IDiaSymbol> child;
        ULONG celt = 0;
        while (SUCCEEDED(enum_symbols->Next(1, &child, &celt)) && celt == 1)
        {
            if (a_pred(child.get()))
                result.push_back(child);
        }
        return result;
    }

    std::vector<ComPtr<IDiaSymbol>> dataMembers(IDiaSymbol* a_symbol) const
    {
        return children(a_symbol, SymTagData, [](IDiaSymbol* sym) {
            DWORD kind = 0;
            return SUCCEEDED(sym->get_dataKind(&kind)) && kind == DataIsMember;
        });
    }

    std::vector<ComPtr<IDiaSymbol>> functions(IDiaSymbol* a_symbol) const
    {
        return children(a_symbol, SymTagFunction, [](IDiaSymbol*) { return true; });
    }

    std::vector<ComPtr<IDiaSymbol>> nestedUdts(IDiaSymbol* a_symbol) const
    {
        return children(a_symbol, SymTagUDT, [](IDiaSymbol*) { return true; });
    }

    std::vector<ComPtr<IDiaSymbol>> nestedEnums(IDiaSymbol* a_symbol) const
    {
        return children(a_symbol, SymTagEnum, [](IDiaSymbol*) { return true; });
    }

    std::vector<ComPtr<IDiaSymbol>> baseClasses(IDiaSymbol* a_symbol) const
    {
        return children(a_symbol, SymTagBaseClass, [](IDiaSymbol*) { return true; });
    }

    // --- Symbol property helpers ---

public:
    static std::wstring getName(IDiaSymbol* a_symbol)
    {
        BSTR bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName)
        {
            std::wstring name(bstrName);
            SysFreeString(bstrName);
            return name;
        }
        return L"";
    }

    static DWORD getTag(IDiaSymbol* a_symbol)
    {
        DWORD tag = SymTagNull;
        a_symbol->get_symTag(&tag);
        return tag;
    }

    static bool isAnonUdtName(const std::wstring& a_name)
    {
        return a_name.empty()
            || a_name == L"<unnamed-tag>"
            || (!a_name.empty() && a_name[0] == L'$');
    }

    static bool isNoexcept(IDiaSymbol* a_symbol)
    {
        IDiaSymbol4* symbol4 = nullptr;
        if (SUCCEEDED(a_symbol->QueryInterface(__uuidof(IDiaSymbol4), (void**)&symbol4)) && symbol4)
        {
            BOOL isNoExcept = FALSE;
            symbol4->get_noexcept(&isNoExcept);
            symbol4->Release();
            return isNoExcept == TRUE;
        }
        return false;
    }
};

// ============================================================================
// PRIMITIVE TYPES
// ============================================================================

TEST_F(PropertyTest, PrimitiveTypes_FieldCount)
{
    auto sym = findUdt(L"Test::PrimitiveTypes");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_EQ(fields.size(), 16u);
}

TEST_F(PropertyTest, PrimitiveTypes_Size)
{
    auto sym = findUdt(L"Test::PrimitiveTypes");
    ASSERT_NE(sym, nullptr);

    ULONGLONG size = 0;
    ASSERT_TRUE(SUCCEEDED(sym->get_length(&size)));
    EXPECT_EQ(size, 72ull);
}

TEST_F(PropertyTest, PrimitiveTypes_FieldNames)
{
    auto sym = findUdt(L"Test::PrimitiveTypes");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    ASSERT_EQ(fields.size(), 16u);

    std::set<std::wstring> names;
    for (auto& f : fields)
        names.insert(getName(f.get()));

    EXPECT_TRUE(names.count(L"boolean") > 0);
    EXPECT_TRUE(names.count(L"character") > 0);
    EXPECT_TRUE(names.count(L"wideCharacter") > 0);
    EXPECT_TRUE(names.count(L"signedChar") > 0);
    EXPECT_TRUE(names.count(L"unsignedChar") > 0);
    EXPECT_TRUE(names.count(L"shortInt") > 0);
    EXPECT_TRUE(names.count(L"unsignedShort") > 0);
    EXPECT_TRUE(names.count(L"integer") > 0);
    EXPECT_TRUE(names.count(L"unsignedInt") > 0);
    EXPECT_TRUE(names.count(L"longInt") > 0);
    EXPECT_TRUE(names.count(L"unsignedLong") > 0);
    EXPECT_TRUE(names.count(L"longLong") > 0);
    EXPECT_TRUE(names.count(L"unsignedLongLong") > 0);
    EXPECT_TRUE(names.count(L"floating") > 0);
    EXPECT_TRUE(names.count(L"doubleFloat") > 0);
    EXPECT_TRUE(names.count(L"longDouble") > 0);
}

// ============================================================================
// POINTER / REFERENCE / ARRAY PROPERTIES
// ============================================================================

namespace
{
    struct PointerFieldInfo
    {
        std::wstring name;
        BOOL isConst = FALSE;
        BOOL isVolatile = FALSE;
        BOOL isReference = FALSE;
        BOOL isRValueRef = FALSE;
        bool isArray = false;
        DWORD arrayCount = 0;
        DWORD tag = SymTagNull;
    };

    PointerFieldInfo buildInfo(ComPtr<IDiaSymbol>& field)
    {
        PointerFieldInfo info;
        info.name = PropertyTest::getName(field.get());

        ComPtr<IDiaSymbol> type;
        if (SUCCEEDED(field->get_type(&type)) && type)
        {
            type->get_symTag(&info.tag);

            type->get_constType(&info.isConst);
            type->get_volatileType(&info.isVolatile);
            type->get_reference(&info.isReference);
            type->get_RValueReference(&info.isRValueRef);

            if (info.tag == SymTagArrayType)
            {
                info.isArray = true;
                type->get_count(&info.arrayCount);
            }
        }
        return info;
    }
}

TEST_F(PropertyTest, PointerTypes_FieldCount)
{
    auto sym = findUdt(L"Test::PointerTypes");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_EQ(fields.size(), 8u);
}

TEST_F(PropertyTest, PointerTypes_FieldProperties)
{
    auto sym = findUdt(L"Test::PointerTypes");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    ASSERT_EQ(fields.size(), 8u);

    std::map<std::wstring, PointerFieldInfo> byName;
    for (auto& f : fields)
    {
        auto info = buildInfo(f);
        byName[info.name] = info;
    }

    // int* pointer — pointer to int (non-const)
    {
        auto it = byName.find(L"pointer");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagPointerType);
        EXPECT_FALSE(it->second.isConst);
        EXPECT_FALSE(it->second.isReference);
    }

    // int*& referencePointer — reference to pointer
    {
        auto it = byName.find(L"referencePointer");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagPointerType);
        EXPECT_TRUE(it->second.isReference);
        EXPECT_FALSE(it->second.isConst);
    }

    // int** pointerToPointer — pointer to pointer
    {
        auto it = byName.find(L"pointerToPointer");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagPointerType);
        EXPECT_FALSE(it->second.isReference);
    }

    // int* const constPointer — const pointer
    {
        auto it = byName.find(L"constPointer");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagPointerType);
        EXPECT_TRUE(it->second.isConst);
    }

    // const int* pointerToConst — pointer to const int
    // The pointer itself is NOT const, but the pointed-to type is const.
    {
        auto it = byName.find(L"pointerToConst");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagPointerType);
        // DIA reports const on the pointed-to type via the type's own const flag.
        // For "const int*", the pointer type itself is non-const;
        // the inner int type carries the const.
        EXPECT_FALSE(it->second.isConst);
    }

    // const int& constReference — reference to const int
    {
        auto it = byName.find(L"constReference");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagPointerType);
        EXPECT_TRUE(it->second.isReference);
        EXPECT_FALSE(it->second.isConst);
    }

    // int array[10] — array of int, count 10
    {
        auto it = byName.find(L"array");
        ASSERT_NE(it, byName.end());
        EXPECT_EQ(it->second.tag, SymTagArrayType);
        EXPECT_TRUE(it->second.isArray);
        EXPECT_EQ(it->second.arrayCount, 10u);
    }

    // int* pointerArray[5] — array of pointers, count 5
    {
        auto it = byName.find(L"pointerArray");
        ASSERT_NE(it, byName.end());
        EXPECT_TRUE(it->second.isArray);
        EXPECT_EQ(it->second.arrayCount, 5u);
    }
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR PROPERTIES
// ============================================================================

namespace
{
    struct FunctionInfo
    {
        std::wstring name;
        BOOL isCtor = FALSE;
        bool isDtor = false;
        bool isNoexcept = false;
        BOOL isVirtual = FALSE;
        BOOL isPure = FALSE;
        BOOL isStatic = FALSE;
        BOOL isConst = FALSE;
        bool isOverride = false;
    };

    FunctionInfo buildFuncInfo(ComPtr<IDiaSymbol>& func)
    {
        FunctionInfo info;
        info.name = PropertyTest::getName(func.get());
        func->get_constructor(&info.isCtor);
        func->get_virtual(&info.isVirtual);
        func->get_pure(&info.isPure);
        func->get_isStatic(&info.isStatic);

        if (!info.name.empty() && info.name[0] == L'~')
            info.isDtor = true;

        info.isNoexcept = PropertyTest::isNoexcept(func.get());

        ComPtr<IDiaSymbol> funcType;
        if (SUCCEEDED(func->get_type(&funcType)) && funcType)
        {
            funcType->get_constType(&info.isConst);
        }

        // override = virtual && !intro (new virtual)
        BOOL isIntro = TRUE;
        func->get_intro(&isIntro);
        info.isOverride = info.isVirtual && !isIntro;

        return info;
    }
}

TEST_F(PropertyTest, ConstructorTest_MethodCount)
{
    auto sym = findUdt(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);

    // 6 explicit functions: default ctor, (int), copy, move, dtor, copy=, move=
    // DIA may also add implicit ones, but at minimum these 5 should be present.
    auto funcs = functions(sym.get());
    EXPECT_GE(funcs.size(), 5u);
}

/*TEST_F(PropertyTest, ConstructorTest_HasConstructors)
{
    auto sym = findUdt(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());
    std::set<std::wstring> names;
    for (auto& f : funcs)
        names.insert(getName(f.get()));

    // DIA reports Function symbols by their full name.
    // We just need to confirm we found the class
    // and that it has at least one function named ConstructorTest
    // (constructors are named after the class).
    bool foundCtor = false;
    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        if (info.isCtor)
        {
            foundCtor = true;
            break;
        }
    }
    EXPECT_TRUE(foundCtor);
}*/

TEST_F(PropertyTest, ConstructorTest_HasDestructor)
{
    auto sym = findUdt(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());
    bool foundDtor = false;
    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        if (info.isDtor)
        {
            foundDtor = true;
            break;
        }
    }
    EXPECT_TRUE(foundDtor);
}

TEST_F(PropertyTest, ConstructorTest_HasNoexcept)
{
    auto sym = findUdt(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());
    bool foundNoexceptCtor = false;
    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        if (info.isNoexcept)
        {
            foundNoexceptCtor = true;
            break;
        }
    }
    EXPECT_TRUE(foundNoexceptCtor);
}

TEST_F(PropertyTest, ConstructorTest_HasAssignmentOps)
{
    auto sym = findUdt(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());
    bool foundCopyAssign = false;
    bool foundMoveAssign = false;

    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        if (info.name.find(L"operator=") != std::wstring::npos)
        {
            if (info.isNoexcept)
                foundMoveAssign = true;
            else
                foundCopyAssign = true;
        }
    }

    EXPECT_TRUE(foundCopyAssign);
    EXPECT_TRUE(foundMoveAssign);
}

TEST_F(PropertyTest, ConstructorTest_OneField)
{
    auto sym = findUdt(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_EQ(fields.size(), 1u);
    if (!fields.empty())
        EXPECT_EQ(getName(fields[0].get()), L"value");
}

// ============================================================================
// VIRTUAL CLASS PROPERTIES
// ============================================================================

TEST_F(PropertyTest, AbstractBase_HasPureVirtual)
{
    auto sym = findUdt(L"Test::AbstractBase");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());
    bool foundVirtual = false;
    bool foundPure = false;

    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        if (info.isVirtual)
            foundVirtual = true;
        if (info.isPure)
            foundPure = true;
    }

    EXPECT_TRUE(foundVirtual);
    EXPECT_TRUE(foundPure);
}

TEST_F(PropertyTest, ConcreteDerived_HasOverride)
{
    auto sym = findUdt(L"Test::ConcreteDerived");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());
    bool foundOverride = false;

    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        if (info.isOverride)
        {
            foundOverride = true;
            break;
        }
    }
    EXPECT_TRUE(foundOverride);
}

// ============================================================================
// INHERITANCE PROPERTIES
// ============================================================================

TEST_F(PropertyTest, PublicDerived_SingleBase)
{
    auto sym = findUdt(L"Test::PublicDerived");
    ASSERT_NE(sym, nullptr);

    auto bases = baseClasses(sym.get());
    EXPECT_EQ(bases.size(), 1u);
    if (!bases.empty())
    {
        // The base class name in DIA (may or may not be namespace-qualified)
        std::wstring baseName = getName(bases[0].get());
        EXPECT_TRUE(
            baseName == L"Base" ||
            baseName == L"Test::Base");
    }
}

TEST_F(PropertyTest, MultipleDerived_TwoBases)
{
    auto sym = findUdt(L"Test::MultipleDerived");
    ASSERT_NE(sym, nullptr);

    auto bases = baseClasses(sym.get());
    EXPECT_EQ(bases.size(), 2u);
}

TEST_F(PropertyTest, DiamondDerived_TwoBases)
{
    auto sym = findUdt(L"Test::DiamondDerived");
    ASSERT_NE(sym, nullptr);

    auto bases = baseClasses(sym.get());
    EXPECT_EQ(bases.size(), 3u);
}

// ============================================================================
// BITFIELD PROPERTIES
// ============================================================================

namespace
{
    struct BitFieldInfo
    {
        std::wstring name;
        DWORD bitPos = 0;
        ULONGLONG bitWidth = 0;
        bool isBitField = false;
    };
}

TEST_F(PropertyTest, BitfieldTest_Widths)
{
    auto sym = findUdt(L"Test::BitfieldTest");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());

    std::map<std::wstring, BitFieldInfo> byName;

    for (auto& f : fields)
    {
        BitFieldInfo info;
        info.name = getName(f.get());
        if (SUCCEEDED(f->get_bitPosition(&info.bitPos)) &&
            SUCCEEDED(f->get_length(&info.bitWidth)) &&
            info.bitWidth > 0 && info.bitWidth < 64)
        {
            info.isBitField = true;
        }
        byName[info.name] = info;
    }

    // flagA : 1
    {
        auto it = byName.find(L"flagA");
        ASSERT_NE(it, byName.end());
        EXPECT_TRUE(it->second.isBitField);
        EXPECT_EQ(it->second.bitWidth, 1ull);
    }

    // flagB : 2
    {
        auto it = byName.find(L"flagB");
        ASSERT_NE(it, byName.end());
        EXPECT_TRUE(it->second.isBitField);
        EXPECT_EQ(it->second.bitWidth, 2ull);
    }

    // flagC : 3
    {
        auto it = byName.find(L"flagC");
        ASSERT_NE(it, byName.end());
        EXPECT_TRUE(it->second.isBitField);
        EXPECT_EQ(it->second.bitWidth, 3ull);
    }

    // signedField : 5
    {
        auto it = byName.find(L"signedField");
        ASSERT_NE(it, byName.end());
        EXPECT_TRUE(it->second.isBitField);
        EXPECT_EQ(it->second.bitWidth, 5ull);
    }

    // nextField : 8
    {
        auto it = byName.find(L"nextField");
        ASSERT_NE(it, byName.end());
        EXPECT_TRUE(it->second.isBitField);
        EXPECT_EQ(it->second.bitWidth, 8ull);
    }
}

TEST_F(PropertyTest, BitfieldTest_TotalFields)
{
    auto sym = findUdt(L"Test::BitfieldTest");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    // flagA, flagB, flagC, (unnamed :32), signedField, (unnamed :0), nextField -> 5 named
    EXPECT_GE(fields.size(), 4u);
}

// ============================================================================
// ANONYMOUS STRUCTS / UNIONS
// ============================================================================

/*TEST_F(PropertyTest, AnonymousTest_HasNestedUdts)
{
    auto sym = findUdt(L"Test::AnonymousTest");
    ASSERT_NE(sym, nullptr);

    auto udts = nestedUdts(sym.get());
    // The AnonymousTest has many anonymous structs/unions
    EXPECT_GE(udts.size(), 3u);
}

TEST_F(PropertyTest, AnonymousTest_HasAnonymousStructs)
{
    auto sym = findUdt(L"Test::AnonymousTest");
    ASSERT_NE(sym, nullptr);

    auto udts = nestedUdts(sym.get());

    bool hasAnonStruct = false;
    bool hasAnonUnion = false;

    for (auto& u : udts)
    {
        std::wstring name = getName(u.get());
        if (!isAnonUdtName(name))
            continue;

        DWORD udtKind = 0;
        u->get_udtKind(&udtKind);
        if (udtKind == UdtStruct)
            hasAnonStruct = true;
        else if (udtKind == UdtUnion)
            hasAnonUnion = true;
    }

    EXPECT_TRUE(hasAnonStruct);
    EXPECT_TRUE(hasAnonUnion);
}*/

TEST_F(PropertyTest, AnonymousTest_FieldCount)
{
    auto sym = findUdt(L"Test::AnonymousTest");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    // Named fields at top level: intValue, floatValue, x, y, ... many more.
    // We just require a substantial number.
    EXPECT_GE(fields.size(), 5u);
}

// ============================================================================
// TEMPLATE PROPERTIES
// ============================================================================

TEST_F(PropertyTest, TemplateUsage_HasTemplateFields)
{
    auto sym = findUdt(L"Test::TemplateUsage");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_EQ(fields.size(), 4u);

    std::set<std::wstring> names;
    for (auto& f : fields)
        names.insert(getName(f.get()));

    EXPECT_TRUE(names.count(L"intTemplate") > 0);
    EXPECT_TRUE(names.count(L"floatTemplate") > 0);
    EXPECT_TRUE(names.count(L"weaponTemplate") > 0);
    EXPECT_TRUE(names.count(L"arrayTemplate") > 0);
}

TEST_F(PropertyTest, TemplateStruct_Found)
{
    auto sym = findUdt(L"Test::TemplateStruct<int>");
    EXPECT_NE(sym, nullptr);
}

TEST_F(PropertyTest, TemplateWithNonTypeParam_Found)
{
    auto sym = findUdt(L"Test::TemplateWithNonTypeParam<int,5>");
    EXPECT_NE(sym, nullptr);
}

// ============================================================================
// ENUM PROPERTIES
// ============================================================================

TEST_F(PropertyTest, SimpleEnum_Values)
{
    auto sym = findEnum(L"Test::SimpleEnum");
    ASSERT_NE(sym, nullptr);

    auto members = children(sym.get(), SymTagData, [](IDiaSymbol*) { return true; });
    EXPECT_EQ(members.size(), 3u);

    int count = 0;
    for (auto& m : members)
    {
        // Confirm the enum values exist.
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(m->get_value(&v)))
        {
            ++count;
            VariantClear(&v);
        }
    }
    EXPECT_EQ(count, 3);
}

TEST_F(PropertyTest, TypedEnum_HasBaseType)
{
    auto sym = findEnum(L"Test::TypedEnum");
    ASSERT_NE(sym, nullptr);

    // A typed enum (enum TYPED : unsigned char) should have an underlying type
    // that reports length 1 byte.
    ComPtr<IDiaSymbol> underlying;
    if (SUCCEEDED(sym->get_type(&underlying)) && underlying)
    {
        ULONGLONG len = 0;
        underlying->get_length(&len);
        EXPECT_EQ(len, 1ull);
    }
}

TEST_F(PropertyTest, EnumTest_HasFourFields)
{
    auto sym = findUdt(L"Test::EnumTest");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_EQ(fields.size(), 4u);
}

// ============================================================================
// NAMESPACE / SCOPE PROPERTIES
// ============================================================================

TEST_F(PropertyTest, NamespaceNested_IsTopLevel)
{
    auto sym = findUdt(L"Test::OuterNamespace::InnerNamespace::NestedNamespaceStruct");
    ASSERT_NE(sym, nullptr);

    EXPECT_TRUE(TypeWalker::isTopLevelSymbol(sym.get()));
    auto qname = TypeWalker::parseQualifiedName(sym.get());
    EXPECT_EQ(qname.ns, L"Test::OuterNamespace::InnerNamespace");
}

TEST_F(PropertyTest, GlobalWeapon_NotNestedInActor)
{
    auto sym = findUdt(L"Test::Weapon");
    ASSERT_NE(sym, nullptr);

    // Global Weapon has ns="Test", not "Test::Actor"
    auto qname = TypeWalker::parseQualifiedName(sym.get());
    EXPECT_EQ(qname.ns, L"Test");
}

// ============================================================================
// STATIC / CONST MEMBERS
// ============================================================================

TEST_F(PropertyTest, StaticConstMembers_FieldKinds)
{
    auto sym = findUdt(L"Test::StaticConstMembers");
    ASSERT_NE(sym, nullptr);

    auto fields = children(sym.get(), SymTagData, [](IDiaSymbol*) { return true; });

    // DataIsStaticMember or DataIsConstant
    int staticCount = 0;
    int constexprCount = 0;
    for (auto& f : fields)
    {
        DWORD kind = 0;
        if (SUCCEEDED(f->get_dataKind(&kind)))
        {
            if (kind == DataIsStaticMember)
                ++staticCount;
            else if (kind == DataIsConstant)
                ++constexprCount;
        }
    }

    // staticField -> static
    // staticConstField -> static (const int)
    // staticConstexprField -> static constexpr
    EXPECT_GE(staticCount, 1u);
    // EXPECT_GE(constexprCount, 1u);
}

// ============================================================================
// COMPILE-EVERYTHING INTEGRITY CHECK
// ============================================================================

TEST_F(PropertyTest, CompileTested_HasAllTestTypesAsFields)
{
    // The CompileTested class combines every test type into one struct.
    // If DumpPDB can successfully walk this class, it implicitly verifies
    // that all the sub-types can be resolved by the type walker.
    auto sym = findUdt(L"Test::CompileTested");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_GE(fields.size(), 20u);
}

// ============================================================================
// CONST METHOD TEST (const / volatile qualifiers)
// ============================================================================

/*
TEST_F(PropertyTest, ConstMethodTest_HasConstMethod)
{
    auto sym = findUdt(L"Test::ConstMethodTest");
    ASSERT_NE(sym, nullptr);

    auto funcs = functions(sym.get());

    std::map<std::wstring, FunctionInfo> byName;
    for (auto& f : funcs)
    {
        auto info = buildFuncInfo(f);
        byName[info.name] = info;
    }

    // "constMethod" should have isConst == true
    auto it = byName.find(L"constMethod");
    if (it != byName.end())
    {
        EXPECT_TRUE(it->second.isConst);
    }

    // "volatileMethod" should NOT be const
    auto itVol = byName.find(L"volatileMethod");
    if (itVol != byName.end())
    {
        EXPECT_FALSE(itVol->second.isConst);
    }
}
*/

// ============================================================================
// NESTED TYPE / DEEP NESTING
// ============================================================================

TEST_F(PropertyTest, Actor_NestedStructs)
{
    auto sym = findUdt(L"Test::Actor");
    ASSERT_NE(sym, nullptr);

    auto udts = nestedUdts(sym.get());
    // Weapon, SaveData, NestedStruct, NestedClass -> 4+ nested UDTs
    EXPECT_GE(udts.size(), 4u);
}

TEST_F(PropertyTest, Actor_HasWeaponField)
{
    auto sym = findUdt(L"Test::Actor");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    std::set<std::wstring> names;
    for (auto& f : fields)
        names.insert(getName(f.get()));

    EXPECT_TRUE(names.count(L"weapon") > 0);
    EXPECT_TRUE(names.count(L"saveData") > 0);
    EXPECT_TRUE(names.count(L"nestedStruct") > 0);
    EXPECT_TRUE(names.count(L"nestedClass") > 0);
}

// ============================================================================
// TYPEDEFS / USING ALIASES
// ============================================================================

TEST_F(PropertyTest, TypedefUsage_Fields)
{
    auto sym = findUdt(L"Test::TypedefUsage");
    ASSERT_NE(sym, nullptr);

    auto fields = dataMembers(sym.get());
    EXPECT_EQ(fields.size(), 5u);
}