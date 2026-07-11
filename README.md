# mangly-cpp

Fast, header-only de/remangling of Itanium C++ ABI mangled names, plus a bundled
`mangly` CLI. A C++17 reimagining of [mangly](../mangly).

Design constraints (see `devdocs/plans.md`):

- **Header-only**, dependency-free, **cstdlib only** — the library headers pull
  in `<cstdlib>/<cstring>/<cstdint>/<cstddef>` and nothing else. No `<string>`,
  `<vector>`, `<memory>`, `<unordered_map>`, `<iostream>`, or `<format>`, which
  keeps object-code emission lean and portable.
- **No RTTI** — node identity is a `Kind` tag, never `<typeinfo>`/`dynamic_cast`.
- **No exceptions** — parsing reports failure by returning `nullptr`.
- **Fast** — the AST is bump-allocated from an `Arena` (raw pointers, no
  per-node `malloc`, no atomic refcounts); substitutions share nodes via raw
  pointers; text is built into a `realloc`-growable `OutputBuffer`. The mangler
  matches substitutions by structural equality — no key strings, no hash map.

The build compiles with `-fno-rtti`/`/GR-` everywhere and the CLI with
`-fno-exceptions` (GCC/Clang), so the constraints are enforced, not just
intended.

## Use as a library

FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(mangly GIT_REPOSITORY <url> GIT_TAG <rev>)
FetchContent_MakeAvailable(mangly)
target_link_libraries(your_target PRIVATE mangly::mangly)
```

CPM:

```cmake
CPMAddPackage("gh:kmx00/mangly-cpp@0.0.1")
target_link_libraries(your_target PRIVATE mangly::mangly)
```

Then:

```cpp
#include <mangly/mangly.hpp>

// Reusable: one arena + one buffer, rewound each call (near-zero steady-state
// allocation when processing many names). Returned pointer is valid until the
// next call on the same Demangler.
mangly::Demangler d;
const char* human = d.demangle(mangled);   // nullptr on invalid input
// "MethodInfo::CIECPNHFEEN::HPKKALNGNCB<bool>(bool,System::Object [])"
const char* canon = d.remangle(mangled);   // canonical re-mangling (invalidates `human`)

// Low-level: bring your own arena/buffer.
mangly::Arena arena;
const mangly::Node* ast = mangly::parse(mangled, arena);  // nullptr on failure
mangly::OutputBuffer out;
if (ast && mangly::mangle(ast, out)) { /* out.c_str() is the mangling */ }

// Structural equality (substitution-independent) of two ASTs:
bool same = mangly::structurally_equal(a, b);
```

`parse` returns AST nodes whose `StringView`s reference the input `mangled`
buffer, which must outlive them. The strings returned by `Demangler` are owned
copies and outlive the input.

## Build

Requires CMake (>= 3.16) and a C++17 compiler.

```
./build.sh              # configure, build, run tests
./build.sh --skip-tests # configure and build only
./build.sh --clean      # remove the build dir first
```

Or drive CMake directly:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## CLI

Demangle names passed as arguments:

```
$ mangly _ZN10MethodInfo11CIECPNHFEEN11HPKKALNGNCBIbEEvbA_N6System6ObjectE
MethodInfo::CIECPNHFEEN::HPKKALNGNCB<bool>(bool,System::Object [])
```

Read names from stdin, one per line:

```
$ printf '%s\n' _ZN10MethodInfo11LPLDGFJCAFP11IHKGBJCNIFFEN11CIECPNHFEENEA_N6System6ObjectE | mangly
MethodInfo::LPLDGFJCAFP::IHKGBJCNIFF(CIECPNHFEEN,System::Object [])
```

Print the canonical re-mangling instead of the demangled form:

```
$ mangly -r _ZN10MethodInfo11CIECPNHFEEN11HPKKALNGNCBIbEEvbA_N6System6ObjectE
_ZN10MethodInfo11CIECPNHFEEN11HPKKALNGNCBIbEEvbA_N6System6ObjectE
```

With no arguments on a terminal, usage is printed. Non-mangled inputs are echoed
unchanged.

## Notes / known limits

- Grammar covered: nested names, template-ids (type, literal, and dependent
  expression arguments), builtins, pointer/reference/rvalue-reference/array/cv
  types, function types and pointers/references to them, pointer-to-member,
  operator names, constructor/destructor names, template parameters (`T_`),
  special names (vtable/typeinfo/typeinfo-name/VTT/thunks), `decltype`, local
  names, guard variables, lambda/unnamed closure types, function-parameter
  expressions, and substitutions. Not yet handled: pack expansion, vendor/
  abi-tag extensions, and the remaining `<expression>` forms (casts, scope
  resolution, member access, folds).
- The template return type is parsed but intentionally not rendered (matches the
  source tool: template functions drop the leading return type).
- Array element spacing is presentation-only and follows the source tool:
  substitution element -> `Elem[]`, spelled-out element -> `Elem []`.
- The mangler emits the strict ABI-canonical form (matches `g++`/`clang++`):
  unscoped single-component names are bare, only genuinely nested names use
  `N...E`. Byte-exact remangling holds for canonical input; a non-canonical
  producer's output (e.g. IL2CPP wrapping a single-component type in a redundant
  `N...E`) is normalized to the canonical form, which re-parses to an identical
  AST (verified via `structurally_equal`).

## Tests

Pure C++ (no test framework dependency); `ctest` runs the suite. Two grounded
corpora drive it:

- `samples.txt` (`MangledName-:-UnmangledName`) pins the human-readable demangle
  style.
- `tests/corpus.txt` is a machine-generated ground truth: `tools/gen_corpus.py`
  emits weird-but-legal signatures, compiles them with a real Itanium compiler
  (`g++`/`clang++`), and extracts the mangled symbols with `nm`. Every symbol
  must parse and re-mangle byte-for-byte, so the parser and canonical mangler are
  checked against a real compiler at scale without hand-authoring. Regenerate
  with `python3 tools/gen_corpus.py` (needs `$CXX` + `nm`).

## License

MIT. See LICENSE.
