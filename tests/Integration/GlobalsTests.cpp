#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include <Core/PdbToolset.hpp>

#ifndef TEST_COMPILAND_PDB
#error TEST_COMPILAND_PDB is not defined
#endif

// ============================================================================
// Global functions / variables / statics / public symbols
//
// These verify the top-level (non-type) symbol output that used to be dropped
// silently: free functions, namespace/global variables, file statics, static
// members, function-local statics and public symbols, plus the "-type" fallback
// for names that are not types.
// ============================================================================

namespace fs = std::filesystem;

namespace
{
bool contains(const std::wstring& a_text, const wchar_t* a_needle)
{
    return a_text.find(a_needle) != std::wstring::npos;
}
} // namespace

class GlobalsTest : public ::testing::Test
{
protected:

    void SetUp() override
    {
        const fs::path pdbPath = TEST_COMPILAND_PDB;
        ASSERT_TRUE(fs::exists(pdbPath)) << "Missing PDB: " << pdbPath.string();
        ASSERT_TRUE(PdbToolset::instance().initialize(pdbPath));

        DumpConfig cfg;
        PdbToolset::instance().dumper().setConfig(cfg);
    }

    static PdbToolset& tool() { return PdbToolset::instance(); }
};

TEST_F(GlobalsTest, FreeFunctionIsListedInItsNamespaceWithParameters)
{
    const std::wstring out = tool().dumpGlobalFunctions(L"freePlainFunction", false);

    EXPECT_TRUE(contains(out, L"namespace Test")) << "functions must be grouped by namespace";
    EXPECT_TRUE(contains(out, L"freePlainFunction(int"));
    EXPECT_TRUE(contains(out, L"a_value")) << "parameter names must be kept";
}

TEST_F(GlobalsTest, FunctionsCarryAddressAndSourceLine)
{
    const std::wstring out = tool().dumpGlobalFunctions(L"freePlainFunction", false);

    EXPECT_TRUE(contains(out, L"// RVA 0x"));
    EXPECT_TRUE(contains(out, L"Tests.cpp:")) << "source file:line comment expected";
}

TEST_F(GlobalsTest, AddressCommentsCanBeSwitchedOff)
{
    DumpConfig cfg;
    cfg.m_showAddress = false;
    cfg.m_showSourceLine = false;
    tool().dumper().setConfig(cfg);

    const std::wstring out = tool().dumpGlobalFunctions(L"freePlainFunction", false);
    EXPECT_TRUE(contains(out, L"freePlainFunction"));
    EXPECT_FALSE(contains(out, L"RVA"));
    EXPECT_FALSE(contains(out, L"Tests.cpp"));
}

TEST_F(GlobalsTest, VariadicFreeFunctionKeepsEllipsis)
{
    const std::wstring out = tool().dumpGlobalFunctions(L"freeVariadicFunction", false);
    EXPECT_TRUE(contains(out, L"..."));
}

TEST_F(GlobalsTest, FilterIsCaseInsensitiveSubstring)
{
    EXPECT_FALSE(tool().dumpGlobalFunctions(L"FREEPLAIN", false).empty());
    EXPECT_TRUE(tool().dumpGlobalFunctions(L"definitely_not_a_function_name", false).find(L"freePlain")
                == std::wstring::npos);
}

TEST_F(GlobalsTest, MemberDefinitionsAreHiddenUnlessRequested)
{
    // OperatorSuite's special members are defined out of line in Tests.cpp.
    const std::wstring hidden = tool().dumpGlobalFunctions(L"OperatorSuite", false);
    EXPECT_FALSE(contains(hidden, L"OperatorSuite::OperatorSuite"));

    DumpConfig cfg;
    cfg.m_globalsIncludeMembers = true;
    tool().dumper().setConfig(cfg);
    const std::wstring shown = tool().dumpGlobalFunctions(L"OperatorSuite", false);
    EXPECT_TRUE(contains(shown, L"OperatorSuite::OperatorSuite"));
    EXPECT_TRUE(contains(shown, L"OperatorSuite::operator delete"));
}

TEST_F(GlobalsTest, GlobalVariableWithAddress)
{
    const std::wstring out = tool().dumpGlobalVariables(L"g_testGlobalCounter", false);

    EXPECT_TRUE(contains(out, L"g_testGlobalCounter"));
    EXPECT_TRUE(contains(out, L"RVA 0x"));
}

TEST_F(GlobalsTest, FileStaticIsMarkedStatic)
{
    const std::wstring out = tool().dumpGlobalVariables(L"s_testFileStatic", false);

    EXPECT_TRUE(contains(out, L"static"));
    EXPECT_TRUE(contains(out, L"s_testFileStatic"));
}

