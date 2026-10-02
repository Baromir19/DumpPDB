#pragma once

#include <cstdint>

// ============================================================================
// TestCompiland — RTTI / vftable reconstruction fixture
//
// Purpose
//   TestCompiland/Test/Tests.hpp exercises the *PDB / DIA* dump path (type
//   walker, member layout, function signatures). This header is a separate,
//   self-contained fixture for the *EXE RTTI* path
//   (RttiReader + TypeReconstructor + FieldRecovery), which never opens the
//   PDB at all:
//
//       DumpPDB CLI : --exe TestCompiland.exe --exe-type <name>
//       Python API  : pdb.exe_reconstruct_type("<name>")
//
//   Everything below is therefore designed so the reconstruction can be
//   verified from the binary alone:
//
//     * every type is polymorphic, so it has a vftable + RTTI COL;
//     * every constructor, destructor and virtual function is defined out of
//       line in RttiTests.cpp with a *distinct* body. Two identical bodies
//       could be folded by the linker (ICF), which would make a genuine
//       override look like an inherited, untouched slot;
//     * the concrete leaves are created with `new` in
//       Test::Rtti::touchAllObjects(), so operator-new allocation sites exist
//       and FieldRecovery can read the real object size.
//
// Covered here
//   1.  Single inheritance with overrides at 1, 2 and 3 levels, plus a slot
//       deliberately skipped by an intermediate class
//   2.  Virtual destructors, and one class with virtual methods but a
//       *non-virtual* destructor (slot-0 heuristic probe)
//   3.  struct vs class (mangled '?AU' vs '?AV')
//   4.  public / protected / private inheritance
//   5.  Virtual (diamond) inheritance
//   6.  Multiple inheritance where an interface is inherited *transitively*
//       through an intermediate class — the subobject vftable owner must
//       still be reported as that interface, not as the intermediate class
//   7.  A wide range of virtual-function parameter and return types
//   8.  Abstract classes with pure virtuals
//   9.  Field detection through constructors: padding gaps, bitfields,
//       arrays, embedded members, pointers, floating point, bool flags
//
// Not covered (impossible from RTTI — do not expect it in the output)
//   * public / protected / private on the base clause: MSVC RTTI encodes only
//     the *virtual* attribute, never the access specifier, so the reconstructor
//     always renders bases as `public`.
//   * member names and function names: RTTI stores none. Fields are emitted as
//     fld_0xNN / pad_0xNN and functions as func_<slot>_<owner>.
//
// Known limitations this fixture deliberately exposes
//   * A class with virtual functions but a *non-virtual* destructor
//     (NonVirtualDtor below): slot 0 of the vftable is then the class's first
//     virtual function, not a destructor. RttiReader assumes "slot 0 ==
//     destructor" (see RttiReader::readVftableSlots), so the output renders
//     `virtual ~NonVirtualDtor()` while the listed RVA actually belongs to
//     tag(), and tag() itself is not printed at all. Verified with dumpbin:
//     the RVA reported for slot 0 is `mov eax, dword ptr [ecx+4]`.
//   * Virtual-inheritance layouts (DiamondTop/Left/Right/Bottom): the
//     nontrivial hierarchy offsets are not all reported yet — DiamondBottom's
//     primary subobject and its transitive DiamondTop base are missing from the
//     output. The classes are kept here so the layout can be fixed and then
//     verified against a known-good declaration.
//   * The fixture must be compiled without optimisation (see the /Od note in
//     CMakeLists.txt): the constructor finder attributes a vftable write to a
//     function by looking for that function's prologue, and a prologue-less
//     leaf constructor (which /O2 produces for types this small) makes it walk
//     back into the preceding function.
// ============================================================================

