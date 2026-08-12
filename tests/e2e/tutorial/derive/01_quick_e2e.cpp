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
        // A wiring mistake must fail here rather than quietly turn this into a no-op.
        CLAPP_CHECK(has_peer());

        const run_result mine = run(args);
        const run_result peer = run_peer(args);
        CLAPP_CHECK(mine.status == peer.status);
        CLAPP_CHECK(mine.out == peer.out);
        CLAPP_CHECK(mine.err == peer.err);
    }

}  // namespace

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 01: with nothing given, only the debug counter reports") {
    // The optional positional and the optional option print nothing at all when absent,
    // which is what makes `std::optional` the right field type for them.
    const run_result r = run({});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Debug mode is off\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("tutorial 01: the positional, the option and the counter together") {
    const run_result r = run({"Alice", "-c", "/etc/app.toml", "-dd"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Value for name: Alice\n"
                         "Value for config: /etc/app.toml\n"
                         "Debug mode is on\n");
}

CLAPP_TEST("tutorial 01: -d counts occurrences rather than setting a flag") {
    CLAPP_CHECK(run({"-d"}).out == "Debug mode is kind of on\n");
    CLAPP_CHECK(run({"-d", "-d"}).out == "Debug mode is on\n");
    CLAPP_CHECK(run({"-ddd"}).out == "Don't be crazy\n");
    // The negative half: a counter that had been wired as a plain flag would answer
    // "kind of on" for every one of the four.
    CLAPP_CHECK(run({}).out == "Debug mode is off\n");
}

CLAPP_TEST("tutorial 01: --config accepts both spellings") {
    CLAPP_CHECK(run({"--config", "a.toml"}).out_has("Value for config: a.toml\n"));
    CLAPP_CHECK(run({"-ca.toml"}).out_has("Value for config: a.toml\n"));
}

CLAPP_TEST("tutorial 01: the subcommand is optional and carries its own flag") {
    const run_result without = run({"test"});
    CLAPP_CHECK(without.status == 0);
    CLAPP_CHECK(without.out == "Debug mode is off\n"
                               "Not printing testing lists...\n");

    const run_result with = run({"test", "-l"});
    CLAPP_CHECK(with.status == 0);
    CLAPP_CHECK(with.out == "Debug mode is off\n"
                            "Printing testing lists...\n");
}

CLAPP_TEST("tutorial 01: an unknown flag is rejected with a usage line") {
    const run_result r = run({"--bogus"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == "error: unexpected argument '--bogus' found\n"
                         "\n"
                         "  tip: to pass '--bogus' as a value, use '-- --bogus'\n"
                         "\n"
                         "Usage: 01_quick [OPTIONS] [name] [COMMAND]\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

// ---------------------------------------------------------------------------
// The rendered pages
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 01: --help lists commands, arguments and options in that order") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "A quick tour of clapp's derive API\n"
                         "\n"
                         "Usage: 01_quick [OPTIONS] [name] [COMMAND]\n"
                         "\n"
                         "Commands:\n"
                         "  test  does testing things\n"
                         "  help  Print this message or the help of the given subcommand(s)\n"
                         "\n"
                         "Arguments:\n"
                         "  [name]  Optional name to operate on\n"
                         "\n"
                         "Options:\n"
                         "  -c, --config <FILE>  Sets a custom config file\n"
                         "  -d, --debug...       Turn debugging information on\n"
                         "  -h, --help           Print help\n"
                         "  -V, --version        Print version\n");
}

CLAPP_TEST("tutorial 01: with no long_about, -h and --help are the same page") {
    // The step sets `.about` and nothing else, so there is only one description to
    // render and the injected help row says nothing about a second screen. Step 2 is
    // where the two pages diverge, and it asserts the difference.
    CLAPP_CHECK(run({"-h"}).out == run({"--help"}).out);
    CLAPP_CHECK(!run({"-h"}).out_has("see more with"));
}

CLAPP_TEST("tutorial 01: the subcommand has its own help page and its own usage line") {
    const run_result r = run({"test", "--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "does testing things\n"
                         "\n"
                         "Usage: 01_quick test [OPTIONS]\n"
                         "\n"
                         "Options:\n"
                         "  -l, --list  lists test values\n"
                         "  -h, --help  Print help\n");
}

CLAPP_TEST("tutorial 01: --version prints the annotated version") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "01_quick 1.0.0\n");
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 01: the builder edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({"test", "--help"});
    pair_agrees({});
    pair_agrees({"Alice", "-c", "/etc/app.toml", "-dd"});
    pair_agrees({"test", "-l"});
    pair_agrees({"--bogus"});
    pair_agrees({"-c"});
    pair_agrees({"a", "b"});
}

#else

CLAPP_TEST("tutorial 01: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
