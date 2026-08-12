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
// value_parser<T>() supplies the same domain the derive layer deduces
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05 builder: value_parser<run_mode>() alone produces the value set") {
    // No possible-value list is written anywhere in the builder example; it comes from the
    // enum, through the parser. If it did not, this message would have no bracketed tail.
    const run_result r = run({"quick", "--major"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("invalid value 'quick' for '<MODE>'"));
    CLAPP_CHECK(r.err_has("[possible values: fast, slow]"));
}

CLAPP_TEST("tutorial 05 builder: and the same set reaches the help page") {
    CLAPP_CHECK(run({"-h"}).out_has("[possible values: fast, slow]"));
    CLAPP_CHECK(run({"--help"}).out_has("          - fast: Run swiftly\n"));
    CLAPP_CHECK(run({"--help"}).out_has("          - slow: Crawl slowly but steadily\n"));
}

CLAPP_TEST("tutorial 05 builder: the custom parser's own sentence and range are reported") {
    CLAPP_CHECK(run({"fast", "--major", "--port", "1"}).out_has("port: Some(1)\n"));
    CLAPP_CHECK(run({"fast", "--major", "--port", "65535"}).out_has("port: Some(65535)\n"));
    CLAPP_CHECK(run({"fast", "--major", "--port", "65536"})
                        .err_has("invalid value '65536' for '--port <PORT>': port not in range "
                                 "1-65535"));
    CLAPP_CHECK(run({"fast", "--major", "--port", "0"}).err_has("port not in range 1-65535"));
    CLAPP_CHECK(
            run({"fast", "--major", "--port", "eight"}).err_has("invalid digit found in string"));
}

// ---------------------------------------------------------------------------
// Ids, long spellings and value placeholders are three different strings
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05 builder: the id is set_ver and the option is --set-ver") {
    // `main` reads `get_one<std::string>("set_ver")` while the user types `--set-ver`, and
    // `conflicts_with("major")` names an id rather than a spelling. Getting either wrong
    // is a run-time abort or a silently absent relation, never a compile error.
    CLAPP_CHECK(run({"slow", "--set-ver", "4.5.6"}).out_has("version: 4.5.6\n"));
    CLAPP_CHECK(run({"fast", "--major"}).out_has("version: 2.0.0\n"));

    const run_result underscored = run({"slow", "--set_ver", "4.5.6"});
    CLAPP_CHECK(underscored.status == 2);
    CLAPP_CHECK(underscored.err_has("unexpected argument '--set_ver' found"));
}

CLAPP_TEST("tutorial 05 builder: the placeholder comes from value_name, not from the id") {
    // `<VER>` and `<PORT>` rather than `<set_ver>` and `<port>`; `<MODE>` rather than
    // `<mode>`. Every one of the three is a value_name() call in the tree.
    CLAPP_CHECK(run({"-h"}).out_has("      --set-ver <VER>  Set the version manually\n"));
    CLAPP_CHECK(run({"-h"}).out_has("      --port <PORT>    Network port to use\n"));
    CLAPP_CHECK(run({"-h"}).out_has("Usage: 05_validation [OPTIONS] <MODE>\n"));
}

// ---------------------------------------------------------------------------
// The relations
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05 builder: conflicts_with and required_unless_present are both live") {
    const run_result both = run({"fast", "--major", "--set-ver", "9"});
    CLAPP_CHECK(both.status == 2);
    CLAPP_CHECK(both.err_has("the argument '--major' cannot be used with '--set-ver <VER>'"));

    const run_result neither = run({"fast"});
    CLAPP_CHECK(neither.status == 2);
    CLAPP_CHECK(neither.err_has("  --set-ver <VER>\n"));
}

CLAPP_TEST("tutorial 05 builder: required() on the positional is independent of the relations") {
    const run_result r = run({"--set-ver", "1.0"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("  <MODE>\n"));
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 05 builder: the derive edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"fast", "--major"});
    pair_agrees({"slow", "--set-ver", "4.5.6", "--port", "8080"});
    pair_agrees({"fast", "--major", "--port", "1"});
    pair_agrees({"fast", "--major", "--port", "65535"});
    pair_agrees({"fast", "--major", "--port", "0"});
    pair_agrees({"fast", "--major", "--port", "eight"});
    pair_agrees({"quick", "--major"});
    pair_agrees({"FAST", "--major"});
    pair_agrees({"fast"});
    pair_agrees({"fast", "--major", "--set-ver", "9"});
    pair_agrees({"slow", "--set_ver", "4.5.6"});
    pair_agrees({"--set-ver", "1.0"});
}

#else

CLAPP_TEST("tutorial 05 builder: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