namespace Test
{
namespace Rtti
{
// ========================================================================
// 1. OVERRIDE DEPTH — 1, 2 and 3 levels deep
// ========================================================================
//
// Declaring class and override depth of every virtual function:
//
//   area()       declared in Shape,   overridden in Polygon, Rect, Square
//                                     -> level 1, 2 and 3
//   perimeter()  declared in Shape,   overridden in Rect only
//                                     -> level 1 (Polygon deliberately skips it)
//   centroidX()  declared in Shape,   overridden in Rect only -> level 1
//   vertexCount() declared in Polygon (new slot), never overridden
//   scaleBy()    declared in Rect    (new slot), overridden in Square -> level 1
//   sideCount()  declared in Square  (new slot)
//
// Expected vftable slot layout (slot 0 is the destructor):
//
//   Shape   : [1] area          [2] perimeter          [3] centroidX
//   Polygon : [1] area (ovr)    [2] perimeter (inh)    [3] centroidX (inh)
//             [4] vertexCount (new)
//   Rect    : [1] area (ovr)    [2] perimeter (ovr)    [3] centroidX (ovr)
//             [4] vertexCount (inh) [5] scaleBy (new)
//   Square  : [1] area (ovr)    [2] perimeter (inh)    [3] centroidX (inh)
//             [4] vertexCount (inh) [5] scaleBy (ovr)  [6] sideCount (new)
//
// So in Square, area() is a 3rd-level override but the tool names it after its
// direct base Rect. That difference is exactly what this section is for.
// ------------------------------------------------------------------------

class Shape
{
public:

    Shape();
    virtual ~Shape();

    virtual float area();
    virtual float perimeter();
    virtual float centroidX();

protected:

    float m_shapeScale;
};

class Polygon : public Shape
{
public:

    Polygon();
    ~Polygon() override;

    float area() override;

    virtual int vertexCount();

protected:

    int m_vertexCount;
};

class Rect : public Polygon
{
public:

    Rect();
    ~Rect() override;

    float area() override;
    float perimeter() override;
    float centroidX() override;

    virtual void scaleBy(float a_factor);

protected:

    float m_width;
    float m_height;
};

class Square : public Rect
{
public:

    Square();
    ~Square() override;

    float area() override;
    void scaleBy(float a_factor) override;

    virtual int sideCount();

protected:

    float m_side;
};

// ========================================================================
// 2. DESTRUCTORS
// ========================================================================

// The normal, well-defined case: a virtual destructor that every level
// overrides. Slot 0 of each vftable is the scalar deleting destructor.
class VirtualDtorBase
{
public:

    VirtualDtorBase();
    virtual ~VirtualDtorBase();

    virtual int kind();

protected:

    int m_kind;
};

class VirtualDtorDerived : public VirtualDtorBase
{
public:

    VirtualDtorDerived();
    ~VirtualDtorDerived() override;

    int kind() override;

protected:

    int m_extraKind;
};

// Virtual methods but a NON-virtual destructor. MSVC still keeps a destructor
// entry at slot 0 (the scalar deleting destructor is only emitted for virtual
// destructors, but the vftable slot is still occupied by the first declared
// virtual function). This is the case that probes the
// `isDestructor = (slotIndex == 0)` heuristic in RttiReader.
class NonVirtualDtor
{
public:

    NonVirtualDtor();
    ~NonVirtualDtor();

    virtual int tag();

protected:

    int m_tag;
};

// ========================================================================
// 3. struct vs class
// ========================================================================
//
// Mangled names differ ('?AU' for a struct, '?AV' for a class), which is what
// TypeReconstructor::detectIsStruct keys on when printing the tag.

struct StructWithVirtuals
{
    StructWithVirtuals();
    virtual ~StructWithVirtuals();

    virtual int structValue();

    int structField;
};

class ClassWithVirtuals
{
public:

    ClassWithVirtuals();
    virtual ~ClassWithVirtuals();

    virtual int classValue();

    int classField;
};

// ========================================================================
// 4. INHERITANCE ACCESS MODIFIERS
// ========================================================================
//
// MSVC RTTI does not record the access specifier of a base clause, so the
// reconstructor prints every base as `public`. These three leave a record of
// what the real hierarchy looked like; the *offsets* and vftables must still
// come out identical to the public case.

class AccessBase
{
public:

    AccessBase();
    virtual ~AccessBase();

    virtual int accessValue();

protected:

    int m_accessBase;
};

class PublicAccess : public AccessBase
{
public:

    PublicAccess();
    ~PublicAccess() override;

    int accessValue() override;

protected:

    int m_publicField;
};

class ProtectedAccess : protected AccessBase
{
public:

    ProtectedAccess();
    ~ProtectedAccess() override;

    int accessValue() override;

protected:

