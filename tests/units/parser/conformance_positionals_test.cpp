#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/validator.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

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

    using clapp::detail::usage_renderer;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    std::vector<std::string> raw_of(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    // clap's `value_parser(["test123"])`, expressed the way clapp enumerates a domain.
    enum class only_test123 { test123 };

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_only_pos_follow() {
        command_builder app("onlypos");
        std::move(app)
                .arg(arg_builder("f").short_('f').num_args(value_range::optional()))
                .arg(arg_builder("arg").index(1));
        return app.freeze();
    }
    constexpr command_spec only_pos_follow = make_only_pos_follow();

    consteval command_spec make_compiletest() {
        command_builder app("compiletest");
        std::move(app)
                .arg(arg_builder("exact").long_("exact").action(arg_action::set_true))
                .arg(arg_builder("filter")
                             .index(1)
                             .action(arg_action::set)
                             .allow_hyphen_values()
                             .help("filters to apply to output"));
        return app.freeze();
    }
    constexpr command_spec compiletest = make_compiletest();

    consteval command_spec make_flag_and_positional() {
        command_builder app("positional");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("positional").index(1));
        return app.freeze();
    }
    constexpr command_spec flag_and_positional = make_flag_and_positional();

    consteval command_spec make_multi_positional() {
        command_builder app("positional_multiple");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("positional")
                             .index(1)
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec multi_positional = make_multi_positional();

    consteval command_spec make_unbounded_positional() {
        command_builder app("opts");
        std::move(app).arg(
                arg_builder("opt").index(1).required().num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec unbounded_positional = make_unbounded_positional();

    consteval command_spec make_enumerated_positional() {
        command_builder app("positional_possible_values");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("positional").index(1).value_parser<only_test123>());
        return app.freeze();
    }
    constexpr command_spec enumerated_positional = make_enumerated_positional();

    consteval command_spec make_lone_positional() {
        command_builder app("test");
        std::move(app).arg(arg_builder("test").index(1).help("testing testing"));
        return app.freeze();
    }
    constexpr command_spec lone_positional = make_lone_positional();

    consteval command_spec make_dummy() {
        command_builder app("test");
        std::move(app).arg(arg_builder("dummy").index(1));
        return app.freeze();
    }
    constexpr command_spec dummy = make_dummy();

    consteval command_spec make_two_required() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("FILE1").index(1).required())
                .arg(arg_builder("FILE2").index(2).required());
        return app.freeze();
    }
    constexpr command_spec two_required = make_two_required();

    consteval command_spec make_last_positional() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("TARGET").index(1).required())
                .arg(arg_builder("CORPUS").index(2))
                .arg(arg_builder("ARGS").index(3).num_args(value_range::at_least(1)).last());
        return app.freeze();
    }
    constexpr command_spec last_positional = make_last_positional();

    consteval command_spec make_last_after_multi() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("TARGET").index(1).required())
                .arg(arg_builder("CORPUS").index(2).num_args(value_range::at_least(1)))
                .arg(arg_builder("ARGS").index(3).num_args(value_range::at_least(1)).last());
        return app.freeze();
    }
    constexpr command_spec last_after_multi = make_last_after_multi();

    consteval command_spec make_hyphen_on_last() {
        command_builder app("foo");
        std::move(app)
                .arg(arg_builder("cmd")
                             .index(1)
                             .num_args(value_range::at_least(1))
                             .last()
                             .allow_hyphen_values())
                .arg(arg_builder("name")
                             .long_("name")
                             .short_('n')
                             .action(arg_action::set)
                             .required(false));
        return app.freeze();
    }
    constexpr command_spec hyphen_on_last = make_hyphen_on_last();

    // ---------------------------------------------------------------------------
    // Usage-line fixtures. clap compares `render_usage()` against a literal; clapp's
    // renderer is constexpr, so these are decided by the compiler.
    // ---------------------------------------------------------------------------

    consteval command_spec make_usage_single() {
        command_builder app("test");
        std::move(app).arg(arg_builder("FILE").index(1).help("some file"));
        return app.freeze();
    }
    constexpr command_spec usage_single = make_usage_single();

    consteval command_spec make_usage_single_multiple() {
        command_builder app("test");
        std::move(app).arg(
                arg_builder("FILE").index(1).num_args(value_range::at_least(1)).help("some file"));
        return app.freeze();
    }
    constexpr command_spec usage_single_multiple = make_usage_single_multiple();

    consteval command_spec make_usage_two() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("FILE").index(1).help("some file"))
                .arg(arg_builder("FILES")
                             .index(2)
                             .num_args(value_range::at_least(1))
                             .help("some file"));
        return app.freeze();
    }
    constexpr command_spec usage_two = make_usage_two();

    consteval command_spec make_usage_one_required() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("FILE").index(1).required().help("some file"))
                .arg(arg_builder("FILES")
                             .index(2)
                             .num_args(value_range::at_least(1))
                             .help("some file"));
        return app.freeze();
    }
    constexpr command_spec usage_one_required = make_usage_one_required();

    consteval command_spec make_usage_required() {
        command_builder app("test");
        std::move(app).arg(arg_builder("FILE").index(1).required().help("some file"));
        return app.freeze();
    }
    constexpr command_spec usage_required = make_usage_required();

    // The fixtures carry what the cases below name.
    static_assert(last_positional.find_arg("ARGS")->is_last_set());
    static_assert(compiletest.find_arg("filter")->is_allow_hyphen_values_set());
    static_assert(multi_positional.find_arg("positional")->get_num_args() ==
                  value_range::at_least(1));
    static_assert(enumerated_positional.find_arg("positional")->get_possible_values().size() == 1);

    // ---------------------------------------------------------------------------
    // The five usage-string cases, decided at compile time
    // ---------------------------------------------------------------------------

    consteval bool usage_line_of(const command_spec& cmd, std::string_view expected) {
        const std::optional<clapp::styled_str> line =
                usage_renderer{cmd}.create_usage_with_title({});
        return line.has_value() && line->to_string() == expected;
    }

    // positionals.rs::single_positional_usage_string
    static_assert(usage_line_of(usage_single, "Usage: test [FILE]"));
    // positionals.rs::single_positional_multiple_usage_string
    static_assert(usage_line_of(usage_single_multiple, "Usage: test [FILE]..."));
    // positionals.rs::multiple_positional_usage_string
    static_assert(usage_line_of(usage_two, "Usage: test [FILE] [FILES]..."));
    // positionals.rs::multiple_positional_one_required_usage_string
    static_assert(usage_line_of(usage_one_required, "Usage: test <FILE> [FILES]..."));
    // positionals.rs::single_positional_required_usage_string
    static_assert(usage_line_of(usage_required, "Usage: test <FILE>"));

    std::vector<std::string> repeated_words(std::string_view word, std::size_t count) {
        std::vector<std::string> out;
        out.emplace_back("");
        for (std::size_t i = 0; i < count; ++i) out.emplace_back(word);
        return out;
    }

}  // namespace

