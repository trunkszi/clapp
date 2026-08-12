#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <print>
#include <span>
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

    /**
     * \brief Whole-page comparison, printing both sides on a mismatch. clap compares the
     *        entire screen and so does this; a substring check cannot tell a missing
     *        `Options:` section from a present one.
     */
    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    /** clap: `Command::new("example").subcommand(Command::new("test").about("Subcommand"))` */
    consteval command_spec make_example() {
        command_builder app("example");
        std::move(app).subcommand(command_builder("test").about("Subcommand"));
        return app.freeze();
    }
    constexpr command_spec example = make_example();

    /**
     * clap:
     *     Command::new("test_app")
     *         .disable_help_flag(true)
     *         .subcommand(Command::new("test").about("Subcommand"))
     */
    consteval command_spec make_test_app() {
        command_builder app("test_app");
        std::move(app).disable_help_flag().subcommand(command_builder("test").about("Subcommand"));
        return app.freeze();
    }
    constexpr command_spec test_app = make_test_app();

    /**
     * clap:
     *     Command::new("example")
     *         .version("1.0")
     *         .propagate_version(true)
     *         .subcommand(Command::new("subcommand"))
     */
    consteval command_spec make_propagating() {
        command_builder app("example");
        std::move(app).version("1.0").propagate_version().subcommand(command_builder("subcommand"));
        return app.freeze();
    }
    constexpr command_spec propagating = make_propagating();

    /**
     * clap:
     *     Command::new("test")
     *         .arg(arg!(-h --hex <NUM>).required(true))
     *         .arg(arg!(--help).action(ArgAction::Help))
     *         .disable_help_flag(true)
     *
     * `arg!(-h --hex <NUM>)` expands to id `hex`, short `h`, long `hex`, value name `NUM`,
     * `ArgAction::Set` and `num_args(1)`; `arg!(--help)` to id `help`, long `help`, no
     * short. clapp spells both out.
     */
    consteval command_spec make_hex() {
        command_builder app("test");
        std::move(app)
                .disable_help_flag()
                .arg(arg_builder("hex")
                             .short_('h')
                             .long_("hex")
                             .value_name("NUM")
                             .required()
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("help").long_("help").action(arg_action::help));
        return app.freeze();
    }
    constexpr command_spec hex_cmd = make_hex();

    /**
     * clap:
     *     Command::new("myapp")
     *         .disable_help_flag(true)
     *         .version("1.0")
     *         .author("foo")
     *         .about("bar")
     *         .long_about("something really really long, with\n…")
     *         .arg(Arg::new("arg1").help("some option"))
     *         .arg(Arg::new("short").short('?').action(ArgAction::HelpShort))
     *         .arg(Arg::new("long").short('h').long("help").action(ArgAction::HelpLong))
     */
    consteval command_spec make_myapp() {
        command_builder app("myapp");
        std::move(app)
                .disable_help_flag()
                .version("1.0")
                .author("foo")
                .about("bar")
                .long_about(
                        "something really really long, with\nmultiple lines of text\nthat should "
                        "be displayed")
                .arg(arg_builder("arg1").index(1).help("some option"))
                .arg(arg_builder("short").short_('?').action(arg_action::help_short))
                .arg(arg_builder("long").short_('h').long_("help").action(arg_action::help_long));
        return app.freeze();
    }
    constexpr command_spec myapp = make_myapp();

    // ---------------------------------------------------------------------------
    // Spec-shape assertions — clap's `disable_help_flag_affects_help_subcommand` and the
    // first half of `help_without_short`, which are builder assertions in clap and therefore
    // compile-time assertions here.
    //
    // Both sides of every predicate are asserted, per trap 10 in CLAUDE.md: a `has_*` that
    // is only ever fed the absent case proves nothing about the present one.
    // ---------------------------------------------------------------------------

    // The generated `help` subcommand exists on both trees…
    static_assert(example.has_subcommand("help"));
    static_assert(test_app.has_subcommand("help"));

    // …and in neither of them does it carry a `help` argument. clap asserts only the
    // `disable_help_flag(true)` tree; the un-disabled tree is asserted alongside it because
    // that is what makes this a statement about the `help` SUBCOMMAND rather than about
    // `disable_help_flag()` — clap's own comment on the previous case says the flag is
    // useless there regardless of the parent's setting.
    static_assert(!example.find_subcommand("help")->has_arg("help"));
    static_assert(!test_app.find_subcommand("help")->has_arg("help"));

    // clap does not stop at a membership test: it COLLECTS every argument id of the generated
    // `help` subcommand (`get_arguments().map(get_id).collect::<Vec<_>>()`) and asserts `help`
    // is not among them. That collection is reproduced here rather than only the one
    // membership question, because `!has_arg("help")` is also satisfied by a `help` subcommand
    // that grew some *other* spurious flag. The generated command carries exactly one
    // argument, the topic positional.
    consteval bool help_subcommand_args_are_exactly_subcommand(const command_spec& spec) {
        const std::span<const clapp::arg_spec> args = spec.find_subcommand("help")->get_arguments();
        if (args.size() != 1) return false;
        return args[0].get_id().name() == std::string_view{"subcommand"};
    }
    static_assert(help_subcommand_args_are_exactly_subcommand(example));
    static_assert(help_subcommand_args_are_exactly_subcommand(test_app));
    static_assert(help_subcommand_args_are_exactly_subcommand(propagating));

    // The positive side of the same predicate, so `has_arg` is not merely always false here:
    // the topic argument IS present, and the parent's own `help` flag is present exactly when
    // `disable_help_flag()` was not called.
    static_assert(example.find_subcommand("help")->has_arg("subcommand"));
    static_assert(example.has_arg("help"));
    static_assert(!test_app.has_arg("help"));

    // `dont_propagate_version_to_help_subcommand`, spec half: the version flag reaches the
    // real subcommand and stops at the generated one.
    static_assert(propagating.find_subcommand("subcommand")->has_arg("version"));
    static_assert(!propagating.find_subcommand("help")->has_arg("version"));

    // `help_without_short`, first half: `assert_eq!(help.get_short(), None)`. `-h` belongs to
    // `--hex`, and the `help` argument has no short spelling at all.
    static_assert(hex_cmd.has_arg("help"));
    static_assert(hex_cmd.find_arg("help")->get_short() == std::nullopt);
    static_assert(hex_cmd.find_arg("hex")->get_short() == std::optional<char>{'h'});

}  // namespace

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::subcommand_help_doesnt_have_useless_help_flag") {
    // clap's comment: "Since the `help` subcommand currently ignores the `--help` flag,
    // the output shouldn't have it." There is no `Options:` section at all — not an
    // empty one, not one holding only `-h, --help`.
    const outcome got = clapp::parse(example, raw_args{"example", "help", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Usage: example help [COMMAND]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [COMMAND]...  Print help for the subcommand(s)\n"));
}

CLAPP_TEST("help.rs::disable_help_flag_affects_help_subcommand") {
    // clap builds the command and inspects `find_subcommand("help").get_arguments()`.
    // clapp's tree is frozen in `consteval`, so the assertion is the `static_assert`
    // block above; this case exists so the clap name has a runtime witness that reports
    // the same conclusion, per the testing convention in CLAUDE.md.
    CLAPP_CHECK(!test_app.find_subcommand("help")->has_arg("help"));

    // And the screen agrees with the builder: `test_app help help` lists only the topic.
    const outcome got = clapp::parse(test_app, raw_args{"test_app", "help", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Usage: test_app help [COMMAND]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [COMMAND]...  Print help for the subcommand(s)\n"));
}

CLAPP_TEST("help.rs::dont_propagate_version_to_help_subcommand") {
    // `version("1.0")` + `propagate_version(true)`, and the page is byte-identical to
    // `subcommand_help_doesnt_have_useless_help_flag`'s: no `-V, --version` anywhere.
    const outcome got = clapp::parse(propagating, raw_args{"example", "help", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Usage: example help [COMMAND]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [COMMAND]...  Print help for the subcommand(s)\n"));
}

CLAPP_TEST("help.rs::help_without_short") {
    // `-h` is the author's, not the help system's: `test -h 0x100` must parse a value.
    const outcome got = clapp::parse(hex_cmd, raw_args{"test", "-h", "0x100"});
    CLAPP_CHECK(got.has_value());
    if (got.has_value())
        CLAPP_CHECK(one_string(*got, "hex") == std::optional<std::string>{"0x100"});

    // The other half, restated at runtime: the `help` argument has no short spelling.
    CLAPP_CHECK(hex_cmd.find_arg("help")->get_short() == std::nullopt);
}

CLAPP_TEST("help.rs::explicit_short_long_help") {
    // `-?` is `HelpShort`: the compact screen, headed by `about`.
    const outcome short_form = clapp::parse(myapp, raw_args{"myapp", "-?"});
    CLAPP_CHECK(kind_of(short_form) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(short_form),
                     "bar\n"
                     "\n"
                     "Usage: myapp [arg1]\n"
                     "\n"
                     "Arguments:\n"
                     "  [arg1]  some option\n"
                     "\n"
                     "Options:\n"
                     "  -?             \n"
                     "  -h, --help     \n"
                     "  -V, --version  Print version\n"));

    // `-h` is `HelpLong`, and the SPELLING does not get a vote: a short flag prints the
    // long screen, headed by `long_about`, with every description on its own line.
    const std::string_view long_page = "something really really long, with\n"
                                       "multiple lines of text\n"
                                       "that should be displayed\n"
                                       "\n"
                                       "Usage: myapp [arg1]\n"
                                       "\n"
                                       "Arguments:\n"
                                       "  [arg1]\n"
                                       "          some option\n"
                                       "\n"
                                       "Options:\n"
                                       "  -?\n"
                                       "          \n"
                                       "\n"
                                       "  -h, --help\n"
                                       "          \n"
                                       "\n"
                                       "  -V, --version\n"
                                       "          Print version\n";

    const outcome via_short_spelling = clapp::parse(myapp, raw_args{"myapp", "-h"});
    CLAPP_CHECK(kind_of(via_short_spelling) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(via_short_spelling), long_page));

    // …and the same argument's long spelling prints the same screen, which is what makes
    // the previous assertion a statement about the ACTION rather than about `-h`.
    const outcome via_long_spelling = clapp::parse(myapp, raw_args{"myapp", "--help"});
    CLAPP_CHECK(kind_of(via_long_spelling) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(via_long_spelling), long_page));
}
