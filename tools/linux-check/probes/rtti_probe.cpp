#include <Core/RTTI/RttiReader.hpp>
#include <chrono>
#include <cstdio>
#include <map>
using namespace DumpPDB;
int main(int argc,char**argv){
    if(argc<2) return 1;
    std::wstring w; for(char*c=argv[1];*c;++c) w.push_back(*c);
    auto t0=std::chrono::steady_clock::now();
    RttiReader r; bool ok=r.load(w);
    auto t1=std::chrono::steady_clock::now();
    printf("load ok=%d bitness=%d vftables=%zu typedescs=%zu time=%.2fs purecall=%llx typeinfo_vft=%llx relocs=%zu\n",ok,(int)r.bitness(),r.vftables().size(),r.typeDescriptors().size(),std::chrono::duration<double>(t1-t0).count(),(unsigned long long)r.pureCallRva(),(unsigned long long)r.typeInfoVftableRva(),r.relocations().size());
    size_t fail=0,tmpl=0,anon=0,local=0,virt=0,noVft=0,pure=0,slots=0,mi=0;
    for(auto&v:r.vftables()){ fail+=v.demangleFailed; tmpl+=v.isTemplateInstance; anon+=v.isInAnonymousNamespace; local+=v.isLocalToFunction; mi+=v.objectOffset!=0; for(auto&b:v.bases) virt+=b.isVirtual&&b.isDirect; for(auto&s:v.vfuncs){slots++; pure+=s.isPureVirtual;} }
    for(auto&t:r.typeDescriptors()) noVft+=!t.hasVftable;
    printf("demangleFail=%zu templates=%zu anon=%zu local=%zu secondaryVft=%zu virtualBases=%zu slots=%zu pureSlots=%zu typesWithoutVftable=%zu\n",fail,tmpl,anon,local,mi,virt,slots,pure,noVft);
    int n=0; for(auto&v:r.vftables()){ if(n++%900==0) printf("%08llx off=%u slots=%zu bases=%zu chdAttr=%u  %s\n",(unsigned long long)v.vftableRva,v.objectOffset,v.vfuncs.size(),v.bases.size(),v.chdAttributes,v.fullName.c_str()); }
    n=0; for(auto&t:r.typeDescriptors()) if(!t.hasVftable && n++<8) printf("  no-vft: %s (usedAsBase=%u)\n",t.fullName.c_str(),t.usedAsBaseCount);
}
