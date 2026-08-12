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

    std::optional<std::string_view> selected(const outcome& got) {
        if (!got.has_value()) return std::nullopt;
        return got->subcommand_name();
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval arg_builder test_flag() {
        return arg_builder("test")
                .short_('t')
                .long_("test")
                .help("testing testing")
                .action(arg_action::set_true);
    }

    consteval command_spec make_normal() {
        command_builder app("test");
        std::move(app).subcommand(
                command_builder("some").short_flag('S').long_flag("some").arg(test_flag()));
        return app.freeze();
    }
    constexpr command_spec normal = make_normal();

    consteval command_spec make_aliased() {
        command_builder app("myprog");
        std::move(app).subcommand(command_builder("some")
                                          .short_flag('S')
                                          .long_flag("S")
                                          .arg(test_flag())
                                          .alias("result")
                                          .aliases({"subc-do-stuff", "subc-do-tests"})
                                          .visible_alias("many")
                                          .visible_aliases({"several", "few"}));
        return app.freeze();
    }
    constexpr command_spec aliased = make_aliased();

    consteval command_spec make_short_only() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some").short_flag('S').arg(test_flag()));
        return app.freeze();
    }
    constexpr command_spec short_only = make_short_only();

    consteval command_spec make_short_flag_aliases() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some")
                                          .short_flag('S')
                                          .arg(test_flag())
                                          .short_flag_alias('M')
                                          .short_flag_alias('B'));
        return app.freeze();
    }
    constexpr command_spec short_flag_aliases = make_short_flag_aliases();

    consteval command_spec make_short_alias_same_letter() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some").short_flag('S').short_flag_alias('S'));
        return app.freeze();
    }
    constexpr command_spec short_alias_same_letter = make_short_alias_same_letter();

    consteval command_spec make_long_alias_same_spelling() {
        command_builder app("test");
        std::move(app).subcommand(
                command_builder("some").long_flag("sync").long_flag_alias("sync"));
        return app.freeze();
    }
    constexpr command_spec long_alias_same_spelling = make_long_alias_same_spelling();

    consteval command_spec make_short_flag_mixed_aliases() {
        command_builder app("test");
        std::move(app).subcommand(
                command_builder("some")
                        .short_flag('S')
                        .arg(arg_builder("test").short_('t').long_("test").help("testing testing"))
                        .visible_short_flag_alias('X')
                        .visible_short_flag_aliases({'M', 'B'})
                        .short_flag_alias('C'));
        return app.freeze();
    }
    constexpr command_spec short_flag_mixed_aliases = make_short_flag_mixed_aliases();

    consteval command_spec make_long_flag_mixed_aliases() {
        command_builder app("test");
        std::move(app).subcommand(
                command_builder("some")
                        .long_flag("sync")
                        .arg(arg_builder("test").short_('t').long_("test").help("testing testing"))
                        .visible_long_flag_alias("several")
                        .visible_long_flag_aliases({"result", "someall"})
                        .long_flag_alias("flag"));
        return app.freeze();
    }
    constexpr command_spec long_flag_mixed_aliases = make_long_flag_mixed_aliases();

    consteval command_spec make_short_flag_alias_list() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some")
                                          .short_flag('S')
                                          .arg(test_flag())
                                          .short_flag_aliases({'M', 'B'}));
        return app.freeze();
    }
    constexpr command_spec short_flag_alias_list = make_short_flag_alias_list();

    consteval command_spec make_pacman_arg() {
        command_builder app("pacman");
        std::move(app)
                .subcommand(command_builder("sync").short_flag('S').arg(
                        arg_builder("clean").short_('c').action(arg_action::set_true)))
                .arg(arg_builder("arg").long_("arg").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec pacman_arg = make_pacman_arg();

    consteval command_spec make_long_only() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some").long_flag("some").arg(test_flag()));
        return app.freeze();
    }
    constexpr command_spec long_only = make_long_only();

    consteval command_spec make_long_flag_alias() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some")
                                          .long_flag("some")
                                          .arg(test_flag())
                                          .long_flag_alias("result"));
        return app.freeze();
    }
    constexpr command_spec long_flag_alias = make_long_flag_alias();

    consteval command_spec make_long_flag_alias_list() {
        command_builder app("test");
        std::move(app).subcommand(command_builder("some")
                                          .long_flag("some")
                                          .arg(test_flag())
                                          .long_flag_aliases({"result", "someall"}));
        return app.freeze();
    }
    constexpr command_spec long_flag_alias_list = make_long_flag_alias_list();

    consteval command_spec make_nested_flags() {
        command_builder app("test");
        std::move(app).subcommand(
                command_builder("some")
                        .short_flag('S')
                        .long_flag("some")
                        .arg(arg_builder("flag")
                                     .short_('f')
                                     .long_("flag")
                                     .help("some flag")
                                     .action(arg_action::set_true))
                        .arg(arg_builder("print")
                                     .short_('p')
                                     .long_("print")
                                     .help("print something")
                                     .action(arg_action::set_true))
                        .subcommand(command_builder("result")
                                            .short_flag('R')
                                            .long_flag("result")
                                            .arg(arg_builder("flag")
                                                         .short_('f')
                                                         .long_("flag")
                                                         .help("some flag")
                                                         .action(arg_action::set_true))
                                            .arg(arg_builder("print")
                                                         .short_('p')
                                                         .long_("print")
                                                         .help("print something")
                                                         .action(arg_action::set_true))));
        return app.freeze();
    }
    constexpr command_spec nested_flags = make_nested_flags();

    consteval command_spec make_infer_one() {
        command_builder app("prog");
        std::move(app).infer_subcommands().subcommand(command_builder("test").long_flag("test"));
        return app.freeze();
    }
    constexpr command_spec infer_one = make_infer_one();

    consteval command_spec make_infer_two() {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("test").long_flag("test"))
                .subcommand(command_builder("temp").long_flag("temp"));
        return app.freeze();
    }
    constexpr command_spec infer_two = make_infer_two();

    consteval command_spec make_infer_exact() {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("test").long_flag("test"))
                .subcommand(command_builder("testa").long_flag("testa"))
                .subcommand(command_builder("testb").long_flag("testb"));
        return app.freeze();
    }
    constexpr command_spec infer_exact = make_infer_exact();

    // clap's three usage-string fixtures, differing only in which flag forms `query` has.
    consteval command_builder query_child() {
        return command_builder("query")
                .about("Query the package database.")
                .arg(arg_builder("search")
                             .short_('s')
                             .long_("search")
                             .help("search locally installed packages for matching strings")
                             .conflicts_with("info")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)))
                .arg(arg_builder("info")
                             .long_("info")
                             .short_('i')
                             .conflicts_with("search")
                             .help("view package information")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
    }

    consteval command_builder pacman_shell() {
        return command_builder("pacman")
                .about("package manager utility")
                .version("5.2.1")
                .subcommand_required()
                .author("Pacman Development Team");
    }

    consteval command_spec make_pacman_both() {
        command_builder app = pacman_shell();
        std::move(app).subcommand(query_child().short_flag('Q').long_flag("query"));
        return app.freeze();
    }
    constexpr command_spec pacman_both = make_pacman_both();

    consteval command_spec make_pacman_long() {
        command_builder app = pacman_shell();
        std::move(app).subcommand(query_child().long_flag("query"));
        return app.freeze();
    }
    constexpr command_spec pacman_long = make_pacman_long();

    consteval command_spec make_pacman_short() {
        command_builder app = pacman_shell();
        std::move(app).subcommand(query_child().short_flag('Q'));
        return app.freeze();
    }
    constexpr command_spec pacman_short = make_pacman_short();

    static_assert(normal.find_subcommand("some")->get_short_flag() == std::optional<char>{'S'});
    static_assert(normal.find_subcommand("some")->get_long_flag() ==
                  std::optional<std::string_view>{"some"});
    static_assert(pacman_short.find_subcommand("query")->get_long_flag() == std::nullopt);
    static_assert(pacman_long.find_subcommand("query")->get_short_flag() == std::nullopt);

}  // namespace

