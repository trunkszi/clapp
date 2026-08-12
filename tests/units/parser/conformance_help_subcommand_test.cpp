#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // clap's shared `utils::complex_app()`
    // ---------------------------------------------------------------------------
    //
    // Transcribed rather than shared with conformance_tests_test.cpp and
    // conformance_opts_test.cpp, for the reason that file's own header gives: each
    // conformance file is self-contained so that a change made for one file's sake cannot
    // silently rewrite another file's expectations.
    //
    // TWO THINGS THIS COPY HAS THAT conformance_tests_test.cpp's DELIBERATELY DROPS, because
    // that file compares `ArgMatches` dumps and this one compares help screens:
    //
    //   * `help_template(FULL_TEMPLATE)` on the root AND on `subcmd`. clap's
    //     `utils::complex_app()` sets both; `subcmd`'s is what produces the three heading
    //     lines in `complex_subcommand_help_output`.
    //   * `author(...)` on `subcmd`. Only `{author-with-newline}` can show it, so it is
    //     invisible without the template.
    //
    // One deliberate simplification:
    // clap hangs `["fast", "slow"]` / `["vi", "emacs"]` on the ARGUMENT via `.value_parser`,
    // while in clapp a value domain belongs to the TYPE. Neither domain is on `subcmd`, and
    // no case in this file renders the root's page, so nothing here observes the difference.

    /** clap's `utils::FULL_TEMPLATE`, byte for byte. */
    inline constexpr std::string_view full_template = "{before-help}{name} {version}\n"
                                                      "{author-with-newline}{about-with-newline}\n"
                                                      "{usage-heading} {usage}\n"
                                                      "\n"
                                                      "{all-args}{after-help}";

    consteval command_spec make_complex_app() {
        command_builder app("clap-test");
        std::move(app)
                .version("v1.4.8")
                .about("tests clap library")
                .author("Kevin K. <kbknapp@gmail.com>")
                .help_template(full_template)
                .arg(arg_builder("option")
                             .short_('o')
                             .long_("option")
                             .value_name("opt")
                             .help("tests options")
                             .required(false)
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("positional").index(1).help("tests positionals"))
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("tests flags")
                             .action(arg_action::count)
                             .global())
                .arg(arg_builder("flag2")
                             .short_('F')
                             .help("tests flags with exclusions")
                             .conflicts_with("flag")
                             .requires_("long-option-2")
                             .action(arg_action::set_true))
                .arg(arg_builder("long-option-2")
                             .long_("long-option-2")
                             .value_name("option2")
                             .help("tests long options with exclusions")
                             .conflicts_with("option")
                             .requires_("positional2")
                             .action(arg_action::set))
                .arg(arg_builder("positional2").index(2).help("tests positionals with exclusions"))
                .arg(arg_builder("option3")
                             .short_('O')
                             .long_("option3")
                             .value_name("option3")
                             .help("specific vals")
                             .action(arg_action::set))
                .arg(arg_builder("positional3")
                             .index(3)
                             .help("tests specific values")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("multvals")
                             .long_("multvals")
                             .help("Tests multiple values, not mult occs")
                             .value_names({"one", "two"}))
                .arg(arg_builder("multvalsmo")
                             .long_("multvalsmo")
                             .help("Tests multiple values, and mult occs")
                             .value_names({"one", "two"})
                             .action(arg_action::append))
                .arg(arg_builder("minvals2")
                             .long_("minvals2")
                             .value_name("minvals")
                             .help("Tests 2 min vals")
                             .num_args(value_range::at_least(2)))
                .arg(arg_builder("maxvals3")
                             .long_("maxvals3")
                             .value_name("maxvals")
                             .help("Tests 3 max vals")
                             .num_args(value_range{1, 3}))
                .arg(arg_builder("optvaleq")
                             .long_("optvaleq")
                             .value_name("optval")
                             .help("Tests optional value, require = sign")
                             .num_args(value_range{0, 1})
                             .require_equals())
                .arg(arg_builder("optvalnoeq")
                             .long_("optvalnoeq")
                             .value_name("optval")
                             .help("Tests optional value")
                             .num_args(value_range{0, 1}))
                .subcommand(command_builder("subcmd")
                                    .about("tests subcommands")
                                    .version("0.1")
                                    .author("Kevin K. <kbknapp@gmail.com>")
                                    .help_template(full_template)
                                    .arg(arg_builder("option")
                                                 .short_('o')
                                                 .long_("option")
                                                 .value_name("scoption")
                                                 .help("tests options")
                                                 .num_args(value_range::at_least(1)))
                                    .arg(arg_builder("subcmdarg")
                                                 .short_('s')
                                                 .long_("subcmdarg")
                                                 .value_name("subcmdarg")
                                                 .help("tests other args")
                                                 .action(arg_action::set))
                                    .arg(arg_builder("scpositional")
                                                 .index(1)
                                                 .help("tests positionals")));
        return app.freeze();
    }
    constexpr command_spec complex_app = make_complex_app();

    // ---------------------------------------------------------------------------
    // clap's `multi_level_sc_help` fixture
    // ---------------------------------------------------------------------------
    //
    // The middle command is bare on purpose: no about, no version, no arguments. It is the
    // thing an implementation that resolves only one level of `help <path>` would print
    // instead of `multi`'s page, and its page is not merely different but nearly empty, so
    // the failure is unmistakable.

    consteval command_spec make_ctest() {
        command_builder app("ctest");
        std::move(app).subcommand(command_builder("subcmd").subcommand(
                command_builder("multi")
                        .about("tests subcommands")
                        .author("Kevin K. <kbknapp@gmail.com>")
                        .version("0.1")
                        .arg(arg_builder("flag")
                                     .short_('f')
                                     .long_("flag")
                                     .help("tests flags")
                                     .action(arg_action::set_true))
                        .arg(arg_builder("option")
                                     .short_('o')
                                     .long_("option")
                                     .value_name("scoption")
                                     .help("tests options")
                                     .required(false)
                                     .num_args(value_range::at_least(1))
                                     .action(arg_action::append))));
        return app.freeze();
    }
    constexpr command_spec ctest = make_ctest();

    // ---------------------------------------------------------------------------
    // clap's `help_subcmd_help` / `subcmd_help_subcmd_help` fixture
    // ---------------------------------------------------------------------------
    //
    // clap declares this identically for both cases; the two differ only in argv.

    consteval command_spec make_myapp() {
        command_builder app("myapp");
        std::move(app).subcommand(
                command_builder("subcmd").subcommand(command_builder("multi").version("1.0")));
        return app.freeze();
    }
    constexpr command_spec myapp = make_myapp();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants — asserted where the fixtures are built, because every
    // expectation below is meaningless if these are not true.
    // ---------------------------------------------------------------------------

    // `-f, --flag` is declared on the ROOT and is global. If it ever stopped being either,
    // `complex_subcommand_help_output` would still be a valid-looking screen with one fewer
    // row, and nothing else in this file would notice.
    static_assert(complex_app.find_arg("flag")->get_action() == arg_action::count);
    static_assert(complex_app.find_arg("flag")->is_global_set());
    static_assert(!complex_app.find_subcommand("subcmd")->has_arg("flag2"));

    // The propagated clone keeps the PARENT's display order — 1, since `option` is the
    // root's first non-positional and positionals do not consume a number. This is the
    // single fact that puts `-f` between `-o` and `-s` on the child's page; asserting it
    // here names the mechanism, and the rendered screen below proves it is honoured.
    static_assert(complex_app.find_arg("flag")->get_display_order() == 1);
    static_assert(complex_app.find_subcommand("subcmd")->find_arg("flag")->get_display_order() ==
                  1);
    static_assert(complex_app.find_subcommand("subcmd")->find_arg("option")->get_display_order() ==
                  0);
    static_assert(
            complex_app.find_subcommand("subcmd")->find_arg("subcmdarg")->get_display_order() == 1);

    // The template is per-command and is NOT inherited from the root by propagation: clap
    // sets it twice, so clapp must too. If clapp ever started propagating it, dropping the
    // inner `help_template()` call would go unnoticed — hence the assertion on the child.
    static_assert(complex_app.find_subcommand("subcmd")->get_help_template().has_value());

    // `{name}` on the child is hyphen-joined and `{usage}` is space-joined; both appear on
    // `complex_subcommand_help_output`'s screen, three lines apart.
    static_assert(complex_app.find_subcommand("subcmd")->get_display_name() ==
                  std::optional<std::string_view>{"clap-test-subcmd"});

    // The generated `help` command exists at BOTH levels of `myapp`, which is what
    // `subcmd_help_subcmd_help` addresses. A single root-level `help` would make that case
    // indistinguishable from `help_subcmd_help`.
    static_assert(myapp.has_subcommand("help"));
    static_assert(myapp.find_subcommand("subcmd")->has_subcommand("help"));

    // ...and neither generated command has a help or version flag of its own, which is why
    // the two pages below have no `Options:` section at all.
    static_assert(!myapp.find_subcommand("help")->has_arg("help"));
    static_assert(!myapp.find_subcommand("help")->has_arg("version"));

}  // namespace