TEST_F(GlobalsTest, StaticMemberNeedsIncludeMembers)
{
    EXPECT_FALSE(contains(tool().dumpGlobalVariables(L"s_count", false), L"StaticMemberHolder::s_count"));

    DumpConfig cfg;
    cfg.m_globalsIncludeMembers = true;
    tool().dumper().setConfig(cfg);
    const std::wstring out = tool().dumpGlobalVariables(L"s_count", false);
    EXPECT_TRUE(contains(out, L"StaticMemberHolder::s_count"));
    EXPECT_TRUE(contains(out, L"RVA 0x"));
}

TEST_F(GlobalsTest, FunctionLocalStaticNeedsIncludeLocalStatics)
{
    EXPECT_FALSE(contains(tool().dumpGlobalVariables(L"s_testLocalStatic", false), L"s_testLocalStatic"));

    DumpConfig cfg;
    cfg.m_globalsIncludeLocalStatics = true;
    tool().dumper().setConfig(cfg);
    const std::wstring out = tool().dumpGlobalVariables(L"s_testLocalStatic", false);
    EXPECT_TRUE(contains(out, L"s_testLocalStatic"));
    EXPECT_TRUE(contains(out, L"static-local in ")) << "owner function must be named in the comment";
    EXPECT_TRUE(contains(out, L"useStatics"));
}

TEST_F(GlobalsTest, ClassFieldsAreNeverListedAsVariables)
{
    DumpConfig cfg;
    cfg.m_globalsIncludeMembers = true;
    cfg.m_globalsIncludeLocalStatics = true;
    tool().dumper().setConfig(cfg);

    // StaticMemberHolder::value is an ordinary field, not a global.
    EXPECT_FALSE(contains(tool().dumpGlobalVariables(L"StaticMemberHolder", false), L"StaticMemberHolder::value"));
}

TEST_F(GlobalsTest, PublicSymbolsAreListed)
{
    const std::wstring out = tool().dumpPublicSymbols(L"freePlainFunction", false);
    EXPECT_TRUE(contains(out, L"freePlainFunction"));
    EXPECT_TRUE(contains(out, L"public-function"));
}

TEST_F(GlobalsTest, TypeLookupFallsBackToFunctionsAndVariables)
{
    // `-type <name>` for something that is not a type used to print nothing.
    EXPECT_TRUE(contains(tool().dumpTypeByName(L"Test::freePlainFunction", false), L"freePlainFunction"));
    EXPECT_TRUE(contains(tool().dumpTypeByName(L"Test::g_testGlobalCounter", false), L"g_testGlobalCounter"));
}

TEST_F(GlobalsTest, SymbolTableHasHeaderAndRows)
{
    const std::wstring out = tool().dumpSymbolTable(L"freePlainFunction", false, true);

    EXPECT_EQ(out.rfind(L"kind\tname\tundecorated\trva", 0), 0u) << "header must come first";
    EXPECT_TRUE(contains(out, L"function\tTest::freePlainFunction"));
    EXPECT_TRUE(contains(out, L"a_value\t")) << "parameters column (with names)";
}

TEST_F(GlobalsTest, MemberAddressesAppearInClassDumpsOnRequest)
{
    // OperatorSuite's members are defined (and therefore have an address) in Tests.cpp.
    const std::wstring plain = tool().dumpTypeByName(L"Test::OperatorSuite", false);
    EXPECT_FALSE(contains(plain, L"RVA 0x"));

    DumpConfig cfg;
    cfg.m_showMemberAddress = true;
    tool().dumper().setConfig(cfg);
    const std::wstring annotated = tool().dumpTypeByName(L"Test::OperatorSuite", false);
    EXPECT_TRUE(contains(annotated, L"RVA 0x"));
}

TEST_F(GlobalsTest, ExportToFileWritesUtf8)
{
    const fs::path out = fs::temp_directory_path() / "dumppdb_symbols_test.tsv";
    const std::wstring outW = out.wstring();
    std::wstring error;
    ASSERT_TRUE(tool().exportToFile(PdbToolset::EXPORT_SYMBOL_TABLE, L"freePlainFunction", false, outW.c_str(), error))
        << std::string(error.begin(), error.end());
    EXPECT_GT(fs::file_size(out), 20u);
    fs::remove(out);
}

TEST_F(GlobalsTest, PdbInfoHasGuidAndAge)
{
    const std::wstring info = tool().pdbInfo();
    EXPECT_TRUE(contains(info, L"guid="));
    EXPECT_TRUE(contains(info, L"age="));
}