// ---------------------------------------------------------------------------
// Every spelling reaches the same child
// ---------------------------------------------------------------------------

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_normal") {
    const outcome got = clapp::parse(normal, raw_args{"myprog", "some", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_normal_with_alias_vis_and_hidden") {
    for (const std::string_view spelling :
         {"some", "result", "subc-do-stuff", "subc-do-tests", "many", "several", "few"}) {
        const outcome got =
                clapp::parse(aliased, raw_args{"myprog", std::string(spelling), "--test"});
        CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
        CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
    }
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_normal_with_alias") {
    const outcome got = clapp::parse(aliased, raw_args{"myprog", "result", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short") {
    const outcome got = clapp::parse(short_only, raw_args{"myprog", "-S", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_with_args") {
    // One cluster: `S` selects the child and `t` is the child's flag.
    const outcome got = clapp::parse(short_only, raw_args{"myprog", "-St"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_with_alias") {
    const outcome got = clapp::parse(short_flag_aliases, raw_args{"myprog", "-Bt"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_with_alias_same_as_short_flag") {
    const outcome got = clapp::parse(short_alias_same_letter, raw_args{"myprog", "-S"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_with_alias_same_as_long_flag") {
    const outcome got = clapp::parse(long_alias_same_spelling, raw_args{"myprog", "--sync"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_with_aliases_vis_and_hidden") {
    for (const std::string_view spelling : {"-M", "-C", "-B", "-X", "-S"}) {
        const outcome got =
                clapp::parse(short_flag_mixed_aliases, raw_args{"test", std::string(spelling)});
        CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    }
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_with_aliases_vis_and_hidden") {
    for (const std::string_view spelling :
         {"--result", "--flag", "--someall", "--several", "--sync"}) {
        const outcome got =
                clapp::parse(long_flag_mixed_aliases, raw_args{"test", std::string(spelling)});
        CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    }
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_with_aliases") {
    const outcome got = clapp::parse(short_flag_alias_list, raw_args{"myprog", "-Bt"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_after_long_arg") {
    const outcome got = clapp::parse(pacman_arg, raw_args{"pacman", "--arg", "foo", "-Sc"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* sync = got->subcommand_matches("sync");
    CLAPP_CHECK(sync != nullptr);
    CLAPP_CHECK(sync->get_flag("clean"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long") {
    const outcome got = clapp::parse(long_only, raw_args{"myprog", "--some", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_with_alias") {
    const outcome got = clapp::parse(long_flag_alias, raw_args{"myprog", "--result", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_with_aliases") {
    const outcome got =
            clapp::parse(long_flag_alias_list, raw_args{"myprog", "--result", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});
    CLAPP_CHECK(got->subcommand_matches("some")->get_flag("test"));
}

// ---------------------------------------------------------------------------
// One cluster, three commands
// ---------------------------------------------------------------------------

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_multiple") {
    const outcome got = clapp::parse(nested_flags, raw_args{"myprog", "-SfpRfp"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"some"});

    const arg_matches* some = got->subcommand_matches("some");
    CLAPP_CHECK(some != nullptr);
    CLAPP_CHECK(some->get_flag("flag"));
    CLAPP_CHECK(some->get_flag("print"));
    CLAPP_CHECK(some->subcommand_name() == std::optional<std::string_view>{"result"});

    const arg_matches* result = some->subcommand_matches("result");
    CLAPP_CHECK(result != nullptr);
    CLAPP_CHECK(result->get_flag("flag"));
    CLAPP_CHECK(result->get_flag("print"));
}

// ---------------------------------------------------------------------------
// Inference over long flags
// ---------------------------------------------------------------------------

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_infer_pass") {
    const outcome got = clapp::parse(infer_one, raw_args{"prog", "--te"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_infer_fail") {
    // Ambiguous, and reported as an unknown *argument* rather than an unknown
    // subcommand: the token arrived through the long-flag path.
    const outcome got = clapp::parse(infer_two, raw_args{"prog", "--te"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_infer_pass_close") {
    const outcome got = clapp::parse(infer_two, raw_args{"prog", "--tes"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_infer_exact_match") {
    const outcome got = clapp::parse(infer_exact, raw_args{"prog", "--test"});
    CLAPP_CHECK(selected(got) == std::optional<std::string_view>{"test"});
}

// ---------------------------------------------------------------------------
// The usage line names every way in
// ---------------------------------------------------------------------------

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_short_normal_usage_string") {
    const outcome got = clapp::parse(pacman_both, raw_args{"pacman", "-Qh"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Query the package database.\n"
                     "\n"
                     "Usage: pacman {query|--query|-Q} [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -s, --search <search>...  search locally installed packages for "
                     "matching strings\n"
                     "  -i, --info <info>...      view package information\n"
                     "  -h, --help                Print help\n"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_long_normal_usage_string") {
    const outcome got = clapp::parse(pacman_long, raw_args{"pacman", "query", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Query the package database.\n"
                     "\n"
                     "Usage: pacman {query|--query} [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -s, --search <search>...  search locally installed packages for "
                     "matching strings\n"
                     "  -i, --info <info>...      view package information\n"
                     "  -h, --help                Print help\n"));
}

CLAPP_TEST("flag_subcommands.rs::flag_subcommand_short_normal_usage_string") {
    const outcome got = clapp::parse(pacman_short, raw_args{"pacman", "query", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Query the package database.\n"
                     "\n"
                     "Usage: pacman {query|-Q} [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -s, --search <search>...  search locally installed packages for "
                     "matching strings\n"
                     "  -i, --info <info>...      view package information\n"
                     "  -h, --help                Print help\n"));
}
