#include <gtest/gtest.h>
#include <Core/Dump/Functions/FunctionRenderer.hpp>

// ============================================================================
// FunctionRenderer::isConversionOperator — pure-logic unit tests
//
// Only genuine conversion functions (`operator bool`, `operator int`, ...) may
// be classified as conversions — their "return type" is part of the operator
// name. Symbolic operators and `new`/`delete` keep their rendered return type.
// ============================================================================

TEST(FunctionRendererIsConversionOperator, ConversionFunctions)
{
    EXPECT_TRUE(FunctionRenderer::isConversionOperator(L"operator bool"));
    EXPECT_TRUE(FunctionRenderer::isConversionOperator(L"operator int"));
    EXPECT_TRUE(FunctionRenderer::isConversionOperator(L"operator const char *"));
    EXPECT_TRUE(FunctionRenderer::isConversionOperator(L"operator unsigned long long"));
    EXPECT_TRUE(FunctionRenderer::isConversionOperator(L"Test::OperatorSuite::operator bool"));
}

TEST(FunctionRendererIsConversionOperator, SymbolicOperators)
{
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator+"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator%"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator="));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator=="));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator<<"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator[]"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator()"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator++"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator~"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator->"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator\"\"_x"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"Test::OperatorSuite::operator%"));
}

TEST(FunctionRendererIsConversionOperator, AllocationOperators)
{
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator new"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator new[]"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator delete"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator delete[]"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"Test::OperatorSuite::operator new"));
}

TEST(FunctionRendererIsConversionOperator, NonOperatorNames)
{
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L""));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"operator"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"plainFunction"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"OperatorSuite"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"~OperatorSuite"));
    EXPECT_FALSE(FunctionRenderer::isConversionOperator(L"myoperator"));
}