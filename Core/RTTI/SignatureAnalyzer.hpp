#pragma once

#include <Core/PE/PEImage.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <Zydis/Zydis.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// SignatureAnalyzer — recovers approximate signatures of virtual functions
///
/// Disassembles a function body (RVA from a vftable slot) and infers argument
/// count/types and the return type. Approximate by design: base types only
/// (int8/16/32/64, uint*, float, double, bool, void*).
///
/// x64 Microsoft ABI model (verified empirically on MSVC /Od code):
///   * Arguments occupy POSITIONS starting at 1. For member functions the
///     hidden `this` is position 1 (rcx). Position N maps to:
///         GP  channel: rcx, rdx, r8, r9   (N = 1..4)
///         XMM channel: xmm0..xmm3         (N = 1..4, only if the arg is float)
///     so a float argument at position 2 of a MEMBER function arrives in
///     xmm1 — the XMM index is always (position - 1).
///   * Positions >= 5 are stack arguments at [rsp + 0x28 + 8*(N-5)] at entry.
///   * "Home" slots at [rsp+8..0x20] mirror rcx/rdx/r8/r9.
///
/// x86 thiscall model: `this` in ecx (off the stack), declared arguments on
/// the stack at [esp+4+4k] at entry ([ebp+8+4k] after the standard prologue).
/// The trailing `ret imm16` gives the total stack-argument byte count.
/// ============================================================================
class SignatureAnalyzer
{
public:

    SignatureAnalyzer() = default;

    /// Analyze the function at a_funcRva. a_bitness selects the ABI.
    /// a_isVirtualMember is true for slots coming from a vftable.
    [[nodiscard]] VFuncSignature analyze(
        uint64_t a_funcRva, const PEImage& a_pe, PEBitness a_bitness, bool a_isVirtualMember = true)
    {
        VFuncSignature sig;
        if (a_funcRva == 0)
            return sig;

        m_pe = &a_pe;
        m_bitness = a_bitness;
        m_text = nullptr;
        for (const auto& s : a_pe.sections())
            if (s.name() == ".text")
            {
                m_text = &s;
                break;
            }
        if (!m_text)
            return sig;

        if (a_bitness == PEBitness::Bits64)
            ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        else
            ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);

        // Follow incremental-linking jmp thunks to the real body.
        const uint64_t bodyRva = resolveThunk(a_funcRva);
        if (bodyRva == 0)
            return sig;

        resetState(a_isVirtualMember);
        trace(bodyRva);
        finalize(sig);
        return sig;
    }

