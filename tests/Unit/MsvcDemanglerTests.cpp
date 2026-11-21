#include <gtest/gtest.h>

#include <string>

#include <Core/RTTI/MsvcDemangler.hpp>

using DumpPDB::MsvcDemangler;

// ============================================================================
// MsvcDemangler — RTTI TypeDescriptor name demangling.
// Every expectation is written the way MSVC spells the name in a PDB, which is
// what DIA reports (no class/struct keywords, ',' without space, "> >").
// ============================================================================

namespace
{
std::string dm(const char* a_mangled)
{
    return MsvcDemangler::demangle(a_mangled);
}
} // namespace

TEST(MsvcDemangler, PlainAndNestedNames)
{
    EXPECT_EQ(dm(".?AVFoo@@"), "Foo");
    EXPECT_EQ(dm(".?AUBar@@"), "Bar");
    EXPECT_EQ(dm(".?AVBlockStreamer@NavMeshPrivate@AI@@"), "AI::NavMeshPrivate::BlockStreamer");
    EXPECT_EQ(dm(".?AW4MyEnum@Ns@@"), "Ns::MyEnum");
}

TEST(MsvcDemangler, KindIsReported)
{
    EXPECT_EQ(MsvcDemangler::parse(".?AVFoo@@").kind, MsvcDemangler::Kind::Class);
    EXPECT_EQ(MsvcDemangler::parse(".?AUFoo@@").kind, MsvcDemangler::Kind::Struct);
    EXPECT_EQ(MsvcDemangler::parse(".?ATFoo@@").kind, MsvcDemangler::Kind::Union);
    EXPECT_EQ(MsvcDemangler::parse(".?AW4Foo@@").kind, MsvcDemangler::Kind::Enum);
}

TEST(MsvcDemangler, AnonymousNamespace)
{
    const auto r = MsvcDemangler::parse(".?AUStringPool@?A0x0a9a5557@@");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.name, "`anonymous namespace'::StringPool");
    EXPECT_TRUE(r.isAnonymous);
}

TEST(MsvcDemangler, TemplatesWithTypeAndValueArguments)
{
    EXPECT_EQ(dm(".?AV?$CCallback@VSteamEnvironment@@UDlcInstalled_t@@$0A@@@"),
        "CCallback<SteamEnvironment,DlcInstalled_t,0>");
    EXPECT_EQ(dm(".?AV?$Foo@$0BA@@@"), "Foo<16>");
    EXPECT_EQ(dm(".?AV?$Foo@$0?0@@"), "Foo<-1>");
    EXPECT_EQ(dm(".?AV?$Foo@_N@@"), "Foo<bool>");
    EXPECT_EQ(dm(".?AV?$Foo@_J_K@@"), "Foo<__int64,unsigned __int64>");
    EXPECT_EQ(dm(".?AV?$Foo@W4Color@@@@"), "Foo<Color>");
    EXPECT_TRUE(MsvcDemangler::parse(".?AV?$Foo@H@@").isTemplate);
}

TEST(MsvcDemangler, PointersReferencesAndFunctionPointers)
{
    EXPECT_EQ(dm(".?AV?$Foo@PAD@@"), "Foo<char*>");
    EXPECT_EQ(dm(".?AV?$Foo@PBD@@"), "Foo<const char*>");
    EXPECT_EQ(dm(".?AV?$Foo@PAPAH@@"), "Foo<int**>");
    EXPECT_EQ(dm(".?AV?$Foo@AAH@@"), "Foo<int&>");
    EXPECT_EQ(dm(".?AV?$Foo@$$QAH@@"), "Foo<int&&>");
    EXPECT_EQ(dm(".?AV?$Foo@P6AXH@Z@@"), "Foo<void (__cdecl*)(int)>");
}

TEST(MsvcDemangler, ConstQualifiedTemplateArgument)
{
    // "$$CB" = const; used by std::pair<const K, V> inside map nodes.
    EXPECT_EQ(dm(".?AV?$Foo@$$CBVAString@@@@"), "Foo<const AString>");
}

TEST(MsvcDemangler, NameBackReferencesUseTheirOwnTablePerTemplate)
{
    EXPECT_EQ(dm(".?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"),
        "std::basic_string<char,std::char_traits<char>,std::allocator<char> >");
    EXPECT_EQ(dm(".?AV?$Node@VEmpty@@V1@@@"), "Node<Empty,Empty>");
}

TEST(MsvcDemangler, ClosingBracketsAreSeparatedLikeMsvcDoes)
{
    EXPECT_EQ(dm(".?AV?$A@V?$B@H@@@@"), "A<B<int> >");
    EXPECT_EQ(dm(".?AV?$A@V?$B@V?$C@H@@@@@@"), "A<B<C<int> > >");

    MsvcDemangler::Options compact;
    compact.angleBracketSpace = false;
    EXPECT_EQ(MsvcDemangler::demangle(".?AV?$A@V?$B@H@@@@", compact), "A<B<int>>");
}

TEST(MsvcDemangler, ClassLocalToAFunctionKeepsTheEnclosingScope)
{
    const auto r = MsvcDemangler::parse(".?AUEntry@?1??Create@Builder@@QAEXW4Kind@@PAD@Z@");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.name, "Builder::Create::Entry");
    EXPECT_TRUE(r.isLocal);
}

TEST(MsvcDemangler, LocalClassCanBackReferToNamesOfItsScope)
{
    // "ABV3@" refers to the 4th name seen so far (shared table with the enclosing name).
    const auto r = MsvcDemangler::parse(
        ".?AVFilter@?1??FindVictim@Grapple@@CAPAVActor@@ABV3@AAW4Kind@@PAV3@2@Z@");
    EXPECT_TRUE(r.ok) << r.name;
}

TEST(MsvcDemangler, GarbageIsRejectedNotMangledIntoNonsense)
{
    EXPECT_TRUE(dm("").empty());
    EXPECT_TRUE(dm(".?AV").empty());
    EXPECT_TRUE(dm("not a type").empty());
    EXPECT_TRUE(dm(".?AVFoo").empty());          // missing terminator
    EXPECT_TRUE(dm(".?AV?$Foo@@").empty());      // template without the qualified-name terminator
    EXPECT_EQ(dm(".?AV?$Foo@@@"), "Foo<>");      // ... and the well-formed empty argument list
    EXPECT_TRUE(dm(".?AV?$Foo@Q").empty());      // truncated argument list
    EXPECT_TRUE(dm(".?AVFoo@@trailing").empty()); // everything must be consumed
    EXPECT_TRUE(dm(".?AV9@@").empty());          // back-reference with an empty table
}

TEST(MsvcDemangler, ScopeHelpersIgnoreSeparatorsInsideTemplates)
{
    EXPECT_EQ(MsvcDemangler::leafName("A::B::Foo<X::Y>"), "Foo<X::Y>");
    EXPECT_EQ(MsvcDemangler::scopeOf("A::B::Foo<X::Y>"), "A::B");
    EXPECT_EQ(MsvcDemangler::bareLeafName("A::Foo<int>"), "Foo");
    EXPECT_EQ(MsvcDemangler::scopeOf("Top"), "");
    EXPECT_EQ(MsvcDemangler::leafName("`anonymous namespace'::Foo"), "Foo");
}
