#include "support/check.hpp"
#include "support/subprocess.hpp"

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_result;

CLAPP_TEST("escaped: the invocation from clap's own README") {
    const run_result r = run({"-f", "-p=bob", "--", "sloppy", "slop", "slop"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "-f used: true\n"
                         "-p's value: Some(\"bob\")\n"
                         "'slops' values: [\"sloppy\", \"slop\", \"slop\"]\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("escaped: nothing at all is legal and everything stays empty") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "-f used: false\n"
                         "-p's value: None\n"
                         "'slops' values: []\n");
}

CLAPP_TEST("escaped: -- with one word fills the vector with one word") {
    const run_result r = run({"--", "a"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("'slops' values: [\"a\"]\n"));
}

CLAPP_TEST("escaped: -- protects words that look like options") {
    const run_result r = run({"-f", "--", "--not-an-option", "-x"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("-f used: true\n"));
    CLAPP_CHECK(r.out_has("'slops' values: [\"--not-an-option\", \"-x\"]\n"));
}

CLAPP_TEST("escaped: without -- a bare word has nowhere to go") {
    // The negative half of `.last = true`.
    const run_result r = run({"sloppy"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("sloppy"));
}

CLAPP_TEST("escaped: -p takes a detached value too") {
    const run_result r = run({"-p", "bob"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("-p's value: Some(\"bob\")\n"));
}

CLAPP_TEST("escaped: no_long really suppressed --eff") {
    const run_result r = run({"--eff"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("eff"));
}

CLAPP_TEST("escaped: no_long really suppressed --pea") {
    const run_result r = run({"--pea", "bob"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("pea"));
}

CLAPP_TEST("escaped: --help shows the escaped positional in the usage line") {
    // `clap/examples/escaped-positional.md`'s `$ escaped-positional --help`, byte for
    // byte, with `escaped-positional[EXE]` written as `escaped-positional` (the snapshot
    // machinery's Windows placeholder) and this example's own `about` and `help` prose in
    // place of clap's empty strings.
    //
    // TWO ROWS ARE TRAILING-SPACE SENSITIVE, and both are clap's. `  [SLOP]...  ` pads to
    // the description column even though `slop` has no help — clap emits the padding
    // unconditionally, and dropping it would be a silent divergence that no `out_has`
    // could see. That is exactly why this is `==` and not `out_has` now.
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "Demonstrates the -- escape\n"
                         "\n"
                         "Usage: escaped-positional [OPTIONS] [-- <SLOP>...]\n"
                         "\n"
                         "Arguments:\n"
                         "  [SLOP]...  \n"
                         "\n"
                         "Options:\n"
                         "  -f             A flag\n"
                         "  -p <PEAR>      A value\n"
                         "  -h, --help     Print help\n"
                         "  -V, --version  Print version\n");
}

CLAPP_TEST("escaped: --version reports the annotated version") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "escaped-positional 1.0.0\n");
}

#else

CLAPP_TEST("escaped: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
