#pragma once

// Minimal dependency-free test harness: AP_TEST registers a case, CHECK/CHECK_NEAR
// record failures without aborting so one run reports everything.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace aptest
{

struct Case { std::string name; std::function<void()> fn; };

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures()               { static int f = 0; return f; }
inline int& checks()                 { static int c = 0; return c; }

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
};

inline void fail (const char* file, int line, const std::string& msg)
{
    ++failures();
    std::printf ("  FAIL %s:%d  %s\n", file, line, msg.c_str());
}

inline int runAll()
{
    for (auto& c : registry())
    {
        const int before = failures();
        std::printf ("[ RUN  ] %s\n", c.name.c_str());
        c.fn();
        std::printf ("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", c.name.c_str());
    }
    std::printf ("\n%d checks, %d failures, %zu cases\n", checks(), failures(), registry().size());
    return failures() == 0 ? 0 : 1;
}

} // namespace aptest

#define AP_TEST(name) \
    static void name##_body(); \
    static aptest::Registrar name##_reg (#name, name##_body); \
    static void name##_body()

#define CHECK(cond) \
    do { ++aptest::checks(); if (! (cond)) aptest::fail (__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { ++aptest::checks(); const double _a = (double) (a), _b = (double) (b); \
         if (std::fabs (_a - _b) > (double) (tol)) \
             aptest::fail (__FILE__, __LINE__, std::string ("CHECK_NEAR(" #a ", " #b ") got ") + std::to_string (_a) + " vs " + std::to_string (_b)); } while (0)

#define CHECK_EQ(a, b) \
    do { ++aptest::checks(); if (! ((a) == (b))) \
             aptest::fail (__FILE__, __LINE__, std::string ("CHECK_EQ(" #a ", " #b ") got ") + std::to_string (a) + " vs " + std::to_string (b)); } while (0)
