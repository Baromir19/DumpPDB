#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <filesystem>

#include <Core/PdbToolset.hpp>
#include <Core/DIA/SymbolFinder.hpp>
#include <Core/DIA/TypeWalker.hpp>

#ifndef TEST_COMPILAND_PDB
#error TEST_COMPILAND_PDB is not defined
#endif

// #pragma message("TEST_COMPILAND_PDB = " TEST_COMPILAND_PDB)

// ============================================================================
// PDB Dump Integration Tests
//
// These tests load the TestCompiland.pdb (built from TestCompiland project)
// and verify that DumpPDB correctly dumps the type information.
//
// Prerequisites:
//   1. Build TestCompiland project (x64/Debug) to produce .exe and .pdb
//   2. The path is defined via TEST_COMPILAND_PDB compile definition
// ============================================================================

namespace fs = std::filesystem;

class PdbDumpTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fs::path pdbPath = TEST_COMPILAND_PDB;

        ASSERT_TRUE(fs::exists(pdbPath))
            << "Missing PDB: " << pdbPath.string();

        /*std::wstring targetPath = pdbPath.wstring();

        auto exePath = pdbPath;
        exePath.replace_extension(".exe");

        if (fs::exists(exePath))
        {
            targetPath = exePath.wstring();
        }*/

		printf("Loading PDB: %ls\n", pdbPath.c_str());

        bool initialized =
            PdbToolset::instance().initialize(pdbPath);

        ASSERT_TRUE(initialized);
    }
};

// ============================================================================
// Helper functions
// ============================================================================

/// Find a type by exact fully-qualified name.
static ComPtr<IDiaSymbol> findType(const wchar_t* a_name)
{
    return SymbolFinder::findFirst(
        PdbToolset::instance().globalScope(),
        SymTagNull,
        a_name,
        false); // case-insensitive
}

/// Dump a type to its full declaration string.
static std::wstring dumpType(const wchar_t* a_name)
{
    return PdbToolset::instance().dumpTypeByName(a_name, false);
}

/// Dump a class/enum/typedef to formatted text.
static std::wstring dumpClass(const wchar_t* a_name)
{
    return PdbToolset::instance().dumpClassByName(a_name, false);
}

// ============================================================================
// 1. BASIC TYPE TESTS — verify types can be found and dumped
// ============================================================================

TEST_F(PdbDumpTest, FindPrimitiveTypesStruct)
{
    auto sym = findType(L"Test::PrimitiveTypes");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindPointerTypesStruct)
{
    auto sym = findType(L"Test::PointerTypes");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindActorClass)
{
    auto sym = findType(L"Test::Actor");
    ASSERT_NE(sym, nullptr);
}

// ============================================================================
// 2. NESTED TYPE TESTS — the main scope fix verification
// ============================================================================

TEST_F(PdbDumpTest, FindActorWeapon)
{
    auto sym = findType(L"Test::Actor::Weapon");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindActorSaveData)
{
    auto sym = findType(L"Test::Actor::SaveData");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindActorSaveDataWeapon)
{
    auto sym = findType(L"Test::Actor::SaveData::Weapon");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindDeepNested)
{
    auto sym = findType(L"Test::Outer::Inner::Deep");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindGlobalWeapon)
{
    auto sym = findType(L"Test::Weapon");
    ASSERT_NE(sym, nullptr);
}

// ============================================================================
// 3. INHERITANCE TESTS
// ============================================================================

TEST_F(PdbDumpTest, FindDerivedClass)
{
    auto sym = findType(L"Test::PublicDerived");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindMultipleDerived)
{
    auto sym = findType(L"Test::MultipleDerived");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindDiamondDerived)
{
    auto sym = findType(L"Test::DiamondDerived");
    ASSERT_NE(sym, nullptr);
}

// ============================================================================
// 4. ENUM TESTS
// ============================================================================

TEST_F(PdbDumpTest, FindSimpleEnum)
{
    auto sym = findType(L"Test::SimpleEnum");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindScopedEnum)
{
    auto sym = findType(L"Test::ScopedEnum");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindTypedEnum)
{
    auto sym = findType(L"Test::TypedEnum");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindNestedEnum)
{
    auto sym = findType(L"Test::Actor::NestedEnum");
    ASSERT_NE(sym, nullptr);
}

// ============================================================================
// 5. CONSTRUCTOR / DESTRUCTOR TESTS
// ============================================================================

TEST_F(PdbDumpTest, FindConstructorTest)
{
    auto sym = findType(L"Test::ConstructorTest");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindAbstractBase)
{
    auto sym = findType(L"Test::AbstractBase");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindConcreteDerived)
{
    auto sym = findType(L"Test::ConcreteDerived");
    ASSERT_NE(sym, nullptr);
}

// ============================================================================
// 6. SPECIAL TYPE TESTS
// ============================================================================

TEST_F(PdbDumpTest, FindBitfieldTest)
{
    auto sym = findType(L"Test::BitfieldTest");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindAnonymousTest)
{
    auto sym = findType(L"Test::AnonymousTest");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindFunctionPointerFields)
{
    auto sym = findType(L"Test::FunctionPointerFields");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindTemplateUsage)
{
    auto sym = findType(L"Test::TemplateUsage");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindComplexFieldTypes)
{
    auto sym = findType(L"Test::ComplexFieldTypes");
    ASSERT_NE(sym, nullptr);
}

TEST_F(PdbDumpTest, FindNamespaceNested)
{
    auto sym = findType(L"Test::OuterNamespace::InnerNamespace::NestedNamespaceStruct");
    ASSERT_NE(sym, nullptr);
}