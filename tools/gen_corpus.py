#!/usr/bin/env python3
"""Generate a real Itanium-mangled test corpus.

Emits a dummy translation unit full of weird-but-legal function signatures,
compiles it with a real Itanium C++ compiler, and extracts the mangled symbol
names with ``nm``. The result (one mangled name per line) is a ground-truth
corpus of *canonical* manglings: our parser must accept every line and our
mangler must reproduce it byte-for-byte.

Scope is deliberately confined to the grammar mangly-cpp supports today:
builtins, nested class types, class-template-ids with concrete type arguments,
pointers/references/rvalue-references/arrays/cv-qualifiers, and substitutions.
It intentionally avoids function/member pointers and function templates (which
mangle with `F...E`, `M`, and `T_` forms not yet handled).

Usage:
    python3 tools/gen_corpus.py [output_path]
Environment:
    CXX  C++ compiler (default: g++, then clang++)
    NM   symbol lister (default: nm, then llvm-nm)
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------------- type building

BUILTINS = [
    "int", "char", "bool", "double", "long long", "unsigned char", "float",
    "short", "unsigned int", "long", "unsigned long", "wchar_t", "long double",
    "unsigned short", "signed char", "unsigned long long",
]

CLASS_TYPES = [
    "TopClass", "ns1::Inner", "ns1::ns2::Deep", "ns1::ns2::ns3::Deeper",
]

TEMPLATE_TYPES = [
    "Box<int>", "Box<TopClass>", "Box<ns1::ns2::Deep>",
    "Pair<int,char>", "Pair<TopClass,ns1::Inner>", "Trio<int,char,bool>",
    "Box<Box<int> >", "Pair<Box<int>,ns1::ns2::Deep>",
    "Box<Pair<int,ns1::Inner> >", "Trio<Box<int>,TopClass,ns1::ns2::ns3::Deeper>",
]

VALUE_TYPES = BUILTINS + CLASS_TYPES + TEMPLATE_TYPES

# Modifiers: each maps a value-type spelling to a legal, in-grammar param type.
MODS = [
    ("val", lambda t: t),
    ("ptr", lambda t: t + "*"),
    ("ptrptr", lambda t: t + "**"),
    ("cptr", lambda t: "const " + t + "*"),
    ("cref", lambda t: "const " + t + "&"),
    ("rref", lambda t: t + "&&"),
    ("cvptr", lambda t: "const volatile " + t + "*"),
    ("refarr", lambda t: t + "(&)[4]"),
]

PREAMBLE = """\
struct TopClass {};
namespace ns1 {
  struct Inner {};
  namespace ns2 {
    struct Deep {};
    namespace ns3 { struct Deeper {}; }
  }
}
template <class T> struct Box {};
template <class T, class U> struct Pair {};
template <class T, class U, class V> struct Trio {};

namespace app {
  struct Service {
    void handle(int, TopClass*);
    void mix(const ns1::ns2::Deep&, Box<int>, ns1::Inner**);
    void repeat(TopClass, TopClass*, const TopClass&, TopClass&&);
  };
}
void app::Service::handle(int, TopClass*) {}
void app::Service::mix(const ns1::ns2::Deep&, Box<int>, ns1::Inner**) {}
void app::Service::repeat(TopClass, TopClass*, const TopClass&, TopClass&&) {}

// Non-type template parameters (exercise L<type><value>E literal args).
template <int N> struct FixedArr {};
template <bool B> struct Toggle {};
template <unsigned long U> struct Tag {};
template <int N> struct Signed {};

