#include <gtest/gtest.h>
#include <Core/DIA/TypeWalker.hpp>

// ============================================================================
// TypeWalker pure-logic unit tests
//
// These functions do NOT require a real DIA session:
//   - isValidIntStyle()   — used by IniSerializer / SettingTraits
//   - isSyntheticName()   — used by SymbolDumper
//   - leafName()          — used by SymbolDumper::dumpFunction
// ============================================================================

// ----------------------------------------------------------------------------
// isValidIntStyle
// ----------------------------------------------------------------------------

TEST(TypeWalkerIsValidIntStyle, ValidValues)
{
    EXPECT_TRUE(isValidIntStyle(static_cast<long>(IntStyle::MsvcNative)));
    EXPECT_TRUE(isValidIntStyle(static_cast<long>(IntStyle::Cstdint)));
}

TEST(TypeWalkerIsValidIntStyle, BelowRange)
{
    EXPECT_FALSE(isValidIntStyle(static_cast<long>(IntStyle::MsvcNative) - 1));
}

TEST(TypeWalkerIsValidIntStyle, AboveRange)
{
    EXPECT_FALSE(isValidIntStyle(static_cast<long>(IntStyle::Cstdint) + 1));
}

// ----------------------------------------------------------------------------
// isSyntheticName — compiler-generated synthetic names
// ----------------------------------------------------------------------------

TEST(TypeWalkerIsSyntheticName, EmptyName)
{
    EXPECT_TRUE(TypeWalker::isSyntheticName(L""));
}

TEST(TypeWalkerIsSyntheticName, UnnamedTag)
{
    EXPECT_TRUE(TypeWalker::isSyntheticName(L"<unnamed-tag>"));
}

TEST(TypeWalkerIsSyntheticName, DollarPrefixed)
{
    EXPECT_TRUE(TypeWalker::isSyntheticName(L"$T1"));
    EXPECT_TRUE(TypeWalker::isSyntheticName(L"$HASH"));
}

TEST(TypeWalkerIsSyntheticName, RegularName)
{
    EXPECT_FALSE(TypeWalker::isSyntheticName(L"Actor"));
    EXPECT_FALSE(TypeWalker::isSyntheticName(L"Test::Weapon"));
}

TEST(TypeWalkerIsSyntheticName, UnterminatedDollarOnly)
{
    // A lone '$' is still considered synthetic by the current rule.
    EXPECT_TRUE(TypeWalker::isSyntheticName(L"$"));
}

// ----------------------------------------------------------------------------
// leafName — strip the namespace/scope prefix from a qualified name
// ----------------------------------------------------------------------------

TEST(TypeWalkerLeafName, SimpleName)
{
    EXPECT_EQ(TypeWalker::leafName(L"Actor"), L"Actor");
}

TEST(TypeWalkerLeafName, NamespacedName)
{
    EXPECT_EQ(TypeWalker::leafName(L"Test::Weapon"), L"Weapon");
}

TEST(TypeWalkerLeafName, DeepNestedName)
{
    EXPECT_EQ(TypeWalker::leafName(L"Test::Actor::SaveData::Weapon"), L"Weapon");
}

TEST(TypeWalkerLeafName, EmptyString)
{
    EXPECT_EQ(TypeWalker::leafName(L""), L"");
}

TEST(TypeWalkerLeafName, TrailingSeparator)
{
    // "A::" — rfind finds the last "::" at index 1, so leaf = "".
    EXPECT_EQ(TypeWalker::leafName(L"A::"), L"");
}