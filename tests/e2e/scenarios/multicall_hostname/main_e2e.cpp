#include "support/check.hpp"
#include "support/subprocess.hpp"

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_as;
using clapp::test::run_result;

CLAPP_TEST("hostname: argv[0] selects the applet") {
    const run_result r = run_as("hostname", {});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "www\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("hostname: a different argv[0] selects a different applet") {
    const run_result r = run_as("dnsdomainname", {});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "example.com\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("hostname: a full path in argv[0] still resolves to the applet") {
    // Real invocations carry a path, not a bare name — `/usr/bin/hostname`. clap strips
    // the directory before matching and so must clapp, or multicall would only work from
    // the current directory.
    const run_result r = run_as("/usr/bin/hostname", {});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "www\n");
}

CLAPP_TEST("hostname: an argv[0] that names no applet is an error") {
    // The negative half; see the file header for why it carries the weight.
    const run_result r = run_as("multicall-hostname", {});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("multicall-hostname"));
}

CLAPP_TEST("hostname: the applet name is NOT read from argv[1]") {
    // The other half of "multicall": under the default rules `argv[0]` is skipped and
    // `hostname` here would select the applet. Under multicall it is an unexpected
    // argument, because the applet was already chosen — wrongly — by argv[0].
    const run_result r = run({"hostname"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
}

CLAPP_TEST("hostname: an applet still has its own --help") {
    // `Usage: hostname`, NOT `Usage: cli hostname`. A multicall root has no name a user
    // ever types — `argv[0]` names the applet — so clap's `_build_bin_names_internal`
    // starts the child's path from an empty string rather than from the root's name
    // (`if is_multicall_set { bin_name.unwrap_or("") }`). clapp did not, until M5:
    // `clapp::detail::child_base_path()` in `parser/parse.hpp` is that rule, and this
    // assertion is what holds it. clap pins the same shape in `multicall_render_help`
    // (tests/builder/subcommands.rs), which expects `Usage: foo bar` under a root named
    // `repl`.
    const run_result r = run_as("hostname", {"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "show hostname part of FQDN\n"
                         "\n"
                         "Usage: hostname\n"
                         "\n"
                         "Options:\n"
                         "  -h, --help  Print help\n");
}

CLAPP_TEST("hostname: the root keeps its own name when nothing selected an applet") {
    // The other half of the rule above, and the reason it is `child_base_path()` rather
    // than an empty `bin_path_`: only what a CHILD inherits is emptied. The root's own
    // diagnostic still says `cli`, because clap's `get_usage_name_fallback` falls back to
    // the command's name. Emptying both would print a bare `Usage: <COMMAND>`.
    const run_result r = run_as("multicall-hostname", {});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err == "error: unrecognized subcommand 'multicall-hostname'\n"
                         "\n"
                         "Usage: cli <COMMAND>\n"
                         "\n"
                         "For more information, try 'help'.\n");
}

CLAPP_TEST("hostname: an applet takes no arguments") {
    const run_result r = run_as("hostname", {"extra"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("extra"));
}

#else

CLAPP_TEST("hostname: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
