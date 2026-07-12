// Correctness/robustness hardening intended as the regression net around
// performance work on the parser, mangler, and the hand-rolled Arena /
// OutputBuffer / Vec. Every case here is self-validating (no hand-authored
// expected strings) and is most valuable when the binary is built with
// -DMANGLY_SANITIZE=address (or address+undefined): the loops then double as
// out-of-bounds / UB sweeps over the whole corpus and over pathological inputs.
#include <cstdio>
#include <string>
#include <vector>

#include "corpus_loader.hpp"
#include "mangly/mangly.hpp"
#include "test_api.hpp"
#include "test_framework.hpp"

namespace {

// Demangle+remangle a symbol with a FRESH Demangler (own arena+buffer).
struct Pair {
    std::string demangled;  // empty if demangle failed
    std::string remangled;  // empty if remangle failed
    bool demangle_ok;
    bool remangle_ok;
};
Pair fresh_pair(const std::string& m) {
    mangly::Demangler d;
    Pair p{};
    const char* h = d.demangle(m.c_str());  // valid until next call on d
    p.demangle_ok = h != nullptr;
    if (h) p.demangled = h;
    const char* r = d.remangle(m.c_str());
    p.remangle_ok = r != nullptr;
    if (r) p.remangled = r;
    return p;
}

std::string source_name(const std::string& id) {
    return std::to_string(id.size()) + id;
}

}  // namespace

// Regression: a local name with NO discriminator (e.g. a variable declared once
// in a function body) parses with an empty StringView discriminator whose data
// pointer is null. The mangler appends that discriminator verbatim, so it must
// not hand memcpy a null source -- which is UB (C 7.24.1/2) even for length 0
// and is caught by UBSan (glibc annotates memcpy nonnull; CI's clang asan+ubsan
// job flags it). Output is byte-exact either way, so only a sanitizer sees the
// defect: this pins the exact triggering symbol as a named case.
TEST(local_name_without_discriminator_round_trips) {
    const char* const cases[] = {
        "_ZZ5tallyiE1a",    // int a; in tally(int)
        "_ZZ7countervE1n",  // n in counter()
    };
    for (const char* m : cases) {
        bool ok = false;
        std::string human = do_demangle(m, ok);
        CHECK(ok);
        CHECK(!human.empty());
        bool rok = false;
        std::string re = do_remangle(m, rok);
        CHECK(rok);
        CHECK_EQ(re, std::string(m));
    }
}

// The library's headline optimization is a reused Demangler that rewinds its
// arena and output buffer instead of reallocating. A single reused instance
// must produce byte-identical results to a per-symbol fresh instance -- and
// must do so regardless of what it processed before, or Arena::reset() /
// OutputBuffer::clear() are leaking state. We prove order-independence by
// driving the reused instance forward AND backward over the corpus and
// comparing every result to the fresh baseline.
TEST(reuse_matches_fresh_instance) {
    const auto syms = testing::load_corpus();
    CHECK(syms.size() > 100);

    std::vector<Pair> baseline;
    baseline.reserve(syms.size());
    for (const auto& m : syms) baseline.push_back(fresh_pair(m));

    mangly::Demangler d;  // one instance, reused across every call below
    for (std::size_t i = 0; i < syms.size(); ++i) {
        const char* h = d.demangle(syms[i].c_str());
        CHECK(h != nullptr);  // corpus is all-valid
        CHECK_EQ(std::string(h), baseline[i].demangled);
        const char* r = d.remangle(syms[i].c_str());
        CHECK(r != nullptr);
        CHECK_EQ(std::string(r), baseline[i].remangled);
    }
    // Reverse order: same reused instance, results must still match the fresh
    // baseline (no dependence on the previously processed symbol).
    for (std::size_t i = syms.size(); i-- > 0;) {
        const char* h = d.demangle(syms[i].c_str());
        CHECK(h != nullptr);
        CHECK_EQ(std::string(h), baseline[i].demangled);
        const char* r = d.remangle(syms[i].c_str());
        CHECK(r != nullptr);
        CHECK_EQ(std::string(r), baseline[i].remangled);
    }
}

// Feed every proper prefix of every corpus symbol to the parser. Truncated
// manglings are the cheapest high-coverage fuzz: they drive every partial
// production to the point the input ends, which is exactly where a missing
// bounds check reads past the buffer. The contract is weak by design -- parse
// may succeed or fail -- but it must never crash or read out of bounds (caught
// by ASan) and success must yield non-empty output.
TEST(truncated_prefixes_never_crash) {
    const auto syms = testing::load_corpus();
    std::size_t checked = 0;
    for (const auto& m : syms) {
        for (std::size_t len = 1; len < m.size(); ++len) {
            const std::string prefix = m.substr(0, len);
            bool ok = false;
            std::string human = do_demangle(prefix.c_str(), ok);
            if (ok) CHECK(!human.empty());
            bool rok = false;
            std::string re = do_remangle(prefix.c_str(), rok);
            if (rok) CHECK(!re.empty());
            ++checked;
        }
    }
    CHECK(checked > 1000);  // the sweep actually ran over a real corpus
}

// Deeply nested pointer type: f(int*******...). Stresses recursion depth in the
// parser, renderer, and mangler simultaneously. Each level is a single
// substitutable component with no back-reference, so the canonical mangling is
// the input verbatim -- a byte-exact round-trip at depth. Depth is kept well
// inside a default 1 MiB stack for the ~one-frame-per-level recursion.
TEST(deep_pointer_nesting_round_trips) {
    const int depth = 512;
    std::string m = "_Z1f";
    for (int i = 0; i < depth; ++i) m += "P";
    m += "i";  // innermost: int

    bool ok = false;
    std::string human = do_demangle(m.c_str(), ok);
    CHECK(ok);
    CHECK(!human.empty());
    CHECK(testing::starts_with(human, "f("));

    bool rok = false;
    std::string re = do_remangle(m.c_str(), rok);
    CHECK(rok);
    CHECK_EQ(re, m);  // canonical: verbatim round-trip
}

// Wide substitution table: f(C000*, C001*, ..., C(N-1)*) -- N params, each a
// pointer to a uniquely named class. Every class and every pointer is a first
// occurrence, so the table grows to ~2N distinct entries and the mangler runs
// ~2N structurally_equal scans against a growing table. This is the O(n^2)
// tripwire: a change that makes per-component matching super-linear turns this
// case cubic and it will visibly stall. Byte-exact because nothing repeats.
TEST(wide_substitution_table_round_trips) {
    const int n = 400;
    std::string m = "_Z1f";
    for (int i = 0; i < n; ++i) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "C%03d", i);
        m += "P" + source_name(buf);  // pointer to class "C###"
    }

    bool ok = false;
    std::string human = do_demangle(m.c_str(), ok);
    CHECK(ok);
    CHECK(testing::contains(human, "C000"));
    CHECK(testing::contains(human, "C399"));

    bool rok = false;
    std::string re = do_remangle(m.c_str(), rok);
    CHECK(rok);
    CHECK_EQ(re, m);  // no repeats -> no back-refs -> verbatim
}
