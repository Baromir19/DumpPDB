#include <Test/Tests.hpp>

namespace Test
{
ConstructorTest::ConstructorTest()
    : value(0)
{
}

ConstructorTest::ConstructorTest(int a_value)
    : value(a_value)
{
}

ConstructorTest::ConstructorTest(const ConstructorTest& a_other)
    : value(a_other.value)
{
}

ConstructorTest::ConstructorTest(ConstructorTest&& a_other) noexcept
    : value(a_other.value)
{
}

ConstructorTest::~ConstructorTest()
{
}

ConstructorTest& ConstructorTest::operator=(const ConstructorTest& a_other)
{
    value = a_other.value;
    return *this;
}

ConstructorTest& ConstructorTest::operator=(ConstructorTest&& a_other) noexcept
{
    value = a_other.value;
    return *this;
}

OuterWithInnerCtorDtor::Inner::Inner()
    : data(0)
{
}

OuterWithInnerCtorDtor::Inner::~Inner()
{
}

int ConstMethodTest::getValue() const
{
    return 42;
}

// ------------------------------------------------------------------------
// 21. Variadic and qualifier functions
// ------------------------------------------------------------------------

void VariadicFunctionTest::plainMethod(int a_value)
{
    (void)a_value;
}

void VariadicFunctionTest::constMethod(int a_value) const
{
    (void)a_value;
}

void VariadicFunctionTest::volatileMethod(int a_value) volatile
{
    (void)a_value;
}

void VariadicFunctionTest::constVolatileMethod(int a_value) const volatile
{
    (void)a_value;
}

void VariadicFunctionTest::staticMethod(int a_value)
{
    (void)a_value;
}

void VariadicFunctionTest::variadicMethod(int a_count, ...)
{
    (void)a_count;
}

void VariadicFunctionTest::constVariadicMethod(int a_count, ...) const
{
    (void)a_count;
}

void VariadicFunctionTest::staticVariadicMethod(int a_count, ...)
{
    (void)a_count;
}

void VariadicFunctionTest::variadicOnly(...)
{
}

void freeVariadicFunction(const char* a_format, ...)
{
    (void)a_format;
}

void freePlainFunction(int a_value)
{
    (void)a_value;
}
} // namespace Test