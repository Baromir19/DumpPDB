#pragma once

#include <Core/PE/PEImage.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <Zydis/Zydis.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
    [[nodiscard]] std::vector<FieldInfo> recover(uint64_t a_vftableRva,
        const PEImage& a_pe,
        PEBitness a_bitness,
        const std::vector<uint64_t>& a_baseVftRvas,
        const std::vector<uint32_t>& a_baseVftOffsets,
        const std::vector<uint64_t>& a_knownVftRvas,
        const std::vector<std::string>& a_knownVftNames,
        uint32_t& a_objectSize,
        uint32_t& a_maxObservedOffset,
        uint64_t* a_outCtorRva = nullptr,
        const std::vector<uint64_t>* a_ctorChain = nullptr,
        std::vector<StaticMemberInfo>* a_statics = nullptr)
    {
        m_pe = &a_pe;
        m_imageBase = a_pe.imageBase();
        m_vftableRva = a_vftableRva;
        m_bitness = a_bitness;
        m_layout = RttiLayout::forBitness(a_bitness);
        m_baseVftOffsets = a_baseVftOffsets;
        m_knownVftRvas = a_knownVftRvas;
        m_knownVftNames = a_knownVftNames;
        m_ctorChain = a_ctorChain;
        a_objectSize = 0;
        a_maxObservedOffset = 0;

        m_text = nullptr;
        m_rdata = nullptr;
        for (const auto& s : a_pe.sections())
        {
            if (s.name() == ".text")
                m_text = &s;
            if (s.name() == ".rdata")
                m_rdata = &s;
        }
        if (!m_text)
            return {};

        if (m_bitness == PEBitness::Bits64)
            ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        else
            ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);

        uint64_t ctorRva = findConstructor();
        if (dbgOn())
            std::fprintf(stderr,
                "[FR] vft=0x%llX ctor=0x%llX\n",
                static_cast<unsigned long long>(a_vftableRva),
                static_cast<unsigned long long>(ctorRva));
        if (!ctorRva)
            return {};
        if (a_outCtorRva)
            *a_outCtorRva = ctorRva;

        a_objectSize = extractAllocationSize(ctorRva);

        std::map<uint32_t, AccessRecord> accesses;
        {
            const std::clock_t tTrace = std::clock();
            traceThisAccesses(ctorRva, accesses, a_statics);
            if (dbgOn())
                std::fprintf(stderr,
                    "[FR-T] trace(vft=0x%llX) %d ms\n",
                    static_cast<unsigned long long>(a_vftableRva),
                    static_cast<int>((std::clock() - tTrace) * 1000 / CLOCKS_PER_SEC));
        }

        if (dbgOn())
        {
            std::fprintf(stderr,
                "[FR] vft=0x%llX rawAccesses=%zu objSize=%u\n",
                static_cast<unsigned long long>(a_vftableRva),
                accesses.size(),
                a_objectSize);
            for (const auto& kv : accesses)
                std::fprintf(stderr,
                    "[FR]   raw off=0x%X size=%u kind=%d hint='%s' foreign='%s'\n",
                    kv.second.offset,
                    kv.second.size,
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
        if (a_objectSize > 0 && a_maxObservedOffset > 0 && a_objectSize < a_maxObservedOffset)
        {
            if (dbgOn())
                std::fprintf(stderr,
                    "[FR] reject objSize=0x%X (< maxObserved=0x%X)\n",
                    a_objectSize,
                    a_maxObservedOffset);
            a_objectSize = 0;
        }
        return fields;
    }

private:

    static constexpr size_t kMaxFuncBytes = 16384;
    static constexpr size_t kSearchRange = 128 * 1024;
    static constexpr int kRegCount = ZYDIS_REGISTER_MAX_VALUE + 1;

    /// Canonical slot for a GPR, folding 32-bit sub-registers onto the
    /// 64-bit register (EAX→RAX). `mov eax,1` must invalidate RAX's
    /// `this`-tracking state too. Returns -1 for non-GPRs.
    static int regSlot(ZydisRegister r)
    {
        switch (r)
        {
        case ZYDIS_REGISTER_RAX:
        case ZYDIS_REGISTER_EAX:
            return 0;
        case ZYDIS_REGISTER_RCX:
        case ZYDIS_REGISTER_ECX:
            return 1;
        case ZYDIS_REGISTER_RDX:
        case ZYDIS_REGISTER_EDX:
            return 2;
        case ZYDIS_REGISTER_RBX:
        case ZYDIS_REGISTER_EBX:
            return 3;
        case ZYDIS_REGISTER_RSP:
        case ZYDIS_REGISTER_ESP:
            return 4;
        case ZYDIS_REGISTER_RBP:
        case ZYDIS_REGISTER_EBP:
            return 5;
        case ZYDIS_REGISTER_RSI:
        case ZYDIS_REGISTER_ESI:
            return 6;
        case ZYDIS_REGISTER_RDI:
        case ZYDIS_REGISTER_EDI:
            return 7;
        case ZYDIS_REGISTER_R8:
            return 8;
        case ZYDIS_REGISTER_R9:
            return 9;
        case ZYDIS_REGISTER_R10:
            return 10;
        case ZYDIS_REGISTER_R11:
            return 11;
        case ZYDIS_REGISTER_R12:
            return 12;
        case ZYDIS_REGISTER_R13:
            return 13;
        case ZYDIS_REGISTER_R14:
            return 14;
        case ZYDIS_REGISTER_R15:
            return 15;
        default:
            return -1;
        }
    }

    /// Canonical slot index for a GPR (short alias of regSlot for call sites).
    static int slot(ZydisRegister r)
    {
        return regSlot(r);
    }

    /// True for callee-saved canonical register slots (rbx, rbp, rsi, rdi,
    /// r12-r15). CALLs do not clobber them — O2 code keeps the object pointer
    /// in such registers across helper calls.
    static bool isCalleeSaved(int s)
    {
        return s == 3 || s == 5 || s == 6 || s == 7 || (s >= 12 && s <= 15);
    }

    /// Diagnostics switch: set DUMPPDB_DEBUG=1 in the environment to dump
    /// constructor-search / tracing internals to stderr.
    [[nodiscard]] static bool dbgOn()
    {
        static const bool on = std::getenv("DUMPPDB_DEBUG") != nullptr;
        return on;
    }

    struct AccessRecord
    {
        uint32_t offset = 0;
        uint32_t size = 0;
        FieldKind kind = FieldKind::Unknown;
        bool isBitfield = false;
        uint8_t bitOffset = 0;
        uint8_t bitSize = 0;
        /// Demangled name of class whose constructor was called with this+offset in ecx/rcx.
        std::string ctorCallHint;
        /// Demangled name of class whose vftable was written to this+offset (embedded object).
        std::string foreignVftableName;
        uint64_t foreignVftableRva = 0;
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

    [[nodiscard]] bool decode(const uint8_t* a_ptr,
        size_t a_rem,
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
        const BinaryView& tv = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        const uint64_t vftAbsVa = m_imageBase + m_vftableRva;

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

        size_t pos = 0;
        const size_t sz = tv.size();

        // ── Pass 1: collect every function that WRITES this vftable ──
        //
        // A vftable VA merely being LOADED (comparison, runtime registration,
        // RTTI bookkeeping) is not enough — optimised (O2) binaries reference
        // vftable VAs for many non-write purposes. Register-mediated patterns
        // therefore only count when the loaded register is STORED to memory
        // within a short window (the actual vfptr write).
        std::vector<uint64_t> candidates;

        const std::clock_t tPass1 = std::clock();

        int pendingSlot[kRegCount]; // reg slot -> VA load awaiting a store
        uint64_t pendingRva[kRegCount];
        size_t pendingPos[kRegCount];
        for (int i = 0; i < kRegCount; ++i)
        {
            pendingSlot[i] = -1;
            pendingRva[i] = 0;
            pendingPos[i] = 0;
        }

        while (pos + 2 < sz)
        {
            const uint8_t* ptr = tv.data() + pos;
            const size_t rem = sz - pos;

            if (!decode(ptr, rem, insn, ops))
            {
                pos++;
                continue;
            }

            const uint64_t curRva = textVa + static_cast<uint64_t>(pos);
            bool hit = false;
            uint64_t hitRva = curRva;

            if (insn.mnemonic == ZYDIS_MNEMONIC_RET)
            {
                for (int i = 0; i < kRegCount; ++i)
                    pendingSlot[i] = -1;
            }

            const int dstS = (insn.operand_count >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
                                 ? slot(ops[0].reg.value)
                                 : -1;

            // Pending bookkeeping on register definitions.
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int sSrc = slot(ops[1].reg.value); // copy propagates pending
                if (dstS >= 0)
                {
                    pendingSlot[dstS] = (sSrc >= 0) ? pendingSlot[sSrc] : -1;
                    pendingRva[dstS] = (sSrc >= 0) ? pendingRva[sSrc] : 0;
                    pendingPos[dstS] = (sSrc >= 0) ? pendingPos[sSrc] : 0;
                }
            }
            else if (dstS >= 0)
            {
                pendingSlot[dstS] = -1; // any other def clobbers
            }

            if (m_bitness == PEBitness::Bits64)
            {
                // Pattern A: MOV reg, imm64 where imm == vftAbsVa — pending a store.
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(ops[1].imm.value.u) == vftAbsVa && dstS >= 0)
                {
                    pendingSlot[dstS] = dstS;
                    pendingRva[dstS] = curRva;
                    pendingPos[dstS] = pos;
                }

                // Pattern B: LEA reg, [RIP+disp] where RIP+disp == vftableRva.
                if (insn.mnemonic == ZYDIS_MNEMONIC_LEA && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && ops[1].mem.base == ZYDIS_REGISTER_RIP && dstS >= 0)
                {
                    const uint64_t target
                        = curRva + insn.length
                          + static_cast<uint64_t>(static_cast<int64_t>(ops[1].mem.disp.value));
                    if (target == m_vftableRva)
                    {
                        pendingSlot[dstS] = dstS;
                        pendingRva[dstS] = curRva;
                        pendingPos[dstS] = pos;
                    }
                }
            }
            else // x86
            {
                // Pattern A: MOV dword ptr [GPR+disp], imm32 where imm == vftAbsVa
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(ops[1].imm.value.u) == vftAbsVa
                    && isThisReg32(ops[0].mem.base))
                {
                    hit = true;
                }

                // Pattern B: MOV reg, imm32 == vftAbsVa — pending a store.
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(ops[1].imm.value.u) == vftAbsVa && dstS >= 0)
                {
                    pendingSlot[dstS] = dstS;
                    pendingRva[dstS] = curRva;
                    pendingPos[dstS] = pos;
                }
            }

            // A pending register STORED to memory completes the vfptr write.
            // The store must go through a real object register — a stack-slot
            // spill of the VA (`mov [rbp-N], rax`) is not a vfptr write.
            if (!hit && insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int sSrc = slot(ops[1].reg.value);
                const ZydisRegister sb = ops[0].mem.base;
                const bool stackBased = sb == ZYDIS_REGISTER_NONE || sb == ZYDIS_REGISTER_ESP
                                        || sb == ZYDIS_REGISTER_RSP || sb == ZYDIS_REGISTER_EBP
                                        || sb == ZYDIS_REGISTER_RBP;
                if (sSrc >= 0 && pendingSlot[sSrc] >= 0 && pos - pendingPos[sSrc] <= 64
                    && !stackBased)
                {
                    hit = true;
                    hitRva = pendingRva[sSrc];
                }
            }

            if (hit)
            {
                uint64_t ctorRva = findFunctionStart(hitRva, textVa, tv);
                if (ctorRva != 0
                    && std::find(candidates.begin(), candidates.end(), ctorRva) == candidates.end())
                    candidates.push_back(ctorRva);
            }

            pos += insn.length;
        }

        if (dbgOn())
            std::fprintf(stderr,
                "[FR-T] pass1(vft=0x%llX) %d ms\n",
                static_cast<unsigned long long>(m_vftableRva),
                static_cast<int>((std::clock() - tPass1) * 1000 / CLOCKS_PER_SEC));

        if (candidates.empty())
        {
            if (dbgOn())
                std::fprintf(stderr,
                    "[FR] no ctor candidates for vft=0x%llX\n",
                    static_cast<unsigned long long>(m_vftableRva));
            return 0;
        }
        if (dbgOn())
            std::fprintf(stderr,
                "[FR] %zu ctor candidate(s) for vft=0x%llX\n",
                candidates.size(),
                static_cast<unsigned long long>(m_vftableRva));
        if (candidates.size() == 1)
            return candidates[0];
        // ── Ctor call-graph chain filter (ICF disambiguation) ─────────────────
        // Identical COMDAT folding can make MANY unrelated class ctors share
        // ONE base vftable (e.g. 227 candidates for a common interface).
        // The genuine base ctor is the one CALLED (directly or indirectly)
        // from the most-derived ctor — restrict the candidate set to chain
        // members when such a hint is available.
        if (m_ctorChain && !m_ctorChain->empty())
        {
            std::vector<uint64_t> filtered;
            for (uint64_t c : candidates)
            {
                if (std::find(m_ctorChain->begin(), m_ctorChain->end(), c) != m_ctorChain->end())
                    filtered.push_back(c);
            }
            if (!filtered.empty())
            {
                if (dbgOn())
                    std::fprintf(stderr,
                        "[FR]   ctor-chain filter: %zu/%zu candidates for vft=0x%llX\n",
                        filtered.size(),
                        candidates.size(),
                        static_cast<unsigned long long>(m_vftableRva));
                candidates = std::move(filtered);
            }
        }

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

        const std::clock_t tRank = std::clock();

        // Check if ANY candidate writes a secondary vftable.
        bool anySecondary = false;
        for (uint64_t ctor : candidates)
        {
            if (ctorWritesSecondaryVftable(ctor, textVa, tv))
            {
                anySecondary = true;
                break;
            }
        }

        uint64_t bestCtor = 0;
        int bestScore = -1;
        uint32_t bestAlloc = 0;
        uint32_t bestWrites = 0;

        for (uint64_t ctor : candidates)
        {
            const uint32_t alloc = peekAllocationSize(ctor, textVa, tv);
            const uint32_t writes = countFieldWrites(ctor, textVa, tv);
            const bool writesSecondary = ctorWritesSecondaryVftable(ctor, textVa, tv);

            if (dbgOn())
                std::fprintf(stderr,
                    "[FR]   cand rva=0x%llX alloc=%u writes=%u secondary=%d\n",
                    static_cast<unsigned long long>(ctor),
                    alloc,
                    writes,
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

            if (score < bestScore)
                continue;
            if (score == bestScore)
            {
                // Tie-break: more writes, then bigger alloc, then earlier RVA.
                if (writes < bestWrites)
                    continue;
                if (writes == bestWrites && alloc < bestAlloc)
                    continue;
                if (writes == bestWrites && alloc == bestAlloc)
                    continue; // keep first
            }
            bestScore = score;
            bestCtor = ctor;
            bestAlloc = alloc;
            bestWrites = writes;
        }
        if (dbgOn())
            std::fprintf(stderr,
                "[FR-T] rank(vft=0x%llX, %zu cands) %d ms\n",
                static_cast<unsigned long long>(m_vftableRva),
                candidates.size(),
                static_cast<int>((std::clock() - tRank) * 1000 / CLOCKS_PER_SEC));

        return bestCtor;
    }

    /// Scan backward from a_hitRva to find the containing function prologue.
    [[nodiscard]] uint64_t findFunctionStart(
        uint64_t a_hitRva, uint64_t a_textVa, const BinaryView& a_tv) const
    {
        const uint64_t hitOff = a_hitRva - a_textVa;
        const uint64_t scanFrom = (hitOff > kSearchRange) ? hitOff - kSearchRange : 0;

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

        for (uint64_t back = hitOff; back > scanFrom; --back)
        {
            const uint8_t b = a_tv[back];
            if (b != 0xC3 && b != 0xCC && b != 0xC2 && b != 0x90)
                continue;

            uint64_t candidateOff = back + 1;
            while (candidateOff < hitOff)
            {
                uint8_t cb = a_tv[candidateOff];
                if (cb != 0xCC && cb != 0x90)
                    break;
                candidateOff++;
            }
            if (candidateOff >= hitOff)
                continue;

            // A run of >=2 INT3 bytes directly before the candidate is the
            // linker's inter-function padding — accept the boundary even
            // without a recognised prologue (leaf/cold fragments in optimised
            // builds often start with XOR/MOV instead of PUSH/SUB).
            // The run scan is capped (max alignment padding is 16 bytes) to
            // keep the backward scan linear.
            {
                uint64_t runStart = candidateOff;
                size_t runLen = 0;
                while (runStart > scanFrom && runLen < 16)
                {
                    if (a_tv[runStart - 1] != 0xCC)
                        break;
                    --runStart;
                    ++runLen;
                }
                if (runLen >= 2)
                    return a_textVa + candidateOff;
            }

            const uint8_t* ptr = a_tv.data() + candidateOff;
            const size_t rem = static_cast<size_t>(a_tv.size() - candidateOff);

            bool looks = false;
            size_t sub = 0;
            for (int n = 0; n < 8 && !looks; ++n)
            {
                if (!decode(ptr + sub, rem - sub, insn, ops))
                    break;

                // push reg / push ebp / push rbp
                if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH)
                {
                    looks = true;
                    break;
                }
                // sub esp/rsp, N
                if (insn.mnemonic == ZYDIS_MNEMONIC_SUB
                    && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && (ops[0].reg.value == ZYDIS_REGISTER_ESP
                        || ops[0].reg.value == ZYDIS_REGISTER_RSP))
                {
                    looks = true;
                    break;
                }
                // mov [esp+X], reg  (shadow home / frame save)
                if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && (ops[0].mem.base == ZYDIS_REGISTER_ESP
                        || ops[0].mem.base == ZYDIS_REGISTER_RSP))
                {
                    looks = true;
                    break;
                }

                sub += insn.length;
            }

            if (looks)
                return a_textVa + candidateOff;
        }

        return a_hitRva; // fallback: hit location itself
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Public utility (used by TypeReconstructor for ICF disambiguation)
    // ─────────────────────────────────────────────────────────────────────────

