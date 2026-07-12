// mangly - fast, header-only, cstdlib-only de/remangling of Itanium C++ ABI
// mangled names. No RTTI, no exceptions, no general-purpose-allocator STL.
//
// Low-level (bring your own buffers/arena):
//   parse(mangled[, len], arena) -> const Node*   (nullptr on failure)
//   demangle(mangled[, len], out) -> bool         (renders human form)
//   mangle(node, out) -> bool                     (canonical mangling)
//
// High-level (reuses one arena + buffer across calls):
//   Demangler d;
//   const char* s = d.demangle(name);   // nullptr on failure; valid until next call
//   const char* m = d.remangle(name);
#ifndef MANGLY_MANGLY_HPP
#define MANGLY_MANGLY_HPP

#include <cstdint>
#include <cstring>  // std::strlen in the const char* overloads

#include "mangly/mangler.hpp"
#include "mangly/nodes.hpp"
#include "mangly/parser.hpp"
#include "mangly/support.hpp"

namespace mangly {

inline constexpr const char* version = "0.0.1";

// Render the human-readable form of `mangled` into `out`. Returns false if the
// input is not a valid (supported) mangled name.
inline bool demangle(const char* mangled, std::uint32_t len, OutputBuffer& out) {
    Arena arena;
    const Node* node = parse(mangled, len, arena);
    if (!node) return false;
    render(node, out);
    return !out.failed();
}
inline bool demangle(const char* mangled, OutputBuffer& out) {
    return demangle(mangled, static_cast<std::uint32_t>(std::strlen(mangled)), out);
}

// A reusable de/remangler. The arena and output buffer persist across calls and
// are rewound each time, so processing many names allocates almost nothing in
// steady state. Returned pointers are owned by the Demangler and valid only
// until the next call on the same instance.
class Demangler {
public:
    Demangler() : parser_(arena_), mangler_(out_) {}

    const char* demangle(const char* mangled, std::uint32_t len) {
        arena_.reset();
        out_.clear();
        parser_.reset_input(mangled, len);
        const Node* node = parser_.parse();
        if (!node) return nullptr;
        render(node, out_);
        return out_.failed() ? nullptr : out_.c_str();
    }
    const char* demangle(const char* mangled) {
        return demangle(mangled, static_cast<std::uint32_t>(std::strlen(mangled)));
    }

    const char* remangle(const char* mangled, std::uint32_t len) {
        arena_.reset();
        out_.clear();
        parser_.reset_input(mangled, len);
        const Node* node = parser_.parse();
        if (!node) return nullptr;
        return mangler_.mangle(node) ? out_.c_str() : nullptr;
    }
    const char* remangle(const char* mangled) {
        return remangle(mangled, static_cast<std::uint32_t>(std::strlen(mangled)));
    }

private:
    // Declaration order matters: arena_/out_ must outlive the parser_/mangler_
    // that hold references to them.
    Arena arena_;
    OutputBuffer out_;
    Parser parser_;
    Mangler mangler_;
};

}  // namespace mangly

#endif  // MANGLY_MANGLY_HPP
