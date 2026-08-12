#include "support/check.hpp"
#include "support/subprocess.hpp"

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_result;

// ---------------------------------------------------------------------------
// implicit: the types clapp already knows
// ---------------------------------------------------------------------------

CLAPP_TEST("typed: implicit parses an integer, a path, a double and an enum") {
    const run_result r =
            run({"implicit", "-O", "3", "-I", "/tmp", "--sleep", "1.5", "--bump-level", "minor"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "optimization: Some(3)\n"
                         "include: Some(/tmp)\n"
                         "sleep: Some(1.5)\n"
                         "bump-level: Some(minor)\n");
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("typed: every implicit option is optional and stays nullopt") {
    const run_result r = run({"implicit"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "optimization: None\n"
                         "include: None\n"
                         "sleep: None\n"
                         "bump-level: None\n");
}

CLAPP_TEST("typed: an unknown enum value lists what enumerators_of found") {
    const run_result r = run({"implicit", "--bump-level", "nope"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("nope"));
    CLAPP_CHECK(r.err_has("major"));
    CLAPP_CHECK(r.err_has("minor"));
    CLAPP_CHECK(r.err_has("patch"));
}

CLAPP_TEST("typed: -O rejects a non-number") {
    const run_result r = run({"implicit", "-O", "eight"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("eight"));
}

// ---------------------------------------------------------------------------
// builtin: restricted domains, member-initializer defaults, arity from the type
// ---------------------------------------------------------------------------

CLAPP_TEST("typed: builtin with nothing prints the defaults") {
    const run_result r = run({"builtin"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "port: 22\n"
                         "log-level: info\n"
                         "bind: localhost:0\n"
                         "origin: 0,0,0\n");
}

CLAPP_TEST("typed: std::pair takes exactly two values and std::array exactly three") {
    const run_result r = run({"builtin",
                              "--port",
                              "80",
                              "--log-level",
                              "warn",
                              "--bind",
                              "example.com",
                              "8080",
                              "--origin",
                              "1",
                              "2",
                              "3"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "port: 80\n"
                         "log-level: warn\n"
                         "bind: example.com:8080\n"
                         "origin: 1,2,3\n");
}

CLAPP_TEST("typed: --origin with too few values is an error") {
    // The negative half of "arity comes from the type". Without it, an implementation
    // that ignored the extent would pass the case above.
    const run_result r = run({"builtin", "--origin", "1", "2"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("--origin"));
}

CLAPP_TEST("typed: --bind with one value is an error") {
    const run_result r = run({"builtin", "--bind", "example.com"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("--bind"));
}

CLAPP_TEST("typed: a custom value_parser reports its possible_values on rejection") {
    const run_result r = run({"builtin", "--port", "443"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("443"));
    CLAPP_CHECK(r.err_has("22"));
    CLAPP_CHECK(r.err_has("80"));
}

CLAPP_TEST("typed: a hidden, renamed enum value is still matched") {
    const run_result r = run({"builtin", "--log-level", "off"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out_has("log-level: off\n"));
}

CLAPP_TEST("typed: the hidden value is kept out of the listed set") {
    const run_result r = run({"builtin", "--log-level", "nope"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("trace"));
    CLAPP_CHECK(r.err_has("error"));
    // `quiet` was renamed to `off` AND hidden; the renamed spelling parses (above) but
    // neither spelling may appear in the list.
    CLAPP_CHECK(!r.err_has("off"));
    CLAPP_CHECK(!r.err_has("quiet"));
}

// ---------------------------------------------------------------------------
// fn-parser: one hand-written parser, used repeatedly
// ---------------------------------------------------------------------------

CLAPP_TEST("typed: -D appends, one key_value per occurrence") {
    const run_result r = run({"fn-parser", "-D", "KEY=1", "-D", "OTHER=2"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "defines: 2\n"
                         "  KEY = 1\n"
                         "  OTHER = 2\n");
}

CLAPP_TEST("typed: -D with no defines leaves the vector empty") {
    const run_result r = run({"fn-parser"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "defines: 0\n");
}

CLAPP_TEST("typed: the specialization's own reason reaches the user") {
    const run_result r = run({"fn-parser", "-D", "nope"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("nope"));
    CLAPP_CHECK(r.err_has("no `=` found in the value"));
}

CLAPP_TEST("typed: delegating to value_parser<int> reuses its wording") {
    // The right-hand side is parsed by the builtin integer parser, so the sentence is the
    // one a bare `int` argument would produce rather than a second, private wording.
    const run_result r = run({"fn-parser", "-D", "k=x"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("k=x"));
    CLAPP_CHECK(r.err_has("invalid digit found in string"));
}

// ---------------------------------------------------------------------------
// custom: a parser that publishes possible_values() but accepts more
// ---------------------------------------------------------------------------

CLAPP_TEST("typed: a listed spelling parses as the relative form") {
    const run_result r = run({"custom", "--target-version", "major"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "target-version: Relative(major)\n");
}

CLAPP_TEST("typed: an unlisted spelling is still accepted, as clap's parser does") {
    // possible_values() is a hint here, not a contract: clapp never re-validates against
    // it, so the parser stays the single authority.
    const run_result r = run({"custom", "--target-version", "1.2.3"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "target-version: Absolute(1.2.3)\n");
}

CLAPP_TEST("typed: custom with nothing leaves the optional empty") {
    const run_result r = run({"custom"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "target-version: None\n");
}

// ---------------------------------------------------------------------------
// The root
// ---------------------------------------------------------------------------

CLAPP_TEST("typed: --version reports the annotated version") {
    const run_result r = run({"--version"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "typed 1.0.0\n");
}

CLAPP_TEST("typed: --help names the root, a subcommand --help names the subcommand") {
    const run_result root = run({"--help"});
    CLAPP_CHECK(root.status == 0);
    CLAPP_CHECK(root.err.empty());
    CLAPP_CHECK(root.out ==
                "Typed values, four ways\n"
                "\n"
                "Usage: typed <COMMAND>\n"
                "\n"
                "Commands:\n"
                "  implicit   Types clapp already knows how to parse\n"
                "  builtin    Restricted value sets and fixed arities\n"
                "  fn-parser  A hand-written value_parser for KEY=VALUE\n"
                "  custom     A value_parser that publishes possible_values()\n"
                "  help       Print this message or the help of the given subcommand(s)\n"
                "\n"
                "Options:\n"
                "  -h, --help     Print help\n"
                "  -V, --version  Print version\n");

    const run_result sub = run({"builtin", "--help"});
    CLAPP_CHECK(sub.status == 0);
    CLAPP_CHECK(sub.out_has("Usage: typed builtin"));
}

// ---------------------------------------------------------------------------
// -h versus --help
//
// This is the one scenario in the set that HAS a long form, so it is the only place
// the pair can be asserted end to end. Everything that distinguishes the two screens is
// visible below and nowhere else in the scenario suite:
//
//   * the bracketed tail (`[default: 22] [possible values: 22, 80]`) sits inline on `-h`
//     and moves onto its own lines on `--help`;
//   * `--log-level`'s possible values become a `Possible values:` block, with each value's
//     own help aligned — `- warn:  Only warnings and errors` carries clap's
//     `longest - display_width(name)` padding, which is why there are two spaces there and
//     one after every other colon;
//   * the injected `--help` row changes its own text, from
//     "see more with '--help'" to "see a summary with '-h'".
//
// BOTH PAGES USE NEXT-LINE HELP, and that is a measured consequence rather than a
// setting: `--bind <HOST PORT> <HOST PORT>` makes the first column wider than 40% of the
// 100-column default, which trips clap's `force_next_line`. A renderer that ignored that
// rule would put the descriptions in a second column and fail here.
//
// THE BLANK LINES ARE NOT BLANK. `'          '` between a description and its bracketed
// tail is ten spaces: clap indents *every* line of a wrapped block, empty ones included,
// and `r.out_has(...)` could never see the difference.
// ---------------------------------------------------------------------------

CLAPP_TEST("typed: --help spells the value sets out, -h keeps them inline") {
    const run_result long_form = run({"builtin", "--help"});
    CLAPP_CHECK(long_form.status == 0);
    CLAPP_CHECK(long_form.err.empty());
    CLAPP_CHECK(long_form.out == "Restricted value sets and fixed arities\n"
                                 "\n"
                                 "Usage: typed builtin [OPTIONS]\n"
                                 "\n"
                                 "Options:\n"
                                 "      --port <port>\n"
                                 "          Port to listen on\n"
                                 "          \n"
                                 "          [default: 22]\n"
                                 "          [possible values: 22, 80]\n"
                                 "\n"
                                 "      --log-level <level>\n"
                                 "          Log verbosity\n"
                                 "\n"
                                 "          Possible values:\n"
                                 "          - trace\n"
                                 "          - debug\n"
                                 "          - info\n"
                                 "          - warn:  Only warnings and errors\n"
                                 "          - error\n"
                                 "          \n"
                                 "          [default: info]\n"
                                 "\n"
                                 "      --bind <HOST PORT> <HOST PORT>\n"
                                 "          Host and port to bind\n"
                                 "\n"
                                 "      --origin <origin> <origin> <origin>\n"
                                 "          Three coordinates\n"
                                 "\n"
                                 "  -h, --help\n"
                                 "          Print help (see a summary with '-h')\n");

    const run_result short_form = run({"builtin", "-h"});
    CLAPP_CHECK(short_form.status == 0);
    CLAPP_CHECK(short_form.err.empty());
    CLAPP_CHECK(short_form.out ==
                "Restricted value sets and fixed arities\n"
                "\n"
                "Usage: typed builtin [OPTIONS]\n"
                "\n"
                "Options:\n"
                "      --port <port>\n"
                "          Port to listen on [default: 22] [possible values: 22, 80]\n"
                "      --log-level <level>\n"
                "          Log verbosity [default: info] [possible values: trace, debug, "
                "info, warn, error]\n"
                "      --bind <HOST PORT> <HOST PORT>\n"
                "          Host and port to bind\n"
                "      --origin <origin> <origin> <origin>\n"
                "          Three coordinates\n"
                "  -h, --help\n"
                "          Print help (see more with '--help')\n");

    // The negative half: the two screens must NOT be the same bytes. Without this, a
    // renderer that ignored `use_long` entirely would satisfy one of the two comparisons
    // above and this test would still read as if it covered both.
    CLAPP_CHECK(long_form.out != short_form.out);
}

CLAPP_TEST("typed: a bare variant requires a subcommand") {
    const run_result r = run({});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.err_has("Usage: typed"));
}

#else

CLAPP_TEST("typed: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
