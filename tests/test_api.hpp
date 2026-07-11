// Test-only convenience bridges to std::string. The shipped library is
// cstdlib-only; tests are free to use the STL for ergonomic assertions.
#ifndef MANGLY_TEST_API_HPP
#define MANGLY_TEST_API_HPP

#include <string>

#include "mangly/mangly.hpp"

// Demangle into a std::string; `ok` reports success.
inline std::string do_demangle(const char* m, bool& ok) {
    mangly::OutputBuffer o;
    ok = mangly::demangle(m, o);
    return ok ? std::string(o.data(), o.size()) : std::string();
}

// Canonical remangle into a std::string; `ok` reports success.
inline std::string do_remangle(const char* m, bool& ok) {
    mangly::Arena arena;
    const mangly::Node* n = mangly::parse(m, arena);
    mangly::OutputBuffer o;
    ok = n && mangly::mangle(n, o);
    return ok ? std::string(o.data(), o.size()) : std::string();
}

#endif  // MANGLY_TEST_API_HPP
