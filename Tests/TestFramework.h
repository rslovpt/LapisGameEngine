#pragma once
// Minimal self-registering unit test framework (no external dependencies).
//
//     LAPIS_TEST(Math, Dot) { CHECK_NEAR(Dot(a, b), 3.0f); }

#include <cmath>
#include <cstdio>
#include <exception>
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

// Optional text sink so tools (LapisDiagnostics) can capture results.
inline std::string*& ReportSink() { static std::string* sink = nullptr; return sink; }

inline void Emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    if (ReportSink()) *ReportSink() += line + "\n";
}

inline void ReportFailure(const char* file, int line, const std::string& what) {
    std::string f = file;
    const size_t slash = f.find_last_of("/\\");
    if (slash != std::string::npos) f = f.substr(slash + 1);
    Emit("    FAILED " + f + ":" + std::to_string(line) + ": " + what);
    ++FailureCount();
}

struct Summary { int Total = 0, Failed = 0; };

inline Summary RunAll(std::string* report = nullptr) {
    ReportSink() = report;
    Summary s;
    for (const TestCase& t : Registry()) {
        const int before = FailureCount();
        try {
            t.Fn();
        } catch (const std::exception& e) {
            ReportFailure(t.Suite, 0, std::string("uncaught exception: ") + e.what());
        } catch (...) {
            ReportFailure(t.Suite, 0, "uncaught unknown exception");
        }
        const bool ok = FailureCount() == before;
        ++s.Total;
        if (!ok) ++s.Failed;
        Emit(std::string("[") + (ok ? " OK " : "FAIL") + "] " + t.Suite + "." + t.Name);
    }
    Emit("\n" + std::to_string(s.Total) + " tests, " + std::to_string(s.Failed) + " failed");
    ReportSink() = nullptr;
    return s;
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
