#include <gtest/gtest.h>
#include <Core/DIA/TypeBuilder.hpp>

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
    builder.base(L"int").pointer();
    EXPECT_EQ(builder.build(), L"int*");
}

TEST(TypeBuilderTest, Reference)
{
    TypeBuilder builder;
    builder.base(L"int").reference();
    EXPECT_EQ(builder.build(), L"int&");
}

TEST(TypeBuilderTest, ConstBaseType)
{
    // const int
    TypeBuilder builder;
    builder.base(L"int").constQual();
    EXPECT_EQ(builder.build(), L"const int");
}

TEST(TypeBuilderTest, ConstPointer)
{
    // int* const  — const pointer to int
    TypeBuilder builder;
    builder.base(L"int").pointer().constPointer();
    EXPECT_EQ(builder.build(), L"int* const");
}

TEST(TypeBuilderTest, PointerToConst)
{
    // const int*  — pointer to const int
    TypeBuilder builder;
    builder.base(L"int").constQual().pointer();
    EXPECT_EQ(builder.build(), L"const int*");
}

TEST(TypeBuilderTest, ConstPointerToConst)
{
    // const int* const  — const pointer to const int
    TypeBuilder builder;
    builder.base(L"int").constQual().pointer().constPointer();
    EXPECT_EQ(builder.build(), L"const int* const");
}

TEST(TypeBuilderTest, ConstReference)
{
    // const int&  — reference to const int
    // (const on the referenced type, not the reference itself)
    TypeBuilder builder;
    builder.base(L"int").constQual().reference();
    EXPECT_EQ(builder.build(), L"const int&");
}

TEST(TypeBuilderTest, ReferenceToPointer)
{
    TypeBuilder builder;
    builder.base(L"int").pointer().reference();
    EXPECT_EQ(builder.build(), L"int*&");
}

TEST(TypeBuilderTest, Array)
{
    TypeBuilder builder;
    builder.base(L"int").array(10);
    EXPECT_EQ(builder.build(), L"int[10]");
}

TEST(TypeBuilderTest, PointerToArray)
{
    TypeBuilder builder;
    builder.base(L"int").array(10).pointer();
    EXPECT_EQ(builder.build(), L"int (*)[10]");
}

TEST(TypeBuilderTest, ArrayOfPointers)
{
    TypeBuilder builder;
    builder.base(L"int").pointer().array(5);
    EXPECT_EQ(builder.build(), L"int*[5]");
}

TEST(TypeBuilderTest, FunctionPointer)
{
    TypeBuilder builder;
    builder.base(L"void").function(L"int, float").pointer();
    EXPECT_EQ(builder.build(), L"void (*)(int, float)");
}

TEST(TypeBuilderTest, FunctionReturningPointer)
{
    TypeBuilder builder;
    builder.base(L"int").pointer().function(L"float");
    EXPECT_EQ(builder.build(), L"int*(float)");
}

TEST(TypeBuilderTest, ConstQualifierOnPointer)
{
    // const on the pointer level: void (*const)(int)
    TypeBuilder builder;
    builder.base(L"void").function(L"int").pointer().constPointer();
    EXPECT_EQ(builder.build(), L"void (* const)(int)");
}

TEST(TypeBuilderTest, BitField)
{
    TypeBuilder builder;
    builder.base(L"int").name(L"field").bitField(0, 3);
    EXPECT_EQ(builder.build(), L"int field : 3");
}

TEST(TypeBuilderTest, NamedType)
{
    TypeBuilder builder;
    builder.base(L"int").name(L"myVar");
    EXPECT_EQ(builder.build(), L"int myVar");
}

TEST(TypeBuilderTest, VolatileQualifier)
{
    TypeBuilder builder;
    builder.base(L"int").volatileQual();
    EXPECT_EQ(builder.build(), L"volatile int");
}

TEST(TypeBuilderTest, ConstVolatileQualifier)
{
    TypeBuilder builder;
    builder.base(L"int").constQual().volatileQual();
    EXPECT_EQ(builder.build(), L"volatile const int");
}

TEST(TypeBuilderTest, Reset)
{
    TypeBuilder builder;
    builder.base(L"int").pointer().name(L"x");
    EXPECT_EQ(builder.build(), L"int* x");

    builder.reset();
    EXPECT_EQ(builder.build(), L"");
}

TEST(TypeBuilderTest, PointerToPointerToConst)
{
    // pointer to pointer to const int
    TypeBuilder builder;
    builder.base(L"int").constQual().pointer().pointer();
    EXPECT_EQ(builder.build(), L"const int**");
}

TEST(TypeBuilderTest, RValueReference)
{
    TypeBuilder builder;
    builder.base(L"int").rvalueReference();
    EXPECT_EQ(builder.build(), L"int&&");
}

TEST(TypeBuilderTest, FunctionWithNoArgs)
{
    TypeBuilder builder;
    builder.base(L"void").function(L"").pointer();
    EXPECT_EQ(builder.build(), L"void (*)()");
}

TEST(TypeBuilderTest, MultiDimensionalArray)
{
    TypeBuilder builder;
    builder.base(L"int").array(3).array(4);
    EXPECT_EQ(builder.build(), L"int[3][4]");
}

TEST(TypeBuilderTest, ConstPointerToConstPointerToConst)
{
    // const int* const* const
    TypeBuilder builder;
    builder.base(L"int")
        .constQual()     // const int (base)
        .pointer()       // * (pointer level 1, pointed-to is const int)
        .constPointer()  // * const (pointer level 1 itself is const)
        .pointer()       // * (pointer level 2, pointed-to is const int* const)
        .constPointer(); // * const (pointer level 2 itself is const)
    EXPECT_EQ(builder.build(), L"const int* const* const");
}

TEST(TypeBuilderTest, VolatilePointer)
{
    // int* volatile
    TypeBuilder builder;
    builder.base(L"int").pointer().volatilePointer();
    EXPECT_EQ(builder.build(), L"int* volatile");
}

TEST(TypeBuilderTest, ConstVolatilePointer)
{
    // const int* const volatile
    TypeBuilder builder;
    builder.base(L"int").constQual().pointer().constPointer().volatilePointer();
    EXPECT_EQ(builder.build(), L"const int* const volatile");
}