// ---------------------------------------------------------------------------
// Where a bare word lands
// ---------------------------------------------------------------------------

CLAPP_TEST("positionals.rs::only_pos_follow") {
    const outcome got = clapp::parse(only_pos_follow, raw_args{"", "--", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    // The half that a parser which also marks the flag would fail.
    CLAPP_CHECK(!got->contains_id("f"));
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"-f"});
}

CLAPP_TEST("positionals.rs::positional") {
    const outcome flag_first = clapp::parse(flag_and_positional, raw_args{"", "-f", "test"});
    CLAPP_CHECK(flag_first.has_value());
    CLAPP_CHECK(flag_first->contains_id("positional"));
    CLAPP_CHECK(flag_first->get_flag("flag"));
    CLAPP_CHECK(one_string(*flag_first, "positional") == std::optional<std::string>{"test"});

    // The same two arguments in the other order: a positional does not close the line.
    const outcome value_first = clapp::parse(flag_and_positional, raw_args{"", "test", "--flag"});
    CLAPP_CHECK(value_first.has_value());
    CLAPP_CHECK(value_first->contains_id("positional"));
    CLAPP_CHECK(value_first->get_flag("flag"));
    CLAPP_CHECK(one_string(*value_first, "positional") == std::optional<std::string>{"test"});
}

