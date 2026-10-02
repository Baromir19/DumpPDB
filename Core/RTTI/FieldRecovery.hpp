#pragma once

#include <Core/PE/PEImage.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <Zydis/Zydis.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// FieldRecovery — dual-mode (x86 thiscall / x64 fastcall) field tracer
///
/// x64 (Windows ABI):
///   - this in rcx
///   - Decoder: LONG_64 / STACK_WIDTH_64
///   - Constructor pattern: MOV RAX, <vftableAbsVA>  /  LEA RAX,[RIP+disp]
///                          MOV [RCX], RAX
///
/// x86 (MSVC thiscall):
///   - this in ecx
///   - Decoder: LONG_COMPAT_32 / STACK_WIDTH_32
///   - Constructor pattern: MOV dword ptr [ECX], <vftableAbsVA>   (direct imm32)
///                       or MOV EAX, <vftableAbsVA> ; MOV [ECX], EAX
///
/// Allocation size:
///   x86: operator new(size) -> push imm32 ; call _operator_new
///   x64: mov rcx, imm64    ; call operator_new
/// ============================================================================
class FieldRecovery
{
public:

    FieldRecovery() = default;

    /// Recover fields for the given vftable.
    /// a_vftableRva        — RVA of the primary vftable.
    /// a_pe                — loaded PEImage.
    /// a_bitness           — x86 or x64.
    /// a_baseVftRvas       — RVAs of MI base vftables (their vfptr writes are skipped).
    /// a_baseVftOffsets    — corresponding byte offsets in the object for each base vftable.
    /// a_knownVftRvas      — all vftable RVAs known in the image (for foreign vftable detection).
    /// a_knownVftNames     — corresponding demangled names for a_knownVftRvas.
    /// a_objectSize        — output: inferred allocation size from operator new (0 if not found).
    /// a_maxObservedOffset — output: last_field.offset + last_field.size (for size estimation).
    [[nodiscard]] std::vector<FieldInfo> recover(
        uint64_t                     a_vftableRva,
        const PEImage&               a_pe,
        PEBitness                    a_bitness,
        const std::vector<uint64_t>& a_baseVftRvas,
        const std::vector<uint32_t>& a_baseVftOffsets,
        const std::vector<uint64_t>& a_knownVftRvas,
        const std::vector<std::string>& a_knownVftNames,
        uint32_t&                    a_objectSize,
        uint32_t&                    a_maxObservedOffset)
    {
        m_pe              = &a_pe;
        m_imageBase       = a_pe.imageBase();
        m_vftableRva      = a_vftableRva;
        m_bitness         = a_bitness;
        m_layout          = RttiLayout::forBitness(a_bitness);
        m_baseVftOffsets  = a_baseVftOffsets;
        m_knownVftRvas    = a_knownVftRvas;
        m_knownVftNames   = a_knownVftNames;
        a_objectSize      = 0;
        a_maxObservedOffset = 0;

        m_text  = nullptr;
        m_rdata = nullptr;
        for (const auto& s : a_pe.sections())
        {
            if (s.name() == ".text")  m_text  = &s;
            if (s.name() == ".rdata") m_rdata = &s;
        }
        if (!m_text) return {};

        if (m_bitness == PEBitness::Bits64)
            ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64,         ZYDIS_STACK_WIDTH_64);
        else
            ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32,  ZYDIS_STACK_WIDTH_32);

        uint64_t ctorRva = findConstructor();
        if (dbgOn())
            std::fprintf(stderr, "[FR] vft=0x%llX ctor=0x%llX\n",
                static_cast<unsigned long long>(a_vftableRva),
                static_cast<unsigned long long>(ctorRva));
        if (!ctorRva) return {};

        a_objectSize = extractAllocationSize(ctorRva);

        std::map<uint32_t, AccessRecord> accesses;
        traceThisAccesses(ctorRva, accesses);

        if (dbgOn())
        {
            std::fprintf(stderr, "[FR] vft=0x%llX rawAccesses=%zu objSize=%u\n",
                static_cast<unsigned long long>(a_vftableRva),
                accesses.size(), a_objectSize);
            for (const auto& kv : accesses)
                std::fprintf(stderr, "[FR]   raw off=0x%X size=%u kind=%d hint='%s' foreign='%s'\n",
                    kv.second.offset, kv.second.size,
                    static_cast<int>(kv.second.kind),
                    kv.second.ctorCallHint.c_str(),
                    kv.second.foreignVftableName.c_str());
        }

        auto fields = buildFields(accesses);
        if (!fields.empty())
        {
            const auto& last = fields.back();
            a_maxObservedOffset = last.offset + last.size;
        }

        // ── Validate the allocation size ──────────────────────────────────────
        // The size handed to operator new must at least cover every observed
        // field write. A smaller value cannot be the object's size — it is a
        // stray immediate picked up inside the constructor body (for example
        // `push 8 ; call PODArray::PODArray` for VehicleInstance). Reject it so
        // the caller falls back to the observed extent.
        if (a_objectSize > 0 && a_maxObservedOffset > 0
            && a_objectSize < a_maxObservedOffset)
        {
            if (dbgOn())
                std::fprintf(stderr,
                    "[FR] reject objSize=0x%X (< maxObserved=0x%X)\n",
                    a_objectSize, a_maxObservedOffset);
            a_objectSize = 0;
        }
        return fields;
    }