// ---------------------------------------------------------------------------
// Three spellings, one page
// ---------------------------------------------------------------------------
//
// clap asserts only `ErrorKind::DisplayHelp` for these three. The page each of them
// prints is `complex_subcommand_help_output`'s, asserted there for the `--help`
// spelling; it is repeated in full here for the other two because the kind alone cannot
// tell "printed `subcmd`'s help" from "printed the root's help", and `help subcmd` is
// exactly the spelling where those two can be confused.

/**
 * clap's expected block for `complex_subcommand_help_output`, shared by the four cases
 * that reach `subcmd`'s page. Byte for byte from help.rs:723-742, including the two
 * trailing spaces nothing pads after `[scpositional]`.
 */
constexpr std::string_view complex_subcmd_page =
        "clap-test-subcmd 0.1\n"
        "Kevin K. <kbknapp@gmail.com>\n"
        "tests subcommands\n"
        "\n"
        "Usage: clap-test subcmd [OPTIONS] [scpositional]\n"
        "\n"
        "Arguments:\n"
        "  [scpositional]  tests positionals\n"
        "\n"
        "Options:\n"
        "  -o, --option <scoption>...   tests options\n"
        "  -f, --flag...                tests flags\n"
        "  -s, --subcmdarg <subcmdarg>  tests other args\n"
        "  -h, --help                   Print help\n"
        "  -V, --version                Print version\n";