CLAPP_TEST("positionals.rs::issue_946") {
    // `filter` would take `--exact` as data if it ever got there. It must not get there.
    const outcome got = clapp::parse(compiletest, raw_args{"compiletest", "--exact"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("exact"));
    CLAPP_CHECK(!one_string(*got, "filter").has_value());
}

CLAPP_TEST("positionals.rs::lots_o_vals") {
    const raw_args line{std::from_range, repeated_words("some", 297)};
    const outcome got = clapp::parse(unbounded_positional, line);
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt").size() == 297);
}

// ---------------------------------------------------------------------------
// One slot versus many
// ---------------------------------------------------------------------------

CLAPP_TEST("positionals.rs::positional_multiple") {
    const outcome got =
            clapp::parse(multi_positional, raw_args{"", "-f", "test1", "test2", "test3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(raw_of(*got, "positional") == std::vector<std::string>{"test1", "test2", "test3"});
}

CLAPP_TEST("positionals.rs::positional_multiple_3") {
    // The flag AFTER the values: the multi-valued positional must stop at `--flag`.
    const outcome got =
            clapp::parse(multi_positional, raw_args{"", "test1", "test2", "test3", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(raw_of(*got, "positional") == std::vector<std::string>{"test1", "test2", "test3"});
}

CLAPP_TEST("positionals.rs::positional_multiple_2") {
    // A SINGLE-valued positional; `test2` has no slot.
    const outcome got =
            clapp::parse(flag_and_positional, raw_args{"", "-f", "test1", "test2", "test3"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("positionals.rs::positional_possible_values") {
    const outcome got = clapp::parse(enumerated_positional, raw_args{"", "-f", "test123"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("positional"));
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(raw_of(*got, "positional") == std::vector<std::string>{"test123"});
    CLAPP_CHECK(got->get_one<only_test123>("positional").has_value());

    // ... and the domain really is enforced, which clap's case leaves implicit.
    const outcome rejected = clapp::parse(enumerated_positional, raw_args{"", "-f", "nope"});
    CLAPP_CHECK(!rejected.has_value());
    CLAPP_CHECK(kind_of(rejected) == error_kind::invalid_value);
}

// ---------------------------------------------------------------------------
// Degenerate lines that must not crash
// ---------------------------------------------------------------------------

CLAPP_TEST("positionals.rs::create_positional") {
    const outcome got = clapp::parse(lone_positional, raw_args{""});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("positionals.rs::positional_hyphen_does_not_panic") {
    const outcome got = clapp::parse(dummy, raw_args{"test", "-"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "dummy") == std::optional<std::string>{"-"});
}

CLAPP_TEST("positionals.rs::missing_required_2") {
    const outcome got = clapp::parse(two_required, raw_args{"test", "file"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

// ---------------------------------------------------------------------------
// last(true)
// ---------------------------------------------------------------------------

CLAPP_TEST("positionals.rs::last_positional") {
    const outcome got = clapp::parse(last_positional, raw_args{"test", "tgt", "--", "arg"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "ARGS") == std::vector<std::string>{"arg"});
}

CLAPP_TEST("positionals.rs::last_positional_no_double_dash") {
    // Without `--` the third word has nowhere to go: `last` is not just "the last slot".
    const outcome got = clapp::parse(last_positional, raw_args{"test", "tgt", "crp", "arg"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("positionals.rs::last_positional_second_to_last_mult") {
    const outcome got =
            clapp::parse(last_after_multi, raw_args{"test", "tgt", "crp1", "crp2", "--", "arg"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "CORPUS") == std::vector<std::string>{"crp1", "crp2"});
    CLAPP_CHECK(raw_of(*got, "ARGS") == std::vector<std::string>{"arg"});
}

CLAPP_TEST("positionals.rs::ignore_hyphen_values_on_last") {
    // `allow_hyphen_values` on an unreachable `last` positional must not leak onto the
    // option that IS reachable: `-n foo` still parses normally.
    const outcome got = clapp::parse(hyphen_on_last, raw_args{"test", "-n", "foo"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "name") == std::optional<std::string>{"foo"});
}
