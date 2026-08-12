#include "support/check.hpp"
#include "support/subprocess.hpp"

#include <string_view>

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_result;

// ---------------------------------------------------------------------------
// One subcommand, three spellings
// ---------------------------------------------------------------------------

CLAPP_TEST("pacman: the long subcommand name installs") {
    const run_result r = run({"sync", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Installing clap...\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("pacman: the short flag reaches the same subcommand") {
    const run_result r = run({"-S", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Installing clap...\n");
}

CLAPP_TEST("pacman: the long flag reaches it too") {
    const run_result r = run({"--sync", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Installing clap...\n");
}

// ---------------------------------------------------------------------------
// sync
// ---------------------------------------------------------------------------

CLAPP_TEST("pacman: -Ss bundles the subcommand flag with the subcommand's own short") {
    const run_result r = run({"-Ss", "clap", "ripgrep"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Searching for clap, ripgrep...\n");
}

CLAPP_TEST("pacman: --sync --search is the long form of the same thing") {
    const run_result r = run({"--sync", "--search", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Searching for clap...\n");
}

CLAPP_TEST("pacman: -Si takes the info branch") {
    const run_result r = run({"-Si", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Retrieving info for clap...\n");
}

CLAPP_TEST("pacman: sync takes more than one package") {
    const run_result r = run({"-S", "clap", "ripgrep", "fd"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Installing clap, ripgrep, fd...\n");
}

CLAPP_TEST("pacman: required_unless_present fires when neither is given") {
    // `package` is required unless `--search` was used.
    const run_result r = run({"-S"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("package"));
}

CLAPP_TEST("pacman: --search satisfies required_unless_present") {
    // The positive half of the rule above: with `--search` there need be no package.
    const run_result r = run({"-Ss", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Searching for clap...\n");
}

// ---------------------------------------------------------------------------
// query
// ---------------------------------------------------------------------------

CLAPP_TEST("pacman: -Q with nothing lists everything") {
    const run_result r = run({"-Q"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Displaying all locally installed packages...\n");
}

CLAPP_TEST("pacman: -Qs searches locally") {
    const run_result r = run({"-Qs", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Searching Locally for clap...\n");
}

CLAPP_TEST("pacman: -Qi shows info") {
    const run_result r = run({"-Qi", "clap"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "Retrieving info for clap...\n");
}

CLAPP_TEST("pacman: query's --search and --info conflict") {
    const run_result r = run({"-Q", "--search", "a", "--info", "b"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("--search"));
    CLAPP_CHECK(r.err_has("--info"));
}

// ---------------------------------------------------------------------------
// The root
// ---------------------------------------------------------------------------

// `$ pacman -h` in clap/examples/pacman.md, verbatim.
constexpr std::string_view pacman_help =
        "package manager utility\n"
        "\n"
        "Usage: pacman <COMMAND>\n"
        "\n"
        "Commands:\n"
        "  query, -Q, --query  Query the package database.\n"
        "  sync, -S, --sync    Synchronize packages.\n"
        "  help                Print this message or the help of the given subcommand(s)\n"
        "\n"
        "Options:\n"
        "  -h, --help     Print help\n"
        "  -V, --version  Print version\n";

CLAPP_TEST("pacman: subcommand_required plus arg_required_else_help") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    // `arg_required_else_help` prints the WHOLE short help page, not a usage line —
    // clap's `Validator::validate` calls `write_help_err(false)` here. Until M5 clapp
    // printed the `about` line and the usage line and stopped; asserting the page is what
    // stops that regressing.
    CLAPP_CHECK(r.err == pacman_help);
}

CLAPP_TEST("pacman: --version reports the builder's version") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "pacman 5.2.1\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("pacman: --help goes to stdout with status 0") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == pacman_help);
}

CLAPP_TEST("pacman: a flag subcommand's own help names it the way a user types it") {
    // `$ pacman -S -h` in clap/examples/pacman.md, verbatim. Three things here are not
    // reachable from the root page: the `{sync|--sync|-S}` usage spelling of a flag
    // subcommand, an `Arguments:` section, and a description column measured over that
    // subcommand's own arguments rather than the root's.
    const run_result r = run({"-S", "--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out ==
                "Synchronize packages.\n"
                "\n"
                "Usage: pacman {sync|--sync|-S} [OPTIONS] [package]...\n"
                "\n"
                "Arguments:\n"
                "  [package]...  packages\n"
                "\n"
                "Options:\n"
                "  -s, --search <search>...  search remote repositories for matching strings\n"
                "  -i, --info                view package information\n"
                "  -h, --help                Print help\n");
}

CLAPP_TEST("pacman: an unknown subcommand is an error naming it") {
    const run_result r = run({"upgrade"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("upgrade"));
}

#else

CLAPP_TEST("pacman: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
