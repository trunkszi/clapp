#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
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
    using clapp::help_style;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

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

    consteval command_spec make_single_alias() {
        command_builder app("single_alias");
        std::move(app).arg(arg_builder("alias")
                                   .long_("alias")
                                   .action(arg_action::set)
                                   .help("single short alias")
                                   .short_alias('a'));
        return app.freeze();
    }
    constexpr command_spec single_alias = make_single_alias();

    consteval command_spec make_multiple_aliases() {
        command_builder app("multiple_aliases");
        std::move(app).arg(arg_builder("aliases")
                                   .long_("aliases")
                                   .action(arg_action::set)
                                   .help("multiple aliases")
                                   .short_aliases({'1', '2', '3'}));
        return app.freeze();
    }
    constexpr command_spec multiple_aliases = make_multiple_aliases();

    consteval command_spec make_flag_single_alias() {
        command_builder app("test");
        std::move(app).arg(
                arg_builder("flag").long_("flag").short_alias('f').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_single_alias = make_flag_single_alias();

    consteval command_spec make_flag_multiple_aliases() {
        command_builder app("test");
        std::move(app).arg(arg_builder("flag")
                                   .long_("flag")
                                   .short_aliases({'a', 'b', 'c', 'd', 'e'})
                                   .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_multiple_aliases = make_flag_multiple_aliases();

    consteval command_spec make_subcommand_alias() {
        command_builder app("test");
        std::move(app)
                .subcommand(command_builder("some").arg(arg_builder("test")
                                                                .short_('t')
                                                                .long_("test")
                                                                .action(arg_action::set)
                                                                .short_alias('o')
                                                                .help("testing testing")))
                .arg(arg_builder("other").long_("other").short_aliases({'1', '2', '3'}));
        return app.freeze();
    }
    constexpr command_spec subcommand_alias = make_subcommand_alias();

    consteval command_spec make_invisible_help() {
        command_builder app("ct");
        std::move(app)
                .author("Salim Afiune")
                .subcommand(command_builder("test")
                                    .about("Some help")
                                    .version("1.2")
                                    .arg(arg_builder("opt")
                                                 .long_("opt")
                                                 .short_('o')
                                                 .action(arg_action::set)
                                                 .short_aliases({'a', 'b', 'c'}))
                                    .arg(arg_builder("flag")
                                                 .short_('f')
                                                 .long_("flag")
                                                 .action(arg_action::set_true)
                                                 .short_aliases({'x', 'y', 'z'})));
        return app.freeze();
    }
    constexpr command_spec invisible_help = make_invisible_help();

    consteval command_spec make_visible_help() {
        command_builder app("ct");
        std::move(app)
                .author("Salim Afiune")
                .subcommand(command_builder("test")
                                    .about("Some help")
                                    .version("1.2")
                                    .arg(arg_builder("opt")
                                                 .long_("opt")
                                                 .short_('o')
                                                 .action(arg_action::set)
                                                 .short_alias('i')
                                                 .visible_short_alias('v'))
                                    .arg(arg_builder("flg")
                                                 .long_("flag")
                                                 .short_('f')
                                                 .action(arg_action::set_true)
                                                 .visible_alias("flag1")
                                                 .visible_short_aliases({'a', 'b', 'd'})));
        return app.freeze();
    }
    constexpr command_spec visible_help = make_visible_help();

    static_assert(single_alias.find_arg("alias")->matches_short('a'));
    static_assert(!single_alias.find_arg("alias")->get_short().has_value());
    static_assert(multiple_aliases.find_arg("aliases")->matches_short('2'));

}  // namespace

// ---------------------------------------------------------------------------
// A short alias reaches the canonical id
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_aliases_short.rs::single_short_alias_of_option") {
    const outcome got = clapp::parse(single_alias, raw_args{"", "-a", "cool"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("alias"));
    CLAPP_CHECK(one_string(*got, "alias") == std::optional<std::string>{"cool"});
}

CLAPP_TEST("arg_aliases_short.rs::multiple_short_aliases_of_option") {
    for (const std::string_view spelling : {"--aliases", "-1", "-2", "-3"}) {
        const outcome got =
                clapp::parse(multiple_aliases, raw_args{"", std::string(spelling), "value"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(got->contains_id("aliases"));
        CLAPP_CHECK(one_string(*got, "aliases") == std::optional<std::string>{"value"});
    }
}

CLAPP_TEST("arg_aliases_short.rs::single_short_alias_of_flag") {
    const outcome got = clapp::parse(flag_single_alias, raw_args{"", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("arg_aliases_short.rs::multiple_short_aliases_of_flag") {
    for (const std::string_view spelling : {"--flag", "-a", "-b", "-c"}) {
        const outcome got =
                clapp::parse(flag_multiple_aliases, raw_args{"", std::string(spelling)});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(got->get_flag("flag"));
    }
}

CLAPP_TEST("arg_aliases_short.rs::short_alias_on_a_subcommand_option") {
    const outcome got = clapp::parse(subcommand_alias, raw_args{"test", "some", "-o", "awesome"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* sub = got->subcommand_matches("some");
    CLAPP_CHECK(sub != nullptr);
    CLAPP_CHECK(sub->contains_id("test"));
    CLAPP_CHECK(one_string(*sub, "test") == std::optional<std::string>{"awesome"});
}

// ---------------------------------------------------------------------------
// Visibility is a help concern
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_aliases_short.rs::invisible_short_arg_aliases_help_output") {
    const command_spec* test = invisible_help.find_subcommand("test");
    CLAPP_CHECK(test != nullptr);
    CLAPP_CHECK(same(page(*test, true, "ct test"),
                     "Some help\n"
                     "\n"
                     "Usage: ct test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -o, --opt <opt>  \n"
                     "  -f, --flag       \n"
                     "  -h, --help       Print help\n"
                     "  -V, --version    Print version\n"));
}

CLAPP_TEST("arg_aliases_short.rs::visible_short_arg_aliases_help_output") {
    // `-d` stands in for clap's `-🦆`; see the DIVERGENCE note at the top.
    const command_spec* test = visible_help.find_subcommand("test");
    CLAPP_CHECK(test != nullptr);
    CLAPP_CHECK(same(page(*test, true, "ct test"),
                     "Some help\n"
                     "\n"
                     "Usage: ct test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -o, --opt <opt>  [alias: -v]\n"
                     "  -f, --flag       [aliases: -a, -b, -d, --flag1]\n"
                     "  -h, --help       Print help\n"
                     "  -V, --version    Print version\n"));
}
