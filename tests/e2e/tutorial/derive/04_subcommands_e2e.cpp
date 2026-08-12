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

    /** \brief The full root help page, which this step prints from three different places. */
    constexpr std::string_view root_help =
            "A program with more than one verb\n"
            "\n"
            "Usage: 04_subcommands <COMMAND>\n"
            "\n"
            "Commands:\n"
            "  add     Adds files to myapp\n"
            "  remove  Removes files from myapp\n"
            "  help    Print this message or the help of the given subcommand(s)\n"
            "\n"
            "Options:\n"
            "  -h, --help     Print help\n"
            "  -V, --version  Print version\n";

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
// What a bare std::variant implies
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04: no subcommand prints the help page, on stderr, and fails") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);       // subcommand_required
    CLAPP_CHECK(r.out.empty());       // ... so this is a failure, not help that was asked for
    CLAPP_CHECK(r.err == root_help);  // ... and arg_required_else_help chose the page
    // The page really is the same one, not a lookalike: same bytes, different stream and
    // different status.
    CLAPP_CHECK(r.err == run({"--help"}).out);
}

CLAPP_TEST("tutorial 04: --help is the same page, on the success path") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == root_help);
}

CLAPP_TEST("tutorial 04: an unknown verb is rejected, and is not the same as none") {
    const run_result r = run({"bogus"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err == "error: unrecognized subcommand 'bogus'\n"
                         "\n"
                         "Usage: 04_subcommands <COMMAND>\n"
                         "\n"
                         "For more information, try '--help'.\n");
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04: each verb reaches its own arm") {
    CLAPP_CHECK(run({"add", "notes.txt"}).out == "'add' was used, name is: Some(\"notes.txt\")\n");
    CLAPP_CHECK(run({"remove", "notes.txt"}).out ==
                "'remove' was used, name is: Some(\"notes.txt\")\n");
}

CLAPP_TEST("tutorial 04: each verb's positional is optional") {
    CLAPP_CHECK(run({"add"}).out == "'add' was used, name is: None\n");
    CLAPP_CHECK(run({"remove"}).out == "'remove' was used, name is: None\n");
}

CLAPP_TEST("tutorial 04: a verb declares one positional, so a second is unexpected") {
    const run_result r = run({"add", "a", "b"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("unexpected argument 'b' found"));
    // The usage line is the *subcommand's*, not the root's.
    CLAPP_CHECK(r.err_has("Usage: 04_subcommands add [NAME]\n"));
}

// ---------------------------------------------------------------------------
// propagate_version
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04: the root's version reaches every subcommand") {
    CLAPP_CHECK(run({"--version"}).out == "04_subcommands 1.0.0\n");
    // Without propagate_version these two would be "unexpected argument '--version'".
    //
    // The name on the line is the *hyphenated* one, clap's `display_name`: a version
    // line names the program it came from, and a subcommand's program identity is
    // `<parent>-<verb>`. clap's own examples/tutorial_builder/03_04_subcommands.md pins
    // `clap-add [..]` for this call, and running that example prints `clap-add 4.6.5`.
    //
    // Both lines read `add 1.0.0` / `remove 1.0.0` until 2026-08, which encoded a bug
    // rather than a divergence: `display_name` was never derived for a subcommand. See
    // clapp::command_builder::propagate_into().
    CLAPP_CHECK(run({"add", "--version"}).out == "04_subcommands-add 1.0.0\n");
    CLAPP_CHECK(run({"remove", "--version"}).out == "04_subcommands-remove 1.0.0\n");
}

CLAPP_TEST("tutorial 04: a subcommand's help page names the whole path") {
    const run_result r = run({"add", "--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Adds files to myapp\n"
                         "\n"
                         "Usage: 04_subcommands add [NAME]\n"
                         "\n"
                         "Arguments:\n"
                         "  [NAME]  File to add\n"
                         "\n"
                         "Options:\n"
                         "  -h, --help     Print help\n"
                         "  -V, --version  Print version\n");
}

CLAPP_TEST("tutorial 04: the injected help subcommand reaches the same page") {
    CLAPP_CHECK(run({"help"}).out == root_help);
    CLAPP_CHECK(run({"help", "add"}).out == run({"add", "--help"}).out);
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04: the builder edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"add"});
    pair_agrees({"add", "notes.txt"});
    pair_agrees({"remove"});
    pair_agrees({"add", "--version"});
    pair_agrees({"add", "--help"});
    pair_agrees({"help", "remove"});
    pair_agrees({"bogus"});
    pair_agrees({"add", "a", "b"});
}

#else

CLAPP_TEST("tutorial 04: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
