// Minimal self-registering test framework (no third-party dependencies).
//
// TEST(name) { ... } defines and registers a case. Assertions throw Failure,
// which run_all() reports. CHECK_THROWS verifies an expression raises a given
// exception type. run_all() returns 0 iff every case passed.
#ifndef MANGLY_TEST_FRAMEWORK_HPP
#define MANGLY_TEST_FRAMEWORK_HPP

#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace testing {

struct Failure {
    std::string msg;
};

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline std::string to_text(const std::string& s) { return "\"" + s + "\""; }
inline std::string to_text(const char* s) { return std::string("\"") + s + "\""; }
inline std::string to_text(bool v) { return v ? "true" : "false"; }
inline std::string to_text(int v) { return std::to_string(v); }
inline std::string to_text(unsigned v) { return std::to_string(v); }
inline std::string to_text(long v) { return std::to_string(v); }
inline std::string to_text(unsigned long v) { return std::to_string(v); }
inline std::string to_text(long long v) { return std::to_string(v); }
inline std::string to_text(unsigned long long v) { return std::to_string(v); }

inline bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Trim leading/trailing ASCII whitespace (mirrors Python str.strip()).
inline std::string strip(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
               c == '\v';
    };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline int run_all() {
    int passed = 0;
    int failed = 0;
    for (auto& t : registry()) {
        using clock = std::chrono::steady_clock;
        const auto start = clock::now();
        bool ok = false;
        std::string detail;
        try {
            t.fn();
            ok = true;
        } catch (const Failure& f) {
            detail = f.msg;
        } catch (const std::exception& e) {
            detail = std::string("unexpected exception: ") + e.what();
        } catch (...) {
            detail = "unexpected unknown exception";
        }
        const double ms =
            std::chrono::duration<double, std::milli>(clock::now() - start)
                .count();
        if (ok) {
            ++passed;
            std::printf("[PASS] %s (%.3f ms)\n", t.name.c_str(), ms);
        } else {
            ++failed;
            std::printf("[FAIL] %s (%.3f ms)\n       %s\n", t.name.c_str(), ms,
                        detail.c_str());
        }
    }
    std::printf("\n%d passed, %d failed, %d total\n", passed, failed,
                passed + failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace testing

#define MANGLY_CONCAT_(a, b) a##b
#define MANGLY_CONCAT(a, b) MANGLY_CONCAT_(a, b)

#define TEST(name)                                                       \
    static void name();                                                  \
    static ::testing::Registrar MANGLY_CONCAT(reg_, name)(#name, name);  \
    static void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            throw ::testing::Failure{std::string(__FILE__) + ":" +           \
                                     std::to_string(__LINE__) +              \
                                     ": CHECK failed: " #cond};              \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        auto&& mangly_va = (a);                                              \
        auto&& mangly_vb = (b);                                              \
        if (!(mangly_va == mangly_vb)) {                                     \
            throw ::testing::Failure{                                        \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) +     \
                ": CHECK_EQ failed\n         lhs = " +                       \
                ::testing::to_text(mangly_va) + "\n         rhs = " +        \
                ::testing::to_text(mangly_vb)};                             \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr, ExcType)                                          \
    do {                                                                     \
        bool mangly_threw = false;                                           \
        try {                                                                \
            (void)(expr);                                                    \
        } catch (const ExcType&) {                                           \
            mangly_threw = true;                                             \
        }                                                                    \
        if (!mangly_threw) {                                                 \
            throw ::testing::Failure{std::string(__FILE__) + ":" +           \
                                     std::to_string(__LINE__) +              \
                                     ": expected " #ExcType " from " #expr}; \
        }                                                                    \
    } while (0)

#endif  // MANGLY_TEST_FRAMEWORK_HPP
