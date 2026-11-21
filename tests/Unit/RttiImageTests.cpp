#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Core/PE/PEDirectories.hpp>
#include <Core/PE/PEImage.hpp>
#include <Core/PE/PERelocations.hpp>
#include <Core/RTTI/ExeAnalyzer.hpp>
#include <Core/RTTI/ExeReport.hpp>
#include <Core/RTTI/RttiReader.hpp>

using namespace DumpPDB;

// ============================================================================
// A tiny hand-built PE32 image with real MSVC RTTI structures, so the whole
// executable pipeline (PE parsing, relocations, RTTI, hierarchy, function
// discovery, constructor attribution, reports) is covered without shipping a
// binary.
//
//   class Base            { virtual void f0(); virtual void f1(); };
//   class Ns::Derived : Base { f0 overridden, f1 inherited, f2 new };
//
//   .text  0x1000  functions, 16-byte aligned, 0xCC padding
//   .rdata 0x2000  vftables + COL / CHD / BCD
//   .data  0x3000  TypeDescriptors
//   .reloc 0x4000  base relocations (every pointer above)
// ============================================================================

namespace
{
constexpr uint32_t kBase = 0x400000;

constexpr uint32_t kTextRva = 0x1000;
constexpr uint32_t kRdataRva = 0x2000;
constexpr uint32_t kDataRva = 0x3000;
constexpr uint32_t kRelocRva = 0x4000;

// .text
constexpr uint32_t kFnTypeInfo = 0x1000;
constexpr uint32_t kFnBaseF0 = 0x1010;
constexpr uint32_t kFnBaseF1 = 0x1020;
constexpr uint32_t kFnDerivedF0 = 0x1030;
constexpr uint32_t kFnDerivedF2 = 0x1040;
constexpr uint32_t kFnBaseCtor = 0x1050;
constexpr uint32_t kFnDerivedCtor = 0x1060;

// .rdata
constexpr uint32_t kTypeInfoVft = 0x2000;
constexpr uint32_t kColBase = 0x2010;
constexpr uint32_t kVftBase = 0x2034; // slot 0; the COL pointer sits at 0x2030
constexpr uint32_t kChdBase = 0x2060;
constexpr uint32_t kBcaBase = 0x2080;
constexpr uint32_t kBcdBase = 0x20A0;
constexpr uint32_t kColDerived = 0x20C0;
constexpr uint32_t kChdDerived = 0x20E0;
constexpr uint32_t kBcaDerived = 0x2100;
constexpr uint32_t kBcdDerived = 0x2120;
constexpr uint32_t kVftDerived = 0x2144; // COL pointer at 0x2140

// .data
constexpr uint32_t kTdBase = 0x3000;
constexpr uint32_t kTdDerived = 0x3040;

struct Image
{
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> relocs; // RVAs of 32-bit pointers

    void put32(size_t a_fileOff, uint32_t a_v) { std::memcpy(&bytes[a_fileOff], &a_v, 4); }
    void put16(size_t a_fileOff, uint16_t a_v) { std::memcpy(&bytes[a_fileOff], &a_v, 2); }

    // file offset of an RVA inside our four sections
    static size_t off(uint32_t a_rva)
    {
        if (a_rva >= kRelocRva) return 0xC00 + (a_rva - kRelocRva);
        if (a_rva >= kDataRva) return 0xA00 + (a_rva - kDataRva);
        if (a_rva >= kRdataRva) return 0x600 + (a_rva - kRdataRva);
        return 0x400 + (a_rva - kTextRva);
    }

