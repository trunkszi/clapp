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
} // namespace

// ---------------------------------------------------------------------------
// One case per clapp::arg_matches lookup the example makes
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 01 builder: get_one<std::string> on an absent positional yields nothing") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Debug mode is off\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("tutorial 01 builder: get_one<std::filesystem::path> reads the option back typed") {
    // The id is "config" and the parser is `value_parser<std::filesystem::path>()`; asking
    // for any other type here would abort rather than compile-fail, which is the trade the
    // example's header states.
    const run_result r = run({"Alice", "-c", "/etc/app.toml", "-dd"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Value for name: Alice\n"
        "Value for config: /etc/app.toml\n"
        "Debug mode is on\n");
}

CLAPP_TEST("tutorial 01 builder: get_count returns occurrences, not a boolean") {
    CLAPP_CHECK(run({}).out == "Debug mode is off\n");
    CLAPP_CHECK(run({"-d"}).out == "Debug mode is kind of on\n");
    CLAPP_CHECK(run({"-dd"}).out == "Debug mode is on\n");
    CLAPP_CHECK(run({"-dddd"}).out == "Don't be crazy\n");
}

CLAPP_TEST("tutorial 01 builder: subcommand() hands back the child's own matches") {
    CLAPP_CHECK(run({"test"}).out_has("Not printing testing lists...\n"));
    CLAPP_CHECK(run({"test", "-l"}).out_has("Printing testing lists...\n"));
    // No subcommand at all: the example must print nothing for it rather than treat the
    // absent optional as a match.
    CLAPP_CHECK(!run({}).out_has("testing lists"));
}

CLAPP_TEST("tutorial 01 builder: an option that wants a value and is given none") {
    const run_result r = run({"-c"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("a value is required for '--config <FILE>' but none was supplied"));
}

CLAPP_TEST("tutorial 01 builder: only one positional is declared, so a second is unexpected") {
    const run_result r = run({"a", "b"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("unexpected argument 'b' found"));
}

CLAPP_TEST("tutorial 01 builder: --version comes from version(), which also created -V") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "01_quick 1.0.0\n");
    CLAPP_CHECK(run({"-V"}).out == "01_quick 1.0.0\n");
}

CLAPP_TEST("tutorial 01 builder: help goes to stdout and exits 0, errors to stderr and exit 2") {
    const run_result help = run({"--help"});
    CLAPP_CHECK(help.status == 0);
    CLAPP_CHECK(help.err.empty());
    CLAPP_CHECK(help.out_has("Usage: 01_quick [OPTIONS] [name] [COMMAND]\n"));

    const run_result bad = run({"--bogus"});
    CLAPP_CHECK(bad.status == 2);
    CLAPP_CHECK(bad.out.empty());
    CLAPP_CHECK(bad.err_has("Usage: 01_quick [OPTIONS] [name] [COMMAND]\n"));
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 01 builder: the derive edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({"test", "--help"});
    pair_agrees({"help", "test"});
    pair_agrees({});
    pair_agrees({"Alice", "-c", "/etc/app.toml", "-dd"});
    pair_agrees({"test", "-l"});
    pair_agrees({"--bogus"});
    pair_agrees({"-c"});
}

#else

CLAPP_TEST("tutorial 01 builder: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
