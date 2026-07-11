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
