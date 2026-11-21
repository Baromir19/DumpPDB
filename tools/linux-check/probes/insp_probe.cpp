#include <Core/RTTI/ExeInspector.hpp>
#include <cstdio>
using namespace DumpPDB;
int main(int,char**argv){
    std::wstring w; for(char*c=argv[1];*c;++c) w.push_back(*c);
    RttiReader r; r.load(w); ExeInspector in(r);
    std::string mode=argv[2];
    if(mode=="class") fputs(in.classText(argv[3]).c_str(),stdout);
    else if(mode=="addr") fputs(in.addressText(strtoull(argv[3],0,16)).c_str(),stdout);
    else fputs(in.infoText().c_str(),stdout);
    std::wstring ww=ExeInspector::widen("héllo ✓ 😀"); printf("\nroundtrip=%s\n",ExeInspector::narrow(ww).c_str());
}