// Operators, constructors, destructor (no virtuals -> no vtable symbols).
namespace ops {
  struct Num {
    long v;
    Num();
    Num(long);
    Num(const Num&);
    ~Num();
    Num operator+(const Num&) const;
    Num operator-(const Num&) const;
    Num operator*(const Num&) const;
    Num& operator=(const Num&);
    bool operator==(const Num&) const;
    bool operator<(const Num&) const;
    Num& operator++();
    Num operator++(int);
    long operator()(int, char) const;
    long& operator[](int);
    operator long() const;
    operator bool() const;
  };
  Num::Num() {}
  Num::Num(long) {}
  Num::Num(const Num&) {}
  Num::~Num() {}
  Num Num::operator+(const Num&) const { return {}; }
  Num Num::operator-(const Num&) const { return {}; }
  Num Num::operator*(const Num&) const { return {}; }
  Num& Num::operator=(const Num&) { return *this; }
  bool Num::operator==(const Num&) const { return false; }
  bool Num::operator<(const Num&) const { return false; }
  Num& Num::operator++() { return *this; }
  Num Num::operator++(int) { return {}; }
  long Num::operator()(int, char) const { return 0; }
  long& Num::operator[](int) { return v; }
  Num::operator long() const { return 0; }
  Num::operator bool() const { return false; }
  Num operator/(const Num&, const Num&) { return {}; }
  bool operator!=(const Num&, const Num&) { return false; }
  Num operator""_num(unsigned long long) { return {}; }
}
"""


def emit_functions() -> list[str]:
    """Deterministically emit function definitions covering the grammar."""
    lines: list[str] = []
    n = 0

    def fn(params: list[str]) -> None:
        nonlocal n
        lines.append(f"void f{n}({', '.join(params)}) {{}}")
        n += 1

    # 1) each value type by value, and under each modifier.
    for t in VALUE_TYPES:
        for _, mod in MODS:
            fn([mod(t)])

    # 2) substitution stress: the same base type in several forms in one list.
    for t in CLASS_TYPES + TEMPLATE_TYPES:
        fn([t, t + "*", "const " + t + "&", t + "&&"])
        fn([t + "*", t + "*", t])  # repeated pointer -> shared substitution

    # 3) wide mixed builtin lists.
    fn(BUILTINS[:8])
    fn(BUILTINS[4:12])
    fn(list(reversed(BUILTINS[:10])))

    # 4) mixed value types across families.
    for i in range(len(CLASS_TYPES)):
        a = CLASS_TYPES[i]
        b = TEMPLATE_TYPES[i % len(TEMPLATE_TYPES)]
        c = BUILTINS[i % len(BUILTINS)]
        fn([a, b, c + "*", "const " + a + "&"])

    # 5) namespaced free functions (nested-name encodings for the *name*).
    lines.append("namespace deep { namespace inner {")
    for i, t in enumerate(VALUE_TYPES[:12]):
        lines.append(f"  void g{i}({t}, {t}*) {{}}")
    lines.append("} }")

    # 6) operators, function pointers, member pointers, non-type template args.
    lines.extend([
        "void fp1(void (*)(int)) {}",
        "void fp2(int (*)(char, double), void (*)()) {}",
        "void fp3(int (*)(int, int), int (*)(int, int)) {}",
        "void fr1(long (&)(int)) {}",
        "void pmd1(int ops::Num::*) {}",
        "void pmd2(long ops::Num::*, long ops::Num::*) {}",
        "void pmf1(void (ops::Num::*)()) {}",
        "void pmf2(long (ops::Num::*)(int, char)) {}",
        "void ntt1(FixedArr<7>, Toggle<false>, Tag<1000>) {}",
        "void ntt2(Box<FixedArr<3> >, Toggle<true>) {}",
        "void ftt1(Box<void (int)>, Box<int (char, double)>) {}",
        "void ntt3(Signed<-5>, Signed<7>) {}",
    ])

    # 7) function templates: signatures mangled with T_ template-param refs,
    #    plus dependent expression (sizeof) template arguments.
    lines.extend([
        "template <class T> void ft_a(T, T) {}",
        "template void ft_a<int>(int, int);",
        "template <class T> void ft_b(T*, const T&, T&&) {}",
        "template void ft_b<double>(double*, const double&, double&&);",
        "template <class T, class U> void ft_c(U, T, U, T*) {}",
        "template void ft_c<int, char>(char, int, char, int*);",
        "template <class T> T* ft_d(const T&) { return nullptr; }",
        "template TopClass* ft_d<TopClass>(const TopClass&);",
        "template <class T> void ft_e(Box<T>, T, T*) {}",
        "template void ft_e<ns1::Inner>(Box<ns1::Inner>, ns1::Inner, ns1::Inner*);",
        "template <class T> void ft_f(T (*)[4]) {}",
        "template void ft_f<char>(char (*)[4]);",
        "template <class T> void ft_g(Tag<sizeof(T)>*) {}",
        "template void ft_g<int>(Tag<sizeof(int)>*);",
        "template <class T> void ft_h(Tag<sizeof(T) + 1>*) {}",
        "template void ft_h<double>(Tag<sizeof(double) + 1>*);",
    ])

    return lines


def write_source(path: Path) -> None:
    body = "\n".join(emit_functions())
    path.write_text(PREAMBLE + "\n" + body + "\n", encoding="utf-8")


# --------------------------------------------------------------------- toolchain

def which(candidates: list[str], env: str) -> str:
    val = os.environ.get(env)
    if val:
        return val
    for c in candidates:
        if shutil.which(c):
            return c
    sys.exit(f"error: none of {candidates} found (set ${env})")


def extract_symbols(obj: Path, nm: str) -> list[str]:
    out = subprocess.run([nm, str(obj)], check=True, capture_output=True,
                         text=True).stdout
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        typ, name = parts[-2], parts[-1]
        # text/code symbols only (T/t defined, W/w weak) -> skip vtables,
        # typeinfo, guard variables, and undefined refs.
        if typ in ("T", "t", "W", "w") and name.startswith("_Z"):
            syms.add(name)
    return sorted(syms)


def main() -> int:
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parents[1] / "tests" / "corpus.txt"
    cxx = which(["g++", "clang++"], "CXX")
    nm = which(["nm", "llvm-nm"], "NM")

    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "dummy.cpp"
        obj = Path(td) / "dummy.o"
        write_source(src)
        subprocess.run([cxx, "-std=c++17", "-c", str(src), "-o", str(obj)],
                       check=True)
        syms = extract_symbols(obj, nm)

    header = (
        "# Ground-truth Itanium corpus generated by tools/gen_corpus.py.\n"
        f"# compiler: {cxx}\n"
        "# One canonical mangled name per line. Regenerate; do not hand-edit.\n"
    )
    out_path.write_text(header + "\n".join(syms) + "\n", encoding="utf-8")
    print(f"wrote {len(syms)} symbols to {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
