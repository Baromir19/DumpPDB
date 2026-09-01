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

TEST(TypeWalkerLeafName, TemplateName)
{
    // A "::" inside "<...>" must not be treated as the scope separator.
    EXPECT_EQ(TypeWalker::leafName(L"TB::TList<int, TB::CustomAllocator<int>>"),
              L"TList<int, TB::CustomAllocator<int>>");
}

// ----------------------------------------------------------------------------
// parseQualifiedName — template-aware namespace/leaf splitting
// ----------------------------------------------------------------------------

TEST(TypeWalkerParseQualifiedName, Simple)
{
    auto q = TypeWalker::parseQualifiedName(L"Actor");
    EXPECT_EQ(q.ns, L"");
    EXPECT_EQ(q.leaf, L"Actor");

    auto q2 = TypeWalker::parseQualifiedName(L"Test::Actor::Weapon");
    EXPECT_EQ(q2.ns, L"Test::Actor");
    EXPECT_EQ(q2.leaf, L"Weapon");
}

TEST(TypeWalkerParseQualifiedName, TemplateLeafKeepsArgs)
{
    // The last "::" must be the one before the leaf, not the one inside the args.
    auto q = TypeWalker::parseQualifiedName(L"TB::TList<int, TB::CustomAllocator<int>>");
    EXPECT_EQ(q.ns, L"TB");
    EXPECT_EQ(q.leaf, L"TList<int, TB::CustomAllocator<int>>");

    auto q2 = TypeWalker::parseQualifiedName(L"TList<int, TB::CustomAllocator<int>>");
    EXPECT_EQ(q2.ns, L"");
    EXPECT_EQ(q2.leaf, L"TList<int, TB::CustomAllocator<int>>");
}

// ----------------------------------------------------------------------------
// Namespace block rendering (incl. anonymous namespaces)
// ----------------------------------------------------------------------------

TEST(TypeWalkerNamespace, NamedBlocks)
{
    EXPECT_EQ(TypeWalker::namespaceBlockOpen(L"User::Math"),
              L"namespace User\n{\nnamespace Math\n{\n");
    EXPECT_EQ(TypeWalker::namespaceBlockClose(L"User::Math"), L"}\n}\n");
    EXPECT_EQ(TypeWalker::namespacePartCount(L"User::Math"), 2u);
}

TEST(TypeWalkerNamespace, AnonymousNamespaceBlock)
{
    const std::wstring ns = L"`anonymous-namespace'";
    EXPECT_EQ(TypeWalker::namespaceBlockOpen(ns), L"namespace\n{\n");
    EXPECT_EQ(TypeWalker::namespaceBlockClose(ns), L"}\n");
    EXPECT_TRUE(TypeWalker::isAnonymousNamespacePart(ns));
    EXPECT_FALSE(TypeWalker::isAnonymousNamespacePart(L"TB"));
}

// ----------------------------------------------------------------------------
// User-defined template instantiation -> generic template<...> declaration
// ----------------------------------------------------------------------------

TEST(TypeWalkerTemplateInstantiation, NoTemplates)
{
    auto ti = TypeWalker::makeTemplateInstantiation(L"Actor");
    EXPECT_FALSE(ti.active);

    auto ti2 = TypeWalker::makeTemplateInstantiation(L"TB::Actor");
    EXPECT_FALSE(ti2.active);
}

TEST(TypeWalkerTemplateInstantiation, MixedArgs)
{
    auto ti = TypeWalker::makeTemplateInstantiation(L"Type<float, 11, TB::HighRes>");
    ASSERT_TRUE(ti.active);
    EXPECT_EQ(ti.decl, L"template<typename T, size_t U, typename V>");
    ASSERT_EQ(ti.replacements.size(), 3u);
    EXPECT_EQ(ti.replacements[0].first, L"TB::HighRes"); // longest first
    EXPECT_EQ(ti.replacements[0].second, L"V");
}

TEST(TypeWalkerTemplateInstantiation, IntegerKind)
{
    auto ti = TypeWalker::makeTemplateInstantiation(L"Type<ExType, 11, -3, HighRes>");
    ASSERT_TRUE(ti.active);
    EXPECT_EQ(ti.decl, L"template<typename T, size_t U, int V, typename W>");
}

TEST(TypeWalkerTemplateInstantiation, SubstituteWholeTokens)
{
    auto ti = TypeWalker::makeTemplateInstantiation(L"Type<float, 11, TB::HighRes>");
    ASSERT_TRUE(ti.active);
    const std::wstring in
        = L"class Type<float, 11, TB::HighRes> : public Type<float, 11, TB::HighRes> { x111; };";
    const std::wstring out = TypeWalker::substituteTemplateArgs(in, ti);
    EXPECT_EQ(out,
        L"class Type<T, U, V> : public Type<T, U, V> { x111; };");
}