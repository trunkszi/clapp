#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <expected>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
                                   .help("single alias")
                                   .alias("new-opt"));
        return app.freeze();
    }
    constexpr command_spec single_alias = make_single_alias();

    consteval command_spec make_multiple_aliases() {
        command_builder app("multiple_aliases");
        std::move(app).arg(arg_builder("aliases")
                                   .long_("aliases")
                                   .action(arg_action::set)
                                   .help("multiple aliases")
                                   .aliases({"alias1", "alias2", "alias3"}));
        return app.freeze();
    }
    constexpr command_spec multiple_aliases = make_multiple_aliases();

    consteval command_spec make_flag_single_alias() {
        command_builder app("test");
        std::move(app).arg(
                arg_builder("flag").long_("flag").alias("alias").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_single_alias = make_flag_single_alias();

    consteval command_spec make_flag_multiple_aliases() {
        command_builder app("test");
        std::move(app).arg(arg_builder("flag")
                                   .long_("flag")
                                   .aliases({"invisible", "set", "of", "cool", "aliases"})
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
                                                                .alias("opt")
                                                                .help("testing testing")))
                .arg(arg_builder("other").long_("other").aliases({"o1", "o2", "o3"}));
        return app.freeze();
    }
    constexpr command_spec subcommand_alias = make_subcommand_alias();

    // clap's `get_aliases` fixture: every alias flavour on one argument.
    consteval command_spec make_introspection() {
        command_builder app("introspection");
        std::move(app).arg(arg_builder("aliases")
                                   .long_("aliases")
                                   .action(arg_action::set)
                                   .help("multiple aliases")
                                   .aliases({"alias1", "alias2", "alias3"})
                                   .short_aliases({'a', 'b', 'c'})
                                   .visible_aliases({"alias4", "alias5", "alias6"})
                                   .visible_short_aliases({'d', 'e', 'f'}));
        return app.freeze();
    }
    constexpr command_spec introspection = make_introspection();

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
                                                 .aliases({"invisible", "als1", "more"}))
                                    .arg(arg_builder("flag")
                                                 .short_('f')
                                                 .long_("flag")
                                                 .action(arg_action::set_true)
                                                 .aliases({"unseeable", "flg1", "anyway"})));
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
                                                 .alias("invisible")
                                                 .visible_alias("visible"))
                                    .arg(arg_builder("flg")
                                                 .long_("flag")
                                                 .short_('f')
                                                 .action(arg_action::set_true)
                                                 .visible_aliases({"v_flg", "flag2", "flg3"})));
        return app.freeze();
    }
    constexpr command_spec visible_help = make_visible_help();

    // ---------------------------------------------------------------------------
    // clap's `get_aliases`, as compile-time questions
    // ---------------------------------------------------------------------------

    consteval bool names_match(auto lazy, std::span<const std::string_view> want) {
        std::vector<std::string_view> got;
        for (const std::string_view one : lazy) got.push_back(one);
        return std::ranges::equal(got, want);
    }

    consteval bool letters_match(auto lazy, std::span<const char> want) {
        std::vector<char> got;
        for (const char one : lazy) got.push_back(one);
        return std::ranges::equal(got, want);
    }

    consteval bool introspection_holds() {
        const clapp::arg_spec& a = *introspection.find_arg("aliases");

        // clap: `a.get_short_and_visible_aliases().is_none()` — no short option at all.
        // clapp collapses that onto an empty vector; see the DIVERGENCE note above.
        if (!a.get_short_and_visible_aliases().empty()) return false;

        constexpr std::string_view long_and_visible[] = {"aliases", "alias4", "alias5", "alias6"};
        if (!std::ranges::equal(a.get_long_and_visible_aliases(), long_and_visible)) return false;

        constexpr std::string_view visible[] = {"alias4", "alias5", "alias6"};
        if (!names_match(a.get_visible_aliases(), visible)) return false;

        constexpr std::string_view hidden[] = {"alias1", "alias2", "alias3"};
        if (!names_match(a.get_aliases(), hidden)) return false;

        // clap's `get_all_aliases` is hidden-then-visible because `aliases()` ran first.
        // clapp keeps declaration order too, so the same six names come out in the same
        // sequence — but through `alias_spec`, which carries the visibility flag.
        constexpr std::string_view all[] = {
                "alias1", "alias2", "alias3", "alias4", "alias5", "alias6"};
        std::vector<std::string_view> every;
        for (const clapp::alias_spec& one : a.get_all_aliases()) every.push_back(one.name.name());
        if (!std::ranges::equal(every, all)) return false;

        constexpr char visible_short[] = {'d', 'e', 'f'};
        if (!letters_match(a.get_visible_short_aliases(), visible_short)) return false;

        constexpr char all_short[] = {'a', 'b', 'c', 'd', 'e', 'f'};
        std::vector<char> every_short;
        for (const clapp::short_alias_spec& one : a.get_all_short_aliases())
            every_short.push_back(one.name);
        if (!std::ranges::equal(every_short, all_short)) return false;

        return true;
    }
    static_assert(introspection_holds());

    static_assert(single_alias.find_arg("alias")->matches_long("new-opt"));
    static_assert(single_alias.find_arg("alias")->matches_long("alias"));
    static_assert(!single_alias.find_arg("alias")->matches_long("nope"));

}  // namespace