    int m_protectedField;
};

class PrivateAccess : private AccessBase
{
public:

    PrivateAccess();
    ~PrivateAccess() override;

    int accessValue() override;

protected:

    int m_privateField;
};

// ========================================================================
// 5. VIRTUAL (DIAMOND) INHERITANCE
// ========================================================================
//
// DiamondTop is a single shared subobject; MSVC places a vbtable pointer in
// DiamondBottom and each virtual base carries its own vftable. depth() is
// overridden in DiamondBottom only, so the DIAMOND's subobject vftables must
// report an override while the un-overridden slots stay hidden.

class DiamondTop
{
public:

    DiamondTop();
    virtual ~DiamondTop();

    virtual int depth();
    virtual const char* diamondName();

protected:

    int m_top;
};

class DiamondLeft : public virtual DiamondTop
{
public:

    DiamondLeft();
    ~DiamondLeft() override;

protected:

    int m_left;
};

class DiamondRight : public virtual DiamondTop
{
public:

    DiamondRight();
    ~DiamondRight() override;

protected:

    int m_right;
};

class DiamondBottom : public DiamondLeft, public DiamondRight
{
public:

    DiamondBottom();
    ~DiamondBottom() override;

    int depth() override;              // slot declared by DiamondTop
    const char* diamondName() override;

protected:

    int m_bottom;
};

// ========================================================================
// 6. MULTIPLE INHERITANCE — interface inherited *transitively*
// ========================================================================
//
// Regression case for the subobject-vftable owner lookup.
//
//   MiRoot : ITickable                          ITickable    @ +0x00
//   MiMid  : MiRoot, IUpdatable                 IUpdatable   @ +0xNN (direct)
//   MiLeaf : MiMid                              IUpdatable   @ +0xNN (TRANSITIVE)
//
// In MiLeaf, IUpdatable is no longer a *direct* base, yet its subobject
// vftable still exists at a non-zero offset. The reconstructor must attribute
// that vftable (and its overrides) to IUpdatable. Falling back to "the direct
// base at this offset" yields no match here and used to print a bare
// `vftable (+0xNN)` with the wrong (derived-class) function names.
//
// Real-world equivalent: TrolleyCarInstance -> VehicleInstance ->
// AI::INavMeshRegisterable.

class ITickable
{
public:

    virtual ~ITickable();

    virtual void tick() = 0;
    virtual int  priority() = 0;
};

class IUpdatable
{
public:

    virtual ~IUpdatable();

    virtual void update(float a_delta) = 0;
    virtual void reset() = 0;
};

class MiRoot : public ITickable
{
public:

    MiRoot();
    ~MiRoot() override;

    void tick() override;
    int  priority() override;

protected:

    int m_rootField;
};

class MiMid : public MiRoot, public IUpdatable
{
public:

    MiMid();
    ~MiMid() override;

    void tick() override;
    void update(float a_delta) override;
    void reset() override;

protected:

    float m_midField;
};

class MiLeaf : public MiMid
{
public:

    MiLeaf();
    ~MiLeaf() override;

    void update(float a_delta) override;
    int  priority() override;

protected:

    double m_leafField;
};

// ========================================================================
// 7. VIRTUAL FUNCTION SIGNATURES — argument and return types
// ========================================================================
//
// RTTI stores no signatures, so none of this changes what the reconstructor
// prints (every slot becomes `func_<slot>_<owner>()`). The purpose here is the
// *PDB/DIA* side and future signature recovery: this class is the single place
// where every interesting argument/return shape is declared next to a vftable.

class ArgProbe
{
public:

    ArgProbe();
    virtual ~ArgProbe();

    enum Mode : int
    {
        ModeA,
        ModeB,
    };

    struct Payload
    {
        int   id;
        float weight;
    };

    // --- no arguments / void return ---
    virtual void vNoArgs();

    // --- scalar primitives ---
    virtual void  vPrimitives(bool a_flag, char a_byte, short a_small,
                              int a_int, unsigned int a_uint,
                              long long a_big, unsigned char a_u8);
    virtual void  vFloating(double a_double, float a_single);
    virtual bool  vRetBool(int a_value);
    virtual char  vRetChar(int a_value);
    virtual short vRetShort(int a_value);

