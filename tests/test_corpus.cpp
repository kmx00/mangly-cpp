// Ground-truth corpus tests. tests/corpus.txt holds canonical Itanium manglings
// emitted by a real compiler (see tools/gen_corpus.py). Every symbol must parse,
// demangle to something, and re-mangle byte-for-byte -- a rigorous check of the
// parser + mangler that needs no hand-authored expected strings.
#include <string>
#include <vector>

#include "corpus_loader.hpp"
#include "test_api.hpp"
#include "test_framework.hpp"

TEST(corpus_parses_and_demangles) {
    auto syms = testing::load_corpus();
    CHECK(syms.size() > 100);  // a meaningful corpus, not an empty file
    for (const auto& m : syms) {
        bool ok = false;
        std::string human = do_demangle(m.c_str(), ok);
        CHECK(ok);
        CHECK(!human.empty());
    }
}

TEST(corpus_remangles_byte_exact) {
    // Compiler output is canonical, so our canonical mangler must reproduce it
    // exactly. do_remangle returns "" on failure, so a mismatch names the symbol.
    auto syms = testing::load_corpus();
    for (const auto& m : syms) {
        bool ok = false;
        std::string re = do_remangle(m.c_str(), ok);
        CHECK_EQ(re, m);
    }
}
