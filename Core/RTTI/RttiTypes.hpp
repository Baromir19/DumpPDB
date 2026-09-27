#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DumpPDB
{
/// ============================================================================
/// MSVC RTTI structures — dual mode (x86 / x64)
///
/// KEY DIFFERENCES between x86 and x64:
///
///  Feature                  x86                    x64
///  ─────────────────────────────────────────────────────────
///  COL signature            0                      1
///  COL size                 20 bytes (5×u32)       24 bytes (6×u32, +selfRva)
///  Pointers in .rdata       4-byte absolute VA     4-byte RVA from image base
///  Pointer before vftable   4-byte abs ptr→COL     8-byte abs ptr→COL
///  TypeDescriptor name off  +8  (2×u32)            +16 (2×u64)
///  this register            ecx (thiscall)         rcx (Microsoft x64)
///  Native pointer size      4 bytes                8 bytes
///
/// References:
///   https://www.openrce.org/articles/full_view/23
///   https://github.com/EpicGames/UnrealEngine RTTI reader patterns
/// ============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// Bitness flag — everything dual-mode is parameterised by this
// ─────────────────────────────────────────────────────────────────────────────

enum class PEBitness : uint8_t
{
    Bits32, ///< x86 PE (PE32)
    Bits64, ///< x64 PE (PE32+)
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout constants — centralised per-bitness values
// ─────────────────────────────────────────────────────────────────────────────

struct RttiLayout
{
    uint32_t colSignature;       ///< Expected COL.signature value.
    uint32_t colSize;            ///< sizeof(COL on disk).
    uint32_t ptrSize;            ///< Native pointer size in bytes.
    uint32_t typeDescNameOffset; ///< Byte offset of the name[] in TypeDescriptor.
    bool     colHasSelfRva;      ///< x64 COL has an extra selfRva field.
    bool     rdataPointersAreRva;///< x64 stores RVAs; x86 stores absolute VAs.

    static constexpr RttiLayout x86() noexcept
    {
        return {0, 20, 4, 8, false, false};
    }
    static constexpr RttiLayout x64() noexcept
    {
        return {1, 24, 8, 16, true, true};
    }

    static constexpr RttiLayout forBitness(PEBitness b) noexcept
    {
        return (b == PEBitness::Bits32) ? x86() : x64();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// On-disk representations (field names match MSVC ABI docs)
// ─────────────────────────────────────────────────────────────────────────────

/// PMD — pointer-to-member displacement.
struct PmdOnDisk
{
    int32_t mdisp; ///< Offset of field in the object.
    int32_t pdisp; ///< Offset of vbtable pointer (-1 = non-virtual).
    int32_t vdisp; ///< Offset within the vbtable.
};

/// RTTICompleteObjectLocator — stored immediately BEFORE each vftable in .rdata.
/// x86: 5 × uint32 = 20 bytes (no selfRva).
/// x64: 6 × uint32 = 24 bytes (has selfRva).
/// All pointer-like fields are ALWAYS 32-bit:
///   x86 — absolute virtual addresses (imageBase + rva)
///   x64 — 32-bit RVAs relative to imageBase
struct ColOnDisk
{
    uint32_t signature;          ///< 0 = x86, 1 = x64
    uint32_t offset;             ///< Offset of this vftable in the complete object.
    uint32_t cdOffset;           ///< Constructor displacement offset.
    uint32_t typeDescriptorAddr; ///< x86: absolute VA; x64: RVA
    uint32_t classDescriptorAddr;///< x86: absolute VA; x64: RVA
    // x64 only:
    uint32_t selfAddr;           ///< x64: RVA of this COL itself (for validation)
};

/// RTTIClassHierarchyDescriptor.
struct ChdOnDisk
{
    uint32_t signature;        ///< Always 0.
    uint32_t attributes;       ///< Bit 0: multiple, Bit 1: virtual inheritance.
    uint32_t numBaseClasses;
    uint32_t baseClassArrayAddr; ///< x86: abs VA of BCD array; x64: RVA
};

/// RTTIBaseClassDescriptor.
struct BcdOnDisk
{
    uint32_t typeDescriptorAddr; ///< x86: abs VA; x64: RVA
    uint32_t numContainedBases;
    PmdOnDisk pmd;
    uint32_t attributes;
    uint32_t classDescriptorAddr;///< x64 only (ignored on x86)
};

// ─────────────────────────────────────────────────────────────────────────────
// Parsed / higher-level types
// ─────────────────────────────────────────────────────────────────────────────

struct BaseClassInfo
{
    std::string mangledName;
    std::string demangledName;
    int32_t     offset;    ///< Byte offset in the derived object.
    bool        isVirtual;
    bool        isDirect;  ///< Immediate (not transitive) base.
};

enum class InheritanceKind : uint8_t
{
    None,
    Single,
    Multiple,
    Virtual,
};

struct VfuncSlot
{
    uint32_t slotIndex;
    uint64_t funcRva;       ///< RVA of the function (0 = pure virtual).
    bool     isPureVirtual;
    bool     isDestructor;  ///< Heuristic: slot 0 or 1.
    bool     isOverride;    ///< Same RVA exists in a direct base class at the same slot.
    bool     isNew;         ///< Not present in any direct base (slot index >= base vftable size).
};

/// Hint that a field at some offset may be an embedded object (detected via
/// a foreign vftable pointer at that offset).
struct EmbeddedClassHint
{
    uint32_t    fieldOffset;  ///< Byte offset within the object where the vfptr was found.
    uint64_t    vftableRva;   ///< RVA of the vftable the pointer points to.
    std::string className;    ///< Demangled name of the class owning that vftable (may be empty).
    bool        isMIBase;     ///< True if this matches a known MI base at this offset.
};

struct VftableInfo
{
    uint64_t             vftableRva;
    uint64_t             colRva;
    uint32_t             objectOffset;
    std::string          mangledName;
    std::string          className;
    std::string          fullName;
    InheritanceKind      inheritance;
    std::vector<BaseClassInfo>     bases;
    std::vector<VfuncSlot>         vfuncs;
    std::vector<EmbeddedClassHint> embeddedHints; ///< Foreign vftable pointers found in fields.
};

// ─────────────────────────────────────────────────────────────────────────────
// Field recovery results
// ─────────────────────────────────────────────────────────────────────────────

enum class FieldKind : uint8_t
{
    Unknown,
    Pointer,  ///< 4-byte ptr (x86) or 8-byte ptr (x64)
    Int8,
    UInt8,
    Bool,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
    M128,
    Bitfield,
    Padding,
};

struct FieldInfo
{
    uint32_t  offset;
    uint32_t  size;
    FieldKind kind;
    bool      isBitfield  = false;
    uint8_t   bitOffset   = 0;
    uint8_t   bitSize     = 0;

    /// If non-empty, this field is probably an embedded object of this class.
    std::string embeddedClassName;
    /// RVA of the foreign vftable if embeddedClassName is set.
    uint64_t    embeddedVftableRva = 0;
    /// True if this offset is a known MI base vfptr (not a "field" per se).
    bool        isMIBaseVfptr     = false;

    [[nodiscard]] std::string typeName() const
    {
        switch (kind)
        {
        case FieldKind::Pointer: return "void*";
        case FieldKind::Int8:    return "int8_t";
        case FieldKind::UInt8:   return "uint8_t";
        case FieldKind::Bool:    return "bool";
        case FieldKind::Int16:   return "int16_t";
        case FieldKind::UInt16:  return "uint16_t";
        case FieldKind::Int32:   return "int32_t";
        case FieldKind::UInt32:  return "uint32_t";
        case FieldKind::Int64:   return "int64_t";
        case FieldKind::UInt64:  return "uint64_t";
        case FieldKind::Float:   return "float";
        case FieldKind::Double:  return "double";
        case FieldKind::M128:    return "__m128";
        case FieldKind::Bitfield:return "unsigned int";
        case FieldKind::Padding: return "uint8_t";
        default:
            if (size == 8) return "uint64_t";
            if (size == 4) return "uint32_t";
            if (size == 2) return "uint16_t";
            return "uint8_t";
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Reconstructed type (final output)
// ─────────────────────────────────────────────────────────────────────────────

struct ReconstructedType
{
    std::string              className;
    std::string              fullName;
    std::string              mangledName;
    uint32_t                 objectSize         = 0;  ///< From operator new, or estimated.
    uint32_t                 maxObservedOffset  = 0;  ///< Highest field offset seen.
    bool                     isStruct           = false;
    PEBitness                bitness            = PEBitness::Bits64;
    InheritanceKind          inheritance        = InheritanceKind::None;
    std::vector<BaseClassInfo>  directBases;
    std::vector<VftableInfo>    vftables;
    std::vector<FieldInfo>      fields;
    bool                        hasRtti          = false;
};

} // namespace DumpPDB
