#include <Rtti/RttiTests.hpp>

// ============================================================================
// Definitions for the EXE/RTTI fixture (see RttiTests.hpp).
//
// Every body below is intentionally *different* from its siblings: identical
// bodies can be folded by the linker's identical-COMDAT-folding (/OPT:ICF),
// and two folded functions share one RVA. RttiReader classifies a slot as
// "override" by comparing RVAs against the base vftable, so folded bodies would
// silently look like inherited slots and the test would lie.
//
// Optimisation is disabled for this translation unit in CMakeLists.txt
// (per-file /Od). The RTTI reconstructor disassembles the image: it locates a
// constructor by finding the function prologue that owns the vftable-writing
// instruction. At /O2 this fixture's constructors are tiny leaf functions with
// no prologue at all (a bare `mov [ecx], offset vftable; mov eax, ecx; ...;
// ret`), and the prologue heuristic then walks back into an unrelated function
// and recovers nonsense. /Od emits a conventional frame for every function,
// which is what makes the field-recovery half of this fixture meaningful.
// ============================================================================

namespace Test
{
namespace Rtti
{
// ========================================================================
// 1. OVERRIDE DEPTH — Shape / Polygon / Rect / Square
// ========================================================================

Shape::Shape()
    : m_shapeScale(1.0f)
{
}

Shape::~Shape()
{
}

float Shape::area()
{
    return 0.0f;
}

float Shape::perimeter()
{
    return m_shapeScale;
}

float Shape::centroidX()
{
    return m_shapeScale * 0.5f;
}

// ------------------------------------------------------------------------

Polygon::Polygon()
    : m_vertexCount(0)
{
}

Polygon::~Polygon()
{
}

float Polygon::area()
{
    return m_shapeScale + 1.0f;
}

int Polygon::vertexCount()
{
    return m_vertexCount;
}

// ------------------------------------------------------------------------

Rect::Rect()
    : m_width(1.0f)
    , m_height(2.0f)
{
}

Rect::~Rect()
{
}

float Rect::area()
{
    return m_width * m_height;
}

float Rect::perimeter()
{
    return 2.0f * (m_width + m_height);
}

float Rect::centroidX()
{
    return m_width * 0.25f;
}

void Rect::scaleBy(float a_factor)
{
    m_width *= a_factor;
    m_height *= a_factor;
}

// ------------------------------------------------------------------------

Square::Square()
    : m_side(3.0f)
{
}

Square::~Square()
{
}

float Square::area()
{
    return m_side * m_side;
}

void Square::scaleBy(float a_factor)
{
    m_side *= a_factor;
}

int Square::sideCount()
{
    return 4;
}

// ========================================================================
// 2. DESTRUCTORS
// ========================================================================

VirtualDtorBase::VirtualDtorBase()
    : m_kind(1)
{
}

VirtualDtorBase::~VirtualDtorBase()
{
}

int VirtualDtorBase::kind()
{
    return m_kind;
}

// ------------------------------------------------------------------------

VirtualDtorDerived::VirtualDtorDerived()
    : m_extraKind(2)
{
}

VirtualDtorDerived::~VirtualDtorDerived()
{
}

int VirtualDtorDerived::kind()
{
    return m_kind + m_extraKind;
}

// ------------------------------------------------------------------------

NonVirtualDtor::NonVirtualDtor()
    : m_tag(3)
{
}

NonVirtualDtor::~NonVirtualDtor()
{
}

int NonVirtualDtor::tag()
{
    return m_tag;
}

// ========================================================================
// 3. struct vs class
// ========================================================================

StructWithVirtuals::StructWithVirtuals()
    : structField(11)
{
}

StructWithVirtuals::~StructWithVirtuals()
{
}

int StructWithVirtuals::structValue()
{
    return structField;
}

// ------------------------------------------------------------------------

ClassWithVirtuals::ClassWithVirtuals()
    : classField(22)
{
}

ClassWithVirtuals::~ClassWithVirtuals()
{
}

int ClassWithVirtuals::classValue()
{
    return classField;
}

// ========================================================================
// 4. INHERITANCE ACCESS MODIFIERS
// ========================================================================

AccessBase::AccessBase()
    : m_accessBase(4)
{
}

AccessBase::~AccessBase()
{
}

int AccessBase::accessValue()
{
    return m_accessBase;
}

// ------------------------------------------------------------------------

PublicAccess::PublicAccess()
    : m_publicField(5)
{
}

PublicAccess::~PublicAccess()
{
}

int PublicAccess::accessValue()
{
    return m_accessBase + m_publicField;
}

// ------------------------------------------------------------------------

ProtectedAccess::ProtectedAccess()
    : m_protectedField(6)
{
}

ProtectedAccess::~ProtectedAccess()
{
}

int ProtectedAccess::accessValue()
{
    return m_accessBase + m_protectedField;
}

// ------------------------------------------------------------------------

PrivateAccess::PrivateAccess()
    : m_privateField(7)
{
}

PrivateAccess::~PrivateAccess()
{
}

int PrivateAccess::accessValue()
{
    return m_accessBase + m_privateField;
}

// ========================================================================
// 5. VIRTUAL (DIAMOND) INHERITANCE
// ========================================================================

DiamondTop::DiamondTop()
    : m_top(10)
{
}

DiamondTop::~DiamondTop()
{
}

int DiamondTop::depth()
{
    return m_top;
}

const char* DiamondTop::diamondName()
{
    return "top";
}

// ------------------------------------------------------------------------

DiamondLeft::DiamondLeft()
    : m_left(11)
{
}

DiamondLeft::~DiamondLeft()
{
}

// ------------------------------------------------------------------------

DiamondRight::DiamondRight()
    : m_right(12)
{
}

DiamondRight::~DiamondRight()
{
}

// ------------------------------------------------------------------------

DiamondBottom::DiamondBottom()
    : m_bottom(13)
{
}

DiamondBottom::~DiamondBottom()
{
}

int DiamondBottom::depth()
{
    return m_top + m_bottom;
}

const char* DiamondBottom::diamondName()
{
    return "bottom";
}

// ========================================================================
// 6. MULTIPLE INHERITANCE — interface inherited transitively
// ========================================================================

ITickable::~ITickable()
{
}

IUpdatable::~IUpdatable()
{
}

// ------------------------------------------------------------------------

MiRoot::MiRoot()
    : m_rootField(20)
{
}

MiRoot::~MiRoot()
{
}

void MiRoot::tick()
{
    m_rootField += 1;
}

int MiRoot::priority()
{
    return m_rootField;
}

// ------------------------------------------------------------------------

MiMid::MiMid()
    : m_midField(1.5f)
{
}

MiMid::~MiMid()
{
}

void MiMid::tick()
{
    m_rootField += 2;
}

void MiMid::update(float a_delta)
{
    m_midField += a_delta;
}

void MiMid::reset()
{
    m_midField = 0.0f;
}

// ------------------------------------------------------------------------

MiLeaf::MiLeaf()
    : m_leafField(2.5)
{
}

MiLeaf::~MiLeaf()
{
}

void MiLeaf::update(float a_delta)
{
    m_leafField += static_cast<double>(a_delta);
}

int MiLeaf::priority()
{
    return static_cast<int>(m_leafField);
}

// ========================================================================
// 7. VIRTUAL FUNCTION SIGNATURES
// ========================================================================

ArgProbe::ArgProbe()
    : m_modeValue(0)
    , m_ratio(0.5f)
{
}

ArgProbe::~ArgProbe()
{
}

void ArgProbe::vNoArgs()
{
    ++m_modeValue;
}

void ArgProbe::vPrimitives(bool a_flag, char a_byte, short a_small,
                           int a_int, unsigned int a_uint,
                           long long a_big, unsigned char a_u8)
{
    m_modeValue = (a_flag ? 1 : 0) + a_byte + a_small + a_int
        + static_cast<int>(a_uint) + static_cast<int>(a_big) + a_u8;
}

void ArgProbe::vFloating(double a_double, float a_single)
{
    m_ratio = static_cast<float>(a_double) + a_single;
}

bool ArgProbe::vRetBool(int a_value)
{
    return a_value > 0;
}

char ArgProbe::vRetChar(int a_value)
{
    return static_cast<char>(a_value & 0x7F);
}

short ArgProbe::vRetShort(int a_value)
{
    return static_cast<short>(a_value & 0x7FFF);
}

ArgProbe* ArgProbe::vRetThis()
{
    return this;
}

const ArgProbe* ArgProbe::vRetConstThis() const
{
    return this;
}

int& ArgProbe::vRetRef(int& a_ref)
{
    a_ref += m_modeValue;
    return a_ref;
}

float* ArgProbe::vRetPtr()
{
    return &m_ratio;
}

void ArgProbe::vStrings(const char* a_cstr, char* a_mutable,
                        const wchar_t* a_wide)
{
    if (a_cstr && a_mutable && a_wide)
        m_modeValue = 1;
}

void ArgProbe::vVoidPtr(void* a_ptr, const void* a_constPtr)
{
    m_modeValue = (a_ptr == a_constPtr) ? 1 : 0;
}

void ArgProbe::vRefs(int& a_ref, const ArgProbe& a_obj)
{
    a_ref += a_obj.m_modeValue;
}

ArgProbe::Payload ArgProbe::vStructByValue(Payload a_payload)
{
    Payload out;
    out.id = a_payload.id + m_modeValue;
    out.weight = a_payload.weight + m_ratio;
    return out;
}

ArgProbe::Mode ArgProbe::vEnum(Mode a_mode)
{
    return (a_mode == ModeA) ? ModeB : ModeA;
}

int ArgProbe::vFuncPtr(int (*a_callback)(int, float))
{
    return a_callback ? a_callback(m_modeValue, m_ratio) : 0;
}

int ArgProbe::vMemberPtr(int ArgProbe::* a_member)
{
    return a_member ? this->*a_member : 0;
}

void ArgProbe::vVariadic(int a_count, ...)
{
    m_modeValue += a_count;
}

int ArgProbe::vConstQualified() const
{
    return m_modeValue + 1;
}

void ArgProbe::vDefaultArgs(int a_value, float a_ratio)
{
    m_modeValue += a_value;
    m_ratio += a_ratio;
}

// ========================================================================
// 8. ABSTRACT CLASSES
// ========================================================================

AbstractPure::AbstractPure()
    : m_pureField(30)
{
}

AbstractPure::~AbstractPure()
{
}

int AbstractPure::optionalImplemented()
{
    return m_pureField;
}

// ------------------------------------------------------------------------

ConcreteFromPure::ConcreteFromPure()
    : m_concreteField(31)
{
}

ConcreteFromPure::~ConcreteFromPure()
{
}

int ConcreteFromPure::mustImplement()
{
    return m_pureField + m_concreteField;
}

void ConcreteFromPure::alsoMustImplement(int a_value)
{
    m_concreteField += a_value;
}

// ========================================================================
// 9. FIELD DETECTION THROUGH CONSTRUCTORS
// ========================================================================

ProbeChild::ProbeChild()
    : childValue(40)
    , childRatio(0.75f)
{
}

// ------------------------------------------------------------------------

FieldProbeBase::FieldProbeBase()
    : m_baseId(41)
    , m_baseFlag(1)
{
}

FieldProbeBase::~FieldProbeBase()
{
}

int FieldProbeBase::probeId()
{
    return m_baseId;
}

void FieldProbeBase::touchBase()
{
    ++m_baseFlag;
}

// ------------------------------------------------------------------------

FieldProbe::FieldProbe()
    : m_flagA(0)
    , m_counter(-7)
    , m_scale(1.25f)
    , m_weight(2.5)
    , m_enabled(false)
    , m_label{}
    , m_payload(nullptr)
    , m_flags(1)
    , m_reserved(0)
    , m_nested()
    , m_basePtr(nullptr)
    , m_tailFlag(9)      // written last -> fixes maxObservedOffset
{
    m_label[0] = 'p';
    m_label[1] = 'r';
    m_label[2] = 'o';
    m_label[3] = 'b';
    m_label[4] = 'e';
    m_basePtr = this;
}

FieldProbe::~FieldProbe()
{
}

int FieldProbe::probeId()
{
    return m_baseId * 2;
}

void FieldProbe::touchDerived()
{
    ++m_counter;
    m_flags = 0;
}

// ========================================================================
// 10. ENTRY POINT
// ========================================================================

namespace
{
/// Every object allocated below is recorded here and read back by
/// DrainInstances(). That makes the objects escape, so /O2 cannot fold the
/// `push <size>; call operator new; ...; call <ctor>` sequences away — those
/// sequences are exactly what FieldRecovery reads to recover the real object
/// size. Deliberately leaking them is fine: this is a compile-test fixture.
void*    g_instances[64] = {};
unsigned g_instanceCount = 0;
volatile long long g_instanceSink = 0;

template <typename T>
void Instantiate(T* a_object)
{
    if (g_instanceCount < 64)
        g_instances[g_instanceCount++] = a_object;
}

void DrainInstances()
{
    for (unsigned i = 0; i < g_instanceCount; ++i)
        g_instanceSink += (g_instances[i] != nullptr) ? 1 : 0;
}

} // namespace

void touchAllObjects()
{
    // 1. Override depth — one allocation per level of the chain.
    Instantiate(new Shape());
    Instantiate(new Polygon());
    Instantiate(new Rect());
    Instantiate(new Square());

    // 2. Destructors
    Instantiate(new VirtualDtorBase());
    Instantiate(new VirtualDtorDerived());
    Instantiate(new NonVirtualDtor());

    // 3. struct vs class
    Instantiate(new StructWithVirtuals());
    Instantiate(new ClassWithVirtuals());

    // 4. Inheritance access modifiers
    Instantiate(new AccessBase());
    Instantiate(new PublicAccess());
    Instantiate(new ProtectedAccess());
    Instantiate(new PrivateAccess());

    // 5. Virtual (diamond) inheritance
    Instantiate(new DiamondTop());
    Instantiate(new DiamondLeft());
    Instantiate(new DiamondRight());
    Instantiate(new DiamondBottom());

    // 6. MI with a transitively inherited interface
    Instantiate(new MiRoot());
    Instantiate(new MiMid());
    Instantiate(new MiLeaf());

    // 7. Signature coverage
    Instantiate(new ArgProbe());

    // 8. Concrete implementation of a pure interface
    Instantiate(new ConcreteFromPure());

    // 9. Constructor-observable field layouts
    Instantiate(new FieldProbeBase());
    Instantiate(new FieldProbe());

    DrainInstances();
}

} // namespace Rtti
} // namespace Test