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
// Behaviour
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 02: both options are required and print in declaration order") {
    const run_result r = run({"--one", "1", "--two", "2"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    // Given in the order one, two — printed in the order two, one, because that is the
    // order the fields are declared in.
    CLAPP_CHECK(r.out == "two: \"2\"\n"
                         "one: \"1\"\n");
}

CLAPP_TEST("tutorial 02: an empty command line names every missing argument") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == "error: the following required arguments were not provided:\n"
                         "  --two <VALUE>\n"
                         "  --one <VALUE>\n"
                         "\n"
                         "Usage: MyApp --two <VALUE> --one <VALUE>\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 02: giving one of the two still fails, naming only the other") {
    const run_result r = run({"--two", "2"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("  --one <VALUE>\n"));
    CLAPP_CHECK(!r.err_has("  --two <VALUE>\n"));
}

CLAPP_TEST("tutorial 02: --version reports the name and version the command carries") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "MyApp 1.0\n");
}

// ---------------------------------------------------------------------------
// The rendered pages
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 02: -h is the summary page") {
    const run_result r = run({"-h"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "Does awesome things\n"
                         "\n"
                         "Usage: MyApp --two <VALUE> --one <VALUE>\n"
                         "\n"
                         "Options:\n"
                         "      --two <VALUE>  Listed first, because it is declared first\n"
                         "      --one <VALUE>  Listed second; help order is declaration order\n"
                         "  -h, --help         Print help (see more with '--help')\n"
                         "  -V, --version      Print version\n"
                         "\n"
                         "Run with --help for the long description.\n");
}

CLAPP_TEST("tutorial 02: --help is the full page, with the long description wrapped") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "Does awesome things.\n"
                         "\n"
                         "This paragraph only appears under --help, which is what .long_about is "
                         "for: a sentence for the\n"
                         "summary screen and a description for the full one.\n"
                         "\n"
                         "Usage: MyApp --two <VALUE> --one <VALUE>\n"
                         "\n"
                         "Options:\n"
                         "      --two <VALUE>\n"
                         "          Listed first, because it is declared first\n"
                         "\n"
                         "      --one <VALUE>\n"
                         "          Listed second; help order is declaration order\n"
                         "\n"
                         "  -h, --help\n"
                         "          Print help (see a summary with '-h')\n"
                         "\n"
                         "  -V, --version\n"
                         "          Print version\n"
                         "\n"
                         "Run with --help for the long description.\n");
}

CLAPP_TEST("tutorial 02: the two pages are not the same bytes") {
    // The negative half of the two pins above. Without it, a renderer that ignored the
    // short/long distinction entirely would satisfy one comparison and this file would
    // still read as if it covered both.
    CLAPP_CHECK(run({"-h"}).out != run({"--help"}).out);
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 02: the builder edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"--one", "1", "--two", "2"});
    pair_agrees({"--two", "2"});
    pair_agrees({"--two"});
    pair_agrees({"--three", "3"});
}

#else

CLAPP_TEST("tutorial 02: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
