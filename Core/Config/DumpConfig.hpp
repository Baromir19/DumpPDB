#pragma once

#include <Core/DIA/TypeWalker.hpp>

/// Configuration for dumping output.
struct DumpConfig
{
    bool m_showSize = true;
    bool m_showOffset = true;
    bool m_showAccess = true;
    bool m_showInfoComment = false;
    bool m_showNonScoped = true;
    bool m_showEnumHex = false;
    bool m_showTypeSource = false;
    bool m_curlyBraceNewline = true;
    bool m_hideCompilerGenerated = true; ///< Hide __local_vftable_ctor_closure, etc.
    bool m_templateParams = false; ///< Emit template<...> for requested template instantiations.
    DWORD m_baseAccessType = 0;    ///< Override access type (0 = use symbol's own access).
    IntStyle m_intStyle = IntStyle::Cstdint;

    // ── Global functions / variables / statics / publics ──────────────────
    bool m_showAddress = true;       ///< "// RVA 0x... [section+off] len ..." after functions/variables.
    bool m_showCallingConv = true;   ///< __cdecl/__stdcall/__fastcall (x86 only; x64 has one convention).
    bool m_showFuncAttrs = true;     ///< __declspec(naked/noinline/noreturn), inline.
    bool m_showSourceLine = true;    ///< Source file:line of a function (needs line info in the PDB).
    bool m_showMemberAddress = false; ///< Also annotate members inside class dumps with their RVA.
    bool m_globalsIncludeMembers = false;     ///< List member functions / static members in global listings.
    bool m_globalsIncludeLocalStatics = false; ///< List function-local statics too.
    bool m_globalsSortByAddress = false;      ///< Sort listings by RVA instead of by name.
};
