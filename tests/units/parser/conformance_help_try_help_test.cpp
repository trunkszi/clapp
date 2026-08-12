#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
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

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    /** \brief clap's `write!(&mut buf, "{err}")` — the whole rich-format block. */
    std::string rendered(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    /**
     * \brief Whole-block comparison, printing both sides on a mismatch.
     *
     * \note Bind the string before comparing rather than chaining off `rendered()`: trap 12
     *       in CLAUDE.md. Here the owner is named, so it is safe either way — the named
     *       local is the habit, not a requirement of this call.
     */
    bool same(const outcome& got, std::string_view want) {
        const std::string text = rendered(got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // Eight commands, one per case, written as separate `consteval` builders rather than one
    // parameterised one so that a reader can diff any two of them directly. Three pairs are
    // meant to be read side by side: `plain` / `sub_plain`, `custom_flag` / `sub_custom_flag`,
    // and `custom_flag_no_action` / `sub_custom_flag_no_action` — each pair differs by exactly
    // one `.subcommand(command_builder("foo"))` call, which is the pairing the header
    // describes.
    // ---------------------------------------------------------------------------

    /** clap: `Command::new("ctest").version("1.0").term_width(0)`. */
    consteval command_spec make_plain() {
        command_builder app("ctest");
        std::move(app).version("1.0").term_width(0);
        return app.freeze();
    }
    constexpr command_spec plain = make_plain();

    /**
     * clap: `disable_help_flag(true)` plus `Arg::new("help").long("help").short('h')
     * .action(ArgAction::Help)`. Long spelling wins over short.
     */
    consteval command_spec make_custom_flag() {
        command_builder app("ctest");
        std::move(app)
                .version("1.0")
                .disable_help_flag(true)
                .arg(arg_builder("help").long_("help").short_('h').action(arg_action::help))
                .term_width(0);
        return app.freeze();
    }
    constexpr command_spec custom_flag = make_custom_flag();

    /**
     * clap: `Arg::new("help").short('h').action(ArgAction::HelpShort)` — no long spelling,
     * so the short one is what the closing line must name.
     */
    consteval command_spec make_custom_flag_short() {
        command_builder app("ctest");
        std::move(app)
                .version("1.0")
                .disable_help_flag(true)
                .arg(arg_builder("help").short_('h').action(arg_action::help_short))
                .term_width(0);
        return app.freeze();
    }
    constexpr command_spec custom_flag_short = make_custom_flag_short();

    /**
     * clap: `Arg::new("help").long("help").action(ArgAction::HelpShort)` — the action is
     * `HelpShort` but the spelling is long, and the spelling is what is printed. This is the
     * case that proves the two are independent.
     */
    consteval command_spec make_custom_flag_long() {
        command_builder app("ctest");
        std::move(app)
                .version("1.0")
                .disable_help_flag(true)
                .arg(arg_builder("help").long_("help").action(arg_action::help_short))
                .term_width(0);
        return app.freeze();
    }
    constexpr command_spec custom_flag_long = make_custom_flag_long();

    /**
     * clap: same argument, `ArgAction::Help` deliberately NOT set. An argument merely
     * *named* `help` is not a help flag, and with no subcommands there is nothing left to
     * suggest — the error must end after its usage line.
     */
    consteval command_spec make_custom_flag_no_action() {
        command_builder app("ctest");
        std::move(app)
                .version("1.0")
                .disable_help_flag(true)
                .arg(arg_builder("help").long_("help").global(true))
                .term_width(0);
        return app.freeze();
    }
    constexpr command_spec custom_flag_no_action = make_custom_flag_no_action();

    /** clap: `Command::new("ctest").version("1.0").subcommand(Command::new("foo"))`. */
    consteval command_spec make_sub_plain() {
        command_builder app("ctest");
        std::move(app).version("1.0").subcommand(command_builder("foo")).term_width(0);
        return app.freeze();
    }
    constexpr command_spec sub_plain = make_sub_plain();

    /**
     * `custom_flag` plus a subcommand, and the help argument made `global(true)` as clap
     * writes it. Still `--help`: the user's argument outranks the `help` subcommand.
     */
    consteval command_spec make_sub_custom_flag() {
        command_builder app("ctest");
        std::move(app)
                .version("1.0")
                .disable_help_flag(true)
                .arg(arg_builder("help")
                             .long_("help")
                             .short_('h')
                             .action(arg_action::help)
                             .global(true))
                .subcommand(command_builder("foo"))
                .term_width(0);
        return app.freeze();
    }
    constexpr command_spec sub_custom_flag = make_sub_custom_flag();

    /**
     * `custom_flag_no_action` plus a subcommand. The ONLY fixture in clap's suite that
     * reaches the `help`-subcommand branch of `get_help_flag`.
     */
    consteval command_spec make_sub_custom_flag_no_action() {
        command_builder app("ctest");
        std::move(app)
                .version("1.0")
                .disable_help_flag(true)
                .arg(arg_builder("help").long_("help").global(true))
                .subcommand(command_builder("foo"))
                .term_width(0);
        return app.freeze();
    }
    constexpr command_spec sub_custom_flag_no_action = make_sub_custom_flag_no_action();

    // ---------------------------------------------------------------------------
    // Compile-time preconditions
    //
    // Each of these pins the *input* to `help_flag_of`, so that a fixture which silently
    // stopped expressing what clap wrote cannot make a runtime case pass for the wrong
    // reason. Predicate form throughout (`has_arg`, `has_subcommand`) rather than
    // `find_arg(...) != nullptr` — trap 10 in CLAUDE.md.
    // ---------------------------------------------------------------------------

    static_assert(!plain.is_disable_help_flag_set());
    static_assert(!plain.has_subcommands());

    static_assert(custom_flag.is_disable_help_flag_set());
    static_assert(custom_flag.has_arg("help"));
    static_assert(custom_flag.find_arg("help")->get_action() == arg_action::help);
    static_assert(custom_flag.find_arg("help")->get_long() == std::string_view{"help"});
    static_assert(custom_flag.find_arg("help")->get_short() == 'h');

    static_assert(custom_flag_short.find_arg("help")->get_action() == arg_action::help_short);
    static_assert(!custom_flag_short.find_arg("help")->get_long().has_value());
    static_assert(custom_flag_short.find_arg("help")->get_short() == 'h');

    static_assert(custom_flag_long.find_arg("help")->get_action() == arg_action::help_short);
    static_assert(custom_flag_long.find_arg("help")->get_long() == std::string_view{"help"});
    static_assert(!custom_flag_long.find_arg("help")->get_short().has_value());

    // The whole point of the two `no_action` fixtures: the action must NOT be a help action.
    static_assert(!clapp::is_help(custom_flag_no_action.find_arg("help")->get_action()));
    static_assert(!custom_flag_no_action.has_subcommands());
    static_assert(!clapp::is_help(sub_custom_flag_no_action.find_arg("help")->get_action()));
    static_assert(sub_custom_flag_no_action.has_subcommands());
    static_assert(!sub_custom_flag_no_action.is_disable_help_subcommand_set());

    static_assert(sub_plain.has_subcommand("foo"));
    static_assert(sub_custom_flag.has_subcommand("foo"));
    static_assert(sub_custom_flag.find_arg("help")->get_action() == arg_action::help);

}  // namespace

// ---------------------------------------------------------------------------
// The five spellings of the closing line, with no subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::try_help_default") {
    const outcome got = clapp::parse(plain, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unexpected argument 'bar' found\n"
                     "\n"
                     "Usage: ctest\n"
                     "\n"
                     "For more information, try '--help'.\n"));
}