private:

    /// Per-argument accumulation of type evidence.
    struct ArgEvidence
    {
        bool seen = false;
        bool isFloat = false;    // ss ops / x87 float
        bool isDouble = false;   // sd ops / x87 double
        bool isPointer = false;  // used as a memory base / address
        bool isSigned = false;   // movsx
        bool isUnsigned = false; // movzx
        bool isBool = false;     // setcc / cmp/test on a 1-byte value
        uint32_t maxBytes = 0;   // widest observed MEMORY access
        bool isThis = false;     // the hidden first argument (ecx/rcx)
    };

    enum class RetKind : uint8_t
    {
        None,
        Void,
        Int,
        Bool,
        Float,
        Double,
        Pointer,
    };

    /// Compact per-instruction record kept for the backward epilogue scan.
    struct InsRec
    {
        ZydisMnemonic mnemonic = ZYDIS_MNEMONIC_INVALID;
        uint8_t opCount = 0;
        uint8_t op0Type = 0;  // ZydisOperandType
        uint16_t op0Size = 0; // bits
        ZydisRegister op0Reg = ZYDIS_REGISTER_NONE;
        uint8_t op1Type = 0;
        uint16_t op1Size = 0;
        ZydisRegister op1Reg = ZYDIS_REGISTER_NONE;
        ZydisRegister op1MemBase = ZYDIS_REGISTER_NONE; ///< op1 memory base reg
        int32_t op1Slot = -1;                           ///< op1 arg-slot (x64 homes)
        bool prevSetcc = false;                         ///< previous instruction was SETcc
    };

    /// Symbolic provenance of a general-purpose register.
    enum class Prov : uint8_t
    {
        None,
        ThisPtr,
        Ptr,
        Int
    };

    static size_t regIdx(ZydisRegister r)
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
            return 16;
        }
    }

    // ─── state ───────────────────────────────────────────────────────────────

    void resetState(bool a_isVirtualMember)
    {
        m_argEvidence.clear();
        m_localSlot.clear();
        m_insns.clear();
        m_retKind = RetKind::None;
        m_retBytes = 0;
        m_hasRetImm = false;
        m_retImm = 0;
        m_lastX87Width = 0;
        m_isVirtualMember = a_isVirtualMember;
        m_ebpFrame = false;
        m_ebpSlotBase = 0;
        m_espDelta = 0;
        m_prevSetcc = false;
        m_int3Run = 0;
        m_xmmDead[0] = m_xmmDead[1] = m_xmmDead[2] = m_xmmDead[3] = false;
        for (auto& r : m_regProv)
            r = Prov::None;
        for (auto& r : m_regArgSlot)
            r = -1;

        if (m_bitness == PEBitness::Bits64)
        {
            if (a_isVirtualMember)
                markThis(0);
            // Argument registers initially hold their slots (position - 1).
            m_regProv[regIdx(ZYDIS_REGISTER_RCX)] = Prov::ThisPtr;
            m_regArgSlot[regIdx(ZYDIS_REGISTER_RCX)] = 0;
            m_regArgSlot[regIdx(ZYDIS_REGISTER_RDX)] = 1;
            m_regArgSlot[regIdx(ZYDIS_REGISTER_R8)] = 2;
            m_regArgSlot[regIdx(ZYDIS_REGISTER_R9)] = 3;
        }
        else
        {
            // x86 thiscall: ecx = this (NOT an argument stack slot, so the
            // slot marker stays -1 — dereferencing ecx must not mark arg0).
            m_regProv[regIdx(ZYDIS_REGISTER_ECX)] = Prov::ThisPtr;
            m_regArgSlot[regIdx(ZYDIS_REGISTER_ECX)] = -1;
        }
    }

    void markThis(size_t a_index)
    {
        ArgEvidence e;
        e.seen = true;
        e.isThis = true;
        m_argEvidence[a_index] = e;
    }

    ArgEvidence& ev(size_t a_index)
    {
        auto it = m_argEvidence.find(a_index);
        if (it == m_argEvidence.end())
            it = m_argEvidence.emplace(a_index, ArgEvidence{}).first;
        return it->second;
    }

    // ─── decoding helpers ────────────────────────────────────────────────────

    [[nodiscard]] const uint8_t* codePtr(uint64_t a_rva) const
    {
        if (!m_text)
            return nullptr;
        if (a_rva < m_text->virtualAddress())
            return nullptr;
        const uint64_t off = a_rva - m_text->virtualAddress();
        if (off >= m_text->view().size())
            return nullptr;
        return m_text->view().data() + off;
    }

    /// Follow up to 8 `jmp rel32` thunks (incremental linking emits one per
    /// externally-visible symbol). Returns the final RVA, or 0 on failure.
    [[nodiscard]] uint64_t resolveThunk(uint64_t a_rva) const
    {
        uint64_t rva = a_rva;
        for (int hop = 0; hop < 8; ++hop)
        {
            const uint8_t* p = codePtr(rva);
            if (!p)
                return 0;

            ZydisDecodedInstruction ins;
            ZydisDecodedOperand ops[10];
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&m_decoder, p, 16, &ins, ops)))
                return rva; // give up following, use what we have

            if (ins.mnemonic == ZYDIS_MNEMONIC_JMP && ins.operand_count_visible >= 1
                && ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                rva = rva + ins.length
                      + static_cast<uint64_t>(static_cast<int64_t>(ops[0].imm.value.s));
                continue;
            }
            return rva; // not a thunk
        }
        return rva;
    }

    // ─── linear trace ────────────────────────────────────────────────────────

    bool trace(uint64_t a_startRva)
    {
        uint64_t rva = a_startRva;
        const int kMaxInsns = 1024;

        for (int i = 0; i < kMaxInsns; ++i)
        {
            const uint8_t* p = codePtr(rva);
            if (!p)
                break;

            ZydisDecodedInstruction ins;
            ZydisDecodedOperand ops[10];
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&m_decoder, p, 16, &ins, ops)))
                break;

            const uint64_t insnRva = rva;
            rva += ins.length;

            trackEsp(ins, ops);
            trackFrame(ins, ops);
            classify(ins, ops, insnRva);

            // Record for the backward epilogue scan.
            InsRec rec;
            rec.mnemonic = ins.mnemonic;
            rec.opCount = ins.operand_count_visible;
            rec.prevSetcc = m_prevSetcc;
            if (rec.opCount >= 1)
            {
                rec.op0Type = static_cast<uint8_t>(ops[0].type);
                rec.op0Size = ops[0].size;
                rec.op0Reg = ops[0].reg.value;
            }
            if (rec.opCount >= 2)
            {
                rec.op1Type = static_cast<uint8_t>(ops[1].type);
                rec.op1Size = ops[1].size;
                rec.op1Reg = ops[1].reg.value;
                if (ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    rec.op1MemBase = ops[1].mem.base;
                    rec.op1Slot = argSlotOf(ops[1]);
                }
            }
            m_insns.push_back(rec);
            m_prevSetcc = isSetcc(ins.mnemonic);

            if (ins.mnemonic == ZYDIS_MNEMONIC_RET)
            {
                if (ins.operand_count_visible >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    m_hasRetImm = true;
                    m_retImm = static_cast<uint32_t>(ops[0].imm.value.u & 0xFFFF);
                }
                analyzeReturnBackward();
                return true; // stop at the first epilogue
            }
            // INT3 appears BOTH as end-of-function padding (runs of 0xCC) and
            // as an isolated debug-assert breakpoint INSIDE the body — only a
            // run of two or more marks real padding.
            if (ins.mnemonic == ZYDIS_MNEMONIC_INT3)
            {
                if (++m_int3Run >= 2)
                    break;
            }
            else if (ins.mnemonic == ZYDIS_MNEMONIC_UD2)
            {
                break;
            }
            else
            {
                m_int3Run = 0;
            }
        }
        analyzeReturnBackward();
        return true;
    }

    static bool isSetcc(ZydisMnemonic m)
    {
        switch (m)
        {
        case ZYDIS_MNEMONIC_SETB:
        case ZYDIS_MNEMONIC_SETBE:
        case ZYDIS_MNEMONIC_SETL:
        case ZYDIS_MNEMONIC_SETLE:
        case ZYDIS_MNEMONIC_SETNB:
        case ZYDIS_MNEMONIC_SETNBE:
        case ZYDIS_MNEMONIC_SETNL:
        case ZYDIS_MNEMONIC_SETNLE:
        case ZYDIS_MNEMONIC_SETNO:
        case ZYDIS_MNEMONIC_SETNP:
        case ZYDIS_MNEMONIC_SETNS:
        case ZYDIS_MNEMONIC_SETNZ:
        case ZYDIS_MNEMONIC_SETO:
        case ZYDIS_MNEMONIC_SETP:
        case ZYDIS_MNEMONIC_SETS:
        case ZYDIS_MNEMONIC_SETZ:
            return true;
        default:
            return false;
        }
    }

    static bool isEaxLike(ZydisRegister r)
    {
        return r == ZYDIS_REGISTER_EAX || r == ZYDIS_REGISTER_RAX || r == ZYDIS_REGISTER_AX
               || r == ZYDIS_REGISTER_AL || r == ZYDIS_REGISTER_AH;
    }

    // ─── stack / frame tracking ──────────────────────────────────────────────

    void trackEsp(const ZydisDecodedInstruction& ins, const ZydisDecodedOperand* ops)
    {
        const ZydisRegister sp
            = (m_bitness == PEBitness::Bits32) ? ZYDIS_REGISTER_ESP : ZYDIS_REGISTER_RSP;
        const int32_t ptrSz = (m_bitness == PEBitness::Bits32) ? 4 : 8;

        switch (ins.mnemonic)
        {
        case ZYDIS_MNEMONIC_PUSH:
            m_espDelta -= ptrSz;
            break;
        case ZYDIS_MNEMONIC_POP:
            m_espDelta += ptrSz;
            break;
        case ZYDIS_MNEMONIC_SUB:
            if (ins.operand_count_visible >= 2 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[0].reg.value == sp && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                m_espDelta -= static_cast<int32_t>(ops[1].imm.value.u);
            break;
        case ZYDIS_MNEMONIC_ADD:
            if (ins.operand_count_visible >= 2 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[0].reg.value == sp && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                m_espDelta += static_cast<int32_t>(ops[1].imm.value.u);
            break;
        // `and rsp, -16` stack realignment: per the Microsoft ABI rsp == 8
        // (mod 16) at entry, so alignment subtracts 8 in that case.
        case ZYDIS_MNEMONIC_AND:
            if (m_bitness == PEBitness::Bits64 && ins.operand_count_visible >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                && ops[0].reg.value == ZYDIS_REGISTER_RSP
                && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                const int32_t mod = (m_espDelta % 16 + 16) % 16;
                const int32_t cur = (8 + mod) % 16; // entry rsp == 8 (mod 16)
                if (cur == 8)
                    m_espDelta -= 8;
            }
            break;
        default:
            break;
        }
    }

    // Detect the classic `push ebp; mov ebp, esp` prologue (x86) so argument
    // accesses can be recognised via [ebp+8+4k].
    void trackFrame(const ZydisDecodedInstruction& ins, const ZydisDecodedOperand* ops)
    {
        if (m_bitness != PEBitness::Bits32 || m_ebpFrame)
            return;

        if (ins.mnemonic == ZYDIS_MNEMONIC_MOV && ins.operand_count_visible >= 2
            && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value == ZYDIS_REGISTER_EBP
            && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[1].reg.value == ZYDIS_REGISTER_ESP)
        {
            m_ebpFrame = true;
            m_ebpSlotBase = 8; // [ebp+8] = first stack argument
        }
    }

    // Map a memory operand to a 0-based argument slot index, or -1.
    int32_t argSlotOf(const ZydisDecodedOperand& op) const
    {
        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
            return -1;
        if (op.mem.index != ZYDIS_REGISTER_NONE)
            return -1;
        const int32_t disp = op.mem.disp.has_displacement ? op.mem.disp.value : 0;

        if (m_bitness == PEBitness::Bits32)
        {
            if (m_ebpFrame && op.mem.base == ZYDIS_REGISTER_EBP)
            {
                if (disp >= m_ebpSlotBase)
                    return disp - m_ebpSlotBase;
                return -1;
            }
            if (op.mem.base == ZYDIS_REGISTER_ESP)
            {
                const int32_t absOff = m_espDelta + disp;
                if (absOff >= 4)
                    return absOff - 4;
            }
            return -1;
        }

        // x64: entry-relative addressing. Home slots [8..0x20] mirror
        // rcx/rdx/r8/r9 (slots 0..3); stack args start at entry 0x28 = slot 4.
        if (op.mem.base == ZYDIS_REGISTER_RSP)
        {
            const int32_t entry = m_espDelta + disp;
            if (entry >= 0x08 && entry <= 0x20)
                return static_cast<int32_t>((entry - 0x08) / 8);
            if (entry >= 0x28 && ((entry - 0x28) % 8) == 0)
                return 4 + static_cast<int32_t>((entry - 0x28) / 8);
        }
        else if (op.mem.base == ZYDIS_REGISTER_RBP)
        {
            // After `push rbp; mov rbp, rsp`, rbp = entryRsp - 8, so
            // [rbp+disp] = entry (disp - 8).
            const int32_t entry = disp - 8;
            if (entry >= 0x08 && entry <= 0x20)
                return static_cast<int32_t>((entry - 0x08) / 8);
            if (entry >= 0x28 && ((entry - 0x28) % 8) == 0)
                return 4 + static_cast<int32_t>((entry - 0x28) / 8);
        }
        return -1;
    }

    /// Stable key for a *local* stack variable (not an argument slot):
    /// ebp/rgp-relative -> displacement, esp/rsp-relative -> entry offset.
    /// Returns 0x7FFFFFFF when the operand is not a plain local.
    int32_t localKeyOf(const ZydisDecodedOperand& op) const
    {
        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
            return 0x7FFFFFFF;
        if (op.mem.index != ZYDIS_REGISTER_NONE)
            return 0x7FFFFFFF;
        const int32_t disp = op.mem.disp.has_displacement ? op.mem.disp.value : 0;
        if (op.mem.base == ZYDIS_REGISTER_EBP || op.mem.base == ZYDIS_REGISTER_RBP)
            return disp; // negative for locals, positive would be an arg (handled earlier)
        if (op.mem.base == ZYDIS_REGISTER_ESP || op.mem.base == ZYDIS_REGISTER_RSP)
            return m_espDelta + disp;
        return 0x7FFFFFFF;
    }

    /// Map a GP register to its argument slot (position - 1), or -1.
    /// On x86 thiscall ecx holds `this`, which is NOT a stack slot — so ecx
    /// maps to -1 there (deref of ecx must never mark argument 0).
    int32_t gpRegArgSlot(ZydisRegister r) const
    {
        switch (r)
        {
        case ZYDIS_REGISTER_RCX:
            return (m_bitness == PEBitness::Bits64) ? 0 : -1;
        case ZYDIS_REGISTER_ECX:
            return -1;
        case ZYDIS_REGISTER_RDX:
        case ZYDIS_REGISTER_EDX:
            return 1;
        case ZYDIS_REGISTER_R8:
            return 2;
        case ZYDIS_REGISTER_R9:
            return 3;
        default:
            return -1;
        }
    }

    /// Map an XMM register to its argument slot (== xmm index), or -1.
    /// Position N floats arrive in xmm(N-1) and the slot index is also N-1.
    static int32_t xmmRegArgSlot(ZydisRegister r)
    {
        switch (r)
        {
        case ZYDIS_REGISTER_XMM0:
            return 0;
        case ZYDIS_REGISTER_XMM1:
            return 1;
        case ZYDIS_REGISTER_XMM2:
            return 2;
        case ZYDIS_REGISTER_XMM3:
            return 3;
        default:
            return -1;
        }
    }

    // ─── classification ──────────────────────────────────────────────────────

    void classify(
        const ZydisDecodedInstruction& ins, const ZydisDecodedOperand* ops, uint64_t a_insnRva)
    {
        classifyArgs(ins, ops, a_insnRva);
        trackX87(ins, ops);
        updateProvenance(ins, ops);
    }

    void trackX87(const ZydisDecodedInstruction& ins, const ZydisDecodedOperand* ops)
    {
        switch (ins.mnemonic)
        {
        case ZYDIS_MNEMONIC_FLD:
        case ZYDIS_MNEMONIC_FST:
        case ZYDIS_MNEMONIC_FSTP:
        case ZYDIS_MNEMONIC_FADD:
        case ZYDIS_MNEMONIC_FSUB:
        case ZYDIS_MNEMONIC_FMUL:
        case ZYDIS_MNEMONIC_FDIV:
        case ZYDIS_MNEMONIC_FILD:
        case ZYDIS_MNEMONIC_FIST:
        case ZYDIS_MNEMONIC_FISTP:
            if (ins.operand_count_visible >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY)
                m_lastX87Width = ops[0].size;
            break;
        default:
            break;
        }
    }

    void classifyArgs(
        const ZydisDecodedInstruction& ins, const ZydisDecodedOperand* ops, uint64_t a_insnRva)
    {
        static const bool dbg = std::getenv("DUMPPDB_DEBUG") != nullptr;
        for (uint8_t oi = 0; oi < ins.operand_count_visible; ++oi)
        {
            const ZydisDecodedOperand& op = ops[oi];
            const bool isDest = (oi == 0);
            const bool isMovFamily = isMovLike(ins.mnemonic);

            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                const int32_t slot = argSlotOf(op);
                if (slot < 0)
                {
                    // A local written earlier from an argument register:
                    // `mov eax, [ebp+8]; mov [ebp-0x10], eax; fld [ebp-0x10]`
                    // — the local's accesses refine THAT argument's type.
                    const int32_t key = localKeyOf(op);
                    auto lIt = (key != 0x7FFFFFFF) ? m_localSlot.find(key) : m_localSlot.end();
                    if (lIt != m_localSlot.end() && lIt->second >= 0)
                    {
                        ArgEvidence& e = ev(static_cast<size_t>(lIt->second));
                        e.seen = true;
                        applyAccess(e, ins, op);
                        if (dbg)
                            std::fprintf(stderr,
                                "[SIG] %llX local->slot=%d mnem=%u w=%u\n",
                                static_cast<unsigned long long>(a_insnRva),
                                lIt->second,
                                static_cast<unsigned>(ins.mnemonic),
                                op.size / 8);
                        continue;
                    }

                    // A register still holding an argument value used as a
                    // memory base (`mov eax, [edx+8]`) marks that argument
                    // as a pointer.
                    if (!isDest && op.mem.base != ZYDIS_REGISTER_NONE)
                    {
                        const size_t bi = regIdx(op.mem.base);
                        if (bi < 16 && m_regArgSlot[bi] >= 0)
                            ev(static_cast<size_t>(m_regArgSlot[bi])).isPointer = true;
                    }
                    continue;
                }

                ArgEvidence& e = ev(static_cast<size_t>(slot));
                e.seen = true;
                applyAccess(e, ins, op);
                if (dbg)
                    std::fprintf(stderr,
                        "[SIG] %llX mem slot=%d mnem=%u dest=%d w=%u\n",
                        static_cast<unsigned long long>(a_insnRva),
                        slot,
                        static_cast<unsigned>(ins.mnemonic),
                        isDest ? 1 : 0,
                        op.size / 8);
                continue;
            }

            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const size_t ri = regIdx(op.reg.value);

                // XMM argument registers: track clobbers separately.
                const int32_t xmm = xmmRegArgSlot(op.reg.value);
                if (xmm >= 0)
                {
                    if (isDest && !isNonDestructive(ins.mnemonic))
                    {
                        m_xmmDead[xmm] = true;
                        continue;
                    }
                    if (m_xmmDead[xmm])
                        continue;
                    ArgEvidence& e = ev(static_cast<size_t>(xmm));
                    e.seen = true;
                    applyFloatMnemonic(e, ins);
                    if (dbg)
                        std::fprintf(stderr,
                            "[SIG] %llX xmm slot=%d mnem=%u dest=%d\n",
                            static_cast<unsigned long long>(a_insnRva),
                            xmm,
                            static_cast<unsigned>(ins.mnemonic),
                            isDest ? 1 : 0);
                    continue;
                }

                const int32_t gp = gpRegArgSlot(op.reg.value);
                if (gp < 0)
                    continue;

                if (isDest && !isNonDestructive(ins.mnemonic))
                {
                    // A write to an argument register clobbers it — not
                    // evidence of the argument itself (`add edx, 0x38`,
                    // `movzx edx, cl`, `mov argReg, ...`).
                    if (ri < 16)
                    {
                        m_regProv[ri] = Prov::None;
                        m_regArgSlot[ri] = -1;
                    }
                    continue;
                }

                // Evidence only if the register STILL carries its argument
                // (x86 scratch registers are frequently reused for ecx/edx
                // values that have nothing to do with the argument list).
                if (ri >= 16 || m_regArgSlot[ri] != gp)
                    continue;

                ArgEvidence& e = ev(static_cast<size_t>(gp));
                e.seen = true;
                applyGpMnemonic(e, ins);
                if (dbg)
                    std::fprintf(stderr,
                        "[SIG] %llX reg slot=%d mnem=%u dest=%d held=%d\n",
                        static_cast<unsigned long long>(a_insnRva),
                        gp,
                        static_cast<unsigned>(ins.mnemonic),
                        isDest ? 1 : 0,
                        m_regArgSlot[ri]);
            }
        }

        // `test eax, eax` / `cmp eax, 0` where eax *holds an argument value*
        // (copied from an argument slot earlier) refines that argument —
        // e.g. bool detection after `movzx eax, byte ptr [slot]`.
        if (ins.mnemonic == ZYDIS_MNEMONIC_TEST || ins.mnemonic == ZYDIS_MNEMONIC_CMP)
        {
            for (uint8_t oi = 0; oi < ins.operand_count_visible; ++oi)
            {
                const ZydisDecodedOperand& op = ops[oi];
                if (op.type != ZYDIS_OPERAND_TYPE_REGISTER)
                    continue;
                const size_t ri = regIdx(op.reg.value);
                if (ri >= 16 || m_regArgSlot[ri] < 0)
                    continue;
                applyGpMnemonic(ev(static_cast<size_t>(m_regArgSlot[ri])), ins);
            }
        }
    }

    static bool isMovLike(ZydisMnemonic m)
    {
        switch (m)
        {
        case ZYDIS_MNEMONIC_MOV:
        case ZYDIS_MNEMONIC_MOVSS:
        case ZYDIS_MNEMONIC_MOVSD:
        case ZYDIS_MNEMONIC_MOVAPS:
        case ZYDIS_MNEMONIC_MOVAPD:
        case ZYDIS_MNEMONIC_MOVD:
        case ZYDIS_MNEMONIC_MOVQ:
        case ZYDIS_MNEMONIC_MOVDQU:
        case ZYDIS_MNEMONIC_MOVDQA:
            return true;
        default:
            return false;
        }
    }

    /// Instructions whose "destination" operand is only compared, not written
    /// — using an argument register there IS evidence of the argument.
    static bool isNonDestructive(ZydisMnemonic m)
    {
        switch (m)
        {
        case ZYDIS_MNEMONIC_CMP:
        case ZYDIS_MNEMONIC_TEST:
        case ZYDIS_MNEMONIC_COMISS:
        case ZYDIS_MNEMONIC_COMISD:
        case ZYDIS_MNEMONIC_UCOMISS:
        case ZYDIS_MNEMONIC_UCOMISD:
            return true;
        default:
            return false;
        }
    }

    static void applyFloatMnemonic(ArgEvidence& e, const ZydisDecodedInstruction& ins)
    {
        switch (ins.mnemonic)
        {
        case ZYDIS_MNEMONIC_MOVSS:
        case ZYDIS_MNEMONIC_ADDSS:
        case ZYDIS_MNEMONIC_SUBSS:
        case ZYDIS_MNEMONIC_MULSS:
        case ZYDIS_MNEMONIC_DIVSS:
        case ZYDIS_MNEMONIC_COMISS:
        case ZYDIS_MNEMONIC_UCOMISS:
        case ZYDIS_MNEMONIC_CVTSS2SD:
            e.isFloat = true;
            break;
        case ZYDIS_MNEMONIC_MOVSD:
        case ZYDIS_MNEMONIC_ADDSD:
        case ZYDIS_MNEMONIC_SUBSD:
        case ZYDIS_MNEMONIC_MULSD:
        case ZYDIS_MNEMONIC_DIVSD:
        case ZYDIS_MNEMONIC_COMISD:
        case ZYDIS_MNEMONIC_UCOMISD:
        case ZYDIS_MNEMONIC_CVTSD2SS:
            e.isDouble = true;
            break;
        default:
            break;
        }
    }

    static void applyGpMnemonic(ArgEvidence& e, const ZydisDecodedInstruction& ins)
    {
        switch (ins.mnemonic)
        {
        case ZYDIS_MNEMONIC_MOVSX:
        case ZYDIS_MNEMONIC_MOVSXD:
            e.isSigned = true;
            break;
        case ZYDIS_MNEMONIC_CMP:
        case ZYDIS_MNEMONIC_TEST:
            if (e.maxBytes == 1)
                e.isBool = true;
            break;
        default:
            break;
        }
    }

    /// Evidence from a MEMORY operand: width + mnemonic typing. Only memory
    /// accesses update maxBytes (register width after ABI promotion is
    /// meaningless for the underlying argument type).
    void applyAccess(
        ArgEvidence& e, const ZydisDecodedInstruction& ins, const ZydisDecodedOperand& op)
    {
        const uint32_t width = op.size / 8;
        if (width > e.maxBytes)
            e.maxBytes = width;

        switch (ins.mnemonic)
        {
        case ZYDIS_MNEMONIC_MOVSS:
        case ZYDIS_MNEMONIC_ADDSS:
        case ZYDIS_MNEMONIC_SUBSS:
        case ZYDIS_MNEMONIC_MULSS:
        case ZYDIS_MNEMONIC_DIVSS:
        case ZYDIS_MNEMONIC_COMISS:
        case ZYDIS_MNEMONIC_UCOMISS:
        case ZYDIS_MNEMONIC_CVTSS2SD:
            e.isFloat = true;
            break;
        case ZYDIS_MNEMONIC_MOVSD:
        case ZYDIS_MNEMONIC_ADDSD:
        case ZYDIS_MNEMONIC_SUBSD:
        case ZYDIS_MNEMONIC_MULSD:
        case ZYDIS_MNEMONIC_DIVSD:
        case ZYDIS_MNEMONIC_COMISD:
        case ZYDIS_MNEMONIC_UCOMISD:
        case ZYDIS_MNEMONIC_CVTSD2SS:
            e.isDouble = true;
            break;
        case ZYDIS_MNEMONIC_FLD:
        case ZYDIS_MNEMONIC_FADD:
        case ZYDIS_MNEMONIC_FMUL:
        case ZYDIS_MNEMONIC_FDIV:
        case ZYDIS_MNEMONIC_FSUB:
            if (width == 8)
                e.isDouble = true;
            else
                e.isFloat = true;
            break;

        case ZYDIS_MNEMONIC_MOVSX:
        case ZYDIS_MNEMONIC_MOVSXD:
            e.isSigned = true;
            break;
        case ZYDIS_MNEMONIC_MOVZX:
            e.isUnsigned = true;
            break;

        case ZYDIS_MNEMONIC_CMP:
        case ZYDIS_MNEMONIC_TEST:
            if (width == 1)
                e.isBool = true;
            if (width == 8)
                e.isPointer = true; // qword compare: pointer value
            break;

        // An 8-byte load/spill of a GP argument slot is a pointer value
        // (long long arguments are far rarer than pointers in practice).
        case ZYDIS_MNEMONIC_MOV:
            if (width == 8)
                e.isPointer = true;
            break;

        default:
            break;
        }
    }

    // ─── provenance tracking (forward) ───────────────────────────────────────

    void updateProvenance(const ZydisDecodedInstruction& ins, const ZydisDecodedOperand* ops)
    {
        const bool hasDst
            = ins.operand_count_visible >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER;
        const size_t dst = hasDst ? regIdx(ops[0].reg.value) : 16;

        switch (ins.mnemonic)
        {
        case ZYDIS_MNEMONIC_MOV:
            if (ins.operand_count_visible >= 2)
            {
                if (ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
                {
                    if (hasDst && dst < 16)
                    {
                        const size_t src = regIdx(ops[1].reg.value);
                        if (src < 16)
                        {
                            m_regProv[dst] = m_regProv[src];
                            m_regArgSlot[dst] = m_regArgSlot[src];
                        }
                    }
                }
                else if (ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    if (hasDst && dst < 16)
                    {
                        Prov p = Prov::None;
                        int32_t slot = argSlotOf(ops[1]);
                        if (slot == 0 && m_isVirtualMember && m_bitness == PEBitness::Bits64)
                            p = Prov::ThisPtr; // reloaded `this` from its home slot
                        else if (ops[1].mem.base != ZYDIS_REGISTER_NONE)
                        {
                            const size_t b = regIdx(ops[1].mem.base);
                            if (b < 16
                                && (m_regProv[b] == Prov::ThisPtr || m_regProv[b] == Prov::Ptr))
                            {
                                // 64-bit loads through a pointer are pointer
                                // values; narrower loads produce integers.
                                p = (ops[1].size == 64) ? Prov::Ptr : Prov::Int;
                            }
                        }
                        m_regProv[dst] = p;
                        m_regArgSlot[dst] = slot;

                        // Loading an argument into ECX/RCX to pass it as the
                        // callee's `this`/first object argument is strong
                        // evidence that the argument is a pointer
                        // (`mov ecx, [ebp+8]; push ecx; call ...`).
                        if (slot >= 0
                            && (ops[0].reg.value == ZYDIS_REGISTER_ECX
                                || ops[0].reg.value == ZYDIS_REGISTER_RCX))
                            ev(static_cast<size_t>(slot)).isPointer = true;
                    }
                }
                else if (ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    if (hasDst && dst < 16)
                    {
                        m_regProv[dst] = Prov::None;
                        m_regArgSlot[dst] = -1;
                    }
                }
                else if (hasDst && dst < 16)
                {
                    m_regProv[dst] = Prov::None;
                    m_regArgSlot[dst] = -1;
                }
            }
            // `mov <local>, reg` where reg carries an argument: remember the
            // local so a later `fld [local]` can refine the argument type
            // (x86 float args often arrive via `mov eax, [ebp+8]` + `fld`).
            if (hasDst == false && ins.operand_count_visible >= 2
                && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY
                && ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const size_t src = regIdx(ops[1].reg.value);
                if (src < 16 && m_regArgSlot[src] >= 0)
                {
                    const int32_t key = localKeyOf(ops[0]);
                    if (key != 0x7FFFFFFF)
                        m_localSlot[key] = m_regArgSlot[src];
                }
            }
            return;

        case ZYDIS_MNEMONIC_LEA:
            if (hasDst && dst < 16)
            {
                m_regProv[dst] = Prov::Ptr;
                m_regArgSlot[dst] = -1;
            }
            return;

        case ZYDIS_MNEMONIC_MOVZX:
        case ZYDIS_MNEMONIC_MOVSX:
        case ZYDIS_MNEMONIC_MOVSXD:
            // A widened load of an argument slot keeps the slot association
            // (needed for bool detection via a later `test reg, reg`).
            if (hasDst && dst < 16)
            {
                m_regProv[dst] = Prov::None;
                m_regArgSlot[dst]
                    = (ins.operand_count_visible >= 2 && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
                          ? argSlotOf(ops[1])
                          : -1;
            }
            return;

        case ZYDIS_MNEMONIC_ADD:
        case ZYDIS_MNEMONIC_SUB:
            // Address arithmetic keeps pointer-ness (e.g. `add rax, 0xC`).
            return; // prov unchanged

        case ZYDIS_MNEMONIC_CALL:
            // Volatile registers are clobbered by calls.
            if (m_bitness == PEBitness::Bits64)
            {
                for (size_t ri :
                    {size_t(0), size_t(1), size_t(2), size_t(8), size_t(9), size_t(10), size_t(11)})
                {
                    m_regProv[ri] = Prov::None;
                    m_regArgSlot[ri] = -1;
                }
            }
            else
            {
                for (size_t ri : {size_t(0), size_t(1), size_t(2)})
                {
                    m_regProv[ri] = Prov::None;
                    m_regArgSlot[ri] = -1;
                }
            }
            return;

        default:
            if (hasDst && dst < 16)
            {
                // Any other write to a GP register produces a plain value.
                if (!isSetcc(ins.mnemonic))
                {
                    m_regProv[dst] = Prov::None;
                    m_regArgSlot[dst] = -1;
                }
            }
            return;
        }
    }

    // ─── return-type analysis (backward epilogue scan) ───────────────────────

    static bool isEpilogueInsn(const InsRec& r)
    {
        switch (r.mnemonic)
        {
        case ZYDIS_MNEMONIC_POP:
        case ZYDIS_MNEMONIC_LEAVE:
        case ZYDIS_MNEMONIC_NOP:
        case ZYDIS_MNEMONIC_CLD:
        case ZYDIS_MNEMONIC_RET:
            return true;
        case ZYDIS_MNEMONIC_ADD:
        case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_AND:
            // add/sub/and rsp, imm — stack fixups only.
            return r.op0Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_REGISTER)
                   && (r.op0Reg == ZYDIS_REGISTER_RSP || r.op0Reg == ZYDIS_REGISTER_ESP);
        case ZYDIS_MNEMONIC_MOV:
        case ZYDIS_MNEMONIC_LEA:
            // mov/lea rsp, ... — frame teardown.
            return r.op0Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_REGISTER)
                   && (r.op0Reg == ZYDIS_REGISTER_RSP || r.op0Reg == ZYDIS_REGISTER_ESP);
        default:
            return false;
        }
    }

    void analyzeReturnBackward()
    {
        for (size_t i = m_insns.size(); i-- > 0;)
        {
            const InsRec& r = m_insns[i];
            if (isEpilogueInsn(r))
                continue;

            const bool writesEax = r.opCount >= 1
                                   && r.op0Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_REGISTER)
                                   && isEaxLike(r.op0Reg);
            const bool writesXmm0
                = r.opCount >= 1 && r.op0Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_REGISTER)
                  && r.op0Reg == ZYDIS_REGISTER_XMM0;

            if (isSetcc(r.mnemonic))
            {
                m_retKind = RetKind::Bool;
                return;
            }

            // setcc ; movzx eax, al -> bool
            if (r.mnemonic == ZYDIS_MNEMONIC_MOVZX && writesEax && r.prevSetcc)
            {
                m_retKind = RetKind::Bool;
                return;
            }

            if (writesXmm0)
            {
                switch (r.mnemonic)
                {
                case ZYDIS_MNEMONIC_MOVSS:
                case ZYDIS_MNEMONIC_CVTSI2SS:
                    m_retKind = RetKind::Float;
                    return;
                case ZYDIS_MNEMONIC_MOVSD:
                case ZYDIS_MNEMONIC_CVTSI2SD:
                    m_retKind = RetKind::Double;
                    return;
                default:
                    m_retKind = RetKind::Void;
                    return;
                }
            }

            if (writesEax)
            {
                // A final write to AL specifically is the classic
                // `mov al, <bool local>; ret` epilogue of a bool function.
                if (r.op0Reg == ZYDIS_REGISTER_AL)
                {
                    m_retKind = RetKind::Bool;
                    return;
                }
                switch (r.mnemonic)
                {
                case ZYDIS_MNEMONIC_LEA:
                    m_retKind = RetKind::Pointer;
                    return;

                case ZYDIS_MNEMONIC_MOV:
                {
                    if (r.op1Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_REGISTER))
                    {
                        const size_t si = regIdx(r.op1Reg);
                        if (si < 16
                            && (m_regProv[si] == Prov::ThisPtr || m_regProv[si] == Prov::Ptr))
                        {
                            m_retKind = RetKind::Pointer;
                            return;
                        }
                        m_retKind = RetKind::Int;
                        m_retBytes = (r.op1Size == 64)
                                         ? 8u
                                         : (r.op1Size == 32 ? 4u : (r.op1Size == 16 ? 2u : 1u));
                        return;
                    }
                    if (r.op1Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_IMMEDIATE))
                    {
                        m_retKind = RetKind::Int;
                        m_retBytes = 4;
                        return;
                    }
                    if (r.op1Type == static_cast<uint8_t>(ZYDIS_OPERAND_TYPE_MEMORY))
                    {
                        // 64-bit loads: pointer chasing / this-relative = ptr,
                        // flat 64-bit value = int64.
                        if (r.op1Size == 64)
                        {
                            // An 8-byte load from an argument home/stack slot
                            // is a pointer value; otherwise pointer-chasing
                            // through a proven pointer; else a 64-bit integer.
                            const bool ptrish
                                = r.op1Slot >= 0
                                  || (r.op1MemBase != ZYDIS_REGISTER_NONE
                                      && regIdx(r.op1MemBase) < 16
                                      && (m_regProv[regIdx(r.op1MemBase)] == Prov::ThisPtr
                                          || m_regProv[regIdx(r.op1MemBase)] == Prov::Ptr));
                            m_retKind = ptrish ? RetKind::Pointer : RetKind::Int;
                            m_retBytes = 8;
                            return;
                        }
                        m_retKind = RetKind::Int;
                        m_retBytes = (r.op1Size == 16) ? 2u : (r.op1Size == 8 ? 1u : 4u);
                        return;
                    }
                    m_retKind = RetKind::Int;
                    m_retBytes = 4;
                    return;
                }
                case ZYDIS_MNEMONIC_MOVZX:
                case ZYDIS_MNEMONIC_MOVSX:
                case ZYDIS_MNEMONIC_MOVSXD:
                    m_retKind = RetKind::Int;
                    m_retBytes = (r.op1Size == 8) ? 1u : (r.op1Size == 16 ? 2u : 4u);
                    return;

                case ZYDIS_MNEMONIC_ADD:
                case ZYDIS_MNEMONIC_OR:
                case ZYDIS_MNEMONIC_AND:
                case ZYDIS_MNEMONIC_SUB:
                case ZYDIS_MNEMONIC_XOR:
                case ZYDIS_MNEMONIC_SHL:
                case ZYDIS_MNEMONIC_SHR:
                case ZYDIS_MNEMONIC_INC:
                case ZYDIS_MNEMONIC_DEC:
                case ZYDIS_MNEMONIC_NEG:
                case ZYDIS_MNEMONIC_NOT:
                    // Address math on a pointer stays a pointer
                    // (`add rax, 0xC` in a `return &m_ratio;` getter).
                    if ((r.mnemonic == ZYDIS_MNEMONIC_ADD || r.mnemonic == ZYDIS_MNEMONIC_OR)
                        && (m_regProv[0] == Prov::Ptr || m_regProv[0] == Prov::ThisPtr))
                    {
                        m_retKind = RetKind::Pointer;
                        return;
                    }
                    m_retKind = RetKind::Int;
                    m_retBytes = 4;
                    return;

                case ZYDIS_MNEMONIC_CALL:
                    // Callee result conventionally in eax/rax.
                    m_retKind = RetKind::Int;
                    m_retBytes = 4;
                    return;

                default:
                    m_retKind = RetKind::Void;
                    return;
                }
            }

            // x87 return via ST(0): fld / arithmetic / fstp right before ret.
            switch (r.mnemonic)
            {
            case ZYDIS_MNEMONIC_FLD:
            case ZYDIS_MNEMONIC_FST:
            case ZYDIS_MNEMONIC_FSTP:
            case ZYDIS_MNEMONIC_FADD:
            case ZYDIS_MNEMONIC_FSUB:
            case ZYDIS_MNEMONIC_FMUL:
            case ZYDIS_MNEMONIC_FDIV:
                m_retKind = (m_lastX87Width == 64) ? RetKind::Double : RetKind::Float;
                return;
            default:
                break;
            }

            // A store / branch source right before ret: no return evidence.
            m_retKind = RetKind::Void;
            return;
        }
        m_retKind = RetKind::Void;
    }

    // ─── finalize ────────────────────────────────────────────────────────────

    void finalize(VFuncSignature& sig) const
    {
        sig.analyzed = true;
        sig.returnType = renderReturn();

        size_t maxSlot = 0;
        bool any = false;
        for (const auto& kv : m_argEvidence)
        {
            if (!kv.second.seen)
                continue;
            maxSlot = std::max(maxSlot, kv.first);
            any = true;
        }
        size_t slotCount = any ? maxSlot + 1 : 0;

        if (m_bitness == PEBitness::Bits32)
        {
            // x86 thiscall: `ret imm16` = total stack-argument bytes; each
            // slot is 4 bytes. Slot 0 = first declared argument (this lives
            // in ecx, off the stack).
            if (m_hasRetImm && m_retImm > 0)
                slotCount = std::max(slotCount, static_cast<size_t>(m_retImm / 4));

            for (size_t s = 0; s < slotCount; ++s)
            {
                auto it = m_argEvidence.find(s);
                if (it == m_argEvidence.end())
                {
                    sig.params.push_back({"int32_t"});
                    continue;
                }
                sig.params.push_back({renderArg(it->second)});
                // An 8-byte arg (double / long long) consumes two slots.
                if (it->second.maxBytes > 4)
                    ++s;
            }
            return;
        }

        // x64: slot 0 = first register argument (this for member functions).
        const size_t first = m_isVirtualMember ? 1 : 0;
        for (size_t s = first; s < slotCount; ++s)
        {
            auto it = m_argEvidence.find(s);
            if (it == m_argEvidence.end())
                sig.params.push_back({"int32_t"});
            else
                sig.params.push_back({renderArg(it->second)});
        }
    }

    [[nodiscard]] std::string renderReturn() const
    {
        switch (m_retKind)
        {
        case RetKind::Void:
            return "void";
        case RetKind::Bool:
            return "bool";
        case RetKind::Float:
            return "float";
        case RetKind::Double:
            return "double";
        case RetKind::Pointer:
            return "void*";
        case RetKind::Int:
            if (m_retBytes == 1)
                return "int8_t";
            if (m_retBytes == 2)
                return "int16_t";
            if (m_retBytes == 8)
                return "int64_t";
            return "int32_t";
        case RetKind::None:
        default:
            return "void";
        }
    }

    [[nodiscard]] static std::string renderArg(const ArgEvidence& e)
    {
        if (e.isThis)
            return "void*";
        if (e.isDouble)
            return "double";
        if (e.isFloat)
            return "float";
        if (e.isBool)
            return "bool";
        if (e.isPointer)
            return "void*";
        if (e.maxBytes == 1)
            return e.isUnsigned ? "uint8_t" : "int8_t";
        if (e.maxBytes == 2)
            return e.isUnsigned ? "uint16_t" : "int16_t";
        if (e.maxBytes == 8)
            return e.isSigned ? "int64_t" : "uint64_t";
        return "int32_t";
    }

    // ─── members ─────────────────────────────────────────────────────────────

    const PEImage* m_pe = nullptr;
    PEBitness m_bitness = PEBitness::Bits64;
    const PESection* m_text = nullptr;
    ZydisDecoder m_decoder = {};

    bool m_isVirtualMember = true;
    bool m_ebpFrame = false;
    int32_t m_ebpSlotBase = 0;
    int32_t m_espDelta = 0;
    bool m_prevSetcc = false;
    uint16_t m_lastX87Width = 0;
    int m_int3Run = 0;
    bool m_xmmDead[4] = {false, false, false, false};

    std::map<size_t, ArgEvidence> m_argEvidence;
    std::map<int32_t, int32_t> m_localSlot; ///< local stack var -> arg slot
    std::vector<InsRec> m_insns;
    Prov m_regProv[17];
    int32_t m_regArgSlot[17];

    RetKind m_retKind = RetKind::None;
    uint32_t m_retBytes = 0;
    bool m_hasRetImm = false;
    uint32_t m_retImm = 0;
};

} // namespace DumpPDB
