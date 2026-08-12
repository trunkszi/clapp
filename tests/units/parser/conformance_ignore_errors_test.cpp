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
#include <clapp/parser/value_source.hpp>

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
    using clapp::value_source;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    std::string page(const command_spec& cmd, bool long_form, std::string_view usage_name = {}) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd),
                                             .usage_name = usage_name})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    // clap's `arg!(-c --config <FILE> "…")`: a required-value option.
    consteval command_spec make_config_only() {
        command_builder app("cmd");
        std::move(app)
                .ignore_errors()
                .arg(arg_builder("config")
                             .short_('c')
                             .long_("config")
                             .value_name("FILE")
                             .action(arg_action::set)
                             .help("Sets a custom config file"))
                .arg(arg_builder("unset-flag").long_("unset-flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec config_only = make_config_only();

    consteval command_spec make_four_args() {
        command_builder app("cmd");
        std::move(app)
                .ignore_errors()
                .arg(arg_builder("config")
                             .short_('c')
                             .long_("config")
                             .value_name("FILE")
                             .action(arg_action::set)
                             .help("Sets a custom config file"))
                .arg(arg_builder("stuff")
                             .short_('x')
                             .long_("stuff")
                             .value_name("FILE")
                             .action(arg_action::set)
                             .help("Sets a custom stuff file"))
                .arg(arg_builder("f").short_('f').help("Flag").action(arg_action::set_true))
                .arg(arg_builder("unset-flag").long_("unset-flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec four_args = make_four_args();

    // clap's `arg!(-c --config [FILE] "…")`: the value is optional here.
    consteval command_spec make_optional_value() {
        command_builder app("cmd");
        std::move(app)
                .ignore_errors()
                .arg(arg_builder("config")
                             .short_('c')
                             .long_("config")
                             .value_name("FILE")
                             .num_args(value_range::optional())
                             .help("Sets a custom config file"))
                .arg(arg_builder("unset-flag").long_("unset-flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec optional_value = make_optional_value();

    consteval command_spec make_strict_suggestion() {
        command_builder app("cmd");
        std::move(app).arg(arg_builder("ignore-immutable")
                                   .long_("ignore-immutable")
                                   .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec strict_suggestion = make_strict_suggestion();

    consteval command_spec make_lenient_suggestion() {
        command_builder app("cmd");
        std::move(app).ignore_errors().arg(arg_builder("ignore-immutable")
                                                   .long_("ignore-immutable")
                                                   .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec lenient_suggestion = make_lenient_suggestion();

    consteval command_spec make_with_subcommand() {
        command_builder app("test");
        std::move(app)
                .ignore_errors()
                .subcommand(command_builder("some")
                                    .arg(arg_builder("test")
                                                 .short_('t')
                                                 .long_("test")
                                                 .action(arg_action::set)
                                                 .help("testing testing"))
                                    .arg(arg_builder("stuff")
                                                 .short_('x')
                                                 .long_("stuff")
                                                 .action(arg_action::set)
                                                 .help("stuf value"))
                                    .arg(arg_builder("unset-flag")
                                                 .long_("unset-flag")
                                                 .action(arg_action::set_true)))
                .arg(arg_builder("other").long_("other"))
                .arg(arg_builder("unset-flag").long_("unset-flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec with_subcommand = make_with_subcommand();

    consteval command_spec make_help_only() {
        command_builder app("test");
        std::move(app).ignore_errors();
        return app.freeze();
    }
    constexpr command_spec help_only = make_help_only();

    consteval command_spec make_help_subcommand() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("sub")).ignore_errors();
        return app.freeze();
    }
    constexpr command_spec help_subcommand = make_help_subcommand();

    consteval command_spec make_versioned() {
        command_builder app("test");
        std::move(app).ignore_errors().version("0.1");
        return app.freeze();
    }
    constexpr command_spec versioned = make_versioned();

    static_assert(config_only.is_ignore_errors_set());
    static_assert(!strict_suggestion.is_ignore_errors_set());
    static_assert(lenient_suggestion.is_ignore_errors_set());

}  // namespace

// ---------------------------------------------------------------------------
// An option left without its value
// ---------------------------------------------------------------------------

CLAPP_TEST("ignore_errors.rs::single_short_arg_without_value") {
    const outcome got = clapp::parse(config_only, raw_args{"cmd", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("config"));
    CLAPP_CHECK(one_string(*got, "config") == std::nullopt);
    CLAPP_CHECK(!got->get_flag("unset-flag"));
}

CLAPP_TEST("ignore_errors.rs::single_long_arg_without_value") {
    const outcome got = clapp::parse(config_only, raw_args{"cmd", "--config"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("config"));
    CLAPP_CHECK(one_string(*got, "config") == std::nullopt);
    CLAPP_CHECK(!got->get_flag("unset-flag"));
}

CLAPP_TEST("ignore_errors.rs::multiple_args_and_final_arg_without_value") {
    const outcome got = clapp::parse(four_args, raw_args{"cmd", "-c", "file", "-f", "-x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "config") == std::optional<std::string>{"file"});
    CLAPP_CHECK(got->get_flag("f"));
    CLAPP_CHECK(one_string(*got, "stuff") == std::nullopt);
    CLAPP_CHECK(!got->get_flag("unset-flag"));
}

CLAPP_TEST("ignore_errors.rs::multiple_args_and_intermittent_arg_without_value") {
    // Same three arguments, the broken one FIRST: everything after it still parses.
    const outcome got = clapp::parse(four_args, raw_args{"cmd", "-x", "-c", "file", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "config") == std::optional<std::string>{"file"});
    CLAPP_CHECK(got->get_flag("f"));
    CLAPP_CHECK(one_string(*got, "stuff") == std::nullopt);
    CLAPP_CHECK(!got->get_flag("unset-flag"));
}

CLAPP_TEST("ignore_errors.rs::unexpected_argument") {
    const outcome got =
            clapp::parse(optional_value, raw_args{"cmd", "-c", "config file", "unexpected"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("config"));
    CLAPP_CHECK(one_string(*got, "config") == std::optional<std::string>{"config file"});
    CLAPP_CHECK(!got->get_flag("unset-flag"));
}

// ---------------------------------------------------------------------------
// The same command line, both ways
// ---------------------------------------------------------------------------

CLAPP_TEST("ignore_errors.rs::did_you_mean") {
    const outcome strict = clapp::parse(strict_suggestion, raw_args{"cmd", "--ig"});
    CLAPP_CHECK(!strict.has_value());
    CLAPP_CHECK(kind_of(strict) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(strict) == "error: unexpected argument '--ig' found\n"
                                      "\n"
                                      "  tip: a similar argument exists: '--ignore-immutable'\n"
                                      "\n"
                                      "Usage: cmd --ignore-immutable\n"
                                      "\n"
                                      "For more information, try '--help'.\n");

    const outcome lenient = clapp::parse(lenient_suggestion, raw_args{"cmd", "--ig"});
    CLAPP_CHECK(lenient.has_value());
    CLAPP_CHECK(lenient->contains_id("ignore-immutable"));
    CLAPP_CHECK(lenient->value_source("ignore-immutable") ==
                std::optional<value_source>{value_source::default_value});
}

// ---------------------------------------------------------------------------
// Partial parsing inside a subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("ignore_errors.rs::subcommand") {
    const outcome got = clapp::parse(with_subcommand,
                                     raw_args{"myprog", "some", "--test", "-x", "some other val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"some"});

    const arg_matches* sub = got->subcommand_matches("some");
    CLAPP_CHECK(sub != nullptr);
    CLAPP_CHECK(sub->contains_id("test"));
    CLAPP_CHECK(one_string(*sub, "test") == std::nullopt);
    CLAPP_CHECK(one_string(*sub, "stuff") == std::optional<std::string>{"some other val"});
    CLAPP_CHECK(!sub->get_flag("unset-flag"));

    CLAPP_CHECK(!got->get_flag("unset-flag"));
}

// ---------------------------------------------------------------------------
// Help and version are control flow, not errors
// ---------------------------------------------------------------------------

CLAPP_TEST("ignore_errors.rs::help_flag") {
    const outcome got = clapp::parse(help_only, raw_args{"test", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(page(help_only, true),
                     "Usage: test\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("ignore_errors.rs::help_flag_subcommand") {
    const outcome got = clapp::parse(help_subcommand, raw_args{"test", "sub", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    const command_spec* sub = help_subcommand.find_subcommand("sub");
    CLAPP_CHECK(sub != nullptr);
    CLAPP_CHECK(same(page(*sub, true, "test sub"),
                     "Usage: test sub\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("ignore_errors.rs::version_flag") {
    const outcome got = clapp::parse(versioned, raw_args{"test", "--version"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_version);
    CLAPP_CHECK(message_of(got) == "test 0.1\n");
}
