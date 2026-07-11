// Ground-truth demangle tests driven by samples.txt.
#include <string>

#include "samples.hpp"
#include "test_api.hpp"
#include "test_framework.hpp"

TEST(demangle_matches_sample) {
    for (const auto& s : samples()) {
        bool ok = false;
        std::string got = do_demangle(s.first.c_str(), ok);
        CHECK(ok);
        CHECK_EQ(got, s.second);
    }
}

TEST(substitution_resolves_element_type) {
    // A_S3_ (line 4, param 1) must resolve S3_ to System::Byte, i.e. an array of
    // System::Byte -- and, written via a substitution, render without the space
    // a spelled-out element gets.
    bool ok = false;
    std::string got = do_demangle(
        "_ZN10MethodInfo11CIECPNHFEEN11HPKKALNGNCBIA_N6System4ByteEEEvA_S3_A_"
        "N6System6ObjectE",
        ok);
    CHECK(ok);
    CHECK_EQ(got,
             std::string("MethodInfo::CIECPNHFEEN::HPKKALNGNCB<System::Byte []>"
                         "(System::Byte[],System::Object [])"));
}

TEST(template_function_return_type_is_not_rendered) {
    bool ok = false;
    std::string out = do_demangle(
        "_ZN10MethodInfo11CIECPNHFEEN11HPKKALNGNCBIbEEvbA_N6System6ObjectE", ok);
    CHECK(ok);
    CHECK(testing::starts_with(out, "MethodInfo::CIECPNHFEEN::HPKKALNGNCB<bool>("));
    CHECK(!testing::contains(out, "void"));
}

TEST(reference_and_array_spacing) {
    bool ok = false;
    std::string out = do_demangle(
        "_ZN10MethodInfo11CIECPNHFEEN11HDHLHLABIJLIN11UnityEngine7Vector3EEEvRN"
        "11UnityEngine7Vector3EA_N6System6ObjectE",
        ok);
    CHECK(ok);
    CHECK(testing::contains(out, "UnityEngine::Vector3 &"));  // reference: ' &'
    CHECK(testing::contains(out, "System::Object []"));  // spelled array: ' []'
}

TEST(invalid_inputs_fail) {
    const char* bad[] = {"", "not_mangled", "_Z", "_ZZZ", "_ZN3E", "_ZN1"};
    for (const char* b : bad) {
        bool ok = true;
        do_demangle(b, ok);
        CHECK(!ok);
    }
}