// ---------------------------------------------------------------------------
// An alias reaches the canonical id
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_aliases.rs::single_alias_of_option") {
    const outcome got = clapp::parse(single_alias, raw_args{"", "--new-opt", "cool"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("alias"));
    CLAPP_CHECK(one_string(*got, "alias") == std::optional<std::string>{"cool"});
}

CLAPP_TEST("arg_aliases.rs::multiple_aliases_of_option") {
    for (const std::string_view spelling : {"--aliases", "--alias1", "--alias2", "--alias3"}) {
        const outcome got =
                clapp::parse(multiple_aliases, raw_args{"", std::string(spelling), "value"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(got->contains_id("aliases"));
        CLAPP_CHECK(one_string(*got, "aliases") == std::optional<std::string>{"value"});
    }
}

CLAPP_TEST("arg_aliases.rs::single_alias_of_flag") {
    const outcome got = clapp::parse(flag_single_alias, raw_args{"", "--alias"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("arg_aliases.rs::multiple_aliases_of_flag") {
    for (const std::string_view spelling : {"--flag", "--invisible", "--cool", "--aliases"}) {
        const outcome got =
                clapp::parse(flag_multiple_aliases, raw_args{"", std::string(spelling)});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(got->get_flag("flag"));
    }
}

CLAPP_TEST("arg_aliases.rs::alias_on_a_subcommand_option") {
    const outcome got =
            clapp::parse(subcommand_alias, raw_args{"test", "some", "--opt", "awesome"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* sub = got->subcommand_matches("some");
    CLAPP_CHECK(sub != nullptr);
    CLAPP_CHECK(sub->contains_id("test"));
    CLAPP_CHECK(one_string(*sub, "test") == std::optional<std::string>{"awesome"});
}

// ---------------------------------------------------------------------------
// Visibility is a help concern
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_aliases.rs::invisible_arg_aliases_help_output") {
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

CLAPP_TEST("arg_aliases.rs::visible_arg_aliases_help_output") {
    const command_spec* test = visible_help.find_subcommand("test");
    CLAPP_CHECK(test != nullptr);
    CLAPP_CHECK(same(page(*test, true, "ct test"),
                     "Some help\n"
                     "\n"
                     "Usage: ct test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -o, --opt <opt>  [alias: --visible]\n"
                     "  -f, --flag       [aliases: --v_flg, --flag2, --flg3]\n"
                     "  -h, --help       Print help\n"
                     "  -V, --version    Print version\n"));
}
