#pragma once

#include <string>
#include <vector>

#include <dia2.h>

#include <Core/Config/DumpConfig.hpp>
#include <Core/DIA/TypeWalker.hpp>

/// Shared mutable state of the dumping domain.
/// Owned by SymbolDumper and passed by reference to all domain renderers.
class DumpContext
{
public:

    explicit DumpContext(const DumpConfig& a_config = DumpConfig())
        : m_config(a_config)
    {
    }

    void setConfig(const DumpConfig& a_config)
    {
        m_config = a_config;
    }

    const DumpConfig& config() const
    {
        return m_config;
    }

    ScopeContext& scope()
    {
        return m_scope;
    }

    const ScopeContext& scope() const
    {
        return m_scope;
    }

    void setSession(IDiaSession* a_session)
    {
        m_session = a_session;
    }

    IDiaSession* session() const
    {
        return m_session;
    }

    /// CPU the PDB was built for (CV_CPU_TYPE_e value of the compiland details,
    /// 0 = unknown). Used to decide whether calling conventions are meaningful.
    void setPlatform(DWORD a_platform)
    {
        m_platform = a_platform;
    }

    DWORD platform() const
    {
        return m_platform;
    }

    /// x64 / ARM64 have a single calling convention, so printing one per
    /// function would only add noise.
    bool hasSingleCallingConvention() const
    {
        return m_platform == 0xD0 /* CV_CFL_AMD64 */ || m_platform == 0xF6 /* ARM64 */
               || m_platform == 0xF0 /* ARM64EC-ish */;
    }

    void setTemplateInstantiation(TypeWalker::TemplateInstantiation a_ti)
    {
        m_template = std::move(a_ti);
        m_templateHeaderDone = false;
    }

    const TypeWalker::TemplateInstantiation& templateInstantiation() const
    {
        return m_template;
    }

    bool templateHeaderDone() const
    {
        return m_templateHeaderDone;
    }

    void setTemplateHeaderDone(bool a_done)
    {
        m_templateHeaderDone = a_done;
    }

    std::vector<std::wstring>& typeSources()
    {
        return m_typeSources;
    }

    const std::vector<std::wstring>& typeSources() const
    {
        return m_typeSources;
    }

private:

    DumpConfig m_config;
    ScopeContext m_scope;
    std::vector<std::wstring> m_typeSources;
    IDiaSession* m_session = nullptr;
    DWORD m_platform = 0;
    TypeWalker::TemplateInstantiation m_template;
    bool m_templateHeaderDone = false;
};
