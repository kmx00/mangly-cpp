# current status, checkboxes

## v0.0.1 - DONE
- [x] header-only library (`include/mangly/*.hpp`), consumed via CMake INTERFACE
      target `mangly::mangly` (FetchContent/CPM friendly)
- [x] cstdlib-only core: no `<string>/<vector>/<memory>/<unordered_map>/<iostream>`
  - [x] bump `Arena` for the AST (raw pointers, no per-node malloc, no refcounts)
  - [x] `realloc`-growable `OutputBuffer`; minimal `Vec<T>` for pointer lists
- [x] no RTTI (`Kind` tag, not `<typeinfo>`); enforced with `-fno-rtti`/`/GR-`
- [x] no exceptions in the library; parse returns `nullptr`. CLI built with
      `-fno-exceptions` (GCC/Clang) to prove the header is EH-free
- [x] Itanium demangler: recursive-descent parser + substitution table + AST
  - [x] demangles all `samples.txt` rows exactly
- [x] canonical mangler (AST -> mangled), substitution matching by
      `structurally_equal` (no key strings, no hash map)
  - [x] structural round-trip for all samples
  - [x] byte-exact for canonical input
- [x] canonicalization fix: unscoped single-component names/types emit bare
      (not `N...E`), matching g++/clang; a non-canonical `N...E` normalizes on
      remangle (found by the generated corpus; invisible to the Unity samples)
- [x] generated ground-truth corpus: `tools/gen_corpus.py` compiles weird-but-
      legal signatures with a real Itanium compiler and extracts `nm` symbols to
      `tests/corpus.txt`; every symbol parses + re-mangles byte-exact (325 syms)
- [x] broadened grammar (all validated byte-exact via the corpus):
  - [x] operator names (full table incl. `cv` conversion, `li` literal)
  - [x] constructor/destructor names (`C1/C2`, `D1/D2`)
  - [x] function types + pointer/reference to function (`F...E`, `PF..E`, `RF..E`)
  - [x] pointer-to-member (`M<class><pointee>`), data and function members
  - [x] non-type template-arg literals (`L<type><value>E`, incl. negatives)
  - [x] member-fn cv/ref-qualifiers on the nested-name (`NK..E`), rendered + kept
  - [x] substitution fix: a non-template nested *function* name no longer
        occupies a sub slot (only prefixes/template-prefixes/types do)
- [x] `mangly` CLI (args/stdin; `-r/--remangle`), cstdlib I/O
- [x] pure-C++ test harness (no framework dep); builds+passes on MSVC and g++

### notes / known limits
- Grammar covered: nested names, template-ids (type + non-type literal args),
  builtins, pointer/reference/array/cv types, function types, pointer-to-member,
  operator and ctor/dtor names, substitutions. NOT yet: general `<expression>`
  template args (only literals), vendor/local extensions.
- Template return type parsed but not rendered; array element spacing is
  presentation-only (`Elem[]` for a substitution element, `Elem []` otherwise).

## next
- v0.0.2 - general `<expression>` template arguments (the remaining large item;
  add operators/forms incrementally as grounded corpus rows demand them), plus
  local names / lambdas / vendor-extended types as samples appear.
- grow `tools/gen_corpus.py` as the grammar widens (it already gates every new
  construct behind a real-compiler byte-exact check).