    void dword(uint32_t a_rva, uint32_t a_v) { put32(off(a_rva), a_v); }
    /// Stores an absolute VA and records the relocation, like a linker would.
    void ptr(uint32_t a_rva, uint32_t a_targetRva)
    {
        dword(a_rva, kBase + a_targetRva);
        relocs.push_back(a_rva);
    }
    void code(uint32_t a_rva, std::initializer_list<uint8_t> a_b)
    {
        size_t o = off(a_rva);
        for (uint8_t b : a_b)
            bytes[o++] = b;
    }
    void text(uint32_t a_rva, const char* a_s)
    {
        std::memcpy(&bytes[off(a_rva)], a_s, std::strlen(a_s) + 1);
    }
};

void section(Image& a_img, int a_index, const char* a_name, uint32_t a_va, uint32_t a_vsize,
    uint32_t a_raw, uint32_t a_rawSize, uint32_t a_flags)
{
    const size_t o = 0x178 + static_cast<size_t>(a_index) * 40;
    std::memcpy(&a_img.bytes[o], a_name, std::strlen(a_name));
    a_img.put32(o + 8, a_vsize);
    a_img.put32(o + 12, a_va);
    a_img.put32(o + 16, a_rawSize);
    a_img.put32(o + 20, a_raw);
    a_img.put32(o + 36, a_flags);
}

void colAndHierarchy(Image& img, uint32_t col, uint32_t td, uint32_t chd)
{
    img.dword(col + 0, 0);  // signature
    img.dword(col + 4, 0);  // offset
    img.dword(col + 8, 0);  // cdOffset
    img.ptr(col + 12, td);
    img.ptr(col + 16, chd);
}

void bcd(Image& img, uint32_t at, uint32_t td, uint32_t contained, uint32_t attrs)
{
    img.ptr(at + 0, td);
    img.dword(at + 4, contained);
    img.dword(at + 8, 0);            // mdisp
    img.dword(at + 12, 0xFFFFFFFFu); // pdisp = -1 (not a virtual base)
    img.dword(at + 16, 0);           // vdisp
    img.dword(at + 20, attrs);
}

Image buildImage()
{
    Image img;
    img.bytes.assign(0xE00, 0);

    // ---- headers ---------------------------------------------------------
    img.bytes[0] = 'M';
    img.bytes[1] = 'Z';
    img.put32(0x3C, 0x80);
    std::memcpy(&img.bytes[0x80], "PE\0\0", 4);
    img.put16(0x84, 0x14C); // i386
    img.put16(0x86, 4);     // sections
    img.put32(0x88, 0x5E0F0000); // timestamp
    img.put16(0x94, 0xE0);  // optional header size
    img.put16(0x96, 0x102); // executable, 32-bit
    const size_t opt = 0x98;
    img.put16(opt, 0x10B);
    img.bytes[opt + 2] = 9; // linker 9.0
    img.put32(opt + 16, kFnTypeInfo); // entry point
    img.put32(opt + 28, kBase);
    img.put32(opt + 32, 0x1000);
    img.put32(opt + 36, 0x200);
    img.put32(opt + 56, 0x5000); // size of image
    img.put32(opt + 60, 0x400);  // size of headers
    img.put16(opt + 68, 3);
    img.put32(opt + 92, 16);
    // data directory 5 = base relocations (rva/size filled in below)

    section(img, 0, ".text", kTextRva, 0x200, 0x400, 0x200, 0x60000020);
    section(img, 1, ".rdata", kRdataRva, 0x400, 0x600, 0x400, 0x40000040);
    section(img, 2, ".data", kDataRva, 0x200, 0xA00, 0x200, 0xC0000040);
    section(img, 3, ".reloc", kRelocRva, 0x200, 0xC00, 0x200, 0x42000040);

    // ---- .text: 0xCC padding, functions at 16-byte boundaries --------------
    std::memset(&img.bytes[0x400], 0xCC, 0x200);
    img.code(kFnTypeInfo, {0xC3});
    img.code(kFnBaseF0, {0x55, 0x8B, 0xEC, 0x5D, 0xC3});
    img.code(kFnBaseF1, {0xC3});
    img.code(kFnDerivedF0, {0x55, 0x8B, 0xEC, 0x5D, 0xC3});
    img.code(kFnDerivedF2, {0xC3});
    // Base::Base():  mov dword ptr [ecx], offset Base::vftable ; mov eax,ecx ; ret
    img.code(kFnBaseCtor, {0xC7, 0x01, 0, 0, 0, 0, 0x8B, 0xC1, 0xC3});
    img.ptr(kFnBaseCtor + 2, kVftBase);
    // Derived::Derived(): inlined Base ctor stores the base vptr first, then its own.
    img.code(kFnDerivedCtor,
        {0xC7, 0x01, 0, 0, 0, 0, 0xC7, 0x01, 0, 0, 0, 0, 0x8B, 0xC1, 0xC3});
    img.ptr(kFnDerivedCtor + 2, kVftBase);
    img.ptr(kFnDerivedCtor + 8, kVftDerived);

    // ---- .rdata ---------------------------------------------------------------
    img.ptr(kTypeInfoVft, kFnTypeInfo); // type_info vftable (one slot)

    colAndHierarchy(img, kColBase, kTdBase, kChdBase);
    img.ptr(kVftBase - 4, kColBase);
    img.ptr(kVftBase + 0, kFnBaseF0);
    img.ptr(kVftBase + 4, kFnBaseF1);
    img.dword(kChdBase + 0, 0);
    img.dword(kChdBase + 4, 0);
    img.dword(kChdBase + 8, 1);
    img.ptr(kChdBase + 12, kBcaBase);
    img.ptr(kBcaBase, kBcdBase);
    bcd(img, kBcdBase, kTdBase, 0, 0x40);

    colAndHierarchy(img, kColDerived, kTdDerived, kChdDerived);
    img.ptr(kVftDerived - 4, kColDerived);
    img.ptr(kVftDerived + 0, kFnDerivedF0);
    img.ptr(kVftDerived + 4, kFnBaseF1);
    img.ptr(kVftDerived + 8, kFnDerivedF2);
    img.dword(kChdDerived + 0, 0);
    img.dword(kChdDerived + 4, 0);
    img.dword(kChdDerived + 8, 2);
    img.ptr(kChdDerived + 12, kBcaDerived);
    img.ptr(kBcaDerived + 0, kBcdDerived);
    img.ptr(kBcaDerived + 4, kBcdBase);
    bcd(img, kBcdDerived, kTdDerived, 1, 0x40);

    // ---- .data: TypeDescriptors ---------------------------------------------------
    img.ptr(kTdBase, kTypeInfoVft);
    img.text(kTdBase + 8, ".?AVBase@@");
    img.ptr(kTdDerived, kTypeInfoVft);
    img.text(kTdDerived + 8, ".?AVDerived@Ns@@");

    // ---- .reloc: one block per page ----------------------------------------------------
    std::vector<uint32_t> sorted = img.relocs;
    std::sort(sorted.begin(), sorted.end());
    size_t pos = Image::off(kRelocRva);
    const size_t relocStart = pos;
    for (uint32_t page : {0x1000u, 0x2000u, 0x3000u})
    {
        std::vector<uint16_t> entries;
        for (uint32_t r : sorted)
            if ((r & ~0xFFFu) == page)
                entries.push_back(static_cast<uint16_t>((3u << 12) | (r & 0xFFF)));
        if (entries.size() % 2)
            entries.push_back(0); // pad the block to 4 bytes
        img.put32(pos, page);
        img.put32(pos + 4, static_cast<uint32_t>(8 + entries.size() * 2));
        pos += 8;
        for (uint16_t e : entries)
        {
            img.put16(pos, e);
            pos += 2;
        }
    }
    img.put32(opt + 96 + 5 * 8, kRelocRva);
    img.put32(opt + 96 + 5 * 8 + 4, static_cast<uint32_t>(pos - relocStart));
    return img;
}

std::wstring writeImage(const Image& a_img)
{
    const auto path = std::filesystem::temp_directory_path() / "dumppdb_synthetic_rtti.exe";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(a_img.bytes.data()),
        static_cast<std::streamsize>(a_img.bytes.size()));
    return path.wstring();
}

