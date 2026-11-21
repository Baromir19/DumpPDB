#include <Core/RTTI/ExeAnalyzer.hpp>
#include <chrono>
#include <cstdio>
using namespace DumpPDB;
int main(int,char**argv){
    std::wstring w; for(char*c=argv[1];*c;++c) w.push_back(*c);
    RttiReader r; if(!r.load(w)){puts("load fail");return 1;}
    auto t0=std::chrono::steady_clock::now();
    ExeAnalysis a=ExeAnalyzer::analyze(r);
    auto t1=std::chrono::steady_clock::now();
    auto&s=a.stats;
    printf("analysis %.2fs\n",std::chrono::duration<double>(t1-t0).count());
    printf("functions=%zu named=%zu callSites=%zu relocRefs=%zu\n",s.functions,s.functionsWithName,s.callSites,s.relocRefs);
    printf("vtable targets=%zu independently found=%zu recall=%.2f%%\n",s.vtableTargets,s.vtableTargetsFound,s.vtableRecall);
    printf("strings=%zu referenced=%zu sourcePaths=%zu globals=%zu\n",s.strings,s.referencedStrings,s.sourcePaths,s.globals);
    printf("classes=%zu withCtor=%zu withSource=%zu deletingDtors=%zu/%zu\n",a.classes.size(),s.classesWithCtor,s.classesWithSource,s.deletingDtors,s.deletingDtorCandidates);
    size_t withSrc=0,withStr=0; for(auto&f:a.functions){withSrc+=!f.sourceFile.empty(); withStr+=!f.strings.empty();}
    printf("functions with source=%zu with strings=%zu\n",withSrc,withStr);
    std::map<std::string,int> kinds; for(auto&g:a.globals) kinds[g.kind]++; for(auto&[k,v]:kinds) printf("  globals %s=%d\n",k.c_str(),v);
    printf("-- sample classes\n"); int n=0;
    for(auto&c:a.classes){ if(c.sources.empty()||c.ctorFuncs.empty()) continue; if(n++%400) continue;
        printf("%s slots=%u ctors=%zu dtor=%d del=%d src=%s\n",c.name.c_str(),c.slotCount,c.ctorFuncs.size(),c.dtorFunc,c.deletingDtorFunc,c.sources[0].first.c_str()); }
    printf("-- sample source files\n"); n=0; for(auto&f:a.sourceFiles){ if(n++%150==0) printf("%s refs=%u classes=%zu\n",f.path.c_str(),f.refCount,f.classes.size()); }
    printf("-- named fn samples\n"); n=0; for(auto&f:a.functions){ if(f.name.empty()) continue; if(n++%2500==0) printf("%08llx sz=%u callers=%u %s [%d]\n",(unsigned long long)f.rva,f.size,f.callers,f.name.c_str(),(int)f.confidence);}
}