public:

    /// Collect direct-call targets (two levels deep) from a constructor body —
    /// the set approximates "everything this ctor initialises", which includes
    /// its base-class ctors. Used to disambiguate ICF-folded shared vftables.
    [[nodiscard]] static std::vector<uint64_t> collectCtorChain(
        uint64_t a_ctorRva, PEBitness a_bitness, const PEImage& a_pe)
    {
        std::vector<uint64_t> chain;
        if (!a_ctorRva)
            return chain;
        chain.push_back(a_ctorRva);

        const PESection* text = nullptr;
        for (const auto& s : a_pe.sections())
            if (s.name() == ".text")
            {
                text = &s;
                break;
            }
        if (!text)
            return chain;

        const uint64_t textVa = text->virtualAddress();
        const BinaryView tv = text->view();

        ZydisDecoder dec;
        if (a_bitness == PEBitness::Bits64)
            ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        else
            ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);

        auto addCallsFrom = [&](uint64_t a_rva, std::vector<uint64_t>& a_out)
        {
            if (a_rva < textVa)
                return;
            const uint64_t off = a_rva - textVa;
            if (off >= tv.size())
                return;

            ZydisDecodedInstruction insn;
            ZydisDecodedOperand ops[10];
            size_t p = static_cast<size_t>(off);
            for (int n = 0; n < 384 && p < tv.size(); ++n)
            {
                if (!ZYAN_SUCCESS(
                        ZydisDecoderDecodeFull(&dec, tv.data() + p, tv.size() - p, &insn, ops)))
                    break;
                if (insn.mnemonic == ZYDIS_MNEMONIC_RET)
                    break;
                if (insn.mnemonic == ZYDIS_MNEMONIC_CALL && insn.operand_count_visible >= 1
                    && ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    const uint64_t t
                        = textVa + p + insn.length
                          + static_cast<uint64_t>(static_cast<int64_t>(ops[0].imm.value.s));
                    if (std::find(a_out.begin(), a_out.end(), t) == a_out.end())
                        a_out.push_back(t);
                }
                p += insn.length;
            }
        };

        std::vector<uint64_t> lvl1;
        addCallsFrom(a_ctorRva, lvl1);
        for (uint64_t t : lvl1)
            if (std::find(chain.begin(), chain.end(), t) == chain.end())
                chain.push_back(t);
        for (uint64_t t : lvl1)
        {
            std::vector<uint64_t> lvl2;
            addCallsFrom(t, lvl2);
            for (uint64_t t2 : lvl2)
                if (std::find(chain.begin(), chain.end(), t2) == chain.end())
                    chain.push_back(t2);
        }
        return chain;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Internal helpers (private again)
    // ─────────────────────────────────────────────────────────────────────────