const VftableInfo* find(const RttiReader& r, const char* a_name)
{
    auto v = r.findExact(a_name);
    return v.empty() ? nullptr : v.front();
}
} // namespace

class SyntheticRtti : public ::testing::Test
{
protected:
    void SetUp() override
    {
        path = writeImage(buildImage());
        loaded = reader.load(path);
    }
    void TearDown() override { std::filesystem::remove(path); }

    std::wstring path;
    RttiReader reader;
    bool loaded = false;
};

TEST_F(SyntheticRtti, PeHeadersAndRelocations)
{
    ASSERT_TRUE(loaded);
    const PEImage& pe = reader.pe();
    EXPECT_FALSE(pe.is64Bit());
    EXPECT_EQ(pe.imageBase(), static_cast<uint64_t>(kBase));
    EXPECT_EQ(pe.entryPointRva(), kFnTypeInfo);
    EXPECT_EQ(pe.sections().size(), 4u);
    // 3 in .text (vptr stores) + 19 in .rdata (vftables, COL/CHD/BCD) + 2 in .data (TypeDescriptors)
    EXPECT_EQ(reader.relocations().size(), 24u);
    EXPECT_TRUE(reader.relocations().contains(kVftBase));
    EXPECT_FALSE(reader.relocations().contains(kVftBase + 1));
}

