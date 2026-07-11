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
  - [x] structural round-trip for all samples; byte-exact for canonical input
- [x] canonicalization fix: unscoped single-component names/types emit bare
      (not `N...E`); non-canonical `N...E` normalizes on remangle
- [x] generated ground-truth corpus: `tools/gen_corpus.py` compiles weird-but-
      legal signatures with a real Itanium compiler and extracts `nm` symbols to
      `tests/corpus.txt`; every symbol parses + re-mangles byte-exact (420 syms)
- [x] broadened grammar (all validated byte-exact via the corpus):
  - [x] operator names (full table incl. `cv` conversion, `li` literal)
  - [x] constructor/destructor names (`C1/C2`, `D1/D2`)
  - [x] function types + pointer/reference to function (`F...E`, `PF..E`, `RF..E`)
  - [x] pointer-to-member (`M<class><pointee>`), data and function members
  - [x] non-type template-arg literals (`L<type><value>E`, incl. negatives)
  - [x] member-fn cv/ref-qualifiers on the nested-name (`NK..E`), rendered + kept
  - [x] substitution fix: a non-template nested *function* name no longer
        occupies a sub slot (only prefixes/template-prefixes/types do)
  - [x] **template parameters** `T_`/`T<n>_` (function-template signatures),
        substitutable, resolved to their template args for rendering
  - [x] **dependent expression template args** `X<expr>E`: `sizeof`/`alignof`
        (type & expr), unary/binary/ternary operators, `tl`/`il`/`cl` lists
  - [x] function-template-id is `S_` (the template-id, not the bare name)
  - [x] **special names**: vtable (`TV`), typeinfo (`TI`), typeinfo-name (`TS`),
        VTT (`TT`), and thunks (`Th`/`Tv`/`Tc`) wrapping a base encoding
  - [x] **decltype** (`Dt`/`DT`) types (substitutable)
  - [x] **local names** (`Z <encoding> E <entity>` + discriminators), **guard
        variables** (`GV`), **lambda/unnamed closure types** (`Ul..E.._`/`Ut.._`),
        and **function-parameter expressions** (`fp_`)
  - [x] **variadic templates**: pack expansion (`Dp`), argument packs (`J..E`),
        `sizeof...` (`sZ`); **abi-tags** (`B`); expression **member access**
        (`dt`/`pt`) and **casts** (`cv`)
  - [x] **fold expressions** (`fl`/`fr`/`fL`/`fR`), **construction vtables**
        (`TC`), **virtual thunks** (`Tv`), **vector types** (`Dv`),
        **vendor-extended types** (`u`), and **template-param prefixes**
        (`typename T::type`)
  - [x] **pointer / function-pointer non-type template args** (`L <mangled-name>
        E`, address-of `ad`), **named casts** (`sc`/`dc`/`cc`/`rc`), **scope
        resolution** (`sr`), **`new`** (`nw`/`na`), **pack-expansion-in-
        expression** (`sp`), and the **nullptr literal** (`LDnE`)
- [x] `mangly` CLI (args/stdin; `-r/--remangle`), cstdlib I/O
- [x] pure-C++ test harness (no framework dep); builds+passes on MSVC and g++

### notes / known limits
- Grammar covered: essentially the full Itanium `<encoding>` surface produced by
  g++/clang -- nested names, all template-argument forms (type/literal/
  expression/pack), builtins & vendor/vector types, pointer/reference/array/cv/
  function/member-pointer types, operators, ctor/dtor, template parameters,
  special names (vtable/VTT/typeinfo/construction-vtable/thunks), decltype, local
  names/lambdas/guard variables, fold expressions, abi-tags, and substitutions.
- NOT yet (all rare): `delete` (`dl`/`da`) and `typeid` (`ti`/`te`) expressions,
  placement-`new` initializers, and unusual vendor extensions.
- Pattern pack-expansion (e.g. `Dp P T_`) and `sizeof...` render approximately
  (byte-exact remangle is unaffected).
- Template return type parsed but not rendered; array element spacing is
  presentation-only (`Elem[]` for a substitution element, `Elem []` otherwise).

## next
- v0.0.2: broaden CLI (batch/CSV modes), harden fuzz coverage, and add the last
  rare `<expression>` leaves (`delete`, `typeid`) as grounded samples appear. The
  demangler already covers what g++/clang emit for real-world code.
- keep growing `tools/gen_corpus.py` (it gates each construct behind a real-
  compiler byte-exact check).
