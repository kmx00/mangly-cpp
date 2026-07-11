// Command-line interface for mangly.
//
// Reads mangled names from arguments or, if none are given, from stdin (one per
// line). By default prints the demangled form; with -r/--remangle prints the
// canonical re-mangling of each parsed name instead. cstdlib-only and no-throw:
// results are written into caller OutputBuffers so the flow is testable without
// touching real I/O.
#ifndef MANGLY_CLI_HPP
#define MANGLY_CLI_HPP

#include <cstdint>
#include <cstring>

#include "mangly/mangly.hpp"
#include "mangly/support.hpp"

namespace mangly {

namespace cli_detail {

inline const char* kUsage = "usage: mangly [-h] [-r] [names ...]\n";

inline const char* kHelp =
    "usage: mangly [-h] [-r] [names ...]\n"
    "\n"
    "De/remangle Itanium C++ ABI mangled names.\n"
    "\n"
    "positional arguments:\n"
    "  names           mangled names (default: read stdin)\n"
    "\n"
    "options:\n"
    "  -h, --help      show this help message and exit\n"
    "  -r, --remangle  print the canonical re-mangling instead of the "
    "demangled form\n";

inline bool is_blank(const char* s) {
    for (; *s; ++s) {
        char c = *s;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\f' && c != '\v') {
            return false;
        }
    }
    return true;
}

}  // namespace cli_detail

struct ParsedArgs {
    bool remangle = false;
    bool help = false;
    bool error = false;
    const char* error_arg = nullptr;
    Vec<const char*> names;  // point into argv (must outlive use)
};

// Parse the argv tail, mirroring the Python argparse surface.
inline void parse_args(int argc, const char* const* argv, ParsedArgs& out) {
    for (int k = 1; k < argc; ++k) {
        const char* a = argv[k];
        if (std::strcmp(a, "-r") == 0 || std::strcmp(a, "--remangle") == 0) {
            out.remangle = true;
        } else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            out.help = true;
        } else if (a[0] == '-' && a[1] != '\0') {
            out.error = true;
            out.error_arg = a;  // argparse aborts on the first unknown option
            return;
        } else {
            out.names.push(a);  // includes a lone "-"
        }
    }
}

// De/remangle one name into `out` (no trailing newline). A non-mangled input is
// echoed unchanged, like c++filt.
inline void cli_process(const char* name, bool remangle, Demangler& d,
                        OutputBuffer& out) {
    const char* r = remangle ? d.remangle(name) : d.demangle(name);
    out.append(r ? r : name);
}

// Full CLI behaviour. `stdin_lines` supplies names when none are positional,
// unless `stdin_is_tty` is true (then usage -> `err`, exit 2). Returns exit code.
inline int cli_run(int argc, const char* const* argv,
                   const char* const* stdin_lines, std::uint32_t nlines,
                   bool stdin_is_tty, OutputBuffer& out, OutputBuffer& err) {
    ParsedArgs pa;
    parse_args(argc, argv, pa);

    if (pa.error) {
        err.append(cli_detail::kUsage);
        err.append("mangly: error: unrecognized arguments: ");
        err.append(pa.error_arg);
        err.push('\n');
        return 2;
    }
    if (pa.help) {
        out.append(cli_detail::kHelp);
        return 0;
    }

    Demangler d;
    if (pa.names.size() == 0) {
        if (stdin_is_tty) {
            err.append(cli_detail::kHelp);
            return 2;
        }
        for (std::uint32_t i = 0; i < nlines; ++i) {
            if (!cli_detail::is_blank(stdin_lines[i])) {
                cli_process(stdin_lines[i], pa.remangle, d, out);
                out.push('\n');
            }
        }
    } else {
        for (std::uint32_t i = 0; i < pa.names.size(); ++i) {
            cli_process(pa.names[i], pa.remangle, d, out);
            out.push('\n');
        }
    }
    return 0;
}

}  // namespace mangly

#endif  // MANGLY_CLI_HPP
