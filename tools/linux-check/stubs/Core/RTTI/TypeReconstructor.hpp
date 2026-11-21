#pragma once
// MOCK of the Zydis-dependent TypeReconstructor, only for API syntax checks on Linux.
#include <Core/RTTI/RttiReader.hpp>
#include <string>
namespace DumpPDB {
struct TypeReconstructor {
    ReconstructedType reconstruct(const std::string&, const RttiReader&) { return {}; }
    static std::wstring format(const ReconstructedType&) { return L""; }
};
}
