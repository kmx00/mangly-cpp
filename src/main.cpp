// mangly CLI entry point. cstdlib-only I/O: argv, a raw stdin read, the TTY
// probe, and fwrite. All behaviour lives in cli_run (mangly/cli.hpp).
#include <cstdio>

#include "mangly/cli.hpp"
#include "mangly/support.hpp"

#if defined(_WIN32)
#include <io.h>
static bool stdin_is_tty() { return _isatty(_fileno(stdin)) != 0; }
#else
#include <unistd.h>
static bool stdin_is_tty() { return isatty(fileno(stdin)) != 0; }
#endif

int main(int argc, char** argv) {
    const bool tty = stdin_is_tty();

    // Read stdin only when it will be consumed (no names, not a tty), so passing
    // names never blocks on the terminal.
    mangly::ParsedArgs pre;
    mangly::parse_args(argc, argv, pre);
    const bool need_stdin =
        !pre.error && !pre.help && !pre.version && pre.names.size() == 0 && !tty;

    mangly::OutputBuffer input;   // owns the stdin bytes for the tokenized lines
    mangly::Vec<const char*> lines;
    if (need_stdin) {
        char tmp[4096];
        std::size_t r;
        while ((r = std::fread(tmp, 1, sizeof tmp, stdin)) > 0) {
            input.append(tmp, r);
        }
        input.c_str();  // NUL-terminate for safe in-place tokenizing
        char* buf = input.mutable_data();
        std::size_t len = input.size();
        std::size_t start = 0;
        for (std::size_t p = 0; p <= len; ++p) {
            if (p == len || buf[p] == '\n') {
                if (p > start && buf[p - 1] == '\r') {
                    buf[p - 1] = '\0';  // tolerate CRLF-terminated pipes
                }
                if (p < len) buf[p] = '\0';
                lines.push(buf + start);
                start = p + 1;
            }
        }
    }

    mangly::OutputBuffer out;
    mangly::OutputBuffer err;
    int code = mangly::cli_run(argc, argv, lines.data(), lines.size(), tty, out,
                               err);

    if (out.size()) std::fwrite(out.data(), 1, out.size(), stdout);
    if (err.size()) std::fwrite(err.data(), 1, err.size(), stderr);
    return code;
}
