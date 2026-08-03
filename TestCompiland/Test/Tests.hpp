#pragma once

// ============================================================================
// TestCompiland — comprehensive C++ type coverage for DIA/PDB testing
//
// This file defines a wide range of C++ constructs that, when compiled,
// produce a .pdb with rich type information. DumpPDB integration tests
// load this .pdb and verify that the output matches expected snapshots.
//
// Categories covered:
//   - Primitive types, pointers, references, arrays
//   - Functions (free, member, virtual, pure virtual, const, static)
//   - Constructors, destructors
//   - Nested classes/structs (deep nesting, same names in different scopes)
//   - Inheritance (single, multiple, virtual)
//   - Enums (plain, scoped, typed, nested)
//   - Typedefs, using aliases
//   - Function pointers, member function pointers
//   - Templates (simple, nested, template template)
//   - Anonymous structs/unions
//   - Bitfields
//   - Static/const/constexpr members
//   - Namespaces
// ============================================================================

namespace Test
{
// ========================================================================
// 1. PRIMITIVE TYPES
// ========================================================================

struct PrimitiveTypes
{
    bool boolean;
    char character;
    wchar_t wideCharacter;
    signed char signedChar;
    unsigned char unsignedChar;
    short shortInt;
    unsigned short unsignedShort;
    int integer;
    unsigned int unsignedInt;
    long longInt;
    unsigned long unsignedLong;
    long long longLong;
    unsigned long long unsignedLongLong;
    float floating;
    double doubleFloat;
    long double longDouble;
};

// ========================================================================
// 2. POINTERS, REFERENCES, ARRAYS
// ========================================================================

struct PointerTypes
{
    PointerTypes(int*& value, const int& constValue)
        : pointer(nullptr)
        , referencePointer(value)
        , pointerToPointer(nullptr)
        , constPointer(nullptr)
        , pointerToConst(nullptr)
        , constReference(constValue)
        , array{}
        , pointerArray{}
    {
    }

    int* pointer;
    int*& referencePointer;
    int** pointerToPointer;
    int* const constPointer;
    const int* pointerToConst;
    const int& constReference;
    int array[10];
    int* pointerArray[5];
};

// ========================================================================
// 3. FUNCTIONS
// ========================================================================

void simpleFunction();

int functionWithPrimitiveArgs(int a, float b, bool c);

void functionWithPointer(int* a);

void functionWithConstPointer(const int* a);

void functionWithReference(int& a);

void functionWithConstReference(const int& a);

void functionWithPointerToPointer(int** a);

void functionWithArray(int a[10]);

int functionWithDefaultArg(int a = 42);

// ========================================================================
// 4. FUNCTION POINTERS
// ========================================================================

using SimpleFunctionPtr = void (*)();

using FunctionPtrWithArgs = int (*)(int, float);

struct FunctionPointerFields
{
    void (*plainFunctionPointer)();
    int (*functionPointerWithArgs)(int, float);
    SimpleFunctionPtr typedefFunctionPointer;
    FunctionPtrWithArgs anotherTypedefPointer;
};

void functionTakingFunctionPointer(void (*callback)(int));

// ========================================================================
// 5. MEMBER FUNCTION POINTERS
// ========================================================================

class MemberPointerTest
{
public:

    int method(int value);
    void constMethod() const;
    static int staticMethod(int value);
};

using MethodPtr = int (MemberPointerTest::*)(int);
using ConstMethodPtr = void (MemberPointerTest::*)() const;

// ========================================================================
// 6. ENUMS
// ========================================================================

enum SimpleEnum
{
    SimpleEnum_ValueA,
    SimpleEnum_ValueB,
    SimpleEnum_ValueC
};

enum class ScopedEnum
{
    ValueA,
    ValueB
};

enum TypedEnum : unsigned char
{
    TypedEnum_A = 0,
    TypedEnum_B = 1
};

enum class TypedScopedEnum : char
{
    Min = -128,
    Max = 127
};

class EnumTest
{
    SimpleEnum simple;
    ScopedEnum scoped;
    TypedEnum typed;
    TypedScopedEnum typedScoped;
};

// ========================================================================
// 7. NESTED TYPES (THE MAIN SCOPE TEST)
// ========================================================================

struct Weapon
{
    int damage;
};

class Actor
{
public:

