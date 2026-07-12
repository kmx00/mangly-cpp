// Stress / profiling test. Drives the whole demangle + remangle pipeline
// (parse -> render, parse -> mangle) over the entire ground-truth corpus for
// many passes, so a single case dominates runtime and a profiler attached to
// the test binary sees representative hot paths.
//
// It reuses one mangly::Demangler across every symbol -- the arena/buffer-reuse
// steady state the library is tuned for -- and still asserts correctness on
// every pass, so it is a real test, not a busy loop: each symbol must demangle
// to a non-empty string and remangle byte-for-byte back to the canonical input.
//
// Pass count defaults to a value that runs ~1s+ on a modern desktop core and is
// overridable with MANGLY_STRESS_PASSES for longer profiler sessions.
// MSVC flags std::getenv under /W4; it is standard C and safe here (single
// thread, read-only). Keep the cstdlib-only style rather than _dupenv_s.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "corpus_loader.hpp"
#include "mangly/mangly.hpp"
#include "test_framework.hpp"

namespace {
int stress_passes() {
    if (const char* env = std::getenv("MANGLY_STRESS_PASSES")) {
        const int n = std::atoi(env);
        if (n > 0) return n;
    }
    return 5000;  // ~1.3s over a 504-symbol corpus on a modern desktop core
}
}  // namespace

TEST(stress_demangle_remangle_corpus) {
    const auto syms = testing::load_corpus();
    CHECK(syms.size() > 100);

    const int passes = stress_passes();
    mangly::Demangler d;
    std::size_t demangled_chars = 0;  // keeps the demangle result observable

    for (int p = 0; p < passes; ++p) {
        for (const auto& m : syms) {
            const char* human = d.demangle(m.c_str());
            CHECK(human != nullptr);
            CHECK(human[0] != '\0');
            demangled_chars += std::strlen(human);

            const char* re = d.remangle(m.c_str());
            CHECK(re != nullptr);
            CHECK_EQ(std::string(re), m);
        }
    }

    // Defeat dead-store elimination of the demangle path: with a non-empty
    // corpus every pass appends characters, so the total is strictly positive.
    CHECK(demangled_chars > 0);
}
