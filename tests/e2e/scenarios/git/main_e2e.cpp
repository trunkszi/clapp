#include "support/check.hpp"
#include "support/subprocess.hpp"

#include <string_view>

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_result;

// ---------------------------------------------------------------------------
// One level: dispatch to the right alternative
// ---------------------------------------------------------------------------

CLAPP_TEST("git: clone dispatches and takes its positional") {
    const run_result r = run({"clone", "https://example.com/repo.git"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Cloning https://example.com/repo.git\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("git: push is a different alternative, not clone") {
    const run_result r = run({"push", "origin"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Pushing to origin\n");
}

CLAPP_TEST("git: add collects a std::vector of paths") {
    const run_result r = run({"add", "a.txt", "b.txt", "dir/c.txt"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Adding [\"a.txt\", \"b.txt\", \"dir/c.txt\"]\n");
}

// ---------------------------------------------------------------------------
// Positionals, defaults and the -- escape
// ---------------------------------------------------------------------------

CLAPP_TEST("git: diff with nothing uses both fallbacks") {
    const run_result r = run({"diff"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Diffing stage..worktree  (color=auto)\n");
}

CLAPP_TEST("git: diff with one positional treats it as the path") {
    const run_result r = run({"diff", "base"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Diffing stage..worktree base (color=auto)\n");
}

CLAPP_TEST("git: diff with two positionals fills base and path") {
    const run_result r = run({"diff", "base", "head"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Diffing base..worktree head (color=auto)\n");
}

CLAPP_TEST("git: -- reaches the .last positional") {
    const run_result r = run({"diff", "base", "head", "--", "src/main.cpp"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Diffing base..head src/main.cpp (color=auto)\n");
}

CLAPP_TEST("git: without -- the third word has nowhere to go") {
    // The negative half of `.last = true`. An implementation that dropped `.last` would
    // accept this and pass every case above.
    const run_result r = run({"diff", "base", "head", "src/main.cpp"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("src/main.cpp"));
}

// ---------------------------------------------------------------------------
// The enumeration, with no ValueEnum opt-in
// ---------------------------------------------------------------------------

CLAPP_TEST("git: --color=WHEN parses a plain C++ enum") {
    const run_result r = run({"diff", "--color=never", "base"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Diffing stage..worktree base (color=never)\n");
}

CLAPP_TEST("git: --color with no value uses default_missing_value") {
    // require_equals means a bare `--color` cannot swallow the next word, so the
    // three-state row resolves to the missing value rather than to `base`.
    const run_result r = run({"diff", "--color", "base"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Diffing stage..worktree base (color=always)\n");
}

CLAPP_TEST("git: an unknown --color value lists the ones reflection found") {
    const run_result r = run({"diff", "--color=bogus"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("bogus"));
    // `auto_` lost its keyword-avoidance suffix on the way out of enumerators_of.
    CLAPP_CHECK(r.err_has("always"));
    CLAPP_CHECK(r.err_has("auto"));
    CLAPP_CHECK(r.err_has("never"));
}

// ---------------------------------------------------------------------------
// Two levels, and flatten beside a subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("git: stash with no nested subcommand uses the flattened args") {
    const run_result r = run({"stash"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Pushing StashPushArgs { message: None }\n");
}

CLAPP_TEST("git: stash -m reads the flattened --message") {
    const run_result r = run({"stash", "-m", "wip"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Pushing StashPushArgs { message: Some(\"wip\") }\n");
}

CLAPP_TEST("git: stash push -m reads the same struct as an alternative") {
    const run_result r = run({"stash", "push", "-m", "wip"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Pushing StashPushArgs { message: Some(\"wip\") }\n");
}

CLAPP_TEST("git: stash pop is a variant inside a variant") {
    const run_result r = run({"stash", "pop", "3"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Popping 3\n");
}

CLAPP_TEST("git: stash pop with no argument leaves the optional empty") {
    const run_result r = run({"stash", "pop"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Popping <top>\n");
}

CLAPP_TEST("git: stash apply is the third nested alternative") {
    const run_result r = run({"stash", "apply", "7"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Applying 7\n");
}

// ---------------------------------------------------------------------------
// Help and the required-subcommand rules
// ---------------------------------------------------------------------------

CLAPP_TEST("git: a bare variant makes the subcommand mandatory") {
    // A bare `std::variant` implies clap's `subcommand_required(true)
    // .arg_required_else_help(true)` pair, so an empty command line prints help and
    // leaves with status 2.
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(!r.err.empty());
    CLAPP_CHECK(r.err_has("Usage: git"));
}

CLAPP_TEST("git: arg_required_else_help on clone fires with no arguments") {
    const run_result r = run({"clone"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("Usage: git clone"));
}

CLAPP_TEST("git: arg_required_else_help on add fires with no paths") {
    const run_result r = run({"add"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("Usage: git add"));
}

CLAPP_TEST("git: --help lists the root usage, status 0 on stdout") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    // The `Commands:` table in declaration order, with the injected `help` last because
    // it keeps display order 999 while the five declared ones are numbered from 0. There
    // is no `Options:` entry for `--version`: this command sets none.
    CLAPP_CHECK(r.out ==
                std::string_view{"A fictional versioning CLI\n"
                                 "\n"
                                 "Usage: git <COMMAND>\n"
                                 "\n"
                                 "Commands:\n"
                                 "  clone  Clones repos\n"
                                 "  diff   Compare two commits\n"
                                 "  push   pushes things\n"
                                 "  add    adds things\n"
                                 "  stash  Stash the changes in a dirty working directory\n"
                                 "  help   Print this message or the help of the given "
                                 "subcommand(s)\n"
                                 "\n"
                                 "Options:\n"
                                 "  -h, --help  Print help\n"});
}

CLAPP_TEST("git: a subcommand has its own --help") {
    const run_result r = run({"clone", "--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    // `Usage: git clone`, not `Usage: clone`: the path is threaded down by the parse
    // loop, because a frozen command_spec cannot know its own parent (ADR-0005).
    CLAPP_CHECK(r.out == std::string_view{"Clones repos\n"
                                          "\n"
                                          "Usage: git clone <remote>\n"
                                          "\n"
                                          "Arguments:\n"
                                          "  <remote>  The remote to clone\n"
                                          "\n"
                                          "Options:\n"
                                          "  -h, --help  Print help\n"});
}

CLAPP_TEST("git: a nested subcommand has its own --help too") {
    const run_result r = run({"stash", "--help"});
    CLAPP_CHECK(r.status == 0);
    // Two levels down, and the flattened `-m/--message` sits beside the nested
    // `Commands:` table rather than inside one of them.
    CLAPP_CHECK(r.out == std::string_view{"Stash the changes in a dirty working directory\n"
                                          "\n"
                                          "Usage: git stash [OPTIONS] [COMMAND]\n"
                                          "\n"
                                          "Commands:\n"
                                          "  push   Push a new stash\n"
                                          "  pop    Remove a stash and apply it\n"
                                          "  apply  Apply a stash without removing it\n"
                                          "  help   Print this message or the help of the given "
                                          "subcommand(s)\n"
                                          "\n"
                                          "Options:\n"
                                          "  -m, --message <message>  \n"
                                          "  -h, --help               Print help\n"});
}

CLAPP_TEST("git: an unknown subcommand is an error naming it") {
    const run_result r = run({"bogus"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("bogus"));
}

#else

CLAPP_TEST("git: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
