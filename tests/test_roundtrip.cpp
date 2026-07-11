// Mangler tests: structural round-trip and byte-exact behaviour.
//
// The mangler emits canonical Itanium substitutions. The rigorous contract is a
// structural round-trip: re-mangling a parsed name yields a mangling that parses
// back to a structurally identical AST (structurally_equal). Byte-exact
// reproduction only holds when the source mangling already used canonical
// substitutions.
#include <string>

#include "mangly/mangly.hpp"
#include "samples.hpp"
#include "test_api.hpp"
#include "test_framework.hpp"

TEST(structural_roundtrip) {
    for (const auto& s : samples()) {
        mangly::Arena a1;
        const mangly::Node* ast = mangly::parse(s.first.c_str(), a1);
        CHECK(ast != nullptr);

        mangly::OutputBuffer mbuf;
        CHECK(mangly::mangle(ast, mbuf));

        mangly::Arena a2;
        const mangly::Node* re = mangly::parse(mbuf.c_str(), a2);
        CHECK(re != nullptr);
        CHECK(mangly::structurally_equal(re, ast));
    }
}

TEST(remangling_is_stable) {
    // Canonical mangling is idempotent: mangling the re-parsed AST is a fixpoint.
    for (const auto& s : samples()) {
        bool ok1 = false, ok2 = false;
        std::string once = do_remangle(s.first.c_str(), ok1);
        CHECK(ok1);
        std::string twice = do_remangle(once.c_str(), ok2);
        CHECK(ok2);
        CHECK_EQ(once, twice);
    }
}

TEST(remangled_output_is_valid) {
    // Whatever the mangler emits must itself be demangleable.
    for (const auto& s : samples()) {
        bool ok = false;
        std::string remangled = do_remangle(s.first.c_str(), ok);
        CHECK(ok);
        bool ok2 = false;
        std::string human = do_demangle(remangled.c_str(), ok2);
        CHECK(ok2);
        CHECK(!human.empty());
    }
}

TEST(byte_exact_when_source_is_canonical) {
    // Truly canonical Itanium manglings must reproduce byte-for-byte. Verified
    // against g++ / c++filt. (Note: the Unity samples that wrap a single-
    // component class type in a redundant N...E are NOT canonical; see
    // noncanonical_single_component_is_normalized below.)
    const char* canonical[] = {
        // Unity sample with only nested (multi-part) names -> already canonical.
        "_ZN10MethodInfo11CIECPNHFEEN11HPKKALNGNCBIbEEvbA_N6System6ObjectE",
        // g++-emitted canonical manglings (unscoped names bare, subs shared):
        "_Z1g3Foo",
        "_Z1k3FooS_",
        "_Z9free_ptrsPiPPcPKiPv",
        "_Z10free_substN5Alpha4Beta5GammaEPS1_RS1_",
        "_Z9free_tmpl3BoxIiES_IN5Alpha4Beta5GammaEE4PairIicE",
        "_ZN5Alpha4Beta6Widget6wobbleERKS1_OS1_",
    };
    for (const char* m : canonical) {
        bool ok = false;
        std::string re = do_remangle(m, ok);
        CHECK(ok);
        CHECK_EQ(re, std::string(m));
    }
}

TEST(noncanonical_single_component_is_normalized) {
    // IL2CPP wraps a single-component class type in a redundant N...E
    // (N11CIECPNHFEENE). We parse it, but re-mangle to the canonical bare form
    // (11CIECPNHFEEN); the two are structurally identical.
    const char* m =
        "_ZN10MethodInfo11LPLDGFJCAFP11ICCECIFBEMCEN11CIECPNHFEENEA_"
        "N6System6ObjectE";
    const char* canonical =
        "_ZN10MethodInfo11LPLDGFJCAFP11ICCECIFBEMCE11CIECPNHFEENA_"
        "N6System6ObjectE";
    bool ok = false;
    std::string re = do_remangle(m, ok);
    CHECK(ok);
    CHECK_EQ(re, std::string(canonical));

    mangly::Arena a1, a2;
    const mangly::Node* orig = mangly::parse(m, a1);
    const mangly::Node* norm = mangly::parse(re.c_str(), a2);
    CHECK(orig && norm);
    CHECK(mangly::structurally_equal(orig, norm));
}
