#include <gtest/gtest.h>
#include <Core/DIA/TypeWalker.hpp>

// ============================================================================
// ScopeContext unit tests
// ============================================================================

TEST(ScopeContextTest, EmptyScope)
{
    ScopeContext scope;
    EXPECT_TRUE(scope.empty());
    EXPECT_EQ(scope.full(), L"");
}

TEST(ScopeContextTest, SingleScope)
{
    ScopeContext scope;
    scope.push(L"Actor");
    EXPECT_FALSE(scope.empty());
    EXPECT_EQ(scope.full(), L"Actor");
}

TEST(ScopeContextTest, NestedScope)
{
    ScopeContext scope;
    scope.push(L"Actor");
    scope.push(L"SaveData");
    EXPECT_EQ(scope.full(), L"Actor::SaveData");
}

TEST(ScopeContextTest, DeepNestedScope)
{
    ScopeContext scope;
    scope.push(L"Actor");
    scope.push(L"SaveData");
    scope.push(L"Inner");
    EXPECT_EQ(scope.full(), L"Actor::SaveData::Inner");
}

TEST(ScopeContextTest, PopRestoresPreviousScope)
{
    ScopeContext scope;
    scope.push(L"Actor");
    scope.push(L"SaveData");
    scope.push(L"Inner");
    EXPECT_EQ(scope.full(), L"Actor::SaveData::Inner");

    scope.pop();
    EXPECT_EQ(scope.full(), L"Actor::SaveData");

    scope.pop();
    EXPECT_EQ(scope.full(), L"Actor");

    scope.pop();
    EXPECT_TRUE(scope.empty());
}

TEST(ScopeContextTest, PopEmptyDoesNothing)
{
    ScopeContext scope;
    scope.pop(); // should not crash
    EXPECT_TRUE(scope.empty());
}

// ============================================================================
// Scope stripping tests (simulating getName behavior)
// ============================================================================

/// Simulates TypeWalker::getName() scope stripping logic.
static std::wstring stripCurrentScope(const std::wstring& a_name, const std::wstring& a_currentScope)
{
    if (a_currentScope.empty())
        return a_name;

    const std::wstring prefix = a_currentScope + L"::";

    if (a_name.size() > prefix.size() && a_name.compare(0, prefix.size(), prefix) == 0)
    {
        return a_name.substr(prefix.size());
    }

    return a_name;
}

TEST(ScopeStrippingTest, ExactPrefixMatch)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData::Weapon", L"Actor::SaveData"), L"Weapon");
}

TEST(ScopeStrippingTest, ExternalTypeNotStripped)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::User::Weapon", L"Actor::SaveData"), L"Actor::User::Weapon");
}

TEST(ScopeStrippingTest, PartialPrefixNotStripped)
{
    EXPECT_EQ(stripCurrentScope(L"SaveData::Weapon", L"Actor::SaveData"), L"SaveData::Weapon");
}

TEST(ScopeStrippingTest, ExactMatchNotStripped)
{
    // If name == scope, no "::" prefix to strip
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData", L"Actor::SaveData"), L"Actor::SaveData");
}

TEST(ScopeStrippingTest, EmptyScopeReturnsName)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData::Weapon", L""), L"Actor::SaveData::Weapon");
}

TEST(ScopeStrippingTest, ConstructorName)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData::SaveData", L"Actor::SaveData"), L"SaveData");
}

TEST(ScopeStrippingTest, DestructorName)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData::~SaveData", L"Actor::SaveData"), L"~SaveData");
}

TEST(ScopeStrippingTest, DeepNestedConstructor)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData::Inner::Inner", L"Actor::SaveData::Inner"), L"Inner");
}

TEST(ScopeStrippingTest, DeepNestedDestructor)
{
    EXPECT_EQ(stripCurrentScope(L"Actor::SaveData::Inner::~Inner", L"Actor::SaveData::Inner"), L"~Inner");
}

TEST(ScopeStrippingTest, SameNameDifferentScope)
{
    // Test::Weapon inside Actor::SaveData should NOT be stripped
    EXPECT_EQ(stripCurrentScope(L"Test::Weapon", L"Actor::SaveData"), L"Test::Weapon");
}

TEST(ScopeStrippingTest, TemplateTypeNotStripped)
{
    // Template types with <> should not be affected by stripping
    EXPECT_EQ(stripCurrentScope(L"std::vector<int>", L"Actor"), L"std::vector<int>");
}

TEST(ScopeStrippingTest, NamespacedTypeNotStripped)
{
    EXPECT_EQ(stripCurrentScope(L"Test::Outer::Inner", L"Actor::SaveData"), L"Test::Outer::Inner");
}

// ============================================================================
// parseQualifiedName tests
// ============================================================================

TEST(QualifiedNameTest, SingleNamespace)
{
    auto q = TypeWalker::parseQualifiedName(std::wstring(L"User::Hello"));
    EXPECT_EQ(q.ns, L"User");
    EXPECT_EQ(q.leaf, L"Hello");
}

TEST(QualifiedNameTest, NestedNamespaces)
{
    auto q = TypeWalker::parseQualifiedName(std::wstring(L"A::B::Hello"));
    EXPECT_EQ(q.ns, L"A::B");
    EXPECT_EQ(q.leaf, L"Hello");
}

TEST(QualifiedNameTest, NoNamespace)
{
    auto q = TypeWalker::parseQualifiedName(std::wstring(L"Hello"));
    EXPECT_EQ(q.ns, L"");
    EXPECT_EQ(q.leaf, L"Hello");
}

TEST(QualifiedNameTest, DeepNestedNamespaces)
{
    auto q = TypeWalker::parseQualifiedName(std::wstring(L"Test::Outer::Inner::Nested"));
    EXPECT_EQ(q.ns, L"Test::Outer::Inner");
    EXPECT_EQ(q.leaf, L"Nested");
}

TEST(QualifiedNameTest, EmptyString)
{
    auto q = TypeWalker::parseQualifiedName(std::wstring(L""));
    EXPECT_EQ(q.ns, L"");
    EXPECT_EQ(q.leaf, L"");
}
