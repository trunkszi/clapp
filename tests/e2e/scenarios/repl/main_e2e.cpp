#include "support/check.hpp"
#include "support/subprocess.hpp"

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run_result;
using clapp::test::run_with_input;

CLAPP_TEST("repl: ping answers Pong, exit leaves") {
    const run_result r = run_with_input({}, "ping\nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "$ Pong$ Exiting ...");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("repl: end of input ends the loop as cleanly as exit does") {
    const run_result r = run_with_input({}, "ping\n");
    CLAPP_CHECK(r.status == 0);
    // The trailing `$ ` is the prompt printed before the read that hit EOF.
    CLAPP_CHECK(r.out == "$ Pong$ ");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("repl: a blank line is skipped without a diagnostic") {
    const run_result r = run_with_input({}, "\n   \nping\nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "$ $ $ Pong$ Exiting ...");
}

CLAPP_TEST("repl: a bad line does not end the process") {
    // The case this file exists for. `bogus` must produce a diagnostic AND leave the loop
    // running, so the `ping` that follows still answers.
    const run_result r = run_with_input({}, "bogus\nping\nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("bogus"));
    CLAPP_CHECK(r.out_has("Pong"));
    CLAPP_CHECK(r.out_has("Exiting ..."));
    // Errors are routed to stdout by the loop, not by the error's own stream choice;
    // in a REPL the diagnostic belongs in the transcript.
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("repl: several bad lines in a row all survive") {
    const run_result r = run_with_input({}, "a\nb\nc\nping\nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("Pong"));
    CLAPP_CHECK(r.out_has("Exiting ..."));
}

CLAPP_TEST("repl: --help is control flow, and does not exit the process either") {
    // `--help` is REJECTED here, not honoured: the example sets `disable_help_flag`, as
    // clap's repl.rs does, because a REPL line is a subcommand and `--help` at the top
    // level would be a word the grammar has no place for. What matters is that the
    // rejection is a value the loop can print rather than a call to `std::exit`, which is
    // why `ping` still answers afterwards.
    //
    // The whole transcript is compared, diagnostic included. The `Usage:` line inside it
    // is the same one the M3 renderer produced; the surrounding error text is
    // `clapp::error`'s, not the help placeholder's, and it has not changed in M5 either.
    // It is pinned here because "the loop keeps going" is only worth asserting if what it
    // printed in between is known.
    const run_result r = run_with_input({}, "--help\nping\nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "$ error: unexpected argument '--help' found\n"
                         "\n"
                         "Usage: cli <COMMAND>\n"
                         "\n"
                         "For more information, try 'help'.\n"
                         "$ Pong$ Exiting ...");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("repl: leading and trailing whitespace on a line is ignored") {
    const run_result r = run_with_input({}, "   ping   \nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "$ Pong$ Exiting ...");
}

CLAPP_TEST("repl: a word after the applet is an unexpected argument") {
    const run_result r = run_with_input({}, "ping extra\nexit\n");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("extra"));
    CLAPP_CHECK(r.out_has("Exiting ..."));
}

CLAPP_TEST("repl: nothing at all on stdin still prints one prompt") {
    const run_result r = run_with_input({}, "");
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "$ ");
}

#else

CLAPP_TEST("repl: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
