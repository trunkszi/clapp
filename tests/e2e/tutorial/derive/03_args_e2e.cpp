#include "support/check.hpp"
#include "support/subprocess.hpp"

#if CLAPP_E2E_HAS_SUBPROCESS

#    include <initializer_list>
#    include <string_view>

using clapp::test::has_peer;
using clapp::test::run;
using clapp::test::run_peer;
using clapp::test::run_result;

namespace {

    /**
     * \brief Assert that both editions of this step answer \p args identically.
     * \param args The command line, without the program name.
     */
    void pair_agrees(std::initializer_list<std::string_view> args) {
        CLAPP_CHECK(has_peer());

        const run_result mine = run(args);
        const run_result peer = run_peer(args);
        CLAPP_CHECK(mine.status == peer.status);
        CLAPP_CHECK(mine.out == peer.out);
        CLAPP_CHECK(mine.err == peer.err);
    }

}  // namespace

// ---------------------------------------------------------------------------
// One row of the deduction table per case
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 03: the required positional alone leaves every other shape at rest") {
    const run_result r = run({"Alice"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "name: \"Alice\"\n"
                         "port: 2020\n"
                         "tags: []\n"
                         "verbose: 0\n"
                         "quiet: false\n");
}

CLAPP_TEST("tutorial 03: every shape at once") {
    const run_result r = run({"Alice", "8080", "-t", "red", "-t", "blue", "-vv", "--quiet"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "name: \"Alice\"\n"
                         "port: 8080\n"
                         "tags: [\"red\", \"blue\"]\n"
                         "verbose: 2\n"
                         "quiet: true\n");
}

CLAPP_TEST("tutorial 03: a bare std::string is required") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == "error: the following required arguments were not provided:\n"
                         "  <NAME>\n"
                         "\n"
                         "Usage: 03_args <NAME> [PORT]\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 03: the defaulted positional is optional, and the default is typed") {
    // `port` is `unsigned`, so the default goes through the same value_parser the command
    // line does — which is what makes the next case an error rather than a stored string.
    CLAPP_CHECK(run({"Alice"}).out_has("port: 2020\n"));
    CLAPP_CHECK(run({"Alice", "8080"}).out_has("port: 8080\n"));

    const run_result bad = run({"Alice", "nope"});
    CLAPP_CHECK(bad.status == 2);
    CLAPP_CHECK(bad.err ==
                "error: invalid value 'nope' for '[PORT]': invalid digit found in string\n"
                "\n"
                "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 03: a std::vector appends across occurrences and starts empty") {
    CLAPP_CHECK(run({"Alice"}).out_has("tags: []\n"));
    CLAPP_CHECK(run({"Alice", "-t", "red"}).out_has("tags: [\"red\"]\n"));
    CLAPP_CHECK(
            run({"Alice", "-t", "red", "--tag", "blue"}).out_has("tags: [\"red\", \"blue\"]\n"));
    // One occurrence may carry several values: the row deduces `at_least(1)`, not one.
    CLAPP_CHECK(run({"Alice", "-t", "red", "blue"}).out_has("tags: [\"red\", \"blue\"]\n"));
}

CLAPP_TEST("tutorial 03: a counter counts and a flag does not") {
    CLAPP_CHECK(run({"Alice", "-v"}).out_has("verbose: 1\n"));
    CLAPP_CHECK(run({"Alice", "-vvv"}).out_has("verbose: 3\n"));
    CLAPP_CHECK(run({"Alice", "--verbose", "--verbose"}).out_has("verbose: 2\n"));

    CLAPP_CHECK(run({"Alice", "-q"}).out_has("quiet: true\n"));
    CLAPP_CHECK(run({"Alice"}).out_has("quiet: false\n"));
    // And the difference the other way round: a `bool` is `set_true`, which accepts one
    // occurrence and rejects a second. Only a counter may repeat.
    const run_result twice = run({"Alice", "-q", "-q"});
    CLAPP_CHECK(twice.status == 2);
    CLAPP_CHECK(twice.err_has("the argument '--quiet' cannot be used multiple times"));
}

CLAPP_TEST("tutorial 03: an option that wants a value and is given none") {
    const run_result r = run({"Alice", "-t"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("a value is required for '--tag <TAG>...' but none was supplied"));
}

// ---------------------------------------------------------------------------
// The rendered page
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 03: --help distinguishes all five shapes typographically") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "Positionals, options, flags and counters\n"
                         "\n"
                         "Usage: 03_args [OPTIONS] <NAME> [PORT]\n"
                         "\n"
                         "Arguments:\n"
                         "  <NAME>  Who to greet\n"
                         "  [PORT]  Port to connect on [default: 2020]\n"
                         "\n"
                         "Options:\n"
                         "  -t, --tag <TAG>...  Tag to attach; repeat for more\n"
                         "  -v, --verbose...    Increase logging verbosity\n"
                         "  -q, --quiet         Silence all output\n"
                         "  -h, --help          Print help\n"
                         "  -V, --version       Print version\n");
}

CLAPP_TEST("tutorial 03: with no long help anywhere, -h and --help agree") {
    CLAPP_CHECK(run({"-h"}).out == run({"--help"}).out);
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 03: the builder edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"Alice"});
    pair_agrees({"Alice", "8080", "-t", "red", "-t", "blue", "-vv", "--quiet"});
    pair_agrees({"Alice", "-t", "red", "blue"});
    pair_agrees({"Alice", "nope"});
    pair_agrees({"Alice", "-t"});
    pair_agrees({"Alice", "1", "2"});
}

#else

CLAPP_TEST("tutorial 03: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
