#pragma once
// Minimal self-registering unit test framework (no external dependencies).
//
//     LAPIS_TEST(Math, Dot) { CHECK_NEAR(Dot(a, b), 3.0f); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace LapisTest {

struct TestCase {
    const char* Suite;
    const char* Name;
    std::function<void()> Fn;
};

inline std::vector<TestCase>& Registry() { static std::vector<TestCase> r; return r; }
inline int& FailureCount() { static int n = 0; return n; }

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        Registry().push_back({suite, name, std::move(fn)});
    }
};

inline void ReportFailure(const char* file, int line, const std::string& what) {
    std::printf("    FAILED %s:%d: %s\n", file, line, what.c_str());
    ++FailureCount();
}

inline int RunAll() {
    int failedTests = 0;
    for (const TestCase& t : Registry()) {
        const int before = FailureCount();
        t.Fn();
        const bool ok = FailureCount() == before;
        if (!ok) ++failedTests;
        std::printf("[%s] %s.%s\n", ok ? " OK " : "FAIL", t.Suite, t.Name);
    }
    std::printf("\n%zu tests, %d failed\n", Registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace LapisTest

#define LAPIS_TEST_CONCAT2(a, b) a##b
#define LAPIS_TEST_CONCAT(a, b) LAPIS_TEST_CONCAT2(a, b)

#define LAPIS_TEST(suite, name)                                                            \
    static void LAPIS_TEST_CONCAT(Test_##suite##_, name)();                                \
    static ::LapisTest::Registrar LAPIS_TEST_CONCAT(Reg_##suite##_, name)(                 \
        #suite, #name, &LAPIS_TEST_CONCAT(Test_##suite##_, name));                         \
    static void LAPIS_TEST_CONCAT(Test_##suite##_, name)()

#define CHECK(cond)                                                                        \
    do { if (!(cond)) ::LapisTest::ReportFailure(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_NEAR(a, b)                                                                   \
    do {                                                                                   \
        const double va_ = (a), vb_ = (b);                                                 \
        if (std::fabs(va_ - vb_) > 1e-3)                                                   \
            ::LapisTest::ReportFailure(__FILE__, __LINE__,                                 \
                std::string(#a " ~= " #b "  (") + std::to_string(va_) + " vs " +           \
                std::to_string(vb_) + ")");                                                \
    } while (0)
