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
// The settings that are explicit here and deduced over there
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 03 builder: one occurrence of --tag may carry several values") {
    // This is what `num_args(at_least(1))` buys over the append action's default of one.
    CLAPP_CHECK(run({"Alice", "-t", "red", "blue"}).out_has("tags: [\"red\", \"blue\"]\n"));
    CLAPP_CHECK(run({"Alice", "-t", "red", "-t", "blue"}).out_has("tags: [\"red\", \"blue\"]\n"));
}

CLAPP_TEST("tutorial 03 builder: the same range is what puts ... after <TAG> in help") {
    // The visible half of the setting above, and the reason a dropped `num_args` would
    // break the pair rather than merely change behaviour.
    CLAPP_CHECK(run({"--help"}).out_has("  -t, --tag <TAG>...  Tag to attach; repeat for more\n"));
}

CLAPP_TEST("tutorial 03 builder: the defaulted positional is parsed, not stored as text") {
    CLAPP_CHECK(run({"Alice"}).out_has("port: 2020\n"));
    CLAPP_CHECK(run({"Alice", "8080"}).out_has("port: 8080\n"));

    const run_result bad = run({"Alice", "nope"});
    CLAPP_CHECK(bad.status == 2);
    CLAPP_CHECK(bad.out.empty());
    CLAPP_CHECK(bad.err_has("invalid value 'nope' for '[PORT]': invalid digit found in string"));
}

// ---------------------------------------------------------------------------
// One case per clapp::arg_matches lookup the example makes
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 03 builder: every lookup reports the same values as the derive edition") {
    const run_result r = run({"Alice", "8080", "-t", "red", "-t", "blue", "-vv", "--quiet"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "name: \"Alice\"\n"
                         "port: 8080\n"
                         "tags: [\"red\", \"blue\"]\n"
                         "verbose: 2\n"
                         "quiet: true\n");
}

CLAPP_TEST("tutorial 03 builder: get_many on an argument that collected nothing is empty") {
    // `get_many` returns an empty optional rather than an empty range here, which is why
    // the example's helper has to test it — printing `[]` is the visible consequence.
    CLAPP_CHECK(run({"Alice"}).out_has("tags: []\n"));
}

CLAPP_TEST("tutorial 03 builder: get_count is zero when the counter never matched") {
    CLAPP_CHECK(run({"Alice"}).out_has("verbose: 0\n"));
    CLAPP_CHECK(run({"Alice", "-vvv"}).out_has("verbose: 3\n"));
}

CLAPP_TEST("tutorial 03 builder: get_flag is false when the flag never matched") {
    CLAPP_CHECK(run({"Alice"}).out_has("quiet: false\n"));
    CLAPP_CHECK(run({"Alice", "-q"}).out_has("quiet: true\n"));
}

CLAPP_TEST("tutorial 03 builder: required() on the positional keeps main's read total") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("  <NAME>\n"));
    CLAPP_CHECK(r.err_has("Usage: 03_args <NAME> [PORT]\n"));
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 03 builder: the derive edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"Alice"});
    pair_agrees({"Alice", "8080", "-t", "red", "-t", "blue", "-vv", "--quiet"});
    pair_agrees({"Alice", "-t", "red", "blue"});
    pair_agrees({"Alice", "--tag=red"});
    pair_agrees({"Alice", "nope"});
    pair_agrees({"Alice", "-t"});
    pair_agrees({"Alice", "-q", "-q"});
    pair_agrees({"Alice", "1", "2"});
}

#else

CLAPP_TEST("tutorial 03 builder: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