    struct Weapon
    {
        int damage;
        float range;
    };

    struct SaveData
    {
        struct Weapon
        {
            int damage;
            float range;
            int ammo;
        };

        Weapon localWeapon;
        Test::Weapon globalWeapon;
        Actor::Weapon actorWeapon;
    };

    struct NestedStruct
    {
        int value;
    };

    class NestedClass
    {
    public:

        struct DeepNestedStruct
        {
            int value;
            float ratio;
        };

        int field;
        DeepNestedStruct deep;
    };

    enum class NestedEnum
    {
        First,
        Second,
        Third
    };

    SaveData saveData;
    Weapon weapon;
    NestedStruct nestedStruct;
    NestedClass nestedClass;
};

// ========================================================================
// 8. DEEP NESTING
// ========================================================================

class Outer
{
public:

    class Inner
    {
    public:

        struct Deep
        {
            int value;
        };

        int innerField;
        Deep deep;
    };

    Inner inner;
};

// ========================================================================
// 9. INHERITANCE
// ========================================================================

class Base
{
public:

    int baseField;
    virtual ~Base()
    {
    }
    virtual int virtualMethod()
    {
        return 0;
    }
};

class PublicDerived : public Base
{
public:

    int derivedField;
    int virtualMethod() override
    {
        return 1;
    }
};

class MultipleBaseA
{
public:

    int a;
};

class MultipleBaseB
{
public:

    int b;

    void setb(int a_b)
    {
        this->b = a_b;
    }
};

class MultipleDerived : public MultipleBaseA, protected MultipleBaseB
{
public:

    int c;
};

// ========================================================================
// 10. VIRTUAL INHERITANCE
// ========================================================================

class VirtualBase
{
public:

    int vbField;
};

class VirtualDerivedA : public virtual VirtualBase
{
public:

    int aField;
};

class VirtualDerivedB : public virtual VirtualBase
{
public:

    int bField;
};

class DiamondDerived : public VirtualDerivedA, public VirtualDerivedB
{
public:

    int dField;
};

// ========================================================================
// 11. CONSTRUCTORS / DESTRUCTORS
// ========================================================================

class ConstructorTest
{
public:

    ConstructorTest();
    ConstructorTest(int value);
    ConstructorTest(const ConstructorTest& other);
    ConstructorTest(ConstructorTest&& other) noexcept; // TODO:
    ~ConstructorTest();
    ConstructorTest& operator=(const ConstructorTest& other);     // TODO:
    ConstructorTest& operator=(ConstructorTest&& other) noexcept; // TODO:

    int value;
};

class OuterWithInnerCtorDtor
{
public:

    class Inner
    {
    public:

        Inner();
        ~Inner();
        int data;
    };

    Inner inner;
};

// ========================================================================
// 12. VIRTUAL AND PURE VIRTUAL FUNCTIONS
// ========================================================================

class AbstractBase
{
public:

    virtual ~AbstractBase()
    {
    }
    virtual void pureVirtualMethod() = 0;
    virtual int virtualMethodWithArgs(int a, float b) = 0;
};

class ConcreteDerived : public AbstractBase
{
public:

    void pureVirtualMethod() override
    {
    }
    int virtualMethodWithArgs(int a, float b) override
    {
        return static_cast<int>(a + b);
    }
};

// ========================================================================
// 13. STATIC / CONST / CONSTEXPR MEMBERS
// ========================================================================

class StaticConstMembers
{
public:

