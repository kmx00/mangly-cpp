// Targeted tests for the broadened grammar: operators, ctor/dtor, function
// types, pointer-to-member, and non-type literal template arguments. Manglings
// are real g++ output (see tools/gen_corpus.py); expected demangles are mangly's
// rendering style.
#include <string>
#include <utility>
#include <vector>

#include "test_api.hpp"
#include "test_framework.hpp"

namespace {
// (mangled, expected demangle) pairs, grounded on g++ output.
const std::pair<const char*, const char*> kCases[] = {
    {"_Z3fp1PFviE", "fp1(void (*)(int))"},
    {"_Z3fr1RFliE", "fr1(long (&)(int))"},
    {"_Z4ftt13BoxIFviEES_IFicdEE", "ftt1(Box<void (int)>,Box<int (char,double)>)"},
    {"_Z4ntt36SignedILin5EES_ILi7EE", "ntt3(Signed<-5>,Signed<7>)"},
    {"_Z4pmd1MN3ops3NumEi", "pmd1(int ops::Num::*)"},
    {"_Z4pmf2MN3ops3NumEFlicE", "pmf2(long (ops::Num::*)(int,char))"},
    {"_ZN3ops3NumC1El", "ops::Num::Num(long)"},
    {"_ZN3ops3NumD1Ev", "ops::Num::~Num()"},
    {"_ZN3ops3NumixEi", "ops::Num::operator[](int)"},
    {"_ZN3opsdvERKNS_3NumES2_",
     "ops::operator/(ops::Num const &,ops::Num const &)"},
    {"_ZN3opsli4_numEy", "ops::operator\"\" _num(unsigned long long)"},
    {"_ZNK3ops3NumcvbEv", "ops::Num::operator bool() const"},
    {"_ZNK3ops3NumplERKS0_", "ops::Num::operator+(ops::Num const &) const"},
    // function templates: signatures mangled with T_/T<n>_ (resolved to args)
    {"_Z4ft_aIiEvT_S0_", "ft_a<int>(int,int)"},
    {"_Z4ft_cIicEvT0_T_S0_PS1_", "ft_c<int,char>(char,int,char,int *)"},
    {"_Z4ft_fIcEvPA4_T_", "ft_f<char>(char (*)[4])"},
    // dependent expression template arguments (sizeof, binary op)
    {"_Z4ft_gIiEvP3TagIXstT_EE", "ft_g<int>(Tag<sizeof (int)> *)"},
    {"_Z4ft_hIdEvP3TagIXplstT_Li1EEE",
     "ft_h<double>(Tag<(sizeof (double))+(1)> *)"},
    // special names: vtable / typeinfo / typeinfo-name / thunks
    {"_ZTVN4poly3IfcE", "vtable for poly::Ifc"},
    {"_ZTIN4poly4ImplE", "typeinfo for poly::Impl"},
    {"_ZTSN4poly3AuxE", "typeinfo name for poly::Aux"},
    {"_ZThn8_N4poly4ImplD1Ev", "non-virtual thunk to poly::Impl::~Impl()"},
    {"_ZThn8_NK4poly4Impl4calcEi",
     "non-virtual thunk to poly::Impl::calc(int) const"},
    // decltype
    {"_Z4ft_jIiEvT_DTpltlS0_EtlS0_EE",
     "ft_j<int>(int,decltype ((int{})+(int{})))"},
    // local names, guard variables, lambdas, function params
    {"_ZZ7countervE1n", "counter()::n"},
    {"_ZGVZ7countervE1n", "guard variable for counter()::n"},
    {"_ZZ10use_lambdavENKUliE_clEi",
     "use_lambda()::{lambda(int)#1}::operator()(int) const"},
    {"_Z6invokeIZ10use_lambdavEUliE_EDTclfp_Li0EEET_",
     "invoke<use_lambda()::{lambda(int)#1}>(use_lambda()::{lambda(int)#1})"},
    // variadic templates (pack expansion / argument packs), abi-tags, casts
    {"_Z8variadicIJicdEEvDpT_", "variadic<int, char, double>(int, char, double)"},
    {"_Z11make_taggedB4tag1v", "make_tagged[abi:tag1]()"},
    {"_Z9tagged_fnB2v2i", "tagged_fn[abi:v2](int)"},
    {"_Z7castargIdEvP3TagIXcvistT_EE",
     "castarg<double>(Tag<(int)(sizeof (double))> *)"},
    {"_Z9countpackIJicEEvP3TagIXsZT_EE",
     "countpack<int, char>(Tag<sizeof...(int, char)> *)"},
    // fold expressions, construction vtables, VTT, virtual thunks, vectors
    {"_Z1fIJiiEEvDTflplfp_E", "f<int, int>(decltype ((...+{parm#1})))"},
    {"_Z1hIJiiEEvDTfLplLi1Efp_E", "h<int, int>(decltype ((1+...+{parm#1})))"},
    {"_ZTC4Leaf0_3Mid", "construction vtable for Mid-in-Leaf"},
    {"_ZTT4Leaf", "VTT for Leaf"},
    {"_ZTv0_n24_N3Mid1fEv", "virtual thunk to Mid::f()"},
    {"_Z7takevecDv4_i", "takevec(int __vector(4))"},
};
}  // namespace

TEST(grammar_demangles_render_correctly) {
    for (const auto& c : kCases) {
        bool ok = false;
        std::string got = do_demangle(c.first, ok);
        CHECK(ok);
        CHECK_EQ(got, std::string(c.second));
    }
}

TEST(grammar_remangles_byte_exact) {
    // All cases are canonical g++ manglings, so re-mangling reproduces them.
    for (const auto& c : kCases) {
        bool ok = false;
        std::string re = do_remangle(c.first, ok);
        CHECK_EQ(re, std::string(c.first));
    }
}

TEST(grammar_substitution_index_is_correct) {
    // Regression: a non-template nested function name (ops::operator/) must NOT
    // occupy a substitution slot, or S2_ would resolve one entry short and drop
    // the '&' on the second parameter.
    bool ok = false;
    std::string got = do_demangle("_ZN3opsdvERKNS_3NumES2_", ok);
    CHECK(ok);
    CHECK(testing::contains(got, "const &,ops::Num const &"));
}
