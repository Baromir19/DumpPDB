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

// ------------------------------------------------------------------------
// 22. Final modifiers and operator functions
// ------------------------------------------------------------------------

FinalBase::~FinalBase() = default;
void FinalBase::virtualMethod() {}

void FinalMethodClass::virtualMethod() {}

void FinalSealedClass::virtualMethod() {}

void OpenClass::virtualMethod() {}

OperatorSuite::OperatorSuite()
    : value(0)
{
}

OperatorSuite::OperatorSuite(const OperatorSuite& a_other)
    : value(a_other.value)
{
}

OperatorSuite::~OperatorSuite() = default;

OperatorSuite& OperatorSuite::operator=(const OperatorSuite& a_other)
{
    value = a_other.value;
    return *this;
}

OperatorSuite OperatorSuite::operator+(const OperatorSuite&) const
{
    return *this;
}

OperatorSuite OperatorSuite::operator%(const OperatorSuite&) const
{
    return *this;
}

OperatorSuite& OperatorSuite::operator+=(const OperatorSuite& a_other)
{
    value += a_other.value;
    return *this;
}

bool OperatorSuite::operator==(const OperatorSuite& a_other) const
{
    return value == a_other.value;
}

bool OperatorSuite::operator<(const OperatorSuite& a_other) const
{
    return value < a_other.value;
}

int& OperatorSuite::operator[](int a_index)
{
    (void)a_index;
    return value;
}

const int& OperatorSuite::operator[](int a_index) const
{
    (void)a_index;
    return value;
}

int OperatorSuite::operator()(int a_value) const
{
    return a_value + value;
}

OperatorSuite OperatorSuite::operator++(int)
{
    OperatorSuite previous = *this;
    ++value;
    return previous;
}

OperatorSuite::operator bool() const
{
    return value != 0;
}

OperatorSuite::operator const char*() const
{
    return "";
}

void* OperatorSuite::operator new(std::size_t a_size)
{
    return ::operator new(a_size);
}

void OperatorSuite::operator delete(void* a_ptr)
{
    ::operator delete(a_ptr);
}
} // namespace Test