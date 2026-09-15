#pragma once

#include <Core/DIA/TypeWalker.hpp>

/// Configuration for dumping output.
/// Moved verbatim from <Core/DIA/SymbolDumper.hpp>; include via that facade.
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
    bool m_hideCompilerGenerated = true; // hide __local_vftable_ctor_closure, etc.
    bool m_templateParams = false;       // emit template<...> for requested template instantiations
    DWORD m_baseAccessType = 0;          // override access type
    IntStyle m_intStyle = IntStyle::Cstdint; // __int32 vs int32_t
};