CLAPP_TEST("help.rs::subcommand_short_help") {
    const outcome got = clapp::parse(complex_app, raw_args{"clap-test", "subcmd", "-h"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), complex_subcmd_page));
}

CLAPP_TEST("help.rs::subcommand_long_help") {
    // `subcmd` has no long_about and no long_help anywhere, so `--help` collapses onto
    // the same screen `-h` prints — the help flag's own description stays the bare
    // "Print help" rather than gaining a cross-reference.
    const outcome got = clapp::parse(complex_app, raw_args{"clap-test", "subcmd", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), complex_subcmd_page));
}

CLAPP_TEST("help.rs::subcommand_help_rev") {
    // The reverse spelling: the generated `help` command takes `subcmd` as a TOPIC and
    // must print that sibling's page, not its own. `Usage: clap-test subcmd …` is the
    // only thing separating the two outcomes.
    const outcome got = clapp::parse(complex_app, raw_args{"clap-test", "help", "subcmd"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), complex_subcmd_page));
}

CLAPP_TEST("help.rs::complex_subcommand_help_output") {
    // The case clap writes the page out for. Three things are simultaneously true here
    // and are true nowhere else in the file: `{name}` is the hyphen-joined display name,
    // `{usage}` is the space-joined path, and the global `-f, --flag...` sorts into the
    // middle of the child's own options by the number it was stamped with on the parent.
    const outcome got = clapp::parse(complex_app, raw_args{"clap-test", "subcmd", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), complex_subcmd_page));
    CLAPP_CHECK(!clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., false)`
}

// ---------------------------------------------------------------------------
// Depth
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::multi_level_sc_help") {
    // `ctest help subcmd multi` — two topic words, resolved left to right. `multi` is
    // the only command in the tree with an `about`, a version or any arguments, so the
    // screen below is unreachable from either of the other two commands.
    const outcome got = clapp::parse(ctest, raw_args{"ctest", "help", "subcmd", "multi"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "tests subcommands\n"
                     "\n"
                     "Usage: ctest subcmd multi [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -f, --flag                  tests flags\n"
                     "  -o, --option <scoption>...  tests options\n"
                     "  -h, --help                  Print help\n"
                     "  -V, --version               Print version\n"));
    CLAPP_CHECK(!clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., false)`
}

// ---------------------------------------------------------------------------
// The generated `help` command describing itself
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::help_subcmd_help") {
    const outcome got = clapp::parse(myapp, raw_args{"myapp", "help", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Usage: myapp help [COMMAND]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [COMMAND]...  Print help for the subcommand(s)\n"));
    CLAPP_CHECK(!clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., false)`
}

CLAPP_TEST("help.rs::subcmd_help_subcmd_help") {
    // Identical to the previous page except for the word `subcmd` in the usage line.
    // That word is the whole case: each command generates its OWN `help`, whose usage is
    // prefixed with its own path. A single shared `help` command would print
    // `Usage: myapp help [COMMAND]...` here too, and the two cases would be one.
    const outcome got = clapp::parse(myapp, raw_args{"myapp", "subcmd", "help", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Usage: myapp subcmd help [COMMAND]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [COMMAND]...  Print help for the subcommand(s)\n"));
    CLAPP_CHECK(!clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., false)`
}
