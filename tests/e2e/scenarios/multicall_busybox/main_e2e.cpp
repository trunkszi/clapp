#include "support/check.hpp"
#include "support/subprocess.hpp"

#include <string_view>

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run_as;
using clapp::test::run_result;

// ---------------------------------------------------------------------------
// The applets, reached directly through argv[0]
// ---------------------------------------------------------------------------

CLAPP_TEST("busybox: invoked as true, exits 0 and says nothing") {
    const run_result r = run_as("true", {});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("busybox: invoked as false, exits 1 and says nothing") {
    const run_result r = run_as("false", {});
    CLAPP_CHECK(r.status == 1);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err.empty());
}

// ---------------------------------------------------------------------------
// The same applets, reached through the dispatcher
// ---------------------------------------------------------------------------

CLAPP_TEST("busybox: busybox true is the same applet, one level down") {
    const run_result r = run_as("busybox", {"true"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("busybox: busybox false likewise") {
    const run_result r = run_as("busybox", {"false"});
    CLAPP_CHECK(r.status == 1);
    CLAPP_CHECK(r.out.empty());
}

// `$ busybox` in clap/examples/multicall-busybox.md, with the two differences the
// example's own header already lists: clap sets `subcommand_help_heading("APPLETS")` and
// `subcommand_value_name("APPLET")`, neither of which clapp::command_attr can express, so
// clapp writes the defaults `Commands:` and `[COMMAND]`. `--install [<PATH>]` is likewise
// the example's own `value_name`, over the `0..=1` arity its nested-optional type implies.
//
// `Usage: busybox`, NOT `Usage: cli busybox`: see the note in the hostname driver.
constexpr std::string_view busybox_help =
        "Usage: busybox [OPTIONS] [COMMAND]\n"
        "\n"
        "Commands:\n"
        "  true   does nothing successfully\n"
        "  false  does nothing unsuccessfully\n"
        "  help   Print this message or the help of the given subcommand(s)\n"
        "\n"
        "Options:\n"
        "      --install [<PATH>]  Install hardlinks for all subcommands in path\n"
        "  -h, --help              Print help\n";

CLAPP_TEST("busybox: the dispatcher with nothing prints help, status 2") {
    // `arg_required_else_help` on the `busybox` applet, and it prints the WHOLE short
    // page — clap's `Validator::validate` calls `write_help_err(false)`. Until M5 clapp
    // printed the usage line alone, which `err_has("Usage:")` could not tell apart.
    const run_result r = run_as("busybox", {});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == busybox_help);
}

CLAPP_TEST("busybox: --help on the dispatcher is the same page, on stdout") {
    // Same bytes, different stream and status: the pair is what says
    // `arg_required_else_help` really is "show the help", not "show something".
    const run_result r = run_as("busybox", {"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == busybox_help);
}

// ---------------------------------------------------------------------------
// --install: the three states of a nested optional
// ---------------------------------------------------------------------------

CLAPP_TEST("busybox: --install with no value takes default_missing_value") {
    const run_result r = run_as("busybox", {"--install"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Would install hardlinks in /usr/local/bin\n");
}

CLAPP_TEST("busybox: --install DIR takes the given value") {
    const run_result r = run_as("busybox", {"--install", "/opt/bin"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Would install hardlinks in /opt/bin\n");
}

CLAPP_TEST("busybox: --install=DIR accepts the equals form too") {
    const run_result r = run_as("busybox", {"--install=/opt/bin"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Would install hardlinks in /opt/bin\n");
}

CLAPP_TEST("busybox: a detached --install value is greedy, as 0..=1 implies") {
    // MEASURED, and worth pinning precisely because it surprises. With `0..=1` and no
    // `require_equals`, `--install true` reads `true` as the *value*, not as the applet.
    // clap's builder version behaves the same way; the fix, if a CLI wants the other
    // reading, is `require_equals`, which is what git/'s `--color` uses.
    const run_result r = run_as("busybox", {"--install", "true"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Would install hardlinks in true\n");
}

CLAPP_TEST("busybox: --install=DIR wins over a following applet") {
    // `.exclusive` is set on `--install`, but clap's `exclusive` conflicts with other
    // *arguments*, not with subcommands, so this parses and the program's own precedence
    // decides. Asserted so that a future change to either rule shows up here.
    const run_result r = run_as("busybox", {"--install=/opt/bin", "true"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Would install hardlinks in /opt/bin\n");
}

// ---------------------------------------------------------------------------
// argv[0] really is what selects
// ---------------------------------------------------------------------------

CLAPP_TEST("busybox: an argv[0] that names no applet is an error") {
    const run_result r = run_as("multicall-busybox", {});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("multicall-busybox"));
}

CLAPP_TEST("busybox: a full path in argv[0] resolves to the applet") {
    const run_result r = run_as("/sbin/false", {});
    CLAPP_CHECK(r.status == 1);
    CLAPP_CHECK(r.out.empty());
}

#else

CLAPP_TEST("busybox: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
