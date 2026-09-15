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
    bool m_templateParams = false;       ///< Emit template<...> for requested template instantiations.
    DWORD m_baseAccessType = 0;          ///< Override access type (0 = use symbol's own access).
    IntStyle m_intStyle = IntStyle::Cstdint;
};
