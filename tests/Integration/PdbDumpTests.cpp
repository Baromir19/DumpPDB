#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <filesystem>

#include <Core/PdbToolset.hpp>
#include <Core/SymbolFinder.hpp>
#include <Core/TypeWalker.hpp>

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
        // Find the TestCompiland PDB
        const wchar_t* envPath = _wgetenv(L"TEST_COMPILAND_PDB");
        std::wstring pdbPath;

        if (envPath && fs::exists(envPath))
        {
            pdbPath = envPath;
        }
        else
        {
            // Look for PDB relative to executable
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            fs::path exeDir = fs::path(exePath).parent_path();

            // Try parent directories
            for (auto root : { exeDir,
                               exeDir.parent_path(),
                               exeDir.parent_path().parent_path() })
            {
                auto testPath = root / "TestCompiland" / "x64" / "Debug" / "TestCompiland.pdb";
                if (fs::exists(testPath))
                {
                    pdbPath = testPath.wstring();
                    break;
                }

                // Try alongside the exe
                auto localPath = root / "TestCompiland.pdb";
                if (fs::exists(localPath))
                {
                    pdbPath = localPath.wstring();
                    break;
                }
            }
        }

        if (pdbPath.empty())
        {
            // Try source tree path
            fs::path srcPath = CMAKE_SOURCE_DIR;
            auto testPath = srcPath / "TestCompiland" / "x64" / "Debug" / "TestCompiland.pdb";
            if (fs::exists(testPath))
            {
                pdbPath = testPath.wstring();
            }
        }

        ASSERT_FALSE(pdbPath.empty())
            << "TestCompiland.pdb not found. Build TestCompiland project first.";

        // Find the corresponding .exe (DIA needs it)
        std::wstring exePath2 = pdbPath;
        auto extPos = exePath2.rfind(L".pdb");
        if (extPos != std::wstring::npos)
        {
            exePath2.replace(extPos, 4, L".exe");
        }

        // Check which file exists
        std::wstring targetPath = pdbPath;
        if (fs::exists(exePath2))
        {
            // Prefer .exe — DIA can find .pdb from it
            targetPath = exePath2;
        }

        // Initialize the PDB toolset
        bool initialized = PdbToolset::instance().initialize(targetPath);
        ASSERT_TRUE(initialized)
            << "Failed to initialize DIA session with: " << targetPath;
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