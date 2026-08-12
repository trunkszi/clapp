#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
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
    using clapp::help_style;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    /**
     * \brief The text a `display_help` error carries — clap's `utils::assert_output` with
     *        `stderr = false`, which captures exactly what the process would print.
     */
    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        Same helper as conformance_help_test.cpp.
     */
    std::string page(const command_spec& cmd, bool long_form) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd)})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // clap's `arg!(-H --help "Print help")` names the argument after its LONG spelling, so
    // the ids below are `help` / `hell` / `help1` on purpose: the id is what
    // `get_one`/`get_flag` asks for in the 1112 cases, and in the override_help_* cases it
    // is what a duplicate-id check would collide on if `disable_help_flag()` were ignored.
    // ---------------------------------------------------------------------------

    /** clap's `override_help_short`: the author keeps `--help` but moves the short to `-H`. */
    consteval command_spec make_override_short() {
        command_builder app("test");
        std::move(app).version("0.1").disable_help_flag().arg(arg_builder("help")
                                                                      .short_('H')
                                                                      .long_("help")
                                                                      .help("Print help")
                                                                      .action(arg_action::help));
        return app.freeze();
    }
    constexpr command_spec override_short = make_override_short();

    /** clap's `override_help_long`: the author keeps `-h` but renames the long to `--hell`. */
    consteval command_spec make_override_long() {
        command_builder app("test");
        std::move(app).version("0.1").disable_help_flag().arg(arg_builder("hell")
                                                                      .short_('h')
                                                                      .long_("hell")
                                                                      .help("Print help")
                                                                      .action(arg_action::help));
        return app.freeze();
    }
    constexpr command_spec override_long = make_override_long();

    /** clap's `override_help_about`: both spellings kept, only the description replaced. */
    consteval command_spec make_override_about() {
        command_builder app("test");
        std::move(app).version("0.1").disable_help_flag().arg(
                arg_builder("help")
                        .short_('h')
                        .long_("help")
                        .help("Print custom help information")
                        .action(arg_action::help));
        return app.freeze();
    }
    constexpr command_spec override_about = make_override_about();

    /**
     * clap's `override_help_subcommand`: a real subcommand called `help`, taking a
     * positional, with clap's own `help` subcommand switched off. `not_help` is present in
     * clap's fixture and kept here — it is what makes `help` an ordinary table entry rather
     * than the only child, and it keeps the subcommand lookup from succeeding by accident.
     */
    consteval command_spec make_override_subcommand() {
        command_builder app("bar");
        std::move(app)
                .disable_help_subcommand()
                .subcommand(command_builder("help").arg(arg_builder("arg")
                                                                .index(1)
                                                                .action(arg_action::set)
                                                                .num_args(value_range::exactly(1))))
                .subcommand(command_builder("not_help")
                                    .arg(arg_builder("arg")
                                                 .index(1)
                                                 .action(arg_action::set)
                                                 .num_args(value_range::exactly(1))));
        return app.freeze();
    }
    constexpr command_spec override_subcommand = make_override_subcommand();

    /** clap's `override_help_flag_using_long`: `--help` is a FLAG SUBCOMMAND. */
    consteval command_spec make_help_long_flag_subcommand() {
        command_builder app("foo");
        std::move(app).disable_help_flag().disable_help_subcommand().subcommand(
                command_builder("help").long_flag("help"));
        return app.freeze();
    }
    constexpr command_spec help_long_flag_subcommand = make_help_long_flag_subcommand();

    /** clap's `override_help_flag_using_short`: `-h` is a FLAG SUBCOMMAND. */
    consteval command_spec make_help_short_flag_subcommand() {
        command_builder app("foo");
        std::move(app).disable_help_flag().disable_help_subcommand().subcommand(
                command_builder("help").short_flag('h'));
        return app.freeze();
    }
    constexpr command_spec help_short_flag_subcommand = make_help_short_flag_subcommand();

    /**
     * clap's `issue_1112_setup()`: an ORDINARY `set_true` flag spelled `-h` / `--help`, at
     * the root and again inside `foo`. Nothing here has `arg_action::help`, which is the
     * point — the token must not print anything.
     */
    consteval command_spec make_issue_1112() {
        command_builder app("test");
        std::move(app)
                .version("1.3")
                .disable_help_flag()
                .arg(arg_builder("help1")
                             .long_("help")
                             .short_('h')
                             .help("some help")
                             .action(arg_action::set_true))
                // NOTE the child does NOT repeat `disable_help_flag()`, exactly as clap's
                // fixture does not: clap sets it with `global_setting`, so it reaches every
                // subcommand. Verified that clapp propagates it the same way — a parent with
                // `disable_help_flag()` and a bare child rejects `test foo --help` as
                // `unknown_argument` on both. Repeating it here would have hidden a
                // divergence rather than tested one.
                .subcommand(command_builder("foo").arg(arg_builder("help1")
                                                               .long_("help")
                                                               .short_('h')
                                                               .help("some help")
                                                               .action(arg_action::set_true)));
        return app.freeze();
    }
    constexpr command_spec issue_1112 = make_issue_1112();

    // ---------------------------------------------------------------------------
    // Replacing the help flag, keeping the help behaviour
    // ---------------------------------------------------------------------------

    CLAPP_TEST("help.rs::override_help_short") {
        // clap asserts this page twice: reached by `--help` and reached by `-H`. Both are
        // the same page, and the row is the AUTHOR's `-H, --help`, not clap's `-h, --help`.
        const std::string_view expected = "Usage: test\n"
                                          "\n"
                                          "Options:\n"
                                          "  -H, --help     Print help\n"
                                          "  -V, --version  Print version\n";
        CLAPP_CHECK(same(page(override_short, true), expected));

        const outcome by_long = clapp::parse(override_short, raw_args{"test", "--help"});
        CLAPP_CHECK(kind_of(by_long) == error_kind::display_help);
        CLAPP_CHECK(same(message_of(by_long), expected));

        const outcome by_short = clapp::parse(override_short, raw_args{"test", "-H"});
        CLAPP_CHECK(kind_of(by_short) == error_kind::display_help);
        CLAPP_CHECK(same(message_of(by_short), expected));

        // The half clap gets for free from `disable_help_flag(true)` and never spells out:
        // lowercase `-h` is now UNKNOWN, because nothing claims it. Asserting it is what
        // separates "the author's argument was added" from "the author's argument was added
        // *and* clap's was removed" — the latter is the actual claim of this case.
        // The whole diagnostic is pinned, not just the kind — re-derived from clap 4.6.5 on
        // 2026-08-10 (`Err(UnknownArgument)` plus this exact `to_string()`). Note the
        // "try '--help'" footer: clap points at the AUTHOR's long spelling, which is the one
        // still alive, so this line also witnesses that the substitution reached the usage
        // machinery and not only the Options table.
        const outcome unclaimed = clapp::parse(override_short, raw_args{"test", "-h"});
        CLAPP_CHECK(kind_of(unclaimed) == error_kind::unknown_argument);
        CLAPP_CHECK(same(message_of(unclaimed),
                         "error: unexpected argument '-h' found\n"
                         "\n"
                         "Usage: test\n"
                         "\n"
                         "For more information, try '--help'.\n"));
    }

    CLAPP_TEST("help.rs::override_help_long") {
        // `-h` survives as a short spelling; only the long one moved. Note the column width
        // is computed from `-h, --hell`, which is two characters narrower than
        // `-V, --version` — so the padding here is not the padding of the case above, and a
        // renderer that hard-coded either would fail the other.
        const std::string_view expected = "Usage: test\n"
                                          "\n"
                                          "Options:\n"
                                          "  -h, --hell     Print help\n"
                                          "  -V, --version  Print version\n";
        CLAPP_CHECK(same(page(override_long, true), expected));

        const outcome by_long = clapp::parse(override_long, raw_args{"test", "--hell"});
        CLAPP_CHECK(kind_of(by_long) == error_kind::display_help);
        CLAPP_CHECK(same(message_of(by_long), expected));

        const outcome by_short = clapp::parse(override_long, raw_args{"test", "-h"});
        CLAPP_CHECK(kind_of(by_short) == error_kind::display_help);
        CLAPP_CHECK(same(message_of(by_short), expected));

        // `--help` is gone entirely. clap does not assert this either; it is the same
        // "removed, not merely shadowed" claim as above, on the long side.
        const outcome unclaimed = clapp::parse(override_long, raw_args{"test", "--help"});
        CLAPP_CHECK(kind_of(unclaimed) == error_kind::unknown_argument);
        // Whole diagnostic, re-derived from clap 4.6.5 on 2026-08-10. This one carries three
        // things the kind alone does not: the DID-YOU-MEAN row (`--help` is one edit from the
        // surviving `--hell`, so the suggester has to reach the author's argument), a usage
        // line that names `--hell` because the argument is required-ish in usage terms, and a
        // footer pointing at `--hell` rather than the dead `--help`.
        CLAPP_CHECK(same(message_of(unclaimed),
                         "error: unexpected argument '--help' found\n"
                         "\n"
                         "  tip: a similar argument exists: '--hell'\n"
                         "\n"
                         "Usage: test --hell\n"
                         "\n"
                         "For more information, try '--hell'.\n"));
    }

    CLAPP_TEST("help.rs::override_help_about") {
        // Identical spellings to the injected flag; only the description differs. This is
        // the case that catches a renderer which prints its own "Print help" text rather
        // than the argument's, since every other column is byte-identical to what clap's own
        // injected row would have produced.
        const std::string_view expected = "Usage: test\n"
                                          "\n"
                                          "Options:\n"
                                          "  -h, --help     Print custom help information\n"
                                          "  -V, --version  Print version\n";
        CLAPP_CHECK(same(page(override_about, true), expected));

        const outcome by_long = clapp::parse(override_about, raw_args{"test", "--help"});
        CLAPP_CHECK(kind_of(by_long) == error_kind::display_help);
        CLAPP_CHECK(same(message_of(by_long), expected));

        const outcome by_short = clapp::parse(override_about, raw_args{"test", "-h"});
        CLAPP_CHECK(kind_of(by_short) == error_kind::display_help);
        CLAPP_CHECK(same(message_of(by_short), expected));
    }

    // ---------------------------------------------------------------------------
    // Replacing the `help` subcommand
    // ---------------------------------------------------------------------------

    CLAPP_TEST("help.rs::override_help_subcommand") {
        const outcome got = clapp::parse(override_subcommand, raw_args{"bar", "help", "foo"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;

        const arg_matches* sub = got->subcommand_matches("help");
        CLAPP_CHECK(sub != nullptr);
        if (sub == nullptr) return;

        const std::optional<const std::string*> arg = sub->get_one<std::string>("arg");
        CLAPP_CHECK(arg.has_value());
        CLAPP_CHECK(arg.has_value() && **arg == "foo");

        // clap stops there. Two things it leaves implicit and a plausible bug would break:
        // `foo` must NOT have been read as "print help for foo" (which would have produced a
        // display_help error, checked above by `got.has_value()`), and the sibling must still
        // work the same way — if `help` were being special-cased on its NAME rather than on
        // the disabled setting, `not_help` would be the only one that behaved.
        const outcome sibling =
                clapp::parse(override_subcommand, raw_args{"bar", "not_help", "foo"});
        CLAPP_CHECK(sibling.has_value());
        if (!sibling.has_value()) return;
        const arg_matches* other = sibling->subcommand_matches("not_help");
        CLAPP_CHECK(other != nullptr);
        CLAPP_CHECK(other != nullptr && **other->get_one<std::string>("arg") == "foo");

        // And the page lists the author's `help` as an ordinary child, with no injected
        // "Print this message or the help of the given subcommand(s)" row.
        CLAPP_CHECK(same(page(override_subcommand, true),
                         "Usage: bar [COMMAND]\n"
                         "\n"
                         "Commands:\n"
                         "  help      \n"
                         "  not_help  \n"
                         "\n"
                         "Options:\n"
                         "  -h, --help  Print help\n"));
    }

    // ---------------------------------------------------------------------------
    // Replacing the help flag with a flag SUBCOMMAND
    // ---------------------------------------------------------------------------

    CLAPP_TEST("help.rs::override_help_flag_using_long") {
        const outcome got = clapp::parse(help_long_flag_subcommand, raw_args{"foo", "--help"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return;
        }
        CLAPP_CHECK(got->subcommand_matches("help") != nullptr);
        CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"help"});

        // The bare word still reaches it too — a flag subcommand is the same subcommand.
        const outcome by_word = clapp::parse(help_long_flag_subcommand, raw_args{"foo", "help"});
        CLAPP_CHECK(by_word.has_value());
        CLAPP_CHECK(by_word.has_value() && by_word->subcommand_matches("help") != nullptr);
    }

    CLAPP_TEST("help.rs::override_help_flag_using_short") {
        const outcome got = clapp::parse(help_short_flag_subcommand, raw_args{"foo", "-h"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return;
        }
        CLAPP_CHECK(got->subcommand_matches("help") != nullptr);
        CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"help"});

        const outcome by_word = clapp::parse(help_short_flag_subcommand, raw_args{"foo", "help"});
        CLAPP_CHECK(by_word.has_value());
        CLAPP_CHECK(by_word.has_value() && by_word->subcommand_matches("help") != nullptr);
    }

    // ---------------------------------------------------------------------------
    // Issue 1112 — the user's plain flag wins, at both levels and in both spellings
    // ---------------------------------------------------------------------------

    CLAPP_TEST("help.rs::prefer_user_help_long_1112") {
        const outcome got = clapp::parse(issue_1112, raw_args{"test", "--help"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return;
        }
        CLAPP_CHECK(got->get_flag("help1"));
    }

    CLAPP_TEST("help.rs::prefer_user_help_short_1112") {
        const outcome got = clapp::parse(issue_1112, raw_args{"test", "-h"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return;
        }
        CLAPP_CHECK(got->get_flag("help1"));
    }

    CLAPP_TEST("help.rs::prefer_user_subcmd_help_long_1112") {
        const outcome got = clapp::parse(issue_1112, raw_args{"test", "foo", "--help"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return;
        }
        const arg_matches* sub = got->subcommand_matches("foo");
        CLAPP_CHECK(sub != nullptr);
        CLAPP_CHECK(sub != nullptr && sub->get_flag("help1"));

        // The parent's own `help1` is default-false here, which is the half that would break
        // if the flag were being recorded on the wrong level.
        CLAPP_CHECK(!got->get_flag("help1"));
    }

    CLAPP_TEST("help.rs::prefer_user_subcmd_help_short_1112") {
        const outcome got = clapp::parse(issue_1112, raw_args{"test", "foo", "-h"});
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return;
        }
        const arg_matches* sub = got->subcommand_matches("foo");
        CLAPP_CHECK(sub != nullptr);
        CLAPP_CHECK(sub != nullptr && sub->get_flag("help1"));
        CLAPP_CHECK(!got->get_flag("help1"));
    }

}  // namespace