    // --- pointer / reference returns ---
    virtual ArgProbe*       vRetThis();
    virtual const ArgProbe* vRetConstThis() const;
    virtual int&            vRetRef(int& a_ref);
    virtual float*          vRetPtr();

    // --- pointer / string arguments ---
    virtual void vStrings(const char* a_cstr, char* a_mutable,
                          const wchar_t* a_wide);
    virtual void vVoidPtr(void* a_ptr, const void* a_constPtr);
    virtual void vRefs(int& a_ref, const ArgProbe& a_obj);

    // --- aggregate / enum arguments ---
    virtual Payload vStructByValue(Payload a_payload);
    virtual Mode    vEnum(Mode a_mode);

    // --- callable arguments ---
    virtual int vFuncPtr(int (*a_callback)(int, float));
    virtual int vMemberPtr(int ArgProbe::* a_member);

    // --- variadic and qualifiers ---
    virtual void vVariadic(int a_count, ...);
    virtual int  vConstQualified() const;
    virtual void vDefaultArgs(int a_value = 7, float a_ratio = 1.5f);

protected:

    int   m_modeValue;
    float m_ratio;
};

// ========================================================================
// 8. ABSTRACT CLASSES — pure virtual slots
// ========================================================================
//
// A pure virtual slot has no RVA; the reconstructor must render it as
// `func_<slot>() = 0` and must not claim it is an override or a new function.
// AbstractPure itself is never instantiated — it keeps its vftable only
// because ConcreteFromPure's constructor installs it — while ConcreteFromPure
// implements every pure slot and therefore is concrete.

class AbstractPure
{
public:

    AbstractPure();
    virtual ~AbstractPure();

    virtual int  mustImplement() = 0;
    virtual void alsoMustImplement(int a_value) = 0;
    virtual int  optionalImplemented();

protected:

    int m_pureField;
};

class ConcreteFromPure : public AbstractPure
{
public:

    ConcreteFromPure();
    ~ConcreteFromPure() override;

    int  mustImplement() override;
    void alsoMustImplement(int a_value) override;

protected:

    int m_concreteField;
};

// ========================================================================
// 9. FIELD DETECTION THROUGH CONSTRUCTORS
// ========================================================================
//
// FieldRecovery disassembles the constructor and records every
// `this`-relative store. These classes are built so that a constructor write
// is the *only* way to observe most fields, and so that each interesting
// layout feature appears at least once:
//
//   * a 1-byte flag followed by padding before the next 4-byte field
//   * a 4-byte gap between two members
//   * arrays, pointers, float/double
//   * a bitfield pair (1 + 31 bits)
//   * an embedded member whose own constructor is called with this+offset
//   * a member written last, which is what fixes maxObservedOffset
//
// The leaves are allocated with `operator new` in touchAllObjects(), so the
// exact object size can be recovered from the allocation site.

// Embedded, non-polymorphic member: detected via a ctor call at this+offset.
struct ProbeChild
{
    ProbeChild();

    int   childValue;
    float childRatio;
};

class FieldProbeBase
{
public:

    FieldProbeBase();
    virtual ~FieldProbeBase();

    virtual int  probeId();
    virtual void touchBase();

protected:

    int     m_baseId;
    uint8_t m_baseFlag;      // padding follows
};

class FieldProbe : public FieldProbeBase
{
public:

    FieldProbe();
    ~FieldProbe() override;

    int  probeId() override;
    virtual void touchDerived();

protected:

    uint8_t  m_flagA;        // 1 byte, then padding up to the next int32
    int32_t  m_counter;
    float    m_scale;
    double   m_weight;
    bool     m_enabled;
    char     m_label[8];
    void*    m_payload;
    uint32_t m_flags : 1;
    uint32_t m_reserved : 31;
    ProbeChild m_nested;
    FieldProbeBase* m_basePtr;
    uint8_t  m_tailFlag;     // written last
};

// ========================================================================
// 10. ENTRY POINT
// ========================================================================

/// Allocates one instance of every concrete leaf with `operator new` and
/// exercises every virtual function through a base pointer. Called from
/// main(); it exists purely to keep the vftables, RTTI records, constructors
/// and destructors in the linked image so the RTTI reconstructor can see them.
void touchAllObjects();

} // namespace Rtti
} // namespace Test