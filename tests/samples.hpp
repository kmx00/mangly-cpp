// Loads the ground-truth samples file.
//
// samples.txt uses "-:-" as the record delimiter (a plain CSV was unusable
// because unmangled names contain commas). Trailing tab-separated columns
// (original symbol, C type) are ignored: only the first field after the
// delimiter is the demangled name under test. MANGLY_SAMPLES_PATH is injected
// by CMake as the absolute path to the repo's samples.txt.
#ifndef MANGLY_TEST_SAMPLES_HPP
#define MANGLY_TEST_SAMPLES_HPP

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef MANGLY_SAMPLES_PATH
#error "MANGLY_SAMPLES_PATH must be defined by the build"
#endif

namespace samples_detail {

inline bool is_blank(const std::string& s) {
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\f' && c != '\v') {
            return false;
        }
    }
    return true;
}

inline std::vector<std::pair<std::string, std::string>> load() {
    const char* path = MANGLY_SAMPLES_PATH;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error(std::string("cannot open samples: ") + path);
    }
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    std::vector<std::pair<std::string, std::string>> pairs;
    const std::string delim = "-:-";
    std::size_t pos = 0;
    int lineno = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        ++lineno;
        if (is_blank(line)) {
            continue;
        }
        std::size_t d = line.find(delim);
        if (d == std::string::npos) {
            throw std::runtime_error("samples line " + std::to_string(lineno) +
                                     " missing '-:-': " + line);
        }
        std::string mangled = line.substr(0, d);
        std::string rest = line.substr(d + delim.size());
        if (lineno == 1) {
            if (mangled != "MangledName" || rest != "UnmangledName") {
                throw std::runtime_error("unexpected samples header: " + line);
            }
            continue;  // header row
        }
        std::string unmangled = rest.substr(0, rest.find('\t'));
        pairs.emplace_back(std::move(mangled), std::move(unmangled));
    }
    if (pairs.empty()) {
        throw std::runtime_error("no samples loaded");
    }
    return pairs;
}

}  // namespace samples_detail

// Cached (mangled, unmangled) pairs from samples.txt.
inline const std::vector<std::pair<std::string, std::string>>& samples() {
    static const std::vector<std::pair<std::string, std::string>> data =
        samples_detail::load();
    return data;
}

#endif  // MANGLY_TEST_SAMPLES_HPP
