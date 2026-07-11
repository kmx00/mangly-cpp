// Ground-truth corpus tests. tests/corpus.txt holds canonical Itanium manglings
// emitted by a real compiler (see tools/gen_corpus.py). Every symbol must parse,
// demangle to something, and re-mangle byte-for-byte -- a rigorous check of the
// parser + mangler that needs no hand-authored expected strings.
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_api.hpp"
#include "test_framework.hpp"

#ifndef MANGLY_CORPUS_PATH
#error "MANGLY_CORPUS_PATH must be defined by the build"
#endif

namespace {
std::vector<std::string> load_corpus() {
    std::ifstream f(MANGLY_CORPUS_PATH, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open corpus: " MANGLY_CORPUS_PATH);
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    std::vector<std::string> syms;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("_Z", 0) == 0) syms.push_back(line);  // skip comments
    }
    return syms;
}
}  // namespace

TEST(corpus_parses_and_demangles) {
    auto syms = load_corpus();
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
    auto syms = load_corpus();
    for (const auto& m : syms) {
        bool ok = false;
        std::string re = do_remangle(m.c_str(), ok);
        CHECK_EQ(re, m);
    }
}
