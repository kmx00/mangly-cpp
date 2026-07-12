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
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <utility>
#include <tuple>
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

// Polymorphic hierarchy with non-virtual multiple inheritance: emits vtable
// (TV), typeinfo (TI), typeinfo-name (TS), ctors/dtors, and non-virtual thunks
// (Th) for the secondary base.
namespace poly {
  struct Ifc { virtual void run(); virtual ~Ifc(); };
  struct Aux { virtual long calc(int) const; virtual ~Aux(); };
  struct Impl : Ifc, Aux {
    void run() override;
    long calc(int) const override;
    ~Impl() override;
  };
  void Ifc::run() {}
  Ifc::~Ifc() {}
  long Aux::calc(int) const { return 0; }
  Aux::~Aux() {}
  void Impl::run() {}
  long Impl::calc(int) const { return 0; }
  Impl::~Impl() {}
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
        "template <class T> void ft_j(T, decltype(T{} + T{})) {}",
        "template void ft_j<int>(int, int);",
    ])

    # 8) local names, guard variables, and lambdas (closure types + fp_).
    lines.extend([
        "int side_effect();",
        "int& counter() { static int n = side_effect(); return n; }",
        "long& tally(int) { static long a = side_effect();"
        " static long b = side_effect(); return b ? a : a; }",
        "template <class F> auto invoke(F f) -> decltype(f(0)) { return f(0); }",
        "int use_lambda() { auto g = [](int x) { return x + 1; };"
        " return invoke(g); }",
    ])

    # 9) variadic templates (Dp pack expansion, J argument packs, sizeof...),
    #    abi-tags, and dependent expressions (member access, cast).
    lines.extend([
        "template <class... Ts> void variadic(Ts...) {}",
        "template void variadic<int, char, double>(int, char, double);",
        "template <class T, class... Ts> void mixed(T, Ts*...) {}",
        "template void mixed<int, char, double>(int, char*, double*);",
        "template <class... Ts> void countpack(Tag<sizeof...(Ts)>*) {}",
        "template void countpack<int, char>(Tag<2>*);",
        'struct __attribute__((abi_tag("tag1"))) Tagged { int v; };',
        "Tagged make_tagged() { return {}; }",
        'int __attribute__((abi_tag("v2"))) tagged_fn(int x) { return x; }',
        "struct HasField { int field; };",
        "template <class T> auto memacc(T t) -> decltype(t.field)"
        " { return t.field; }",
        "template int memacc<HasField>(HasField);",
        "template <class T> void castarg(Tag<(int)sizeof(T)>*) {}",
        "template void castarg<double>(Tag<8>*);",
    ])

    # 10) fold expressions, scope-resolution (template-param) prefixes, vector
    #     types, and virtual inheritance (construction vtables / VTT / v-thunks).
    lines.extend([
        "template <class... Ts> auto uleft(Ts... ts) -> decltype((... + ts))"
        " { return (... + ts); }",
        "template int uleft<int, int, int>(int, int, int);",
        "template <class... Ts> auto uright(Ts... ts) -> decltype((ts + ...))"
        " { return (ts + ...); }",
        "template int uright<int, int>(int, int);",
        "template <class... Ts> auto bleft(Ts... ts) -> decltype((0 + ... + ts))"
        " { return (0 + ... + ts); }",
        "template int bleft<int, int>(int, int);",
        "struct Holder { typedef int type; };",
        "template <class T> typename T::type deref() { return {}; }",
        "template int deref<Holder>();",
        "typedef int v4si __attribute__((vector_size(16)));",
        "void takevec(v4si) {}",
        "struct VBase { virtual void f(); virtual ~VBase(); };",
        "struct Mid : virtual VBase { void f() override; ~Mid() override; };",
        "struct Leaf : Mid { void f() override; ~Leaf() override; };",
        "void VBase::f() {} VBase::~VBase() {}",
        "void Mid::f() {} Mid::~Mid() {}",
        "void Leaf::f() {} Leaf::~Leaf() {}",
    ])

    # 11) pointer / function-pointer non-type template args (L <mangled-name> E,
    # address-of), and dependent expressions: named casts (sc/dc), scope
    # resolution (sr), new (nw), pack-expansion-in-call (sp), nullptr literal.
    lines.extend([
        "void target();",
        "int global_obj;",
        "template <void (*)()> struct FnPtr {};",
        "template <int*> struct ObjPtr {};",
        "void use_fnptr(FnPtr<&target>) {}",
        "void use_objptr(ObjPtr<&global_obj>) {}",
        "template <void (*F)()> void call_it() { F(); }",
        "template void call_it<&target>();",
        "template <class T> auto scast(T t) -> decltype(static_cast<long>(t))"
        " { return static_cast<long>(t); }",
        "struct Cv { operator long() const; };",
        "template long scast<Cv>(Cv);",
        "template <class T> auto srv() -> decltype(T::value) { return T::value; }",
        "struct HasValue { static int value; };",
        "template int srv<HasValue>();",
        "template <class T> auto newexpr() -> decltype(new T) { return new T; }",
        "template int* newexpr<int>();",
        "int callee(int, int);",
        "template <class... Ts> auto packcall(Ts... ts)"
        " -> decltype(callee(ts...)) { return callee(ts...); }",
        "template int packcall<int, int>(int, int);",
        "template <decltype(nullptr)> struct Null {};",
        "void use_null(Null<nullptr>) {}",
    ])

    # 12) delete expressions (dl/da) and the global-scope prefix (gs) on
    # new/delete, all in dependent (decltype) contexts.
    lines.extend([
        "template <class T> auto del(T* p) -> decltype(delete p) { delete p; }",
        "template void del<int>(int*);",
        "template <class T> auto delarr(T* p) -> decltype(delete[] p)"
        " { delete[] p; }",
        "template void delarr<int>(int*);",
        "template <class T> auto gnew() -> decltype(::new T) { return ::new T; }",
        "template int* gnew<int>();",
        "template <class T> auto gdel(T* p) -> decltype(::delete p) { ::delete p; }",
        "template void gdel<int>(int*);",
        "template <class T> auto gdelarr(T* p) -> decltype(::delete[] p)"
        " { ::delete[] p; }",
        "template void gdelarr<int>(int*);",
    ])

    # 13) std:: substitutions (St/Sa/Ss + __cxx11 abi-tag namespaces), C++17
    # noexcept function types (Do), and a reference temporary (_ZGR).
    lines.extend([
        "void take_ne(void (*)() noexcept) {}",
        "int call_ne(int (*p)(int) noexcept) { return p(0); }",
        "template <class T> void tfn_ne(T (*)() noexcept) {}",
        "template void tfn_ne<int>(int (*)() noexcept);",
        "const int& g_ref_temp = 42;",
        "std::string make_str() { return {}; }",
        "void take_vec(std::vector<int>) {}",
        "void take_vstr(std::vector<std::string>) {}",
        "std::basic_string<char>& pick_bstr(std::basic_string<char>& s)"
        " { return s; }",
    ])

    # 14) REVIEW PHASE 1 diversification: exhaustive builtins, the full operator
    # table, cv/restrict qualifiers, arrays & member pointers, ref-qualified and
    # cv member functions, varargs, function-returning-function-pointer, deeply
    # nested templates, dependent-noexcept (DO), and more std containers.
    lines.extend([
        # -- dependent-noexcept function types (DO) --
        "template <bool B> void ne_dep(void (*)() noexcept(B)) {}",
        "template void ne_dep<true>(void (*)() noexcept(true));",
        "template <class T> void ne_dep2(void (*)() noexcept(sizeof(T) > 0)) {}",
        "template void ne_dep2<int>(void (*)() noexcept(sizeof(int) > 0));",
        # -- every builtin type as a parameter --
        "void b_all(wchar_t, char16_t, char32_t, signed char, unsigned char,"
        " unsigned short, long long, unsigned long long, long double) {}",
        "void b_ext(__int128, unsigned __int128, __float128) {}",
        "void b_more(bool, short, unsigned int, unsigned long) {}",
        # -- full operator table (free functions on a POD) --
        "struct Op { int v; };",
        "Op operator%(Op, Op) { return {}; }",
        "Op operator^(Op, Op) { return {}; }",
        "Op operator&(Op, Op) { return {}; }",
        "Op operator|(Op, Op) { return {}; }",
        "Op operator~(Op) { return {}; }",
        "bool operator!(Op) { return false; }",
        "Op operator<<(Op, int) { return {}; }",
        "Op operator>>(Op, int) { return {}; }",
        "Op& operator+=(Op& a, Op) { return a; }",
        "Op& operator-=(Op& a, Op) { return a; }",
        "Op& operator*=(Op& a, Op) { return a; }",
        "Op& operator/=(Op& a, Op) { return a; }",
        "Op& operator%=(Op& a, Op) { return a; }",
        "Op& operator^=(Op& a, Op) { return a; }",
        "Op& operator&=(Op& a, Op) { return a; }",
        "Op& operator|=(Op& a, Op) { return a; }",
        "Op& operator<<=(Op& a, int) { return a; }",
        "Op& operator>>=(Op& a, int) { return a; }",
        "bool operator<=(Op, Op) { return false; }",
        "bool operator>=(Op, Op) { return false; }",
        "bool operator&&(Op, Op) { return false; }",
        "bool operator||(Op, Op) { return false; }",
        "Op& operator--(Op& a) { return a; }",
        "Op operator--(Op& a, int) { (void)a; return {}; }",
        "Op* operator,(Op, Op) { return nullptr; }",
        # -- cv / restrict qualifiers --
        "void cv_ptrs(const int*, volatile int*, const volatile int*) {}",
        "void cv_restrict(int* __restrict, const char* __restrict) {}",
        # -- arrays: pointer-to-array, array-of-pointers, ref-to-array --
        "void arr_p2a(int (*)[3][4]) {}",
        "void arr_aop(char* (*)[5]) {}",
        "void arr_r2a(int (&)[10]) {}",
        # -- member data / function pointers --
        "struct MC { int m; void f(int); long g(char) const; };",
        "void mp_data(int MC::*) {}",
        "void mp_fn(void (MC::*)(int)) {}",
        "void mp_cfn(long (MC::*)(char) const) {}",
        # -- ref-qualified and cv member functions --
        "struct RQ { void lref() &; void rref() &&; void cf() const;"
        " void vf() volatile; void cvf() const volatile; };",
        "void RQ::lref() & {}",
        "void RQ::rref() && {}",
        "void RQ::cf() const {}",
        "void RQ::vf() volatile {}",
        "void RQ::cvf() const volatile {}",
        # -- perfect forwarding (rvalue ref collapse) --
        "template <class T> void fwd(T&&) {}",
        "template void fwd<int&>(int&);",
        "template void fwd<int>(int&&);",
        # -- varargs ellipsis + function returning function pointer --
        "void va_fn(int, ...) {}",
        "void (*ret_fp())(int, char) { return nullptr; }",
        # -- deeply nested templates + deep namespace --
        "void nested_tmpl(Box<Box<Box<int> > >) {}",
        "void deep_ns(ns1::ns2::ns3::Deeper) {}",
        # -- more std containers --
        "void s_map(std::map<int, std::string>) {}",
        "void s_pair(std::pair<int, double>) {}",
        "void s_shared(std::shared_ptr<int>) {}",
        "void s_tuple(std::tuple<int, char, double>) {}",
    ])

    # 15) REVIEW PHASE 2 diversification: enums (plain + scoped + fixed base),
    # unions, anonymous-namespace types, template-template parameters, _Complex
    # types, non-type template params (enum / pointer / reference), references to
    # functions and arrays of function pointers, and substitution back-ref stress.
    lines.extend([
        # -- enums (plain, scoped, fixed underlying type) --
        "enum Color { Red, Green };",
        "enum class Dir { North, South };",
        "enum Sized : unsigned char { Lo, Hi };",
        "void e_plain(Color) {}",
        "void e_scoped(Dir) {}",
        "void e_sized(Sized) {}",
        # -- union --
        "union U { int i; float f; };",
        "void u_fn(U*, const U&) {}",
        # -- anonymous namespace type (internal-linkage name) --
        "namespace { struct Hidden {}; }",
        "void anon_use(Hidden*) {}",
        # -- template-template parameter --
        "template <template <class> class TT> struct TTHolder {};",
        "void tt_use(TTHolder<Box>) {}",
        # -- _Complex builtin types (Cf/Cd/Ce) --
        "void cplx(float _Complex, double _Complex, long double _Complex) {}",
        # -- non-type template params: enum value, object pointer, reference --
        "template <Color C> struct EnumNTTP {};",
        "void ent(EnumNTTP<Red>) {}",
        "int g_int;",
        "template <int& R> struct RefNTTP {};",
        "void rnt(RefNTTP<g_int>) {}",
        "template <int* P> struct PtrNTTP {};",
        "void pnt(PtrNTTP<&g_int>) {}",
        # -- reference to function, array of function pointers --
        "void fref(void (&)(int)) {}",
        "void afp(void (*(*)[4])(int)) {}",
        # -- substitution back-reference stress (repeated composite types) --
        "void subs_stress(Box<ns1::Inner>, Box<ns1::Inner>*,"
        " Pair<Box<ns1::Inner>, Box<ns1::Inner> >) {}",
        # -- pointer to member of a template class --
        "void mp_tmpl(int Box<int>::*) {}",
        # -- nested template with a dependent member type in a signature --
        "template <class T> struct Wrap { struct Inner {}; };",
        "template <class T> void wr(typename Wrap<T>::Inner) {}",
        "template void wr<int>(Wrap<int>::Inner);",
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
        # Any DEFINED symbol (skip 'U'/'u' undefined refs) -> keeps functions,
        # vtables (V), typeinfo (V/R), thunks (T/W), etc.
        if typ not in ("U", "u") and name.startswith("_Z"):
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
