// CLI behaviour tests. cli_run performs the full flow into caller buffers, so
// stdin and TTY state are supplied explicitly (no real I/O).
#include <string>

#include "mangly/cli.hpp"
#include "samples.hpp"
#include "test_framework.hpp"

using mangly::cli_run;
using mangly::OutputBuffer;
using testing::strip;

namespace {
// Run the CLI over an argv list; capture stdout/stderr text and the exit code.
struct Run {
    int code;
    std::string out;
    std::string err;
};
Run run_cli(std::initializer_list<const char*> argv,
            std::initializer_list<const char*> stdin_lines, bool tty) {
    const char* argv_arr[8];
    int argc = 0;
    for (const char* a : argv) argv_arr[argc++] = a;
    const char* lines_arr[8];
    std::uint32_t nlines = 0;
    for (const char* l : stdin_lines) lines_arr[nlines++] = l;

    OutputBuffer out, err;
    int code = cli_run(argc, argv_arr, lines_arr, nlines, tty, out, err);
    return Run{code, std::string(out.c_str()), std::string(err.c_str())};
}
}  // namespace

TEST(cli_demangles_argument) {
    const auto& s = samples()[0];
    Run r = run_cli({"mangly", s.first.c_str()}, {}, /*tty=*/false);
    CHECK_EQ(r.code, 0);
    CHECK_EQ(strip(r.out), s.second);
}

TEST(cli_passes_through_non_mangled) {
    Run r = run_cli({"mangly", "definitely_not_a_symbol"}, {}, false);
    CHECK_EQ(r.code, 0);
    CHECK_EQ(strip(r.out), std::string("definitely_not_a_symbol"));
}

TEST(cli_remangle_roundtrips) {
    const std::string mangled = samples()[0].first;
    Run r = run_cli({"mangly", "--remangle", mangled.c_str()}, {}, false);
    CHECK_EQ(r.code, 0);
    std::string remangled = strip(r.out);
    // Re-mangling the CLI output must reproduce the demangled form of the input.
    Run r2 = run_cli({"mangly", remangled.c_str()}, {}, false);
    CHECK_EQ(r2.code, 0);
    CHECK_EQ(strip(r2.out), samples()[0].second);
}

TEST(cli_reads_stdin) {
    const auto& s = samples()[1];
    Run r = run_cli({"mangly"}, {s.first.c_str()}, /*tty=*/false);
    CHECK_EQ(r.code, 0);
    CHECK_EQ(strip(r.out), s.second);
}

TEST(cli_skips_blank_stdin_lines) {
    const auto& s = samples()[1];
    Run r = run_cli({"mangly"}, {"", "   ", s.first.c_str(), "\t"}, false);
    CHECK_EQ(r.code, 0);
    CHECK_EQ(strip(r.out), s.second);  // only the one real name is processed
}

TEST(cli_no_args_on_tty_prints_usage) {
    Run r = run_cli({"mangly"}, {}, /*tty=*/true);
    CHECK_EQ(r.code, 2);
    CHECK_EQ(r.out, std::string(""));
    CHECK(testing::contains(r.err, "usage: mangly"));
}

TEST(cli_help_flag) {
    Run r = run_cli({"mangly", "--help"}, {}, false);
    CHECK_EQ(r.code, 0);
    CHECK(testing::contains(r.out, "usage: mangly"));
}

TEST(cli_unrecognized_option_errors) {
    Run r = run_cli({"mangly", "--nope"}, {}, false);
    CHECK_EQ(r.code, 2);
    CHECK(testing::contains(r.err, "unrecognized arguments: --nope"));
}

TEST(cli_version_flag) {
    Run r = run_cli({"mangly", "--version"}, {}, false);
    CHECK_EQ(r.code, 0);
    CHECK(testing::contains(r.out, "mangly "));
    CHECK(testing::contains(r.out, mangly::version));
}
