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
// The enumeration
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05: both enumerators parse, with no ValueEnum opt-in anywhere") {
    const run_result fast = run({"fast", "--major"});
    CLAPP_CHECK(fast.status == 0);
    CLAPP_CHECK(fast.out == "mode: fast\n"
                            "port: None\n"
                            "version: 2.0.0\n");
    CLAPP_CHECK(run({"slow", "--major"}).out_has("mode: slow\n"));
}

CLAPP_TEST("tutorial 05: an unlisted value is rejected and the accepted set is printed") {
    const run_result r = run({"quick", "--major"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == "error: invalid value 'quick' for '<MODE>'\n"
                         "  [possible values: fast, slow]\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 05: matching is case-sensitive unless asked otherwise") {
    // `.ignore_case` is not set, so `FAST` is as wrong as `quick`. Without this case an
    // implementation that folded case would pass every other enumeration assertion.
    const run_result r = run({"FAST", "--major"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("invalid value 'FAST' for '<MODE>'"));
}

// ---------------------------------------------------------------------------
// The hand-written value_parser
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05: --port accepts both ends of the range it declares") {
    CLAPP_CHECK(run({"fast", "--major", "--port", "1"}).out_has("port: Some(1)\n"));
    CLAPP_CHECK(run({"fast", "--major", "--port", "65535"}).out_has("port: Some(65535)\n"));
    CLAPP_CHECK(run({"fast", "--major"}).out_has("port: None\n"));
}

CLAPP_TEST("tutorial 05: --port rejects both values just outside it, with its own sentence") {
    const run_result high = run({"fast", "--major", "--port", "65536"});
    CLAPP_CHECK(high.status == 2);
    CLAPP_CHECK(high.out.empty());
    CLAPP_CHECK(high.err ==
                "error: invalid value '65536' for '--port <PORT>': port not in range 1-65535\n"
                "\n"
                "For more information, try '--help'.\n");

    const run_result zero = run({"fast", "--major", "--port", "0"});
    CLAPP_CHECK(zero.status == 2);
    CLAPP_CHECK(zero.err_has("invalid value '0' for '--port <PORT>': port not in range 1-65535"));
}

CLAPP_TEST("tutorial 05: delegating the numeric half reuses the built-in wording") {
    // Not "port not in range": the bytes never became a number, so the sentence is the one
    // a bare integer argument would produce. That is what delegating to
    // `clapp::value_parser<unsigned long>` buys.
    const run_result r = run({"fast", "--major", "--port", "eight"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("invalid value 'eight' for '--port <PORT>': invalid digit found in "
                          "string"));
    CLAPP_CHECK(!r.err_has("port not in range"));
}

// ---------------------------------------------------------------------------
// The relations
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05: --set-ver and --major conflict") {
    const run_result r = run({"fast", "--major", "--set-ver", "9"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == "error: the argument '--major' cannot be used with '--set-ver <VER>'\n"
                         "\n"
                         "Usage: 05_validation --major <MODE>\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 05: neither of the two is also an error") {
    // The half a conflict alone does not give you: `required_unless_any` is what makes
    // "give me one of these" rather than "give me at most one of these".
    const run_result r = run({"fast"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err == "error: the following required arguments were not provided:\n"
                         "  --set-ver <VER>\n"
                         "\n"
                         "Usage: 05_validation --set-ver <VER> <MODE>\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 05: either one alone is accepted") {
    CLAPP_CHECK(run({"fast", "--major"}).out_has("version: 2.0.0\n"));
    CLAPP_CHECK(run({"slow", "--set-ver", "4.5.6"}).out_has("version: 4.5.6\n"));
}

CLAPP_TEST("tutorial 05: <MODE> stays required no matter which relation was satisfied") {
    const run_result r = run({"--set-ver", "1.0"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("  <MODE>\n"));
}

CLAPP_TEST("tutorial 05: everything together") {
    const run_result r = run({"slow", "--set-ver", "4.5.6", "--port", "8080"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "mode: slow\n"
                         "port: Some(8080)\n"
                         "version: 4.5.6\n");
}

// ---------------------------------------------------------------------------
// The rendered pages
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05: -h lists the value set inline") {
    const run_result r = run({"-h"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "Value sets, range checks and argument relations\n"
                         "\n"
                         "Usage: 05_validation [OPTIONS] <MODE>\n"
                         "\n"
                         "Arguments:\n"
                         "  <MODE>  What mode to run the program in [possible values: fast, slow]\n"
                         "\n"
                         "Options:\n"
                         "      --port <PORT>    Network port to use\n"
                         "      --set-ver <VER>  Set the version manually\n"
                         "      --major          Auto-increment the major version instead\n"
                         "  -h, --help           Print help (see more with '--help')\n"
                         "  -V, --version        Print version\n");
}

CLAPP_TEST("tutorial 05: --help gives each enumerator its own line and its own help") {
    // The enumerator help comes from `[[= clapp::value{.help = ...}]]`, read by
    // reflection. It is visible on this page and nowhere else.
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "Value sets, range checks and argument relations\n"
                         "\n"
                         "Usage: 05_validation [OPTIONS] <MODE>\n"
                         "\n"
                         "Arguments:\n"
                         "  <MODE>\n"
                         "          What mode to run the program in\n"
                         "\n"
                         "          Possible values:\n"
                         "          - fast: Run swiftly\n"
                         "          - slow: Crawl slowly but steadily\n"
                         "\n"
                         "Options:\n"
                         "      --port <PORT>\n"
                         "          Network port to use\n"
                         "\n"
                         "      --set-ver <VER>\n"
                         "          Set the version manually\n"
                         "\n"
                         "      --major\n"
                         "          Auto-increment the major version instead\n"
                         "\n"
                         "  -h, --help\n"
                         "          Print help (see a summary with '-h')\n"
                         "\n"
                         "  -V, --version\n"
                         "          Print version\n");
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05: the builder edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"fast", "--major"});
    pair_agrees({"slow", "--set-ver", "4.5.6", "--port", "8080"});
    pair_agrees({"fast", "--major", "--port", "1"});
    pair_agrees({"fast", "--major", "--port", "0"});
    pair_agrees({"fast", "--major", "--port", "65536"});
    pair_agrees({"fast", "--major", "--port", "eight"});
    pair_agrees({"quick", "--major"});
    pair_agrees({"fast"});
    pair_agrees({"fast", "--major", "--set-ver", "9"});
    pair_agrees({"--set-ver", "1.0"});
    pair_agrees({"fast", "--major", "extra"});
}

#else

CLAPP_TEST("tutorial 05: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