TEST_F(SyntheticRtti, ClassesNamesAndVftableSizes)
{
    ASSERT_TRUE(loaded);
    ASSERT_EQ(reader.vftables().size(), 2u);

    const VftableInfo* base = find(reader, "Base");
    const VftableInfo* derived = find(reader, "Ns::Derived");
    ASSERT_TRUE(base != nullptr);
    ASSERT_TRUE(derived != nullptr);

    EXPECT_EQ(base->vftableRva, kVftBase);
    EXPECT_EQ(base->vfuncs.size(), 2u);   // ends at the first slot that is not a relocated code pointer
    EXPECT_EQ(derived->vftableRva, kVftDerived);
    EXPECT_EQ(derived->vfuncs.size(), 3u);
    EXPECT_EQ(derived->scopeName, "Ns");
    EXPECT_EQ(derived->className, "Derived");
    EXPECT_FALSE(derived->demangleFailed);
}

TEST_F(SyntheticRtti, HierarchyHasDirectBase)
{
    ASSERT_TRUE(loaded);
    const VftableInfo* derived = find(reader, "Ns::Derived");
    ASSERT_TRUE(derived != nullptr);
    ASSERT_EQ(derived->bases.size(), 2u);
    EXPECT_EQ(derived->bases[1].demangledName, "Base");
    EXPECT_TRUE(derived->bases[1].isDirect);
    EXPECT_FALSE(derived->bases[1].isVirtual) << "pdisp == -1 means a plain base";
    EXPECT_EQ(derived->inheritance, InheritanceKind::Single);
}

TEST_F(SyntheticRtti, TypeDescriptorsAreAllFound)
{
    ASSERT_TRUE(loaded);
    ASSERT_EQ(reader.typeDescriptors().size(), 2u);
    for (const auto& t : reader.typeDescriptors())
        EXPECT_TRUE(t.hasVftable);
    EXPECT_EQ(reader.typeInfoVftableRva(), static_cast<uint64_t>(kTypeInfoVft));
}

TEST_F(SyntheticRtti, ExactLookupIgnoresCaseAndSpaces)
{
    ASSERT_TRUE(loaded);
    EXPECT_EQ(reader.findExact("ns::derived").size(), 1u);
    EXPECT_EQ(reader.findExact("Derived").size(), 1u) << "leaf name matches too";
    EXPECT_EQ(reader.findExact("Nope").size(), 0u);
    EXPECT_EQ(reader.findByName("eriv").size(), 1u);
}

TEST_F(SyntheticRtti, FunctionDiscoveryAndNaming)
{
    ASSERT_TRUE(loaded);
    const ExeAnalysis a = ExeAnalyzer::analyze(reader);

    // Every vftable target must be recognised from the padding alone.
    EXPECT_EQ(a.stats.vtableTargets, 4u);
    EXPECT_EQ(a.stats.vtableTargetsFound, 4u);

    EXPECT_GE(a.functionIndexStartingAt(kFnBaseF0), 0);
    EXPECT_GE(a.functionIndexStartingAt(kFnDerivedCtor), 0);

    // Derived::f1 is inherited: it must be named after Base, where it was introduced.
    const int64_t f1 = a.functionIndexStartingAt(kFnBaseF1);
    ASSERT_GE(f1, 0);
    EXPECT_EQ(a.functions[static_cast<size_t>(f1)].name, "Base::vfunc_1");

    // Overrides and new virtuals belong to the derived class.
    const int64_t f2 = a.functionIndexStartingAt(kFnDerivedF2);
    ASSERT_GE(f2, 0);
    EXPECT_EQ(a.functions[static_cast<size_t>(f2)].name, "Ns::Derived::vfunc_2");
}