    static int staticField;
    const int constField = 1;
    static const int staticConstField = 2;
    static constexpr int staticConstexprField = 42;
    mutable int mutableField; // TODO:
    volatile int volatileField;
};

// ========================================================================
// 14. BITFIELDS
// ========================================================================

struct BitfieldTest
{
    unsigned int flagA : 1;
    unsigned int flagB : 2;
    unsigned int flagC : 3;
    int : 32; // TODO:
    int signedField : 5;
    unsigned int : 0; // unnamed zero-width bitfield for alignment // TODO:
    unsigned int nextField : 8;
};

// ========================================================================
// 15. ANONYMOUS STRUCTS / UNIONS
// ========================================================================

struct AnonymousTest // TODO: fix
{
    union
    {
        int intValue;
        float floatValue;
    };
    struct
    {
        int x;
        int y;
    };
    union
    {
        struct
        {
            int z, w;
        };
        int a;
        struct
        {
            double trouble;
            float doubletrouble;
        };
    };
    int outOfStruct;

    union
    {
        struct
        {
            unsigned int flagA : 1;
            unsigned int flagB : 1;
            unsigned int flagC : 3;
            unsigned int reserved : 27;
        };
        int flagsRaw;
    };

    struct
    {
        short packedA;
        short packedB;
        union
        {
            struct
            {
                char byte0;
                char byte1;
                char byte2;
                char byte3;
            };
            int packedInt;
            float packedFloat;
        };
    };

    union
    {
        int soloUnionInt;
    };

    struct
    {
        int soloStructInt;
    };

    union
    {
        struct
        {
            long long bigA;
            long long bigB;
        };
        struct
        {
            int quadA;
            int quadB;
            int quadC;
            int quadD;
        };
        double bigDouble;
    };

    union
    {
        struct
        {
            unsigned char bitX : 4;
            unsigned char bitY : 4;
        };
        unsigned char bitsRaw;
        struct
        {
            unsigned short wideBit : 9;
            unsigned short wideRest : 7;
        };
    };

    int tailField1;
    int tailField2;

    union
    {
        struct
        {
            int repeatA;
            struct
            {
                short innerX;
                short innerY;
            };
        };
        long long repeatB;
    };
};

// ========================================================================
// 16. TYPEDEFS AND USING ALIASES
// ========================================================================

typedef int IntAlias;
typedef unsigned int UIntAlias;
typedef int* IntPointerAlias;
typedef void (*VoidFunctionPtr)();

using IntUsing = int;
using FunctionAlias = void (*)(int);
using NestedAlias = Test::Outer::Inner;
using ConstIntPtr = const int*;

struct TypedefUsage
{
    IntAlias integer;
    IntPointerAlias pointer;
    VoidFunctionPtr callback;
    FunctionAlias typedCallback;
    NestedAlias nested;
};

// ========================================================================
// 17. TEMPLATES
// ========================================================================

template <typename T>
struct TemplateStruct
{
    T value;
};

template <typename T, int N>
struct TemplateWithNonTypeParam
{
    T data[N];
};

struct TemplateUsage
{
    TemplateStruct<int> intTemplate;
    TemplateStruct<float> floatTemplate;
    TemplateStruct<Test::Weapon> weaponTemplate;
    TemplateWithNonTypeParam<int, 5> arrayTemplate;
};

// ========================================================================
// 18. CONST QUALIFIED FUNCTIONS
// ========================================================================

class ConstMethodTest // TODO:
{
public:

    void nonConstMethod();
    void constMethod() const;
    void volatileMethod() volatile;
    void constVolatileMethod() const volatile;
    int getValue() const;
};

// ========================================================================
// 19. NAMESPACES
// ========================================================================

namespace OuterNamespace
{
namespace InnerNamespace
{
struct NestedNamespaceStruct
{
    int value;
};
} // namespace InnerNamespace

struct SimpleNamespaceStruct
{
    float value;
};
} // namespace OuterNamespace

// ========================================================================
// 20. COMPLEX COMBINATIONS
// ========================================================================

struct ComplexFieldTypes
{
    Test::Outer::Inner::Deep deepNested;
    Test::OuterNamespace::InnerNamespace::NestedNamespaceStruct nsNested;
    Test::TemplateStruct<Test::Weapon> templatedWeapon;
    Test::FunctionPtrWithArgs callback;
    Test::Outer::Inner* innerPtr;
    const Test::Outer::Inner::Deep* constDeepPtr; // TODO: const
};

// ========================================================================
// 21. COMPILE TEST TRIGGER
// ========================================================================

class CompileTested
{
    int* referenceValue = nullptr;
    const int constReferenceValue = 0;

