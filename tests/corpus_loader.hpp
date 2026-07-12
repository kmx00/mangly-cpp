// Shared corpus loader. tests/corpus.txt holds canonical Itanium manglings
// emitted by a real compiler (see tools/gen_corpus.py); lines that do not start
// with "_Z" are comments and are skipped. Kept in one place so the ground-truth
// corpus tests and the stress test read the file identically.
#ifndef MANGLY_TEST_CORPUS_LOADER_HPP
#define MANGLY_TEST_CORPUS_LOADER_HPP

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MANGLY_CORPUS_PATH
#error "MANGLY_CORPUS_PATH must be defined by the build"
#endif

namespace testing {

inline std::vector<std::string> load_corpus() {
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

}  // namespace testing

#endif  // MANGLY_TEST_CORPUS_LOADER_HPP