CLAPP_TEST("help.rs::try_help_custom_flag") {
    // Built-in flag disabled, user flag has both spellings — the LONG one is named.
    const outcome got = clapp::parse(custom_flag, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unexpected argument 'bar' found\n"
                     "\n"
                     "Usage: ctest\n"
                     "\n"
                     "For more information, try '--help'.\n"));
}

CLAPP_TEST("help.rs::try_help_custom_flag_short") {
    // Only a short spelling exists, so `-h` — one byte of difference from the case above,
    // and the reason the whole block is compared rather than a substring.
    const outcome got = clapp::parse(custom_flag_short, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unexpected argument 'bar' found\n"
                     "\n"
                     "Usage: ctest\n"
                     "\n"
                     "For more information, try '-h'.\n"));
}

CLAPP_TEST("help.rs::try_help_custom_flag_long") {
    // `HelpShort` action, long spelling. The action decides *which screen* the flag would
    // print; it does not decide how the flag is spelled here.
    const outcome got = clapp::parse(custom_flag_long, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unexpected argument 'bar' found\n"
                     "\n"
                     "Usage: ctest\n"
                     "\n"
                     "For more information, try '--help'.\n"));
}

CLAPP_TEST("help.rs::try_help_custom_flag_no_action") {
    // No help action anywhere and no subcommands: the block ends at the usage line. Note
    // it still ends with exactly one newline — the trailing blank line of the previous
    // four cases belongs to the try-help paragraph, not to the usage one.
    const outcome got = clapp::parse(custom_flag_no_action, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unexpected argument 'bar' found\n"
                     "\n"
                     "Usage: ctest\n"));
}

// ---------------------------------------------------------------------------
// ...and the same three questions asked of a command that has subcommands, where the
// message and the usage line both change too
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::try_help_subcommand_default") {
    const outcome got = clapp::parse(sub_plain, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unrecognized subcommand 'bar'\n"
                     "\n"
                     "Usage: ctest [COMMAND]\n"
                     "\n"
                     "For more information, try '--help'.\n"));
}

CLAPP_TEST("help.rs::try_help_subcommand_custom_flag") {
    // Subcommands exist AND a user help flag exists. The flag wins: `--help`, not `help`.
    const outcome got = clapp::parse(sub_custom_flag, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unrecognized subcommand 'bar'\n"
                     "\n"
                     "Usage: ctest [COMMAND]\n"
                     "\n"
                     "For more information, try '--help'.\n"));
}

CLAPP_TEST("help.rs::try_help_subcommand_custom_flag_no_action") {
    // The fourth branch, reached by nothing else: no built-in flag, no help action, but a
    // `help` SUBCOMMAND — so the advice is `help`, with no dashes. Compare against
    // `try_help_custom_flag_no_action`, which is this fixture minus `.subcommand("foo")`
    // and which prints no closing line at all.
    const outcome got = clapp::parse(sub_custom_flag_no_action, raw_args{"ctest", "bar"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(clapp::use_stderr(kind_of(got)));  // clap's `assert_output(..., true)`
    CLAPP_CHECK(same(got,
                     "error: unrecognized subcommand 'bar'\n"
                     "\n"
                     "Usage: ctest [COMMAND]\n"
                     "\n"
                     "For more information, try 'help'.\n"));
}
