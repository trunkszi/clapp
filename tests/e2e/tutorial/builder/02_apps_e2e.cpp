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
// required() is what makes main's unconditional reads total
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 02 builder: both values given, both read back") {
    const run_result r = run({"--one", "1", "--two", "2"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "two: \"2\"\n"
                         "one: \"1\"\n");
}

CLAPP_TEST("tutorial 02 builder: a missing argument is rejected before main reads it") {
    // This is the case that must stay an *error*: `main` dereferences both lookups
    // unconditionally, so a tree that had lost `.required()` would abort here instead.
    const run_result none = run({});
    CLAPP_CHECK(none.status == 2);
    CLAPP_CHECK(none.out.empty());
    CLAPP_CHECK(none.err_has("the following required arguments were not provided:"));

    const run_result one_only = run({"--one", "1"});
    CLAPP_CHECK(one_only.status == 2);
    CLAPP_CHECK(one_only.out.empty());
    CLAPP_CHECK(one_only.err_has("  --two <VALUE>\n"));
    CLAPP_CHECK(!one_only.err_has("  --one <VALUE>\n"));
}

CLAPP_TEST("tutorial 02 builder: an option given without its value is a distinct error") {
    const run_result r = run({"--two"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err == "error: a value is required for '--two <VALUE>' but none was supplied\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 02 builder: an unknown option is rejected with the usage line") {
    const run_result r = run({"--three", "3"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err == "error: unexpected argument '--three' found\n"
                         "\n"
                         "Usage: MyApp --two <VALUE> --one <VALUE>\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

CLAPP_TEST("tutorial 02 builder: version() supplies both --version and the -V alias") {
    CLAPP_CHECK(run({"--version"}).out == "MyApp 1.0\n");
    CLAPP_CHECK(run({"-V"}).out == "MyApp 1.0\n");
}

CLAPP_TEST("tutorial 02 builder: the usage line names both required options") {
    // The `Usage:` line is not placeholder text — it comes from the real usage renderer
    // and is the one part of an error page that was already final before M5.
    CLAPP_CHECK(run({}).err_has("Usage: MyApp --two <VALUE> --one <VALUE>\n"));
    CLAPP_CHECK(run({"-h"}).out_has("Usage: MyApp --two <VALUE> --one <VALUE>\n"));
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 02 builder: the derive edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({"-V"});
    pair_agrees({});
    pair_agrees({"--one", "1", "--two", "2"});
    pair_agrees({"--one", "1"});
    pair_agrees({"--two"});
    pair_agrees({"--three", "3"});
}

#else

CLAPP_TEST("tutorial 02 builder: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
