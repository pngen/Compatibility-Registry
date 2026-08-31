#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

namespace testutil {
inline int& failures() { static int n = 0; return n; }
inline int& checks() { static int n = 0; return n; }
inline const char*& current() { static const char* c = ""; return c; }
inline void set_current(const char* n) { current() = n; }

inline void report(bool ok, const char* expr, const char* file, int line) {
    checks()++;
    if (!ok) {
        failures()++;
        std::printf("[FAIL] %s:%d  %s  (%s)\n", file, line, expr, current());
    }
}

inline int finish() {
    if (failures() == 0) { std::printf("[PASS] all %d checks passed\n", checks()); return 0; }
    std::printf("[FAIL] %d / %d checks failed\n", failures(), checks());
    return 1;
}
} // namespace testutil

#define CHECK(cond) do { testutil::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__); } while (0)
#define TEST(name) do { testutil::set_current(name); } while (0)
#define RUN_TESTS() return testutil::finish()
