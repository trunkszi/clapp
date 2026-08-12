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
// The settings that are explicit here
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04 builder: subcommand_required and arg_required_else_help together") {
    // The help page on stderr with status 2 — clap's
    // DisplayHelpOnMissingArgumentOrSubcommand. Dropping either call changes exactly one
    // of the two halves: without arg_required_else_help this is a one-line error, without
    // subcommand_required it is not an error at all.
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("Usage: 04_subcommands <COMMAND>\n"));
    CLAPP_CHECK(r.err_has("  add     Adds files to myapp\n"));
    CLAPP_CHECK(!r.err_has("error:"));
}

CLAPP_TEST("tutorial 04 builder: propagate_version reaches both verbs") {
    CLAPP_CHECK(run({"--version"}).out == "04_subcommands 1.0.0\n");
    // The *hyphenated* name, not the bare verb: a version line names the program it
    // came from, and a subcommand's program identity is `<parent>-<verb>`. clap's own
    // examples/tutorial_builder/03_04_subcommands.md pins `clap-add [..]` for exactly
    // this call, and running that example here prints `clap-add 4.6.5`.
    //
    // These two lines read `add 1.0.0` / `remove 1.0.0` until 2026-08. That was not a
    // stricter expectation than clap's, it was the wrong one: `display_name` was never
    // derived for a subcommand, so every `prog verb --version` in the library reported
    // the verb alone. See clapp::command_builder::propagate_into().
    CLAPP_CHECK(run({"add", "--version"}).out == "04_subcommands-add 1.0.0\n");
    CLAPP_CHECK(run({"remove", "-V"}).out == "04_subcommands-remove 1.0.0\n");
}

// ---------------------------------------------------------------------------
// Dispatching on a name rather than on a type
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04 builder: both verbs reach an arm, never the fallthrough") {
    // Status 70 is the example's "no arm for this subcommand" exit; seeing it here would
    // mean the chain in main and the tree in build() had come apart.
    const run_result add = run({"add", "notes.txt"});
    CLAPP_CHECK(add.status == 0);
    CLAPP_CHECK(add.err.empty());
    CLAPP_CHECK(add.out == "'add' was used, name is: Some(\"notes.txt\")\n");

    const run_result remove = run({"remove"});
    CLAPP_CHECK(remove.status == 0);
    CLAPP_CHECK(remove.err.empty());
    CLAPP_CHECK(remove.out == "'remove' was used, name is: None\n");
}

CLAPP_TEST("tutorial 04 builder: an unknown verb never reaches main at all") {
    // The parser rejects it, so the string comparison in main is only ever handed a name
    // the tree declared.
    const run_result r = run({"bogus"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("unrecognized subcommand 'bogus'"));
}

CLAPP_TEST("tutorial 04 builder: the injected help verb is handled by clapp, not by main") {
    // `help` is a third subcommand that build() never declared, and main has no arm for
    // it — clapp intercepts it before main runs. Reaching the fallthrough here would be
    // status 70 with a message on stderr.
    const run_result r = run({"help", "add"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out_has("Usage: 04_subcommands add [NAME]\n"));
}

CLAPP_TEST("tutorial 04 builder: the verb() helper produced two identical shapes") {
    // Both subcommands come from one consteval function, so their pages must differ only
    // in the three strings it was given.
    CLAPP_CHECK(run({"add", "--help"}).out_has("  [NAME]  File to add\n"));
    CLAPP_CHECK(run({"remove", "--help"}).out_has("  [NAME]  File to remove\n"));
    CLAPP_CHECK(run({"add", "--help"}).out_has("Usage: 04_subcommands add [NAME]\n"));
    CLAPP_CHECK(run({"remove", "--help"}).out_has("Usage: 04_subcommands remove [NAME]\n"));
}

// ---------------------------------------------------------------------------
// The pairing
// ---------------------------------------------------------------------------

CLAPP_TEST("tutorial 04 builder: the derive edition answers identically, on every path") {
    pair_agrees({"--help"});
    pair_agrees({"-h"});
    pair_agrees({"--version"});
    pair_agrees({});
    pair_agrees({"add"});
    pair_agrees({"add", "notes.txt"});
    pair_agrees({"remove", "notes.txt"});
    pair_agrees({"remove", "-V"});
    pair_agrees({"remove", "--help"});
    pair_agrees({"help"});
    pair_agrees({"help", "add"});
    pair_agrees({"bogus"});
    pair_agrees({"add", "a", "b"});
}

#else

CLAPP_TEST("tutorial 04 builder: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