private:

    static constexpr size_t kMaxFuncBytes = 16384;
    static constexpr size_t kSearchRange  = 128 * 1024;
    static constexpr int kRegCount = ZYDIS_REGISTER_MAX_VALUE + 1;

    /// Diagnostics switch: set DUMPPDB_DEBUG=1 in the environment to dump
    /// constructor-search / tracing internals to stderr.
    [[nodiscard]] static bool dbgOn()
    {
        static const bool on = std::getenv("DUMPPDB_DEBUG") != nullptr;
        return on;
    }

    struct AccessRecord
    {
        uint32_t  offset      = 0;
        uint32_t  size        = 0;
        FieldKind kind        = FieldKind::Unknown;
        bool      isBitfield  = false;
        uint8_t   bitOffset   = 0;
        uint8_t   bitSize     = 0;
        /// Demangled name of class whose constructor was called with this+offset in ecx/rcx.
        std::string ctorCallHint;
        /// Demangled name of class whose vftable was written to this+offset (embedded object).
        std::string foreignVftableName;
        uint64_t    foreignVftableRva = 0;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] size_t rvaToFileOff(uint64_t a_rva) const noexcept
    {
        for (const auto& s : m_pe->sections())
        {
            if (a_rva >= s.virtualAddress() && a_rva < s.virtualAddress() + s.virtualSize())
                return static_cast<size_t>(s.fileOffset() + (a_rva - s.virtualAddress()));
        }
        return 0;
    }

    [[nodiscard]] bool decode(const uint8_t* a_ptr, size_t a_rem,
                               ZydisDecodedInstruction& a_insn,
                               ZydisDecodedOperand* a_ops) const
    {
        return ZYAN_SUCCESS(ZydisDecoderDecodeFull(&m_decoder, a_ptr, a_rem, &a_insn, a_ops));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Constructor finder
    //
    // Scans .text for:
    //   x64: MOV RAX/R?X, <vftVa>  followed by MOV [RCX+disp], RAX
    //        or LEA RAX, [RIP+disp] where target == vftableRva
    //   x86: MOV dword ptr [ECX+disp], <vftVa>  (direct write)
    //        or MOV EAX, <vftVa>  ;  MOV [ECX+disp], EAX
    //
    // Pass 1 collects every candidate (function start x ref to the vftable);
    // Pass 2 ranks them and returns the most plausible constructor:
    //   1) candidates that write a *secondary* vftable of this same object
    //      (subobject vfptr at this+K, K>0) — these only appear in the most
    //      derived constructor, never in a base-class constructor;
    //   2) larger allocation sizes from operator new (MSB-stripped) first;
    //   3) more direct field writes (this-relative stores) wins ties;
    //   4) earliest RVA as the final tie-break (deterministic output).
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] uint64_t findConstructor() const
    {
        const BinaryView& tv   = m_text->view();
        const uint64_t textVa  = m_text->virtualAddress();
        const uint64_t vftAbsVa = m_imageBase + m_vftableRva;

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];

        size_t pos = 0;
        const size_t sz = tv.size();

        // ── Pass 1: collect every function that references this vftable ──
        std::vector<uint64_t> candidates;

        while (pos + 2 < sz)
        {
            const uint8_t* ptr = tv.data() + pos;
            const size_t   rem = sz - pos;

            if (!decode(ptr, rem, insn, ops)) { pos++; continue; }

            const uint64_t curRva = textVa + static_cast<uint64_t>(pos);
            bool hit = false;

            if (m_bitness == PEBitness::Bits64)
            {
                // Pattern A: MOV reg, imm64 where imm == vftAbsVa
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                    && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(ops[1].imm.value.u) == vftAbsVa)
                {
                    hit = true;
                }

                // Pattern B: LEA reg, [RIP+disp] where RIP+disp == vftableRva
                if (!hit
                    && insn.mnemonic == ZYDIS_MNEMONIC_LEA
                    && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && ops[1].mem.base == ZYDIS_REGISTER_RIP)
                {
                    uint64_t target = curRva + insn.length
                        + static_cast<uint64_t>(static_cast<int64_t>(ops[1].mem.disp.value));
                    if (target == m_vftableRva)
                        hit = true;
                }
            }
            else // x86
            {
                // Pattern A: MOV dword ptr [ECX+disp], imm32 where imm == vftAbsVa
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                    && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(ops[1].imm.value.u) == vftAbsVa
                    && isThisReg32(ops[0].mem.base))
                {
                    hit = true;
                }

                // Pattern B: MOV EAX/reg, imm32 == vftAbsVa
                if (!hit
                    && insn.mnemonic == ZYDIS_MNEMONIC_MOV
                    && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(ops[1].imm.value.u) == vftAbsVa)
                {
                    hit = true;
                }
            }

            if (hit)
            {
                uint64_t ctorRva = findFunctionStart(curRva, textVa, tv);
                if (ctorRva != 0
                    && std::find(candidates.begin(), candidates.end(), ctorRva)
                        == candidates.end())
                    candidates.push_back(ctorRva);
            }

            pos += insn.length;
        }

        if (candidates.empty())
        {
            if (dbgOn())
                std::fprintf(stderr, "[FR] no ctor candidates for vft=0x%llX\n",
                    static_cast<unsigned long long>(m_vftableRva));
            return 0;
        }
        if (dbgOn())
            std::fprintf(stderr, "[FR] %zu ctor candidate(s) for vft=0x%llX\n",
                candidates.size(), static_cast<unsigned long long>(m_vftableRva));
        if (candidates.size() == 1) return candidates[0];

        // ── Pass 2: rank candidates, most-derived constructor first ──
        //
        // Priority order:
        //  1) Candidates that write a secondary vftable (only most-derived ctor does this)
        //  2) Among remainder: candidate with most field writes (most initialisation)
        //  3) Tie-break: largest allocation size
        //  4) Final tie-break: earliest RVA (deterministic)
        //
        // NOTE: peekAllocationSize is intentionally NOT the primary criterion because
        // base-class ctors do not call operator new themselves; any immediate found
        // at the start of the function may belong to an internal sub-allocation and
        // would produce a false/misleading size.

        // Check if ANY candidate writes a secondary vftable.
        bool anySecondary = false;
        for (uint64_t ctor : candidates)
        {
            if (ctorWritesSecondaryVftable(ctor, textVa, tv))
            { anySecondary = true; break; }
        }

        uint64_t bestCtor   = 0;
        int      bestScore  = -1;
        uint32_t bestAlloc  = 0;
        uint32_t bestWrites = 0;

        for (uint64_t ctor : candidates)
        {
            const uint32_t alloc  = peekAllocationSize(ctor, textVa, tv);
            const uint32_t writes = countFieldWrites(ctor, textVa, tv);
            const bool writesSecondary = ctorWritesSecondaryVftable(ctor, textVa, tv);

            if (dbgOn())
                std::fprintf(stderr, "[FR]   cand rva=0x%llX alloc=%u writes=%u secondary=%d\n",
                    static_cast<unsigned long long>(ctor), alloc, writes,
                    writesSecondary ? 1 : 0);

            int score;
            if (anySecondary)
            {
                // Secondary-vfptr writes dominate: only the most-derived ctor
                // installs the subobject vftables of this same complete object.
                score = writesSecondary ? 1000000 : 0;
                score += static_cast<int>(alloc);
                score += static_cast<int>(writes);
            }
            else
            {
                // No secondary vftable writes found — use field writes as primary
                // signal (base-class ctors that don't call new still initialise
                // most of their own fields).  Allocation size is secondary.
                score = static_cast<int>(writes) * 256 + static_cast<int>(alloc);
            }

            if (score < bestScore) continue;
            if (score == bestScore)
            {
                // Tie-break: more writes, then bigger alloc, then earlier RVA.
                if (writes < bestWrites) continue;
                if (writes == bestWrites && alloc < bestAlloc) continue;
                if (writes == bestWrites && alloc == bestAlloc) continue; // keep first
            }
            bestScore  = score;
            bestCtor   = ctor;
            bestAlloc  = alloc;
            bestWrites = writes;
        }

        return bestCtor;
    }

    /// Scan backward from a_hitRva to find the containing function prologue.
    [[nodiscard]] uint64_t findFunctionStart(uint64_t a_hitRva,
                                              uint64_t a_textVa,
                                              const BinaryView& a_tv) const
    {
        const uint64_t hitOff   = a_hitRva - a_textVa;
        const uint64_t scanFrom = (hitOff > kSearchRange) ? hitOff - kSearchRange : 0;

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];

        for (uint64_t back = hitOff; back > scanFrom; --back)
        {
            const uint8_t b = a_tv[back];
            if (b != 0xC3 && b != 0xCC && b != 0xC2 && b != 0x90) continue;

            uint64_t candidateOff = back + 1;
            while (candidateOff < hitOff)
            {
                uint8_t cb = a_tv[candidateOff];
                if (cb != 0xCC && cb != 0x90) break;
                candidateOff++;
            }
            if (candidateOff >= hitOff) continue;

            const uint8_t* ptr = a_tv.data() + candidateOff;
            const size_t   rem = static_cast<size_t>(a_tv.size() - candidateOff);

            bool looks = false;
            size_t sub = 0;
            for (int n = 0; n < 8 && !looks; ++n)
            {
                if (!decode(ptr + sub, rem - sub, insn, ops)) break;

                // push reg / push ebp / push rbp
                if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH) { looks = true; break; }
                // sub esp/rsp, N
                if (insn.mnemonic == ZYDIS_MNEMONIC_SUB
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && (ops[0].reg.value == ZYDIS_REGISTER_ESP
                        || ops[0].reg.value == ZYDIS_REGISTER_RSP))
                { looks = true; break; }
                // mov [esp+X], reg  (shadow home / frame save)
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                    && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && (ops[0].mem.base == ZYDIS_REGISTER_ESP
                        || ops[0].mem.base == ZYDIS_REGISTER_RSP))
                { looks = true; break; }

                sub += insn.length;
            }

            if (looks) return a_textVa + candidateOff;
        }

        return a_hitRva; // fallback: hit location itself
    }

    /// Count this-relative memory stores in a function (rough "does real
    /// field init" metric used to rank constructor candidates).
    [[nodiscard]] uint32_t countFieldWrites(uint64_t a_ctorRva,
                                             uint64_t a_textVa,
                                             const BinaryView& a_tv) const
    {
        if (a_ctorRva < a_textVa) return 0;
        const size_t startOff = static_cast<size_t>(a_ctorRva - a_textVa);
        if (startOff >= a_tv.size()) return 0;
        const size_t rawSpanW = std::min(kSearchRange, a_tv.size() - startOff);
        const size_t spanW = std::min(rawSpanW, kMaxFuncBytes);
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];
        uint32_t count = 0;
        size_t pos = startOff, endPos = startOff + spanW;
        const ZydisRegister thisReg = (m_bitness == PEBitness::Bits32)
            ? ZYDIS_REGISTER_ECX : ZYDIS_REGISTER_RCX;
        while (pos < endPos)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            if (!decode(ptr, endPos - pos, insn, ops)) { pos++; continue; }
            if (insn.mnemonic == ZYDIS_MNEMONIC_RET) break; // function end
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                && ops[0].mem.base == thisReg)
                count++;
            pos += insn.length;
        }
        return count;
    }

    /// True if the function writes a known *secondary* base vftable to
    /// this+K (K>0) — the signature of a most-derived constructor.
    [[nodiscard]] bool ctorWritesSecondaryVftable(uint64_t a_ctorRva,
                                                  uint64_t a_textVa,
                                                  const BinaryView& a_tv) const
    {
        if (m_baseVftOffsets.empty()) return false;
        if (a_ctorRva < a_textVa) return false;
        const size_t startOff = static_cast<size_t>(a_ctorRva - a_textVa);
        if (startOff >= a_tv.size()) return false;
        const size_t rawSpanS = std::min(kSearchRange, a_tv.size() - startOff);
        const size_t spanS = std::min(rawSpanS, kMaxFuncBytes);
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];
        uint64_t immInReg[kRegCount] = {};
        const ZydisRegister thisReg = (m_bitness == PEBitness::Bits32)
            ? ZYDIS_REGISTER_ECX : ZYDIS_REGISTER_RCX;
        size_t pos = startOff, endPos = startOff + spanS;
        while (pos < endPos)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            if (!decode(ptr, endPos - pos, insn, ops)) { pos++; continue; }
            if (insn.mnemonic == ZYDIS_MNEMONIC_RET) break; // function end
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                && ops[0].reg.value < kRegCount)
                immInReg[ops[0].reg.value] =
                    static_cast<uint64_t>(ops[1].imm.value.u);
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[0].mem.base == thisReg)
            {
                const int64_t disp = ops[0].mem.disp.has_displacement
                    ? ops[0].mem.disp.value : 0;
                if (disp > 0 && disp <= 65535
                    && ops[1].reg.value < kRegCount)
                {
                    const uint64_t val = immInReg[ops[1].reg.value];
                    const uint64_t vftRva = (val >= m_imageBase)
                        ? val - m_imageBase : 0;
                    if (vftRva != 0 && vftRva != m_vftableRva)
                        for (uint32_t off : m_baseVftOffsets)
                            if (off > 0 && off == static_cast<uint32_t>(disp))
                                return true;
                }
            }
            pos += insn.length;
        }
        return false;
    }

    /// Extract the operator-new allocation size visible at the head of a
    /// constructor candidate (push imm32 / mov reg,imm before a call).
    /// x86 callers pass the size in the instruction right before the CALL
    /// (push imm32 then CALL operator_new then ADD ESP — three neighbors).
    /// Returns 0 when no plausible size is found. Values with the MSB set
    /// are flag ORs (EH kind) — the flag bit is stripped.
    /// IMPORTANT: this runs at the CALLER of operator new (the factory that
    /// allocated this object), not inside the ctor itself.
    [[nodiscard]] uint32_t peekAllocationSize(uint64_t a_ctorRva,
                                              uint64_t a_textVa,
                                              const BinaryView& a_tv) const
    {
        if (a_ctorRva < a_textVa) return 0;
        const size_t startOff = static_cast<size_t>(a_ctorRva - a_textVa);
        if (startOff >= a_tv.size()) return 0;
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];
        size_t pos = startOff, endPos = std::min(startOff + 64, a_tv.size());
        while (pos < endPos)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            if (!decode(ptr, endPos - pos, insn, ops)) { pos++; continue; }
            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL) break;
            for (uint8_t oi = 0; oi < insn.operand_count; ++oi)
            {
                if (ops[oi].type != ZYDIS_OPERAND_TYPE_IMMEDIATE) continue;
                uint64_t v = static_cast<uint64_t>(ops[oi].imm.value.u);
                if (v & 0x80000000ULL) v &= ~0x80000000ULL;
                // Reject suspicious sentinel values: 0, 0xFFFF, 0xFFFFFFFF, etc.
                if (v == 0 || v == 0xFFFF || v == 0xFFFFFFFF) continue;
                // A size can never be an image address. Without this, an
                // immediate such as `mov eax, offset ??_7Foo@@6B@` is read as a
                // multi-megabyte allocation and then dominates the constructor
                // ranking score, selecting a function that is not a constructor.
                if (v >= m_imageBase) continue;
                // Reject values that are not plausible object sizes
                // (must be >= 4, < 16MB, and aligned to 4)
                if (v >= 4 && v < 0x1000000 && (v % 4) == 0)
                    return static_cast<uint32_t>(v);
            }
            pos += insn.length;
        }
        return 0;
    }



    // ─────────────────────────────────────────────────────────────────────────
    // Allocation size extraction
    //
    // A most-derived constructor never calls `operator new` for `this`; the
    // object is allocated by the factory that then invokes the constructor:
    //
    //     push <size>            ; x86        (mov rcx, <size> on x64)
    //     call operator new
    //     ...
    //     mov ecx, <ptr>
    //     call <ctor>
    //
    // The authoritative source is therefore the allocation site (a caller of the
    // constructor). Scanning the constructor body is only a fallback and its
    // result is validated against the observed field writes by recover().
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] uint32_t extractAllocationSize(uint64_t a_ctorRva) const
    {
        const uint32_t fromCaller = allocationSizeFromCallers(a_ctorRva);
        if (fromCaller != 0)
        {
            if (dbgOn())
                std::fprintf(stderr,
                    "[FR] allocSize(0x%llX) = 0x%X (allocator site)\n",
                    static_cast<unsigned long long>(a_ctorRva), fromCaller);
            return fromCaller;
        }

        const uint32_t fromHead = allocationSizeFromCtorHead(a_ctorRva);
        if (dbgOn())
            std::fprintf(stderr,
                "[FR] allocSize(0x%llX) = 0x%X (ctor head fallback)\n",
                static_cast<unsigned long long>(a_ctorRva), fromHead);
        return fromHead;
    }

    /// Locate every direct call to the constructor and read the size operand of
    /// the nearest preceding allocator call. Returns the largest plausible size
    /// found, or 0 when the constructor is never reached from an allocation site.
    [[nodiscard]] uint32_t allocationSizeFromCallers(uint64_t a_ctorRva) const
    {
        if (!m_text) return 0;

        const BinaryView& tv  = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        const size_t   sz     = tv.size();
        if (sz < 5) return 0;

        const uint8_t* const data = tv.data();

        // Fast scan for near CALL opcodes (E8 rel32) instead of disassembling
        // the whole .text section. A false hit can only matter if its rel32
        // happens to resolve to this exact ctor RVA, which is vanishingly
        // unlikely and would still be filtered by the observed-size check.
        uint32_t best = 0;
        size_t   pos  = 0;
        while (pos + 5 <= sz)
        {
            const void* hit = std::memchr(data + pos, 0xE8, sz - pos - 4);
            if (!hit) break;
            pos = static_cast<size_t>(static_cast<const uint8_t*>(hit) - data);

            const uint32_t rel =
                  static_cast<uint32_t>(data[pos + 1])
                | (static_cast<uint32_t>(data[pos + 2]) << 8)
                | (static_cast<uint32_t>(data[pos + 3]) << 16)
                | (static_cast<uint32_t>(data[pos + 4]) << 24);

            const uint64_t curRva = textVa + static_cast<uint64_t>(pos);
            const uint64_t target = curRva + 5
                + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rel)));

            if (target == a_ctorRva)
            {
                const uint32_t size = sizeAtAllocationSite(curRva, textVa, tv);
                if (size > best) best = size;
            }
            ++pos;
        }
        return best;
    }

    /// Decode a small window ending at `a_callRva` (a `call <ctor>` instruction
    /// belonging to an allocation site) and return the immediate handed to the
    /// closest preceding call — i.e. the operator-new size. 0 when not found.
    [[nodiscard]] uint32_t sizeAtAllocationSite(uint64_t a_callRva,
                                                uint64_t a_textVa,
                                                const BinaryView& a_tv) const
    {
        if (a_callRva < a_textVa) return 0;
        const size_t callOff = static_cast<size_t>(a_callRva - a_textVa);
        if (callOff > a_tv.size()) return 0;

        constexpr size_t kWindow = 96;
        const size_t start = (callOff > kWindow) ? callOff - kWindow : 0;

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];

        uint32_t pending     = 0;
        bool     havePending = false;
        uint32_t result      = 0;

        size_t pos = start;
        while (pos < callOff)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            const size_t   rem = callOff - pos;
            if (!decode(ptr, rem, insn, ops)) { ++pos; havePending = false; continue; }

            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL)
            {
                if (havePending) result = pending; // nearest allocator before ctor
                havePending = false;
            }
            else
            {
                havePending = loadsSizeImmediate(insn, ops, pending);
            }
            pos += insn.length;
        }
        if (havePending) result = pending; // allocator call sits right before ctor
        return result;
    }

    /// True when the instruction loads a plausible allocation size into the
    /// first argument location:
    ///   x86: PUSH imm32
    ///   x64: MOV RCX/ECX, imm64
    [[nodiscard]] static bool loadsSizeImmediate(const ZydisDecodedInstruction& a_insn,
                                                 const ZydisDecodedOperand* a_ops,
                                                 uint32_t& a_out)
    {
        uint64_t imm   = 0;
        bool     found = false;

        if (a_insn.mnemonic == ZYDIS_MNEMONIC_PUSH
            && a_insn.operand_count >= 1
            && a_ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            imm   = static_cast<uint64_t>(a_ops[0].imm.value.u);
            found = true;
        }
        else if (a_insn.mnemonic == ZYDIS_MNEMONIC_MOV
            && a_insn.operand_count >= 2
            && a_ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
            && a_ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
            && (a_ops[0].reg.value == ZYDIS_REGISTER_RCX
                || a_ops[0].reg.value == ZYDIS_REGISTER_ECX))
        {
            imm   = static_cast<uint64_t>(a_ops[1].imm.value.u);
            found = true;
        }

        if (!found) return false;
        // Plausible object sizes only: >= 4 bytes, < 16 MB, 4-byte aligned.
        if (imm < 4 || imm >= 0x1000000 || (imm % 4) != 0) return false;
        a_out = static_cast<uint32_t>(imm);
        return true;
    }

    /// Legacy fallback: scan the constructor body for the first `push imm32`
    /// followed by any call. Only meaningful when the ctor allocates something
    /// itself; otherwise it picks up an unrelated helper argument (which the
    /// caller rejects because it is smaller than the observed field extent).
    [[nodiscard]] uint32_t allocationSizeFromCtorHead(uint64_t a_ctorRva) const
    {
        if (!m_text) return 0;

        const BinaryView& tv  = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        if (a_ctorRva < textVa) return 0;

        const size_t startOff = static_cast<size_t>(a_ctorRva - textVa);
        const size_t maxBytes = std::min<size_t>(kMaxFuncBytes, tv.size() - startOff);

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];

        uint32_t lastSizeArg = 0;
        size_t   pos         = startOff;

        while (pos < startOff + maxBytes)
        {
            const uint8_t* ptr = tv.data() + pos;
            const size_t   rem = startOff + maxBytes - pos;

            if (!decode(ptr, rem, insn, ops)) { pos++; continue; }

            if (m_bitness == PEBitness::Bits64)
            {
                // MOV RCX, imm (first arg = size)
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[0].reg.value == ZYDIS_REGISTER_RCX
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    lastSizeArg = static_cast<uint32_t>(ops[1].imm.value.u);
                }
            }
            else
            {
                // PUSH imm32 (first stack arg = size for cdecl/stdcall new)
                if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH
                    && ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    lastSizeArg = static_cast<uint32_t>(ops[0].imm.value.u);
                }
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL && lastSizeArg > 0
                && lastSizeArg <= 65536)
            {
                return lastSizeArg;
            }

            pos += insn.length;
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // this-relative access tracer
    // ─────────────────────────────────────────────────────────────────────────

    void traceThisAccesses(uint64_t a_ctorRva, std::map<uint32_t, AccessRecord>& a_out) const
    {
        if (!m_text) return;

        const BinaryView& tv  = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        if (a_ctorRva < textVa) return;

        const size_t startOff = static_cast<size_t>(a_ctorRva - textVa);
        const size_t maxBytes = std::min<size_t>(kMaxFuncBytes, tv.size() - startOff);

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];

        bool isThis[kRegCount]  = {};

        const ZydisRegister thisReg = (m_bitness == PEBitness::Bits64)
                                    ? ZYDIS_REGISTER_RCX
                                    : ZYDIS_REGISTER_ECX;
        isThis[thisReg] = true;

        // Track what offset is in ecx/rcx just before a CALL
        // so we can annotate it as a ctor call hint.
        // lastThisOffset[reg] = offset that was added to `this` and stored in reg
        int32_t lastThisOffset[kRegCount];
        for (int i = 0; i < kRegCount; ++i) lastThisOffset[i] = -1;

        // Also track the last immediate loaded into a register (for foreign vftable detection)
        uint64_t lastImmInReg[kRegCount] = {};

        size_t pos    = startOff;
        size_t endPos = startOff + maxBytes;

        while (pos < endPos)
        {
            const uint8_t* ptr = tv.data() + pos;
            const size_t   rem = endPos - pos;

            if (!decode(ptr, rem, insn, ops)) { pos++; continue; }

            const uint64_t curRva = textVa + static_cast<uint64_t>(pos);

            // ── Track MOV dst, src aliases ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const ZydisRegister dst = ops[0].reg.value;
                const ZydisRegister src = ops[1].reg.value;
                if (src < kRegCount && dst < kRegCount)
                {
                    isThis[dst]          = isThis[src];
                    lastThisOffset[dst]  = lastThisOffset[src];
                    lastImmInReg[dst]    = lastImmInReg[src];
                }
            }

            // ── Track MOV reg, imm — for foreign vftable detection ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                const ZydisRegister dst = ops[0].reg.value;
                if (dst < kRegCount)
                    lastImmInReg[dst] = static_cast<uint64_t>(ops[1].imm.value.u);
            }

            // ── Track LEA reg, [this+disp] — ecx = this+offset pattern (x86 ctor arg) ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_LEA
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                const ZydisRegister dst  = ops[0].reg.value;
                const ZydisRegister base = ops[1].mem.base;
                if (dst < kRegCount && base < kRegCount && isThis[base])
                {
                    const int64_t disp = ops[1].mem.disp.has_displacement
                                       ? ops[1].mem.disp.value : 0;
                    if (disp >= 0 && disp <= 65535)
                    {
                        isThis[dst]         = false; // it's this+offset, not `this`
                        lastThisOffset[dst] = static_cast<int32_t>(disp);
                    }
                }
            }

            // ── x86: MOV ECX, reg where reg holds this+offset ──
            // Pattern: lea eax, [ecx+N] ; mov ecx, eax ; call Ctor
            if (m_bitness == PEBitness::Bits32
                && insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[0].reg.value == ZYDIS_REGISTER_ECX
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const ZydisRegister src = ops[1].reg.value;
                if (src < kRegCount && lastThisOffset[src] >= 0)
                    lastThisOffset[ZYDIS_REGISTER_ECX] = lastThisOffset[src];
            }

            // ── Detect CALL with ecx/rcx = this+offset → ctor call hint ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL)
            {
                const int32_t ctorOffset = lastThisOffset[thisReg];
                if (ctorOffset > 0) // offset 0 = primary ctor, skip
                {
                    const uint32_t off = static_cast<uint32_t>(ctorOffset);
                    auto it = a_out.find(off);
                    if (it == a_out.end())
                    {
                        AccessRecord rec;
                        rec.offset = off;
                        rec.size   = m_layout.ptrSize; // assume at least pointer-sized
                        rec.kind   = FieldKind::Pointer;
                        a_out[off] = rec;
                        it = a_out.find(off);
                    }
                    // Try to resolve the callee to a class name
                    if (it->second.ctorCallHint.empty())
                    {
                        std::string name = resolveCallTarget(insn, ops, curRva);
                        if (!name.empty())
                            it->second.ctorCallHint = name;
                        else
                            it->second.ctorCallHint = "?"; // unknown ctor
                    }
                }

                // Reset per-call
                for (int r = 0; r < kRegCount; ++r)
                    if (r != thisReg) { isThis[r] = false; lastThisOffset[r] = -1; }
                lastThisOffset[thisReg] = -1; // ecx gets clobbered by return value
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_RET) break;

            // ── Analyse memory operands for this-relative field accesses ──
            for (uint8_t oi = 0; oi < insn.operand_count; ++oi)
            {
                const ZydisDecodedOperand& op = ops[oi];
                if (op.type != ZYDIS_OPERAND_TYPE_MEMORY) continue;

                const ZydisRegister base = op.mem.base;
                if (base == ZYDIS_REGISTER_NONE) continue;
                if (base >= kRegCount || !isThis[base]) continue;
                if (op.mem.index != ZYDIS_REGISTER_NONE) continue;

                const int64_t disp = op.mem.disp.has_displacement ? op.mem.disp.value : 0;
                if (disp < 0 || disp > 65535) continue;

                const uint32_t fieldOffset = static_cast<uint32_t>(disp);
                const uint32_t accessSize  = op.size / 8;
                if (accessSize == 0) continue;

                // ── Detect foreign vftable write: MOV [this+offset], imm/reg ──
                // where the immediate is a known vftable VA (not primary, not MI base)
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && oi == 0
                    && accessSize == m_layout.ptrSize)
                {
                    uint64_t writtenVa = 0;
                    bool isVftableWrite = false;

                    if (ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                    {
                        writtenVa = static_cast<uint64_t>(ops[1].imm.value.u);
                        isVftableWrite = true;
                    }
                    else if (ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
                    {
                        const ZydisRegister src = ops[1].reg.value;
                        if (src < kRegCount && lastImmInReg[src] != 0)
                        {
                            writtenVa = lastImmInReg[src];
                            isVftableWrite = true;
                        }
                    }

                    if (isVftableWrite && writtenVa != 0)
                    {
                        const uint64_t writtenRva = (writtenVa >= m_imageBase)
                                                  ? writtenVa - m_imageBase : 0;

                        // Check if primary vfptr write — skip
                        bool isVfptrWrite = (fieldOffset == 0 && writtenRva == m_vftableRva);
                        if (!isVfptrWrite)
                        {
                            for (uint32_t baseOff : m_baseVftOffsets)
                                if (fieldOffset == baseOff) { isVfptrWrite = true; break; }
                        }

                        if (!isVfptrWrite && writtenRva != 0)
                        {
                            // Check if it's a known foreign vftable
                            std::string foreignName = lookupVftableName(writtenRva);
                            if (!foreignName.empty())
                            {
                                auto& rec = a_out[fieldOffset];
                                rec.offset = fieldOffset;
                                rec.size   = m_layout.ptrSize;
                                rec.kind   = FieldKind::Pointer;
                                if (rec.foreignVftableName.empty())
                                {
                                    rec.foreignVftableName = foreignName;
                                    rec.foreignVftableRva  = writtenRva;
                                }
                                pos += insn.length;
                                continue;
                            }
                        }

                        // Regular vfptr skip
                        if (isVfptrWrite) { pos += insn.length; continue; }
                    }
                }

                // ── Skip known vfptr offsets ──
                if (accessSize == m_layout.ptrSize
                    && insn.mnemonic == ZYDIS_MNEMONIC_MOV && oi == 0)
                {
                    bool isVfptrWrite = (fieldOffset == 0);
                    if (!isVfptrWrite)
                        for (uint32_t baseOff : m_baseVftOffsets)
                            if (fieldOffset == baseOff) { isVfptrWrite = true; break; }
                    if (isVfptrWrite) continue;
                }

                FieldKind kind = inferKind(insn, oi, accessSize);

                auto it = a_out.find(fieldOffset);
                if (it == a_out.end())
                {
                    AccessRecord rec;
                    rec.offset = fieldOffset;
                    rec.size   = accessSize;
                    rec.kind   = kind;
                    a_out[fieldOffset] = rec;
                }
                else
                {
                    it->second.kind = upgradedKind(it->second.kind, kind);
                    it->second.size = std::max(it->second.size, accessSize);
                }
            }

            pos += insn.length;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Kind inference from instruction
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] static FieldKind inferKind(const ZydisDecodedInstruction& insn,
                                              uint8_t /*opIdx*/,
                                              uint32_t accessSize)
    {
        switch (insn.mnemonic)
        {
        case ZYDIS_MNEMONIC_MOVSS: case ZYDIS_MNEMONIC_ADDSS: case ZYDIS_MNEMONIC_SUBSS:
        case ZYDIS_MNEMONIC_MULSS: case ZYDIS_MNEMONIC_DIVSS: case ZYDIS_MNEMONIC_COMISS:
        case ZYDIS_MNEMONIC_UCOMISS: case ZYDIS_MNEMONIC_CVTSS2SD: case ZYDIS_MNEMONIC_CVTSI2SS:
            return FieldKind::Float;

        case ZYDIS_MNEMONIC_MOVSD: case ZYDIS_MNEMONIC_ADDSD: case ZYDIS_MNEMONIC_SUBSD:
        case ZYDIS_MNEMONIC_MULSD: case ZYDIS_MNEMONIC_DIVSD: case ZYDIS_MNEMONIC_COMISD:
        case ZYDIS_MNEMONIC_UCOMISD: case ZYDIS_MNEMONIC_CVTSD2SS: case ZYDIS_MNEMONIC_CVTSI2SD:
            return FieldKind::Double;

        case ZYDIS_MNEMONIC_MOVAPS: case ZYDIS_MNEMONIC_MOVUPS:
        case ZYDIS_MNEMONIC_MOVDQA: case ZYDIS_MNEMONIC_MOVDQU:
        case ZYDIS_MNEMONIC_MOVAPD: case ZYDIS_MNEMONIC_MOVUPD:
            return FieldKind::M128;

        case ZYDIS_MNEMONIC_MOVSX:
            if (accessSize == 1) return FieldKind::Int8;
            if (accessSize == 2) return FieldKind::Int16;
            return FieldKind::Int32;

        case ZYDIS_MNEMONIC_MOVZX:
            if (accessSize == 1) return FieldKind::UInt8;
            return FieldKind::UInt16;

        case ZYDIS_MNEMONIC_MOVSXD:
            return FieldKind::Int32;

        case ZYDIS_MNEMONIC_MOV:
            if (accessSize == 8) return FieldKind::Pointer;
            if (accessSize == 4) return FieldKind::Int32;
            if (accessSize == 2) return FieldKind::Int16;
            if (accessSize == 1) return FieldKind::UInt8;
            break;

        case ZYDIS_MNEMONIC_CMP: case ZYDIS_MNEMONIC_TEST:
            if (accessSize == 1) return FieldKind::Bool;
            break;

        // FPU instructions (x86 x87 float/double)
        case ZYDIS_MNEMONIC_FLD:  case ZYDIS_MNEMONIC_FST:  case ZYDIS_MNEMONIC_FSTP:
        case ZYDIS_MNEMONIC_FADD: case ZYDIS_MNEMONIC_FMUL: case ZYDIS_MNEMONIC_FDIV:
        case ZYDIS_MNEMONIC_FSUB:
            if (accessSize == 4) return FieldKind::Float;
            if (accessSize == 8) return FieldKind::Double;
            break;

        default: break;
        }

        return FieldKind::Unknown;
    }

    [[nodiscard]] static FieldKind upgradedKind(FieldKind existing, FieldKind newer)
    {
        if (existing == FieldKind::Unknown) return newer;
        if (newer == FieldKind::Float || newer == FieldKind::Double || newer == FieldKind::M128)
            return newer;
        if (newer == FieldKind::Bool && existing == FieldKind::UInt8) return FieldKind::Bool;
        return existing;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Build FieldInfo list
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<FieldInfo> buildFields(
        const std::map<uint32_t, AccessRecord>& a_accesses) const
    {
        std::vector<FieldInfo> fields;
        fields.reserve(a_accesses.size());

        for (const auto& kv : a_accesses)
        {
            const AccessRecord& rec = kv.second;

            // Skip primary vfptr (offset 0, pointer-sized, Pointer kind)
            if (rec.offset == 0 && rec.size == m_layout.ptrSize
                && rec.kind == FieldKind::Pointer
                && rec.foreignVftableName.empty()
                && rec.ctorCallHint.empty())
                continue;

            // Skip MI base vfptr offsets (unless they have extra hints)
            bool isMIVfptr = false;
            for (uint32_t baseOff : m_baseVftOffsets)
            {
                if (rec.offset == baseOff && rec.size == m_layout.ptrSize
                    && rec.kind == FieldKind::Pointer
                    && rec.foreignVftableName.empty()
                    && rec.ctorCallHint.empty())
                {
                    isMIVfptr = true;
                    break;
                }
            }
            if (isMIVfptr) continue;

            FieldInfo fi;
            fi.offset              = rec.offset;
            fi.size                = rec.size;
            fi.kind                = rec.kind;
            fi.isBitfield          = rec.isBitfield;
            fi.bitOffset           = rec.bitOffset;
            fi.bitSize             = rec.bitSize;
            fi.ctorCallHint        = rec.ctorCallHint;
            fi.foreignVftableName  = rec.foreignVftableName;
            fi.foreignVftableRva   = rec.foreignVftableRva;
            fields.push_back(fi);
        }

        std::sort(fields.begin(), fields.end(),
            [](const FieldInfo& a, const FieldInfo& b) { return a.offset < b.offset; });

        return fields;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    /// True for any 32-bit general-purpose register that can be `this` in x86.
    [[nodiscard]] static bool isThisReg32(ZydisRegister r) noexcept
    {
        return r == ZYDIS_REGISTER_EAX || r == ZYDIS_REGISTER_EBX
            || r == ZYDIS_REGISTER_ECX || r == ZYDIS_REGISTER_EDX
            || r == ZYDIS_REGISTER_ESI || r == ZYDIS_REGISTER_EDI
            || r == ZYDIS_REGISTER_EBP || r == ZYDIS_REGISTER_ESP;
    }

    /// Try to resolve a CALL target RVA to a known vftable class name.
    /// Returns empty string if not found.
    [[nodiscard]] std::string lookupVftableName(uint64_t a_rva) const
    {
        for (size_t i = 0; i < m_knownVftRvas.size(); ++i)
        {
            if (m_knownVftRvas[i] == a_rva && i < m_knownVftNames.size())
                return m_knownVftNames[i];
        }
        return {};
    }

    /// Try to extract the call target RVA from a CALL instruction and look it up.
    /// For direct calls: CALL rel32. For indirect: CALL [mem] — skip.
    [[nodiscard]] std::string resolveCallTarget(
        const ZydisDecodedInstruction& insn,
        const ZydisDecodedOperand* ops,
        uint64_t a_curRva) const
    {
        if (insn.operand_count < 1) return {};

        // Direct CALL rel32
        if (ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            const uint64_t target = a_curRva + insn.length
                + static_cast<uint64_t>(static_cast<int64_t>(ops[0].imm.value.s));
            // Check if target is a known vftable RVA — unlikely for a ctor,
            // but look it up anyway. More useful: the target function's class name.
            // We don't have symbol names, so return empty for now.
            (void)target;
        }
        return {};
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Members
    // ─────────────────────────────────────────────────────────────────────────

    const PEImage*            m_pe              = nullptr;
    uint64_t                  m_imageBase       = 0;
    uint64_t                  m_vftableRva      = 0;
    PEBitness                 m_bitness         = PEBitness::Bits64;
    RttiLayout                m_layout          = RttiLayout::x64();
    std::vector<uint32_t>     m_baseVftOffsets;
    std::vector<uint64_t>     m_knownVftRvas;
    std::vector<std::string>  m_knownVftNames;
    const PESection*          m_text            = nullptr;
    const PESection*          m_rdata           = nullptr;

    ZydisDecoder              m_decoder         = {};
};

} // namespace DumpPDB
