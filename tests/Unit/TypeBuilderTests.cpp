#include <gtest/gtest.h>
#include <Core/TypeBuilder.hpp>

// ============================================================================
// TypeBuilder unit tests
// ============================================================================

TEST(TypeBuilderTest, BaseType)
{
    TypeBuilder builder;
    builder.base(L"int");
    EXPECT_EQ(builder.build(), L"int");
}

TEST(TypeBuilderTest, Pointer)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer();
    EXPECT_EQ(builder.build(), L"int*");
}

TEST(TypeBuilderTest, Reference)
{
    TypeBuilder builder;
    builder.base(L"int")
           .reference();
    EXPECT_EQ(builder.build(), L"int&");
}

TEST(TypeBuilderTest, ConstPointer)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer()
           .constQual();
    EXPECT_EQ(builder.build(), L"const int*");
}

TEST(TypeBuilderTest, PointerToConst)
{
    TypeBuilder builder;
    builder.base(L"int")
           .constPointed()
           .pointer();
    EXPECT_EQ(builder.build(), L"const int*");
}

TEST(TypeBuilderTest, PointerToPointer)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer()
           .pointer();
    EXPECT_EQ(builder.build(), L"int**");
}

TEST(TypeBuilderTest, ReferenceToPointer)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer()
           .reference();
    EXPECT_EQ(builder.build(), L"int*&");
}

TEST(TypeBuilderTest, Array)
{
    TypeBuilder builder;
    builder.base(L"int")
           .array(10);
    EXPECT_EQ(builder.build(), L"int[10]");
}

TEST(TypeBuilderTest, PointerToArray)
{
    TypeBuilder builder;
    builder.base(L"int")
           .array(10)
           .pointer();
    EXPECT_EQ(builder.build(), L"int (*)[10]");
}

TEST(TypeBuilderTest, ArrayOfPointers)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer()
           .array(5);
    EXPECT_EQ(builder.build(), L"int*[5]");
}

TEST(TypeBuilderTest, FunctionPointer)
{
    TypeBuilder builder;
    builder.base(L"void")
           .function(L"int, float")
           .pointer();
    EXPECT_EQ(builder.build(), L"void (*)(int, float)");
}

TEST(TypeBuilderTest, FunctionReturningPointer)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer()
           .function(L"float");
    EXPECT_EQ(builder.build(), L"int*(float)");
}

TEST(TypeBuilderTest, ConstMethodPointer)
{
    TypeBuilder builder;
    builder.base(L"void")
           .function(L"int")
           .pointer()
           .constQual();
    EXPECT_EQ(builder.build(), L"const void (*)(int)");
}

TEST(TypeBuilderTest, BitField)
{
    TypeBuilder builder;
    builder.base(L"int")
           .name(L"field")
           .bitField(0, 3);
    EXPECT_EQ(builder.build(), L"int field : 3");
}

TEST(TypeBuilderTest, NamedType)
{
    TypeBuilder builder;
    builder.base(L"int")
           .name(L"myVar");
    EXPECT_EQ(builder.build(), L"int myVar");
}

TEST(TypeBuilderTest, VolatileQualifier)
{
    TypeBuilder builder;
    builder.base(L"int")
           .volatileQual();
    EXPECT_EQ(builder.build(), L"volatile int");
}

TEST(TypeBuilderTest, ConstVolatileQualifier)
{
    TypeBuilder builder;
    builder.base(L"int")
           .constQual()
           .volatileQual();
    EXPECT_EQ(builder.build(), L"volatile const int");
}

TEST(TypeBuilderTest, Reset)
{
    TypeBuilder builder;
    builder.base(L"int")
           .pointer()
           .name(L"x");
    EXPECT_EQ(builder.build(), L"int* x");

    builder.reset();
    EXPECT_EQ(builder.build(), L"");
}

TEST(TypeBuilderTest, ComplexDeclaration)
{
    // const int** — pointer to pointer to const int
    // (constQual after pointer applies to pointed-to type)
    TypeBuilder builder;
    builder.base(L"int")
           .constQual()
           .pointer()
           .constQual()
           .pointer();
    EXPECT_EQ(builder.build(), L"const int**");
}

TEST(TypeBuilderTest, RValueReference)
{
    TypeBuilder builder;
    builder.base(L"int")
           .rvalueReference();
    EXPECT_EQ(builder.build(), L"int&&");
}

TEST(TypeBuilderTest, FunctionWithNoArgs)
{
    TypeBuilder builder;
    builder.base(L"void")
           .function(L"")
           .pointer();
    EXPECT_EQ(builder.build(), L"void (*)()");
}

TEST(TypeBuilderTest, MultiDimensionalArray)
{
    TypeBuilder builder;
    builder.base(L"int")
           .array(3)
           .array(4);
    EXPECT_EQ(builder.build(), L"int[3][4]");
}