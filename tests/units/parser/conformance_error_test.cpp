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
#include <string>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_format;
    using clapp::error_kind;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string rendered(const outcome& got, error_format which = error_format::rich) {
        return got.has_value() ? std::string{} : got.error().render(which).to_string();
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_bare() {
        command_builder app("test");
        return app.freeze();
    }
    constexpr command_spec bare = make_bare();

    consteval command_spec make_port() {
        command_builder app("test");
        std::move(app).arg(arg_builder("PORT").index(1).required().value_parser<std::size_t>().help(
                "Network port to use"));
        return app.freeze();
    }
    constexpr command_spec port = make_port();

    consteval command_spec make_rg() {
        command_builder app("rg");
        std::move(app).arg(arg_builder("PATTERN").index(1));
        return app.freeze();
    }
    constexpr command_spec rg = make_rg();

    // A package-runner shape: a `last(true)` positional plus one long flag. It is the only
    // case anywhere in clap's suite that renders TWO tips in one error, and the ORDER of the
    // two is the thing worth pinning — the suggestion first, the escape advice second.
    consteval command_spec make_runner() {
        command_builder app("runner");
        std::move(app)
                .arg(arg_builder("TESTNAME").index(1).last())
                .arg(arg_builder("ignore-version")
                             .long_("ignore-version")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec runner = make_runner();

    // ...and the only case that pairs `invalid_subcommand` with a suggestion tip.
    consteval command_spec make_one_sub() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("bar"));
        return app.freeze();
    }
    constexpr command_spec one_sub = make_one_sub();

    static_assert(!bare.has_positionals());
    static_assert(rg.has_positionals());
    static_assert(port.find_arg("PORT")->is_required_set());
    static_assert(runner.find_arg("TESTNAME")->is_last_set());
    static_assert(one_sub.has_subcommand("bar"));

}  // namespace

// ---------------------------------------------------------------------------
// The whole rendered block
// ---------------------------------------------------------------------------

CLAPP_TEST("error.rs::rich_formats_validation_error") {
    const outcome got = clapp::parse(bare, raw_args{"test", "unused"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(rendered(got) == "error: unexpected argument 'unused' found\n"
                                 "\n"
                                 "Usage: test\n"
                                 "\n"
                                 "For more information, try '--help'.\n");
}

CLAPP_TEST("error.rs::cant_use_trailing") {
    // No positional to put it in, so no `-- <value>` tip.
    const outcome got = clapp::parse(bare, raw_args{"test", "--foo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(rendered(got) == "error: unexpected argument '--foo' found\n"
                                 "\n"
                                 "Usage: test\n"
                                 "\n"
                                 "For more information, try '--help'.\n");
}

CLAPP_TEST("error.rs::suggest_trailing") {
    // `rg` HAS a positional, so the tip appears — and it is indented by two spaces and
    // set off by blank lines on both sides.
    const outcome got = clapp::parse(rg, raw_args{"rg", "--foo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(rendered(got) == "error: unexpected argument '--foo' found\n"
                                 "\n"
                                 "  tip: to pass '--foo' as a value, use '-- --foo'\n"
                                 "\n"
                                 "Usage: rg [PATTERN]\n"
                                 "\n"
                                 "For more information, try '--help'.\n");
}

CLAPP_TEST("error.rs::trailing_already_in_use") {
    // Same command, same offending token — but `--` has already been used, so advising
    // it again would be nonsense and the tip must be gone. One line of difference from
    // the case above, and nothing else may move.
    const outcome got = clapp::parse(rg, raw_args{"rg", "--", "--foo", "--foo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(rendered(got) == "error: unexpected argument '--foo' found\n"
                                 "\n"
                                 "Usage: rg [PATTERN]\n"
                                 "\n"
                                 "For more information, try '--help'.\n");
}

// ---------------------------------------------------------------------------
// The trailing newline
// ---------------------------------------------------------------------------

CLAPP_TEST("error.rs::value_validation_has_newline") {
    const outcome got = clapp::parse(port, raw_args{"test", "foo"});
    CLAPP_CHECK(!got.has_value());
    const std::string text = rendered(got);
    CLAPP_CHECK(!text.empty());
    CLAPP_CHECK(text.back() == '\n');
    // No `Usage:` block: clap's Error::value_validation takes no usage parameter, so
    // ContextKind::Usage is absent and RichFormatter prints nothing for it. Measured on
    // clap_builder 4.6.5 with this exact command line. The same holds for
    // Error::invalid_value and Error::empty_value; contrast too_many_values,
    // too_few_values and wrong_number_of_values, which do carry one.
    CLAPP_CHECK(text == "error: invalid value 'foo' for '<PORT>': invalid digit found in string\n"
                        "\n"
                        "For more information, try '--help'.\n");
}

// ---------------------------------------------------------------------------
// The kind-only formatter
// ---------------------------------------------------------------------------

CLAPP_TEST("error.rs::kind_formats_validation_error") {
    // clap's `KindFormatter`: the generic description of the kind, and nothing else —
    // not the offending token, not the usage line, not the try-help line.
    const outcome got = clapp::parse(bare, raw_args{"test", "unused"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(rendered(got, error_format::kind_only) == "error: unexpected argument found\n");
}

CLAPP_TEST("error.rs::kind_prints_help") {
    // A control-flow "error" is a finished document: no `error:` prefix, no usage frame,
    // and the kind-only formatter leaves it exactly as alone as the rich one does.
    const outcome got = clapp::parse(bare, raw_args{"test", "--help"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(rendered(got).find("error:") == std::string::npos);
    CLAPP_CHECK(rendered(got) == rendered(got, error_format::kind_only));
    // ... and it goes to stdout with a success exit code, unlike every case above.
    CLAPP_CHECK(!got.error().use_stderr());
    CLAPP_CHECK(got.error().exit_code() == 0);
}

// ---------------------------------------------------------------------------
// Two tips in one error, and a tip on an unrecognized subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("error.rs::suggest_trailing_last") {
    // Two tips, in this order: what you probably meant, then how to say what you typed.
    // Nothing else in the suite renders more than one, so nothing else can catch a
    // reordering — or a second tip that silently replaces the first.
    //
    // The usage line is the other half: `runner --ignore-version [-- <TESTNAME>]`
    // spells the `last(true)` positional WITH its `--`, because that is the only way to
    // reach it.
    const outcome got = clapp::parse(runner, raw_args{"runner", "--ignored"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(rendered(got) == "error: unexpected argument '--ignored' found\n"
                                 "\n"
                                 "  tip: a similar argument exists: '--ignore-version'\n"
                                 "  tip: to pass '--ignored' as a value, use '-- --ignored'\n"
                                 "\n"
                                 "Usage: runner --ignore-version [-- <TESTNAME>]\n"
                                 "\n"
                                 "For more information, try '--help'.\n");
}

CLAPP_TEST("error.rs::cant_use_trailing_subcommand") {
    const outcome got = clapp::parse(one_sub, raw_args{"test", "baz"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(rendered(got) == "error: unrecognized subcommand 'baz'\n"
                                 "\n"
                                 "  tip: a similar subcommand exists: 'bar'\n"
                                 "\n"
                                 "Usage: test [COMMAND]\n"
                                 "\n"
                                 "For more information, try '--help'.\n");
}
