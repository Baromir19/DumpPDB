#include <Core/RTTI/ExeReport.hpp>
#include <chrono>
#include <cstdio>
using namespace DumpPDB;
int main(int,char**argv){
    std::wstring w; for(char*c=argv[1];*c;++c) w.push_back(*c);
    RttiReader r; if(!r.load(w)){puts("load fail");return 1;}
    auto a=ExeAnalyzer::analyze(r);
    auto t0=std::chrono::steady_clock::now();
    auto res=ExeExporter::exportAll(r,a,argv[2],"LANoire.exe");
    printf("export ok=%d files=%zu %.2fs\n",res.ok,res.files.size(),std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count());
    for(auto&e:res.errors) puts(e.c_str());
}