    PrimitiveTypes compile_PRIMITIVES;
    PointerTypes compile_POINTERS;
    FunctionPointerFields compile_FUNCPTRS;
    Actor compile_ACTOR;
    Outer compile_OUTER;
    Base compile_BASE;
    PublicDerived compile_DERIVED;
    MultipleDerived compile_MULTI;
    DiamondDerived compile_DIAMOND;
    ConstructorTest compile_CTOR;
    OuterWithInnerCtorDtor compile_INNER_CTOR;
    ConcreteDerived compile_CONCRETE;
    BitfieldTest compile_BITFIELD;
    AnonymousTest compile_ANON;
    TypedefUsage compile_TYPEDEF;
    TemplateUsage compile_TEMPLATE;
    ConstMethodTest compile_CONST_METHOD;
    ComplexFieldTypes compile_COMPLEX;
    StaticConstMembers compile_CONST_MEMBERS;
    MemberPointerTest compile_POINTER_TEST;
    EnumTest compile_ENUM;

public:

    CompileTested()
        : compile_POINTERS(referenceValue, constReferenceValue)
    {
        compile_PRIMITIVES.boolean = true;
        compile_PRIMITIVES.integer = 42;
        compile_PRIMITIVES.floating = 3.14f;
        compile_ACTOR.weapon.damage = 10;
        compile_ACTOR.weapon.range = 100.0f;
        compile_ACTOR.saveData.localWeapon.damage = 20;
        compile_ACTOR.saveData.globalWeapon.damage = 30;
        compile_ACTOR.saveData.actorWeapon.damage = 40;
        compile_OUTER.inner.innerField = 1;
        compile_OUTER.inner.deep.value = 2;
        compile_BASE.baseField = 1;
        compile_DERIVED.baseField = 2;
        compile_DERIVED.derivedField = 3;
        compile_MULTI.a = 1;
        // compile_MULTI.b = 2;
        compile_MULTI.c = 3;
        compile_DIAMOND.vbField = 1;
        compile_DIAMOND.aField = 2;
        compile_DIAMOND.bField = 3;
        compile_DIAMOND.dField = 4;
        compile_CTOR.value = 42;
        compile_INNER_CTOR.inner.data = 7;
        compile_BITFIELD.flagA = 1;
        compile_BITFIELD.flagB = 2;
        compile_BITFIELD.flagC = 3;
        compile_BITFIELD.signedField = -1;
        compile_BITFIELD.nextField = 255;
        compile_ANON.intValue = 0;
        compile_ANON.x = 10;
        compile_ANON.y = 20;
        compile_TYPEDEF.integer = 0;
        compile_TYPEDEF.pointer = nullptr;
        compile_TYPEDEF.callback = nullptr;
        compile_TYPEDEF.typedCallback = nullptr;
        compile_TEMPLATE.intTemplate.value = 1;
        compile_TEMPLATE.floatTemplate.value = 2.0f;
        compile_TEMPLATE.weaponTemplate.value.damage = 3;
        compile_CONST_METHOD.getValue();
        compile_COMPLEX.deepNested.value = 1;
        compile_COMPLEX.nsNested.value = 2;
        compile_COMPLEX.innerPtr = &compile_OUTER.inner;
        compile_COMPLEX.constDeepPtr = &compile_OUTER.inner.deep;
    }
};
} // namespace Test