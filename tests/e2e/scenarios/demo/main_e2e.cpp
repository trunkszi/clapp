#include "support/check.hpp"
#include "support/subprocess.hpp"

#include <string>
#include <string_view>

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_result;

// ---------------------------------------------------------------------------
// The binary is really there
// ---------------------------------------------------------------------------

CLAPP_TEST("demo: CMake injected a path and the binary runs") {
    // Without this, every case below would "pass" against a status of -1 if the
    // assertions were sloppy. They are not, but the failure would be reported as a
    // parse bug rather than as a missing binary.
    CLAPP_CHECK(!clapp::test::binary_path().empty());

    const run_result r = run({"--name", "there"});
    CLAPP_CHECK(r.exited());
    CLAPP_CHECK(r.status == 0);
}

// ---------------------------------------------------------------------------
// The happy path
// ---------------------------------------------------------------------------

CLAPP_TEST("demo: long options, count repeats the greeting") {
    const run_result r = run({"--name", "Alice", "--count", "2"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Hello Alice!\nHello Alice!\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("demo: short options derive from the same annotation") {
    const run_result r = run({"-n", "Bob", "-c", "3"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Hello Bob!\nHello Bob!\nHello Bob!\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("demo: default_value makes --count optional") {
    const run_result r = run({"-n", "Carol"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Hello Carol!\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("demo: --count=N and --name=NAME accept the equals form") {
    const run_result r = run({"--name=Dave", "--count=2"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Hello Dave!\nHello Dave!\n");
}

CLAPP_TEST("demo: -c0 prints nothing and still succeeds") {
    const run_result r = run({"-n", "Eve", "-c", "0"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err.empty());
}

// ---------------------------------------------------------------------------
// --help and --version are control flow, not failure
// ---------------------------------------------------------------------------

// The whole page, byte for byte. Six separate things have to be right at once, which is
// why this is one comparison rather than six `out_has` calls:
//
//   * the `about` reached the tree from `[[= clapp::cmd{.about = ...}]]`;
//   * `.name = "demo"` won over the reflected type name `args`;
//   * `--name` is required and `--count` is not, so only the first is in the usage line;
//   * both arguments list in declaration order, and the two injected flags after them;
//   * the description column is `longest + 4` and every row reaches it;
//   * `[default: 1]` is present and `[default: ]` is not — `--name` has no default.
//
// Compare `clap/examples/demo.md`.
constexpr std::string_view demo_help = "Simple program to greet a person\n"
                                       "\n"
                                       "Usage: demo [OPTIONS] --name <name>\n"
                                       "\n"
                                       "Options:\n"
                                       "  -n, --name <name>    Name of the person to greet\n"
                                       "  -c, --count <count>  Number of times to greet "
                                       "[default: 1]\n"
                                       "  -h, --help           Print help\n"
                                       "  -V, --version        Print version\n";

CLAPP_TEST("demo: --help goes to stdout with status 0") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == demo_help);
}

CLAPP_TEST("demo: -h is the short spelling of the injected help flag") {
    const run_result r = run({"-h"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    // Identical to `--help`: nothing in this command has a long form, so clap's
    // `write_help_err` collapses `--help` onto `-h` and clapp's render_help_text() does
    // the same. A `-h` that differed here would mean the collapse was dropped.
    CLAPP_CHECK(r.out == demo_help);
}

CLAPP_TEST("demo: --version prints name and version, status 0") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "demo 1.0.0\n");
    CLAPP_CHECK(r.err.empty());
}

// ---------------------------------------------------------------------------
// Errors: stderr, status 2, nothing on stdout
// ---------------------------------------------------------------------------

CLAPP_TEST("demo: a missing required argument is an error naming the argument") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("--name"));
    CLAPP_CHECK(r.err_has("Usage: demo"));
}

CLAPP_TEST("demo: a value the parser rejects is an error naming the value") {
    const run_result r = run({"--name", "Alice", "--count", "not-a-number"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("not-a-number"));
    CLAPP_CHECK(r.err_has("--count"));
}

CLAPP_TEST("demo: an out-of-range value is rejected by the integer parser") {
    // `count` is `unsigned`; 2^32 does not fit and the value_parser says so rather than
    // wrapping. Which end of the domain is quoted is M5's business, so only the offending
    // value is asserted.
    const run_result r = run({"--name", "Alice", "--count", "4294967296"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("4294967296"));
}

CLAPP_TEST("demo: an unknown long option is an error") {
    const run_result r = run({"--nickname", "Alice"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("nickname"));
}

CLAPP_TEST("demo: an unexpected positional is an error") {
    // `args` declares no positional at all, so a bare word has nowhere to go.
    const run_result r = run({"--name", "Alice", "stray"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("stray"));
}

#else

CLAPP_TEST("demo: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
