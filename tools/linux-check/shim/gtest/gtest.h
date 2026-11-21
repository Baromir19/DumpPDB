// Tiny stand-in for GoogleTest, ONLY to run the repo's unit tests in a sandbox without gtest.
#pragma once
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
namespace testing { struct Test { virtual ~Test() {} virtual void SetUp() {} virtual void TearDown() {} virtual void TestBody() = 0; void RunIt() { SetUp(); if (!fatal_()) TestBody(); TearDown(); } static bool& fatal_(); };
 struct Reg { const char* suite; const char* name; std::function<void()> fn; };
 inline std::vector<Reg>& regs() { static std::vector<Reg> r; return r; }
 inline int& failures() { static int f = 0; return f; }
 inline bool& fatal() { static bool f = false; return f; }
 inline bool& Test::fatal_() { return fatal(); }
 struct Adder { Adder(const char* s, const char* n, std::function<void()> f) { regs().push_back({s, n, f}); } };
 struct Msg { std::ostringstream os; template <class T> Msg& operator<<(const T& v) { os << v; return *this; } };
 struct Reporter { bool isFatal; const char* file; int line; const char* expr;
   void operator=(const Msg& m) const { ++failures(); std::printf("  FAILED %s:%d: %s %s\n", file, line, expr, m.os.str().c_str()); if (isFatal) fatal() = true; } };
}
namespace testing { using ::testing::Test; }
#define GT_CAT2(a,b) a##b
#define GT_CAT(a,b) GT_CAT2(a,b)
#define TEST(S,N) static void GT_CAT(S##_##N,_fn)(); static ::testing::Adder GT_CAT(S##_##N,_add)(#S,#N,GT_CAT(S##_##N,_fn)); static void GT_CAT(S##_##N,_fn)()
#define TEST_F(F,N) struct F##_##N : F { void TestBody() override; }; static ::testing::Adder GT_CAT(F##_##N,_add)(#F,#N,[]{ F##_##N t; t.RunIt(); ::testing::fatal()=false; }); void F##_##N::TestBody()
#define GT_CHECK(cond, fatalFlag, text) if (cond) ; else ::testing::Reporter{fatalFlag, __FILE__, __LINE__, text} = ::testing::Msg()
#define EXPECT_TRUE(c) GT_CHECK((c), false, #c)
#define EXPECT_FALSE(c) GT_CHECK(!(c), false, "!" #c)
#define ASSERT_TRUE(c) if (c) ; else return ::testing::Reporter{true, __FILE__, __LINE__, #c} = ::testing::Msg()
#define EXPECT_EQ(a,b) GT_CHECK(((a)==(b)), false, #a " == " #b)
#define EXPECT_NE(a,b) GT_CHECK(((a)!=(b)), false, #a " != " #b)
#define EXPECT_GT(a,b) GT_CHECK(((a)>(b)), false, #a " > " #b)
#define EXPECT_GE(a,b) GT_CHECK(((a)>=(b)), false, #a " >= " #b)
#define EXPECT_LT(a,b) GT_CHECK(((a)<(b)), false, #a " < " #b)
#define ASSERT_EQ(a,b) if ((a)==(b)) ; else return ::testing::Reporter{true, __FILE__, __LINE__, #a " == " #b} = ::testing::Msg()
#define ASSERT_GE(a,b) if ((a)>=(b)) ; else return ::testing::Reporter{true, __FILE__, __LINE__, #a " >= " #b} = ::testing::Msg()
namespace testing { inline int RunAll() { int ran = 0; for (auto& r : regs()) { int before = failures(); r.fn(); ++ran; std::printf("[%s] %s.%s\n", failures() == before ? " OK " : "FAIL", r.suite, r.name); } std::printf("%d tests, %d failed expectations\n", ran, failures()); return failures() ? 1 : 0; } }
#ifdef GT_SHIM_MAIN
int main() { return ::testing::RunAll(); }
#endif
