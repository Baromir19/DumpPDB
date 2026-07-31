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

    ConstructorTest::ConstructorTest(
        const ConstructorTest& a_other)
        : value(a_other.value)
    {
    }

    ConstructorTest::ConstructorTest(
        ConstructorTest&& a_other) noexcept
        : value(a_other.value)
    {
    }

    ConstructorTest::~ConstructorTest()
    {
    }

    ConstructorTest&
        ConstructorTest::operator=(
            const ConstructorTest& a_other)
    {
        value = a_other.value;
        return *this;
    }

    ConstructorTest&
        ConstructorTest::operator=(
            ConstructorTest&& a_other) noexcept
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
}