TEST_F(SyntheticRtti, ConstructorBelongsToTheMostDerivedClassOnly)
{
    ASSERT_TRUE(loaded);
    const ExeAnalysis a = ExeAnalyzer::analyze(reader);

    // The function storing both vptrs is Derived's constructor, not a second Base ctor.
    const int64_t derivedCtor = a.functionIndexStartingAt(kFnDerivedCtor);
    const int64_t baseCtor = a.functionIndexStartingAt(kFnBaseCtor);
    ASSERT_GE(derivedCtor, 0);
    ASSERT_GE(baseCtor, 0);
    EXPECT_EQ(a.functions[static_cast<size_t>(derivedCtor)].name, "Ns::Derived::ctor");
    EXPECT_EQ(a.functions[static_cast<size_t>(baseCtor)].name, "Base::ctor");

    for (const auto& c : a.classes)
    {
        if (c.name == "Base" || c.name == "Ns::Derived")
        {
            EXPECT_EQ(c.ctorFuncs.size(), 1u) << c.name;
        }
    }
}

TEST_F(SyntheticRtti, ReferencesToTheVftableAreIndexed)
{
    ASSERT_TRUE(loaded);
    const ExeAnalysis a = ExeAnalyzer::analyze(reader);
    const auto range = a.refsTo(kVftBase);
    // Base::ctor and Derived::ctor both store this address.
    EXPECT_EQ(static_cast<size_t>(range.second - range.first), 2u);
}

TEST_F(SyntheticRtti, ReportsAndScriptsAreWellFormed)
{
    ASSERT_TRUE(loaded);
    const ExeAnalysis a = ExeAnalyzer::analyze(reader);
    const auto names = ExeReport::collectNames(reader, a);

    bool sawVftable = false;
    bool sawCtor = false;
    for (const auto& n : names)
    {
        if (n.name == "Ns::Derived::vftable")
            sawVftable = true;
        if (n.name == "Ns::Derived::ctor")
            sawCtor = true;
        // Names handed to disassemblers must be valid identifiers.
        for (char c : n.safeName)
            EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)) || c == '_') << n.safeName;
    }
    EXPECT_TRUE(sawVftable);
    EXPECT_TRUE(sawCtor);

    const std::string ida = ExeReport::idaScript(names, "test.exe");
    EXPECT_TRUE(ida.find("Ns_Derived_ctor") != std::string::npos);
    EXPECT_TRUE(ExeReport::namesJson(names).front() == '[');
    EXPECT_TRUE(ExeReport::summary(reader, a).find("vftables") != std::string::npos);
    EXPECT_TRUE(ExeReport::classesCsv(reader, a).find("Ns::Derived") != std::string::npos);
}

TEST(SourcePaths, ExtractionStripsJunkAndKeepsRealPaths)
{
    EXPECT_EQ(ExeAnalyzer::extractSourcePath("e:\\proj\\a\\b.cpp"), "e:\\proj\\a\\b.cpp");
    EXPECT_EQ(ExeAnalyzer::extractSourcePath(",@e:\\proj\\a\\b.cpp"), "e:\\proj\\a\\b.cpp");
    EXPECT_EQ(ExeAnalyzer::extractSourcePath("Ae:\\proj\\a\\b.cpp"), "e:\\proj\\a\\b.cpp");
    EXPECT_EQ(ExeAnalyzer::extractSourcePath("assert failed in e:/p/x.h(42)"), "e:/p/x.h");
    EXPECT_TRUE(ExeAnalyzer::extractSourcePath("no path here").empty());
    EXPECT_TRUE(ExeAnalyzer::extractSourcePath("justafile.cpp").empty()) << "needs a directory part";
}

TEST(PeRichHeader, ToolchainGuessIsConservative)
{
    EXPECT_EQ(PEDirectories::toolchainGuess(30729), "VS2008 SP1 (9.0)");
    EXPECT_EQ(PEDirectories::toolchainGuess(40219), "VS2010 SP1 (10.0)");
    EXPECT_TRUE(PEDirectories::toolchainGuess(1).empty());
}