private:

    /// Count this-relative memory stores in a function (rough "does real
    /// field init" metric used to rank constructor candidates).
    ///
    /// Keeps a light `this`-provenance model so optimised (O2) builds rank
    /// correctly too: besides the entry ecx/rcx thiscall register it follows
    /// MOV copies, this-slot stack spill/reloads AND the x86 O2/SEH pattern
    /// where the object arrives as the first stack argument ([entry_esp+4],
    /// gated by a later vftable write through that register).
    [[nodiscard]] uint32_t countFieldWrites(
        uint64_t a_ctorRva, uint64_t a_textVa, const BinaryView& a_tv) const
    {
        if (a_ctorRva < a_textVa)
            return 0;
        const size_t startOff = static_cast<size_t>(a_ctorRva - a_textVa);
        if (startOff >= a_tv.size())
            return 0;
        const size_t rawSpanW = std::min(kSearchRange, a_tv.size() - startOff);
        // Ranking only needs the ctor's head (field initialisation is near
        // the entry); a 4 KB window keeps ICF candidate sets (hundreds of
        // shared-vftable candidates) cheap to score.
        const size_t spanW = std::min<size_t>(0x1000, std::min(rawSpanW, kMaxFuncBytes));
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        uint32_t count = 0;
        size_t pos = startOff, endPos = startOff + spanW;

        const int thisS
            = slot((m_bitness == PEBitness::Bits64) ? ZYDIS_REGISTER_RCX : ZYDIS_REGISTER_ECX);
        const ZydisRegister spReg
            = (m_bitness == PEBitness::Bits32) ? ZYDIS_REGISTER_ESP : ZYDIS_REGISTER_RSP;
        const int32_t ptrSz = (m_bitness == PEBitness::Bits32) ? 4 : 8;

        bool isObj[kRegCount] = {};
        if (thisS >= 0)
            isObj[thisS] = true;
        int32_t espDelta = 0;
        std::map<int32_t, bool> slots; // stack slot holds an object pointer

        auto slotKeyOf = [&](const ZydisDecodedOperand& op) -> int32_t
        {
            if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.mem.index != ZYDIS_REGISTER_NONE)
                return 0x7FFFFFFF;
            const int32_t disp
                = op.mem.disp.has_displacement ? static_cast<int32_t>(op.mem.disp.value) : 0;
            if (op.mem.base == spReg)
                return espDelta + disp;
            if (m_bitness == PEBitness::Bits32 && op.mem.base == ZYDIS_REGISTER_EBP)
                return disp - 4;
            if (m_bitness == PEBitness::Bits64 && op.mem.base == ZYDIS_REGISTER_RBP)
                return disp - 8;
            return 0x7FFFFFFF;
        };

        // Entry-arg gate: registers that receive a known vftable write (see
        // the matching pre-scan in traceThisAccesses).
        bool vfWriterSlot[kRegCount] = {};
        {
            ZydisDecodedInstruction pInsn;
            ZydisDecodedOperand pOps[ZYDIS_MAX_OPERAND_COUNT];
            size_t p = startOff;
            while (p < endPos)
            {
                if (!decode(a_tv.data() + p, endPos - p, pInsn, pOps))
                {
                    p++;
                    continue;
                }
                if (pInsn.mnemonic == ZYDIS_MNEMONIC_RET)
                    break;
                if (pInsn.mnemonic == ZYDIS_MNEMONIC_MOV && pInsn.operand_count >= 2
                    && pOps[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && pOps[0].mem.index == ZYDIS_REGISTER_NONE
                    && pOps[0].size == m_layout.ptrSize * 8
                    && pOps[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && static_cast<uint64_t>(pOps[1].imm.value.u) >= m_imageBase)
                {
                    const uint64_t rva = static_cast<uint64_t>(pOps[1].imm.value.u) - m_imageBase;
                    for (uint64_t known : m_knownVftRvas)
                    {
                        if (known == rva)
                        {
                            const int bi = slot(pOps[0].mem.base);
                            if (bi >= 0)
                                vfWriterSlot[bi] = true;
                            break;
                        }
                    }
                }
                p += pInsn.length;
            }
        }
        while (pos < endPos)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            if (!decode(ptr, endPos - pos, insn, ops))
            {
                pos++;
                continue;
            }
            if (insn.mnemonic == ZYDIS_MNEMONIC_RET)
                break; // function end

            // ── Register provenance & stack bookkeeping ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH && insn.operand_count >= 1
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int s = slot(ops[0].reg.value);
                espDelta -= ptrSz;
                if (s >= 0 && isObj[s])
                    slots[espDelta] = true;
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_POP && insn.operand_count >= 1
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int di = slot(ops[0].reg.value);
                espDelta += ptrSz;
                if (di >= 0 && slots.find(espDelta) != slots.end())
                    isObj[di] = true;
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_SUB && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value == spReg
                     && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                espDelta -= static_cast<int32_t>(ops[1].imm.value.u);
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_ADD && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value == spReg
                     && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                espDelta += static_cast<int32_t>(ops[1].imm.value.u);
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                     && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER
                     && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                const int s = slot(ops[1].reg.value); // spill
                const int32_t key = slotKeyOf(ops[0]);
                if (s >= 0 && isObj[s] && key != 0x7FFFFFFF)
                    slots[key] = true;
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                     && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int di = slot(ops[0].reg.value);
                const int si = slot(ops[1].reg.value);
                if (di >= 0)
                    isObj[di] = (si >= 0) ? isObj[si] : false;
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int di = slot(ops[0].reg.value);
                if (di >= 0)
                {
                    const int32_t key = slotKeyOf(ops[1]);
                    if (m_bitness == PEBitness::Bits32 && key == 4 && vfWriterSlot[di])
                        isObj[di] = true; // x86 O2/SEH entry-arg object pointer
                    else if (key != 0x7FFFFFFF && slots.find(key) != slots.end())
                        isObj[di] = true; // reload of spilled object pointer
                    else
                        isObj[di] = false; // value load
                }
            }
            else if (insn.operand_count >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                // Value-producing opcodes clobber the destination.
                switch (insn.mnemonic)
                {
                case ZYDIS_MNEMONIC_MOVZX:
                case ZYDIS_MNEMONIC_MOVSX:
                case ZYDIS_MNEMONIC_MOVSXD:
                case ZYDIS_MNEMONIC_MOVD:
                case ZYDIS_MNEMONIC_MOVQ:
                case ZYDIS_MNEMONIC_IMUL:
                case ZYDIS_MNEMONIC_AND:
                case ZYDIS_MNEMONIC_OR:
                case ZYDIS_MNEMONIC_XOR:
                case ZYDIS_MNEMONIC_SHL:
                case ZYDIS_MNEMONIC_SHR:
                case ZYDIS_MNEMONIC_SAR:
                case ZYDIS_MNEMONIC_ROL:
                case ZYDIS_MNEMONIC_ROR:
                case ZYDIS_MNEMONIC_NEG:
                case ZYDIS_MNEMONIC_NOT:
                case ZYDIS_MNEMONIC_INC:
                case ZYDIS_MNEMONIC_DEC:
                {
                    const int di = slot(ops[0].reg.value);
                    if (di >= 0)
                        isObj[di] = false;
                    break;
                }
                case ZYDIS_MNEMONIC_ADD:
                case ZYDIS_MNEMONIC_SUB:
                    if (ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
                    {
                        const int di = slot(ops[0].reg.value);
                        if (di >= 0)
                            isObj[di] = false;
                    }
                    break;
                default:
                    break;
                }
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL && thisS >= 0)
            {
                for (int r = 0; r < kRegCount; ++r)
                    if (r != thisS && !isCalleeSaved(r))
                        isObj[r] = false;
            }

            // Count this-relative stores.
            if (isWriteMnemonic(insn.mnemonic) && insn.operand_count >= 1
                && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                const int bi = slot(ops[0].mem.base);
                if (bi >= 0 && isObj[bi])
                {
                    const int64_t disp
                        = ops[0].mem.disp.has_displacement ? ops[0].mem.disp.value : 0;
                    if (disp >= 0 && disp <= 65535)
                        count++;
                }
            }
            pos += insn.length;
        }
        return count;
    }

    /// True if the function writes a known *secondary* base vftable to
    /// this+K (K>0) — the signature of a most-derived constructor.
    [[nodiscard]] bool ctorWritesSecondaryVftable(
        uint64_t a_ctorRva, uint64_t a_textVa, const BinaryView& a_tv) const
    {
        if (m_baseVftOffsets.empty())
            return false;
        if (a_ctorRva < a_textVa)
            return false;
        const size_t startOff = static_cast<size_t>(a_ctorRva - a_textVa);
        if (startOff >= a_tv.size())
            return false;
        const size_t rawSpanS = std::min(kSearchRange, a_tv.size() - startOff);
        const size_t spanS = std::min<size_t>(0x1000, std::min(rawSpanS, kMaxFuncBytes));
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        uint64_t immInReg[kRegCount] = {};
        const ZydisRegister thisReg
            = (m_bitness == PEBitness::Bits32) ? ZYDIS_REGISTER_ECX : ZYDIS_REGISTER_RCX;
        size_t pos = startOff, endPos = startOff + spanS;
        while (pos < endPos)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            if (!decode(ptr, endPos - pos, insn, ops))
            {
                pos++;
                continue;
            }
            if (insn.mnemonic == ZYDIS_MNEMONIC_RET)
                break; // function end
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && slot(ops[0].reg.value) >= 0)
                immInReg[slot(ops[0].reg.value)] = static_cast<uint64_t>(ops[1].imm.value.u);
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].mem.base == thisReg)
            {
                const int64_t disp = ops[0].mem.disp.has_displacement ? ops[0].mem.disp.value : 0;
                if (disp > 0 && disp <= 65535 && slot(ops[1].reg.value) >= 0)
                {
                    const uint64_t val = immInReg[slot(ops[1].reg.value)];
                    const uint64_t vftRva = (val >= m_imageBase) ? val - m_imageBase : 0;
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
    [[nodiscard]] uint32_t peekAllocationSize(
        uint64_t a_ctorRva, uint64_t a_textVa, const BinaryView& a_tv) const
    {
        if (a_ctorRva < a_textVa)
            return 0;
        const size_t startOff = static_cast<size_t>(a_ctorRva - a_textVa);
        if (startOff >= a_tv.size())
            return 0;
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        size_t pos = startOff, endPos = std::min(startOff + 64, a_tv.size());
        while (pos < endPos)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            if (!decode(ptr, endPos - pos, insn, ops))
            {
                pos++;
                continue;
            }
            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL)
                break;
            for (uint8_t oi = 0; oi < insn.operand_count; ++oi)
            {
                if (ops[oi].type != ZYDIS_OPERAND_TYPE_IMMEDIATE)
                    continue;
                uint64_t v = static_cast<uint64_t>(ops[oi].imm.value.u);
                if (v & 0x80000000ULL)
                    v &= ~0x80000000ULL;
                // Reject suspicious sentinel values: 0, 0xFFFF, 0xFFFFFFFF, etc.
                if (v == 0 || v == 0xFFFF || v == 0xFFFFFFFF)
                    continue;
                // A size can never be an image address. Without this, an
                // immediate such as `mov eax, offset ??_7Foo@@6B@` is read as a
                // multi-megabyte allocation and then dominates the constructor
                // ranking score, selecting a function that is not a constructor.
                if (v >= m_imageBase)
                    continue;
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
                    static_cast<unsigned long long>(a_ctorRva),
                    fromCaller);
            return fromCaller;
        }

        const uint32_t fromHead = allocationSizeFromCtorHead(a_ctorRva);
        if (dbgOn())
            std::fprintf(stderr,
                "[FR] allocSize(0x%llX) = 0x%X (ctor head fallback)\n",
                static_cast<unsigned long long>(a_ctorRva),
                fromHead);
        return fromHead;
    }

    /// Locate every direct call to the constructor and read the size operand of
    /// the nearest preceding allocator call. Returns the largest plausible size
    /// found, or 0 when the constructor is never reached from an allocation site.
    [[nodiscard]] uint32_t allocationSizeFromCallers(uint64_t a_ctorRva) const
    {
        if (!m_text)
            return 0;

        const BinaryView& tv = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        const size_t sz = tv.size();
        if (sz < 5)
            return 0;

        const uint8_t* const data = tv.data();

        // Fast scan for near CALL opcodes (E8 rel32) instead of disassembling
        // the whole .text section. A false hit can only matter if its rel32
        // happens to resolve to this exact ctor RVA, which is vanishingly
        // unlikely and would still be filtered by the observed-size check.
        uint32_t best = 0;
        size_t pos = 0;
        while (pos + 5 <= sz)
        {
            const void* hit = std::memchr(data + pos, 0xE8, sz - pos - 4);
            if (!hit)
                break;
            pos = static_cast<size_t>(static_cast<const uint8_t*>(hit) - data);

            const uint32_t rel = static_cast<uint32_t>(data[pos + 1])
                                 | (static_cast<uint32_t>(data[pos + 2]) << 8)
                                 | (static_cast<uint32_t>(data[pos + 3]) << 16)
                                 | (static_cast<uint32_t>(data[pos + 4]) << 24);

            const uint64_t curRva = textVa + static_cast<uint64_t>(pos);
            uint64_t target
                = curRva + 5
                  + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rel)));

            // Incremental linking routes calls through `jmp rel32` thunks —
            // follow one hop so allocation sites calling the thunk still
            // match the constructor body.
            if (target != a_ctorRva && target >= textVa && target + 5 <= textVa + sz
                && data[target - textVa] == 0xE9)
            {
                const uint32_t rel2 = static_cast<uint32_t>(data[target - textVa + 1])
                                      | (static_cast<uint32_t>(data[target - textVa + 2]) << 8)
                                      | (static_cast<uint32_t>(data[target - textVa + 3]) << 16)
                                      | (static_cast<uint32_t>(data[target - textVa + 4]) << 24);
                target = target + 5
                         + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rel2)));
            }

            if (target == a_ctorRva)
            {
                const uint32_t size = sizeAtAllocationSite(curRva, textVa, tv);
                if (size > best)
                    best = size;
            }
            ++pos;
        }
        return best;
    }

    /// Decode a small window ending at `a_callRva` (a `call <ctor>` instruction
    /// belonging to an allocation site) and return the immediate handed to the
    /// closest preceding call — i.e. the operator-new size. 0 when not found.
    [[nodiscard]] uint32_t sizeAtAllocationSite(
        uint64_t a_callRva, uint64_t a_textVa, const BinaryView& a_tv) const
    {
        if (a_callRva < a_textVa)
            return 0;
        const size_t callOff = static_cast<size_t>(a_callRva - a_textVa);
        if (callOff > a_tv.size())
            return 0;

        constexpr size_t kWindow = 96;
        const size_t start = (callOff > kWindow) ? callOff - kWindow : 0;

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

        uint32_t pending = 0;
        bool havePending = false;
        uint32_t result = 0;

        size_t pos = start;
        while (pos < callOff)
        {
            const uint8_t* ptr = a_tv.data() + pos;
            const size_t rem = callOff - pos;
            if (!decode(ptr, rem, insn, ops))
            {
                ++pos;
                havePending = false;
                continue;
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL)
            {
                if (havePending)
                    result = pending; // nearest allocator before ctor
                havePending = false;
            }
            else
            {
                havePending = loadsSizeImmediate(insn, ops, pending);
            }
            pos += insn.length;
        }
        if (havePending)
            result = pending; // allocator call sits right before ctor
        return result;
    }

    /// True when the instruction loads a plausible allocation size into the
    /// first argument location:
    ///   x86: PUSH imm32
    ///   x64: MOV RCX/ECX, imm64
    [[nodiscard]] static bool loadsSizeImmediate(
        const ZydisDecodedInstruction& a_insn, const ZydisDecodedOperand* a_ops, uint32_t& a_out)
    {
        uint64_t imm = 0;
        bool found = false;

        if (a_insn.mnemonic == ZYDIS_MNEMONIC_PUSH && a_insn.operand_count >= 1
            && a_ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            imm = static_cast<uint64_t>(a_ops[0].imm.value.u);
            found = true;
        }
        else if (a_insn.mnemonic == ZYDIS_MNEMONIC_MOV && a_insn.operand_count >= 2
                 && a_ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                 && a_ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                 && (a_ops[0].reg.value == ZYDIS_REGISTER_RCX
                     || a_ops[0].reg.value == ZYDIS_REGISTER_ECX))
        {
            imm = static_cast<uint64_t>(a_ops[1].imm.value.u);
            found = true;
        }

        if (!found)
            return false;
        // Plausible object sizes only: >= 4 bytes, < 16 MB, 4-byte aligned.
        if (imm < 4 || imm >= 0x1000000 || (imm % 4) != 0)
            return false;
        a_out = static_cast<uint32_t>(imm);
        return true;
    }

    /// Legacy fallback: scan the constructor body for the first `push imm32`
    /// followed by any call. Only meaningful when the ctor allocates something
    /// itself; otherwise it picks up an unrelated helper argument (which the
    /// caller rejects because it is smaller than the observed field extent).
    [[nodiscard]] uint32_t allocationSizeFromCtorHead(uint64_t a_ctorRva) const
    {
        if (!m_text)
            return 0;

        const BinaryView& tv = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        if (a_ctorRva < textVa)
            return 0;

        const size_t startOff = static_cast<size_t>(a_ctorRva - textVa);
        const size_t maxBytes = std::min<size_t>(kMaxFuncBytes, tv.size() - startOff);

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

        uint32_t lastSizeArg = 0;
        size_t pos = startOff;

        while (pos < startOff + maxBytes)
        {
            const uint8_t* ptr = tv.data() + pos;
            const size_t rem = startOff + maxBytes - pos;

            if (!decode(ptr, rem, insn, ops))
            {
                pos++;
                continue;
            }

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

            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL && lastSizeArg > 0 && lastSizeArg <= 65536)
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

    void traceThisAccesses(uint64_t a_ctorRva,
        std::map<uint32_t, AccessRecord>& a_out,
        std::vector<StaticMemberInfo>* a_statics = nullptr) const
    {
        if (!m_text)
            return;

        const BinaryView& tv = m_text->view();
        const uint64_t textVa = m_text->virtualAddress();
        if (a_ctorRva < textVa)
            return;

        const size_t startOff = static_cast<size_t>(a_ctorRva - textVa);
        const size_t maxBytes = std::min<size_t>(kMaxFuncBytes, tv.size() - startOff);

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

        bool isThis[kRegCount] = {};

        const int thisReg = (m_bitness == PEBitness::Bits64) ? slot(ZYDIS_REGISTER_RCX)
                                                             : slot(ZYDIS_REGISTER_ECX);
        isThis[thisReg] = true;

        // Track what offset is in ecx/rcx just before a CALL
        // so we can annotate it as a ctor call hint.
        // lastThisOffset[reg] = offset that was added to `this` and stored in reg
        int32_t lastThisOffset[kRegCount];
        for (int i = 0; i < kRegCount; ++i)
            lastThisOffset[i] = -1;
        // Entry `this` points at the object start — record offset 0 so the
        // prologue spill (`mov [rsp+8], rcx`) stores 0 and later ADD/LEA
        // offsets build on it instead of on the -1 sentinel.
        lastThisOffset[thisReg] = 0;
        // Also track the last immediate loaded into a register (for foreign vftable detection)
        uint64_t lastImmInReg[kRegCount] = {};

        // ── Track `this` spilled to stack slots and reloaded through pointers ──
        // Release/debug code often stores `this` to a stack slot (x64 home
        // space `mov [rsp+0x08], rcx`, x86 spills `mov [ebp-4], ecx`) and then
        // reloads it into another register (`mov rax, [rsp+0x10]`) through
        // which ALL object writes happen. Keys are function-entry-relative
        // stack offsets so a store before a PUSH matches a load after it.
        int32_t espDelta = 0;
        std::map<int32_t, int32_t> thisSlots; // entry offset -> this offset
        const ZydisRegister spReg
            = (m_bitness == PEBitness::Bits32) ? ZYDIS_REGISTER_ESP : ZYDIS_REGISTER_RSP;
        const int32_t ptrSz = (m_bitness == PEBitness::Bits32) ? 4 : 8;

        auto slotKeyOf = [&](const ZydisDecodedOperand& op) -> int32_t
        {
            if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
                return 0x7FFFFFFF;
            if (op.mem.index != ZYDIS_REGISTER_NONE)
                return 0x7FFFFFFF;
            const int32_t disp
                = op.mem.disp.has_displacement ? static_cast<int32_t>(op.mem.disp.value) : 0;
            if (op.mem.base == spReg)
                return espDelta + disp;
            // Frame-pointer locals: ebp = entry-4 (x86), rbp = entry-8 (x64).
            if (m_bitness == PEBitness::Bits32 && op.mem.base == ZYDIS_REGISTER_EBP)
                return disp - 4;
            if (m_bitness == PEBitness::Bits64 && op.mem.base == ZYDIS_REGISTER_RBP)
                return disp - 8;
            return 0x7FFFFFFF;
        };

        // ── Pre-scan: which registers receive a known vftable pointer? ──
        // Optimised x86 builds (O2 + SEH) may pass the object pointer as the
        // FIRST STACK ARGUMENT ([entry_esp+4]) instead of ecx — e.g.
        //     mov esi, [esp+0x1C]     ; esp+0x1C == entry_esp+4
        //     mov dword ptr [esi], <vftVa>
        // Remember the register that ends up holding the vfptr write so the
        // entry-arg load can bootstrap `this` tracking (see the reload branch
        // below). Without this gate, loading the first *value* argument of a
        // classic ecx-thiscall ctor would be mistaken for `this`.
        bool vfWriterSlot[kRegCount] = {};
        {
            uint64_t pImm[kRegCount] = {};
            ZydisDecodedInstruction pInsn;
            ZydisDecodedOperand pOps[ZYDIS_MAX_OPERAND_COUNT];
            size_t p = startOff;
            const size_t pEnd = startOff + maxBytes;
            while (p < pEnd)
            {
                const uint8_t* pPtr = tv.data() + p;
                if (!decode(pPtr, pEnd - p, pInsn, pOps))
                {
                    p++;
                    continue;
                }
                if (pInsn.mnemonic == ZYDIS_MNEMONIC_RET)
                    break;
                if (pInsn.mnemonic == ZYDIS_MNEMONIC_MOV && pInsn.operand_count >= 2
                    && pOps[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                    && pOps[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    const int si = slot(pOps[0].reg.value);
                    if (si >= 0)
                        pImm[si] = static_cast<uint64_t>(pOps[1].imm.value.u);
                }
                if (pInsn.mnemonic == ZYDIS_MNEMONIC_MOV && pInsn.operand_count >= 2
                    && pOps[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                    && pOps[0].mem.index == ZYDIS_REGISTER_NONE
                    && pOps[0].size == m_layout.ptrSize * 8)
                {
                    uint64_t va = 0;
                    if (pOps[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                        va = static_cast<uint64_t>(pOps[1].imm.value.u);
                    else if (pOps[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
                    {
                        const int si = slot(pOps[1].reg.value);
                        if (si >= 0)
                            va = pImm[si];
                    }
                    if (va >= m_imageBase)
                    {
                        const uint64_t rva = va - m_imageBase;
                        for (uint64_t known : m_knownVftRvas)
                        {
                            if (known == rva)
                            {
                                const int bi = slot(pOps[0].mem.base);
                                if (bi >= 0)
                                    vfWriterSlot[bi] = true;
                                break;
                            }
                        }
                    }
                }
                p += pInsn.length;
            }
        }

        size_t pos = startOff;
        size_t endPos = startOff + maxBytes;

        while (pos < endPos)
        {
            const uint8_t* ptr = tv.data() + pos;
            const size_t rem = endPos - pos;

            if (!decode(ptr, rem, insn, ops))
            {
                pos++;
                continue;
            }

            const uint64_t curRva = textVa + static_cast<uint64_t>(pos);

            // ── Value-producing opcodes clobber their register destination ──
            // O2 code reuses registers for scalars constantly; stale
            // `this`-provenance would misattribute e.g. global-struct reads
            // (`mov ecx, [g_list]; mov eax, [ecx+8]`) as object fields.
            if (insn.operand_count >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                int clobDst = -1;
                switch (insn.mnemonic)
                {
                case ZYDIS_MNEMONIC_MOVZX:
                case ZYDIS_MNEMONIC_MOVSX:
                case ZYDIS_MNEMONIC_MOVSXD:
                case ZYDIS_MNEMONIC_MOVD:
                case ZYDIS_MNEMONIC_MOVQ:
                case ZYDIS_MNEMONIC_IMUL:
                case ZYDIS_MNEMONIC_AND:
                case ZYDIS_MNEMONIC_OR:
                case ZYDIS_MNEMONIC_XOR:
                case ZYDIS_MNEMONIC_SHL:
                case ZYDIS_MNEMONIC_SHR:
                case ZYDIS_MNEMONIC_SAR:
                case ZYDIS_MNEMONIC_ROL:
                case ZYDIS_MNEMONIC_ROR:
                case ZYDIS_MNEMONIC_RCL:
                case ZYDIS_MNEMONIC_RCR:
                case ZYDIS_MNEMONIC_NEG:
                case ZYDIS_MNEMONIC_NOT:
                case ZYDIS_MNEMONIC_BSWAP:
                case ZYDIS_MNEMONIC_INC:
                case ZYDIS_MNEMONIC_DEC:
                    clobDst = slot(ops[0].reg.value);
                    break;
                case ZYDIS_MNEMONIC_ADD:
                case ZYDIS_MNEMONIC_SUB:
                    // reg,reg only — the immediate forms keep the tracked
                    // this-pointer arithmetic (`add reg, imm`).
                    if (ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
                        clobDst = slot(ops[0].reg.value);
                    break;
                default:
                    break;
                }
                if (clobDst >= 0)
                {
                    isThis[clobDst] = false;
                    lastThisOffset[clobDst] = -1;
                }
            }

            // ── Stack-slot bookkeeping for spilled `this` ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH)
            {
                espDelta -= ptrSz;
                const int s = slot(ops[0].reg.value);
                if (insn.operand_count >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && s >= 0
                    && isThis[s])
                    thisSlots[espDelta] = lastThisOffset[s];
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_POP && insn.operand_count >= 1
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && slot(ops[0].reg.value) >= 0)
            {
                auto sIt = thisSlots.find(espDelta);
                if (sIt != thisSlots.end())
                {
                    isThis[slot(ops[0].reg.value)] = true;
                    lastThisOffset[slot(ops[0].reg.value)] = sIt->second;
                }
                espDelta += ptrSz;
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_SUB && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value == spReg
                     && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                espDelta -= static_cast<int32_t>(ops[1].imm.value.u);
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_ADD && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value == spReg
                     && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                espDelta += static_cast<int32_t>(ops[1].imm.value.u);
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                     && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && slot(ops[1].reg.value) >= 0
                     && isThis[slot(ops[1].reg.value)] && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                // `mov [slot], thisReg` — spill.
                const int32_t key = slotKeyOf(ops[0]);
                if (key != 0x7FFFFFFF && slot(ops[1].reg.value) >= 0)
                {
                    thisSlots[key] = lastThisOffset[slot(ops[1].reg.value)];
                    if (dbgOn())
                        std::fprintf(stderr,
                            "[FR]   spill key=%d at %llX\n",
                            key,
                            static_cast<unsigned long long>(curRva));
                }
            }
            else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                     && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value < kRegCount
                     && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                // `mov reg, [slot]` — reload through a pointer.
                const int32_t key = slotKeyOf(ops[1]);
                const int di = slot(ops[0].reg.value);
                auto sIt = thisSlots.find(key);
                if (sIt != thisSlots.end() && di >= 0)
                {
                    isThis[di] = true;
                    lastThisOffset[di] = sIt->second;
                    if (dbgOn())
                        std::fprintf(stderr,
                            "[FR]   reload key=%d reg=%u at %llX\n",
                            key,
                            static_cast<unsigned>(ops[0].reg.value),
                            static_cast<unsigned long long>(curRva));
                }
                else if (di >= 0)
                {
                    // ── x86 O2/SEH entry-arg this bootstrap ──
                    // The object may arrive as the first stack argument
                    // ([entry_esp+4], e.g. `mov esi, [esp+0x1C]`). Only do
                    // this when the register is later used for a known
                    // vftable write (pre-scan) and we are still in the
                    // function's entry region — otherwise this is just a
                    // value load.
                    if (m_bitness == PEBitness::Bits32 && key == 4 && vfWriterSlot[di]
                        && (curRva - a_ctorRva) < 0x100)
                    {
                        isThis[di] = true;
                        lastThisOffset[di] = 0;
                        if (dbgOn())
                            std::fprintf(stderr,
                                "[FR]   entry-arg this reg=%u at %llX\n",
                                static_cast<unsigned>(ops[0].reg.value),
                                static_cast<unsigned long long>(curRva));
                    }
                    else
                    {
                        isThis[di] = false; // plain value load
                        lastThisOffset[di] = -1;
                    }
                }
            }

            // ── Track MOV dst, src aliases ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int di = slot(ops[0].reg.value);
                const int si = slot(ops[1].reg.value);
                if (di >= 0 && si >= 0)
                {
                    isThis[di] = isThis[si];
                    lastThisOffset[di] = lastThisOffset[si];
                    lastImmInReg[di] = lastImmInReg[si];
                }
                else if (di >= 0)
                {
                    isThis[di] = false; // moved from a non-GPR — clobber
                    lastThisOffset[di] = -1;
                }
            }

            // ── Track MOV reg, imm — for foreign vftable detection ──
            // The destination can no longer hold `this`.
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                const int di = slot(ops[0].reg.value);
                if (di >= 0)
                {
                    lastImmInReg[di] = static_cast<uint64_t>(ops[1].imm.value.u);
                    isThis[di] = false;
                    lastThisOffset[di] = -1;
                }
            }

            // ── Track LEA reg, [this+disp] — ecx = this+offset pattern (x86 ctor arg) ──
            if (insn.mnemonic == ZYDIS_MNEMONIC_LEA && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                const int di = slot(ops[0].reg.value);
                const int bi = slot(ops[1].mem.base);
                if (di >= 0 && bi >= 0 && isThis[bi])
                {
                    const int64_t disp
                        = ops[1].mem.disp.has_displacement ? ops[1].mem.disp.value : 0;
                    if (disp >= 0 && disp <= 65535)
                    {
                        isThis[di] = false; // it's this+offset, not `this`
                        lastThisOffset[di] = static_cast<int32_t>(disp);
                    }
                }
            }

            // ── Track ADD reg, imm on a this-pointer ── /Od pattern for
            // embedded-member ctor calls:
            //     mov rax, [slot]      ; this
            //     add rax, 0x44        ; &m_child
            //     mov rcx, rax
            //     call Child::Child
            // (LEA is the other spelling; both must update the offset so the
            // ctor-call hint lands on the embedded member's offset.)
            if (insn.mnemonic == ZYDIS_MNEMONIC_ADD && insn.operand_count >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                const int di = slot(ops[0].reg.value);
                if (di >= 0 && isThis[di])
                {
                    lastThisOffset[di] += static_cast<int32_t>(ops[1].imm.value.u);
                    if (lastThisOffset[di] < 0 || lastThisOffset[di] > 65535)
                    {
                        isThis[di] = false; // left plausible object range
                        lastThisOffset[di] = -1;
                    }
                }
            }

            // ── x86: MOV ECX, reg where reg holds this+offset ──
            // Pattern: lea eax, [ecx+N] ; mov ecx, eax ; call Ctor
            if (m_bitness == PEBitness::Bits32 && insn.mnemonic == ZYDIS_MNEMONIC_MOV
                && insn.operand_count >= 2 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && slot(ops[0].reg.value) == slot(ZYDIS_REGISTER_ECX)
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const int si = slot(ops[1].reg.value);
                if (si >= 0 && lastThisOffset[si] >= 0)
                    lastThisOffset[slot(ZYDIS_REGISTER_ECX)] = lastThisOffset[si];
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
                        rec.size = m_layout.ptrSize; // assume at least pointer-sized
                        rec.kind = FieldKind::Pointer;
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

                // Reset per-call (volatile registers only — callee-saved
                // rbx/rbp/rsi/rdi/r12-15 keep the object pointer).
                for (int r = 0; r < kRegCount; ++r)
                    if (r != thisReg && !isCalleeSaved(r))
                    {
                        isThis[r] = false;
                        lastThisOffset[r] = -1;
                    }
                lastThisOffset[thisReg] = -1; // ecx gets clobbered by return value
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_RET)
                break;

            // ── Analyse memory operands for this-relative field accesses ──
            for (uint8_t oi = 0; oi < insn.operand_count; ++oi)
            {
                const ZydisDecodedOperand& op = ops[oi];
                if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
                    continue;

                const ZydisRegister base = op.mem.base;

                // ── Static-member writes ────────────────────────────────────
                // A write to an absolute address (x86) or a RIP-relative
                // address (x64) that lands in a WRITABLE section is exactly a
                // "static member assigned in the constructor":
                //     mov dword ptr [Class::s_counter], 1
                if (a_statics && oi == 0 && isWriteMnemonic(insn.mnemonic)
                    && op.mem.index == ZYDIS_REGISTER_NONE)
                {
                    uint64_t targetRva = 0;
                    if (base == ZYDIS_REGISTER_NONE && m_bitness == PEBitness::Bits32
                        && op.mem.disp.has_displacement
                        && op.mem.disp.value >= static_cast<int64_t>(m_imageBase))
                    {
                        targetRva = static_cast<uint64_t>(op.mem.disp.value) - m_imageBase;
                    }
                    else if ((base == ZYDIS_REGISTER_RIP || base == ZYDIS_REGISTER_EIP)
                             && m_bitness == PEBitness::Bits64)
                    {
                        const int64_t t
                            = static_cast<int64_t>(curRva + insn.length) + op.mem.disp.value;
                        if (t > 0)
                            targetRva = static_cast<uint64_t>(t);
                    }
                    if (targetRva != 0)
                    {
                        recordStatic(a_statics, insn, ops, oi, targetRva);
                        continue;
                    }
                }

                if (base == ZYDIS_REGISTER_NONE)
                    continue;

                // ── SIB accesses: `mov [this + idx*1 + disp]` (array-element
                // stores) — accept when EXACTLY ONE of base/index is `this`,
                // and record the ARRAY BASE (base disp). Dynamic pointers used
                // as base cannot be mis-attributed this way.
                if (op.mem.index != ZYDIS_REGISTER_NONE)
                {
                    const int bi = slot(op.mem.base);
                    const int ii = slot(op.mem.index);
                    const bool baseThis = bi >= 0 && isThis[bi];
                    const bool indexThis = ii >= 0 && isThis[ii];
                    if (baseThis == indexThis) // both or neither
                        continue;
                }
                else
                {
                    const int bi = slot(base);
                    if (bi < 0 || !isThis[bi])
                        continue;
                }

                const int64_t disp = op.mem.disp.has_displacement ? op.mem.disp.value : 0;
                if (disp < 0 || disp > 65535)
                    continue;

                const uint32_t fieldOffset = static_cast<uint32_t>(disp);
                const uint32_t accessSize = op.size / 8;
                if (accessSize == 0)
                    continue;

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
                        const int si = slot(ops[1].reg.value);
                        if (si >= 0 && lastImmInReg[si] != 0)
                        {
                            writtenVa = lastImmInReg[si];
                            isVftableWrite = true;
                        }
                    }

                    if (isVftableWrite && writtenVa != 0)
                    {
                        const uint64_t writtenRva
                            = (writtenVa >= m_imageBase) ? writtenVa - m_imageBase : 0;

                        // Check if primary vfptr write — skip
                        bool isVfptrWrite = (fieldOffset == 0 && writtenRva == m_vftableRva);
                        if (!isVfptrWrite)
                        {
                            for (uint32_t baseOff : m_baseVftOffsets)
                                if (fieldOffset == baseOff)
                                {
                                    isVfptrWrite = true;
                                    break;
                                }
                        }

                        if (!isVfptrWrite && writtenRva != 0)
                        {
                            // Check if it's a known foreign vftable
                            std::string foreignName = lookupVftableName(writtenRva);
                            if (!foreignName.empty())
                            {
                                auto& rec = a_out[fieldOffset];
                                rec.offset = fieldOffset;
                                rec.size = m_layout.ptrSize;
                                rec.kind = FieldKind::Pointer;
                                if (rec.foreignVftableName.empty())
                                {
                                    rec.foreignVftableName = foreignName;
                                    rec.foreignVftableRva = writtenRva;
                                }
                                pos += insn.length;
                                continue;
                            }
                        }

                        // Regular vfptr skip
                        if (isVfptrWrite)
                        {
                            pos += insn.length;
                            continue;
                        }
                    }
                }

                // ── Skip known vfptr offsets ──
                if (accessSize == m_layout.ptrSize && insn.mnemonic == ZYDIS_MNEMONIC_MOV
                    && oi == 0)
                {
                    bool isVfptrWrite = (fieldOffset == 0);
                    if (!isVfptrWrite)
                        for (uint32_t baseOff : m_baseVftOffsets)
                            if (fieldOffset == baseOff)
                            {
                                isVfptrWrite = true;
                                break;
                            }
                    if (isVfptrWrite)
                        continue;
                }

                FieldKind kind = inferKind(insn, oi, accessSize);

                auto it = a_out.find(fieldOffset);
                if (it == a_out.end())
                {
                    AccessRecord rec;
                    rec.offset = fieldOffset;
                    rec.size = accessSize;
                    rec.kind = kind;
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

    /// True for mnemonics that WRITE their memory operand (used to recognise
    /// static-member stores; reads like CMP/TEST must be excluded).
    [[nodiscard]] static bool isWriteMnemonic(ZydisMnemonic m) noexcept
    {
        switch (m)
        {
        case ZYDIS_MNEMONIC_MOV:
        case ZYDIS_MNEMONIC_MOVSS:
        case ZYDIS_MNEMONIC_MOVSD:
        case ZYDIS_MNEMONIC_MOVAPS:
        case ZYDIS_MNEMONIC_MOVAPD:
        case ZYDIS_MNEMONIC_MOVDQA:
        case ZYDIS_MNEMONIC_MOVDQU:
        case ZYDIS_MNEMONIC_MOVQ:
        case ZYDIS_MNEMONIC_MOVD:
        case ZYDIS_MNEMONIC_MOVBE:
        case ZYDIS_MNEMONIC_ADD:
        case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_AND:
        case ZYDIS_MNEMONIC_OR:
        case ZYDIS_MNEMONIC_XOR:
        case ZYDIS_MNEMONIC_INC:
        case ZYDIS_MNEMONIC_DEC:
            return true;
        default:
            return false;
        }
    }

    /// Record a static-member write (dedup by RVA, writable sections only,
    /// never a known vftable). Captures the immediate initialiser when the
    /// store is `mov [abs], <imm>`.
    void recordStatic(std::vector<StaticMemberInfo>* a_out,
        const ZydisDecodedInstruction& a_insn,
        const ZydisDecodedOperand* a_ops,
        uint8_t a_opIdx,
        uint64_t a_targetRva) const
    {
        if (!a_out || !m_pe || a_targetRva == 0)
            return;

        // Must land inside a WRITABLE section (.data/.bss) — never .text,
        // .rdata (vftables/const pools) or the image header.
        bool writable = false;
        for (const auto& s : m_pe->sections())
        {
            const uint32_t end
                = s.virtualAddress()
                  + std::max(s.virtualSize(), static_cast<uint32_t>(s.view().size()));
            if (a_targetRva >= s.virtualAddress() && a_targetRva < end)
            {
                writable = s.isWritable();
                break;
            }
        }
        if (!writable)
            return;

        // Never report a vftable (paranoia — they are read-only anyway).
        for (uint64_t v : m_knownVftRvas)
            if (v == a_targetRva)
                return;

        const uint32_t size = a_ops[a_opIdx].size / 8;
        if (size == 0 || size > 16)
            return;

        // Dedup: keep the first write; upgrade to an immediate-value record
        // if a later write carries the constant.
        StaticMemberInfo* existing = nullptr;
        for (auto& st : *a_out)
        {
            if (st.rva == a_targetRva)
            {
                existing = &st;
                break;
            }
        }

        StaticMemberInfo sm;
        sm.rva = a_targetRva;
        sm.size = size;
        sm.kind = inferKind(a_insn, a_opIdx, size);
        char buf[40];
        std::snprintf(
            buf, sizeof(buf), "static_0x%llX", static_cast<unsigned long long>(a_targetRva));
        sm.name = buf;

        if (a_insn.mnemonic == ZYDIS_MNEMONIC_MOV && a_insn.operand_count >= 2
            && a_ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            sm.hasInitValue = true;
            sm.initValue = (size <= 8) ? static_cast<int64_t>(a_ops[1].imm.value.s) : 0;
        }

        if (existing)
        {
            if (sm.hasInitValue && !existing->hasInitValue)
                *existing = sm;
            return;
        }
        a_out->push_back(sm);
    }

    [[nodiscard]] static FieldKind inferKind(
        const ZydisDecodedInstruction& insn, uint8_t /*opIdx*/, uint32_t accessSize)
    {
        switch (insn.mnemonic)
        {
        case ZYDIS_MNEMONIC_MOVSS:
        case ZYDIS_MNEMONIC_ADDSS:
        case ZYDIS_MNEMONIC_SUBSS:
        case ZYDIS_MNEMONIC_MULSS:
        case ZYDIS_MNEMONIC_DIVSS:
        case ZYDIS_MNEMONIC_COMISS:
        case ZYDIS_MNEMONIC_UCOMISS:
        case ZYDIS_MNEMONIC_CVTSS2SD:
        case ZYDIS_MNEMONIC_CVTSI2SS:
            return FieldKind::Float;

        case ZYDIS_MNEMONIC_MOVSD:
        case ZYDIS_MNEMONIC_ADDSD:
        case ZYDIS_MNEMONIC_SUBSD:
        case ZYDIS_MNEMONIC_MULSD:
        case ZYDIS_MNEMONIC_DIVSD:
        case ZYDIS_MNEMONIC_COMISD:
        case ZYDIS_MNEMONIC_UCOMISD:
        case ZYDIS_MNEMONIC_CVTSD2SS:
        case ZYDIS_MNEMONIC_CVTSI2SD:
            return FieldKind::Double;

        case ZYDIS_MNEMONIC_MOVAPS:
        case ZYDIS_MNEMONIC_MOVUPS:
        case ZYDIS_MNEMONIC_MOVDQA:
        case ZYDIS_MNEMONIC_MOVDQU:
        case ZYDIS_MNEMONIC_MOVAPD:
        case ZYDIS_MNEMONIC_MOVUPD:
            return FieldKind::M128;

        case ZYDIS_MNEMONIC_MOVSX:
            if (accessSize == 1)
                return FieldKind::Int8;
            if (accessSize == 2)
                return FieldKind::Int16;
            return FieldKind::Int32;

        case ZYDIS_MNEMONIC_MOVZX:
            if (accessSize == 1)
                return FieldKind::UInt8;
            return FieldKind::UInt16;

        case ZYDIS_MNEMONIC_MOVSXD:
            return FieldKind::Int32;

        case ZYDIS_MNEMONIC_MOV:
            if (accessSize == 8)
                return FieldKind::Pointer;
            if (accessSize == 4)
                return FieldKind::Int32;
            if (accessSize == 2)
                return FieldKind::Int16;
            if (accessSize == 1)
                return FieldKind::UInt8;
            break;

        case ZYDIS_MNEMONIC_CMP:
        case ZYDIS_MNEMONIC_TEST:
            if (accessSize == 1)
                return FieldKind::Bool;
            break;

        // FPU instructions (x86 x87 float/double)
        case ZYDIS_MNEMONIC_FLD:
        case ZYDIS_MNEMONIC_FST:
        case ZYDIS_MNEMONIC_FSTP:
        case ZYDIS_MNEMONIC_FADD:
        case ZYDIS_MNEMONIC_FMUL:
        case ZYDIS_MNEMONIC_FDIV:
        case ZYDIS_MNEMONIC_FSUB:
            if (accessSize == 4)
                return FieldKind::Float;
            if (accessSize == 8)
                return FieldKind::Double;
            break;

        default:
            break;
        }

        return FieldKind::Unknown;
    }

    [[nodiscard]] static FieldKind upgradedKind(FieldKind existing, FieldKind newer)
    {
        if (existing == FieldKind::Unknown)
            return newer;
        if (newer == FieldKind::Float || newer == FieldKind::Double || newer == FieldKind::M128)
            return newer;
        if (newer == FieldKind::Bool && existing == FieldKind::UInt8)
            return FieldKind::Bool;
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
            if (rec.offset == 0 && rec.size == m_layout.ptrSize && rec.kind == FieldKind::Pointer
                && rec.foreignVftableName.empty() && rec.ctorCallHint.empty())
                continue;

            // Skip MI base vfptr offsets (unless they have extra hints)
            bool isMIVfptr = false;
            for (uint32_t baseOff : m_baseVftOffsets)
            {
                if (rec.offset == baseOff && rec.size == m_layout.ptrSize
                    && rec.kind == FieldKind::Pointer && rec.foreignVftableName.empty()
                    && rec.ctorCallHint.empty())
                {
                    isMIVfptr = true;
                    break;
                }
            }
            if (isMIVfptr)
                continue;

            FieldInfo fi;
            fi.offset = rec.offset;
            fi.size = rec.size;
            fi.kind = rec.kind;
            fi.isBitfield = rec.isBitfield;
            fi.bitOffset = rec.bitOffset;
            fi.bitSize = rec.bitSize;
            fi.ctorCallHint = rec.ctorCallHint;
            fi.foreignVftableName = rec.foreignVftableName;
            fi.foreignVftableRva = rec.foreignVftableRva;
            fields.push_back(fi);
        }

        std::sort(fields.begin(),
            fields.end(),
            [](const FieldInfo& a, const FieldInfo& b) { return a.offset < b.offset; });

        return fields;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    /// True for any 32-bit general-purpose register that can be `this` in x86.
    [[nodiscard]] static bool isThisReg32(ZydisRegister r) noexcept
    {
        return r == ZYDIS_REGISTER_EAX || r == ZYDIS_REGISTER_EBX || r == ZYDIS_REGISTER_ECX
               || r == ZYDIS_REGISTER_EDX || r == ZYDIS_REGISTER_ESI || r == ZYDIS_REGISTER_EDI
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
    [[nodiscard]] std::string resolveCallTarget(const ZydisDecodedInstruction& insn,
        const ZydisDecodedOperand* ops,
        uint64_t a_curRva) const
    {
        if (insn.operand_count < 1)
            return {};

        // Direct CALL rel32
        if (ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            const uint64_t target
                = a_curRva + insn.length
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

    const PEImage* m_pe = nullptr;
    uint64_t m_imageBase = 0;
    uint64_t m_vftableRva = 0;
    PEBitness m_bitness = PEBitness::Bits64;
    RttiLayout m_layout = RttiLayout::x64();
    std::vector<uint32_t> m_baseVftOffsets;
    std::vector<uint64_t> m_knownVftRvas;
    std::vector<std::string> m_knownVftNames;
    const std::vector<uint64_t>* m_ctorChain = nullptr; ///< ICF-disambiguation hint.
    const PESection* m_text = nullptr;
    const PESection* m_rdata = nullptr;

    ZydisDecoder m_decoder = {};
};

} // namespace DumpPDB
