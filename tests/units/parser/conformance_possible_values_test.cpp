#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// clap's PossibleValuesParser, as a clapp value_parser specialization
// ---------------------------------------------------------------------------

namespace {

    using namespace std::string_view_literals;

    /**
     * \brief The value a domain-checked argument produces: the bytes the user typed.
     *
     * \tparam Domain A tag type publishing `static constexpr std::span<const
     *                clapp::possible_value> values()` and `static constexpr
     *                std::string_view name()`.
     */
    template<class Domain>
    struct choice {
        std::string text;

        friend constexpr bool operator==(const choice&, const choice&) = default;
    };

    // --- clap's inline lists, one tag apiece -----------------------------------

    struct one_value {
        static constexpr std::array<clapp::possible_value, 1> table{
                clapp::make_possible_value("test123")};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "one_value"; }
    };

    struct two_values {
        static constexpr std::array<clapp::possible_value, 2> table{
                clapp::make_possible_value("test123"), clapp::make_possible_value("test321")};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "two_values"; }
    };

    struct speeds {
        static constexpr std::array<clapp::possible_value, 3> table{
                clapp::make_possible_value("slow"),
                clapp::make_possible_value("fast"),
                clapp::make_possible_value("ludicrous speed")};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "speeds"; }
    };

    struct aliased_speeds {
        static constexpr auto fast_aliases = clapp::make_static_aliases(std::array{"fost"sv});
        static constexpr auto ludicrous_names =
                clapp::make_static_aliases(std::array{"ls"sv, "lcs"sv});
        static constexpr std::array<clapp::possible_value, 3> table{
                clapp::make_possible_value("slow"),
                clapp::make_possible_value("fast").with_aliases(fast_aliases),
                clapp::make_possible_value("ludicrous speed").with_aliases(ludicrous_names)};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "aliased_speeds"; }
    };

    struct hidden_speeds {
        static constexpr std::array<clapp::possible_value, 4> table{
                clapp::make_possible_value("slow"),
                clapp::make_possible_value("fast"),
                clapp::make_possible_value("ludicrous speed"),
                clapp::make_possible_value("forbidden speed").with_hide()};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "hidden_speeds"; }
    };

    struct missing_speeds {
        static constexpr auto fast_aliases = clapp::make_static_aliases(std::array{"fost"sv});
        static constexpr std::array<clapp::possible_value, 4> table{
                clapp::make_possible_value("slow"),
                clapp::make_possible_value("fast").with_aliases(fast_aliases),
                clapp::make_possible_value("ludicrous speed"),
                clapp::make_possible_value("forbidden speed").with_hide()};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "missing_speeds"; }
    };

    struct one_alias {
        static constexpr auto names = clapp::make_static_aliases(std::array{"123"sv});
        static constexpr std::array<clapp::possible_value, 2> table{
                clapp::make_possible_value("test123").with_aliases(names),
                clapp::make_possible_value("test321")};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "one_alias"; }
    };

    struct three_aliases {
        static constexpr auto names = clapp::make_static_aliases(std::array{"1"sv, "2"sv, "3"sv});
        static constexpr std::array<clapp::possible_value, 2> table{
                clapp::make_possible_value("test123").with_aliases(names),
                clapp::make_possible_value("test321")};
        static constexpr std::span<const clapp::possible_value> values() noexcept { return table; }
        static constexpr std::string_view name() noexcept { return "three_aliases"; }
    };

}  // namespace

namespace clapp {
    template<class Domain>
    struct value_parser<choice<Domain>> {
        /**
         * \brief Accept \p value when it names a member of `Domain`, and hand back the
         *        bytes as typed — clap's `PossibleValuesParser::parse_ref`.
         */
        [[nodiscard]] static constexpr std::expected<choice<Domain>, parse_error>
        parse(os_str value, bool ignore_case = false) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value())
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_utf8,
                                                   .input     = value,
                                                   .type_name = Domain::name(),
                                                   .reason    = "invalid UTF-8",
                                                   .possible  = Domain::values(),
                                                   .encoding  = text.error()});
            for (const possible_value& one : Domain::values())
                if (one.matches(*text, ignore_case)) return choice<Domain>{std::string(*text)};
            return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                               .input     = value,
                                               .type_name = Domain::name(),
                                               .reason = "value is not one of the accepted values",
                                               .possible = Domain::values()});
        }

        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return Domain::values();
        }
    };
}  // namespace clapp

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

    template<class Domain>
    std::optional<std::string> one_choice(const arg_matches& matches, std::string_view id) {
        const std::optional<const choice<Domain>*> found = matches.get_one<choice<Domain>>(id);
        if (!found.has_value()) return std::nullopt;
        return (*found)->text;
    }

    template<class Domain>
    std::vector<std::string> many_choices(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const std::optional<clapp::values_ref<choice<Domain>>> found =
                matches.get_many<choice<Domain>>(id);
        if (!found.has_value()) return out;
        for (const choice<Domain>& one : *found) out.push_back(one.text);
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_positional_one() {
        command_builder app("possible_values");
        std::move(app).arg(arg_builder("positional").index(1).value_parser<choice<one_value>>());
        return app.freeze();
    }
    constexpr command_spec positional_one = make_positional_one();

    consteval command_spec make_positional_many() {
        command_builder app("possible_values");
        std::move(app).arg(arg_builder("positional")
                                   .index(1)
                                   .action(arg_action::set)
                                   .value_parser<choice<two_values>>()
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec positional_many = make_positional_many();

    consteval command_spec make_option_one() {
        command_builder app("possible_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<one_value>>());
        return app.freeze();
    }
    constexpr command_spec option_one = make_option_one();

    consteval command_spec make_option_append() {
        command_builder app("possible_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .value_parser<choice<two_values>>()
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec option_append = make_option_append();

    consteval command_spec make_speeds_cmd() {
        command_builder app("test");
        std::move(app).arg(arg_builder("option")
                                   .short_('O')
                                   .value_name("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<speeds>>());
        return app.freeze();
    }
    constexpr command_spec speeds_cmd = make_speeds_cmd();

    consteval command_spec make_aliased_speeds_cmd() {
        command_builder app("test");
        std::move(app).arg(arg_builder("option")
                                   .short_('O')
                                   .value_name("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<aliased_speeds>>());
        return app.freeze();
    }
    constexpr command_spec aliased_speeds_cmd = make_aliased_speeds_cmd();

    consteval command_spec make_hidden_speeds_cmd() {
        command_builder app("test");
        std::move(app).arg(arg_builder("option")
                                   .short_('O')
                                   .value_name("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<hidden_speeds>>());
        return app.freeze();
    }
    constexpr command_spec hidden_speeds_cmd = make_hidden_speeds_cmd();

    consteval command_spec make_missing_speeds_cmd() {
        command_builder app("test");
        std::move(app).arg(arg_builder("option")
                                   .short_('O')
                                   .value_name("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<missing_speeds>>());
        return app.freeze();
    }
    constexpr command_spec missing_speeds_cmd = make_missing_speeds_cmd();

    consteval command_spec make_one_alias_cmd() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<one_alias>>()
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec one_alias_cmd = make_one_alias_cmd();

    consteval command_spec make_three_aliases_cmd() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<three_aliases>>()
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec three_aliases_cmd = make_three_aliases_cmd();

    consteval command_spec make_folding() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<two_values>>()
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec folding = make_folding();

    consteval command_spec make_not_folding() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<two_values>>());
        return app.freeze();
    }
    constexpr command_spec not_folding = make_not_folding();

    consteval command_spec make_folding_many() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<two_values>>()
                                   .num_args(value_range::at_least(1))
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec folding_many = make_folding_many();

    consteval command_spec make_not_folding_many() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<choice<two_values>>()
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec not_folding_many = make_not_folding_many();

    // The domain reaches the frozen tree through the argument's parser, not through the
    // argument: that indirection is the divergence noted at the top, so it is asserted.
    static_assert(
            positional_one.find_arg("positional")->get_value_parser()->possible_values().size() ==
            1);
    static_assert(speeds_cmd.find_arg("option")->get_value_parser()->possible_values().size() == 3);
    static_assert(hidden_speeds_cmd.find_arg("option")
                          ->get_value_parser()
                          ->possible_values()[3]
                          .is_hide_set());
    static_assert(folding.find_arg("option")->is_ignore_case_set());
    static_assert(!not_folding.find_arg("option")->is_ignore_case_set());

}  // namespace

// ---------------------------------------------------------------------------
// Positionals
// ---------------------------------------------------------------------------

CLAPP_TEST("possible_values.rs::possible_values_of_positional") {
    const outcome got = clapp::parse(positional_one, raw_args{"myprog", "test123"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("positional"));
    CLAPP_CHECK(one_choice<one_value>(*got, "positional") == std::optional<std::string>{"test123"});
}

CLAPP_TEST("possible_values.rs::possible_value_arg_value") {
    // clap's second case differs only in building the entry through `PossibleValue::new`
    // with `.hide(false).help(…)` rather than from a bare string. In clapp both spellings
    // produce the same clapp::possible_value, so the assertion is that they do.
    constexpr clapp::possible_value with_help =
            clapp::make_possible_value("test123", "It's just a test");
    static_assert(with_help.get_name() == "test123");
    static_assert(!with_help.is_hide_set());
    static_assert(with_help.matches("test123", false));

    const outcome got = clapp::parse(positional_one, raw_args{"myprog", "test123"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("positional"));
    CLAPP_CHECK(one_choice<one_value>(*got, "positional") == std::optional<std::string>{"test123"});
}

CLAPP_TEST("possible_values.rs::possible_values_of_positional_fail") {
    const outcome got = clapp::parse(positional_one, raw_args{"myprog", "notest"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

CLAPP_TEST("possible_values.rs::possible_values_of_positional_multiple") {
    const outcome got = clapp::parse(positional_many, raw_args{"myprog", "test123", "test321"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("positional"));
    CLAPP_CHECK(many_choices<two_values>(*got, "positional") ==
                std::vector<std::string>{"test123", "test321"});
}

CLAPP_TEST("possible_values.rs::possible_values_of_positional_multiple_fail") {
    const outcome got = clapp::parse(positional_many, raw_args{"myprog", "test123", "notest"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

CLAPP_TEST("possible_values.rs::possible_values_of_option") {
    const outcome got = clapp::parse(option_one, raw_args{"myprog", "--option", "test123"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(one_choice<one_value>(*got, "option") == std::optional<std::string>{"test123"});
}

CLAPP_TEST("possible_values.rs::possible_values_of_option_fail") {
    const outcome got = clapp::parse(option_one, raw_args{"myprog", "--option", "notest"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

CLAPP_TEST("possible_values.rs::possible_values_of_option_multiple") {
    const outcome got =
            clapp::parse(option_append, raw_args{"", "--option", "test123", "--option", "test321"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(many_choices<two_values>(*got, "option") ==
                std::vector<std::string>{"test123", "test321"});
}

CLAPP_TEST("possible_values.rs::possible_values_of_option_multiple_fail") {
    const outcome got =
            clapp::parse(option_append, raw_args{"", "--option", "test123", "--option", "notest"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

// ---------------------------------------------------------------------------
// The rendered message — three fixtures, one expected page
// ---------------------------------------------------------------------------

namespace {
    constexpr std::string_view slo_error = "error: invalid value 'slo' for '-O <option>'\n"
                                           "  [possible values: slow, fast, \"ludicrous speed\"]\n"
                                           "\n"
                                           "  tip: a similar value exists: 'slow'\n"
                                           "\n"
                                           "For more information, try '--help'.\n";
}  // namespace

CLAPP_TEST("possible_values.rs::possible_values_output") {
    const outcome got = clapp::parse(speeds_cmd, raw_args{"clap-test", "-O", "slo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(message_of(got) == slo_error);
}

CLAPP_TEST("possible_values.rs::possible_values_alias_output") {
    // Aliases are matched but never listed: byte for byte the message above.
    const outcome got = clapp::parse(aliased_speeds_cmd, raw_args{"clap-test", "-O", "slo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(message_of(got) == slo_error);
}

CLAPP_TEST("possible_values.rs::possible_values_hidden_output") {
    // Nor are hidden values: `forbidden speed` matches and does not print.
    const outcome got = clapp::parse(hidden_speeds_cmd, raw_args{"clap-test", "-O", "slo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(message_of(got) == slo_error);
    // ...and it really does match, which is what makes its absence above a *filter*
    // rather than an omission.
    const outcome accepted =
            clapp::parse(hidden_speeds_cmd, raw_args{"clap-test", "-O", "forbidden speed"});
    CLAPP_CHECK(accepted.has_value());
}

CLAPP_TEST("possible_values.rs::escaped_possible_values_output") {
    const outcome got = clapp::parse(speeds_cmd, raw_args{"clap-test", "-O", "ludicrous"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(message_of(got) == "error: invalid value 'ludicrous' for '-O <option>'\n"
                                   "  [possible values: slow, fast, \"ludicrous speed\"]\n"
                                   "\n"
                                   "  tip: a similar value exists: 'ludicrous speed'\n"
                                   "\n"
                                   "For more information, try '--help'.\n");
}

CLAPP_TEST("possible_values.rs::missing_possible_value_error") {
    const outcome got = clapp::parse(missing_speeds_cmd, raw_args{"clap-test", "-O"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(message_of(got) ==
                "error: a value is required for '-O <option>' but none was supplied\n"
                "  [possible values: slow, fast, \"ludicrous speed\"]\n"
                "\n"
                "For more information, try '--help'.\n");
}

// ---------------------------------------------------------------------------
// Aliases and case folding
// ---------------------------------------------------------------------------

CLAPP_TEST("possible_values.rs::alias") {
    const outcome got = clapp::parse(one_alias_cmd, raw_args{"pv", "--option", "123"});
    CLAPP_CHECK(got.has_value());
    // The value echoed back is what the user typed, not the canonical name.
    CLAPP_CHECK(one_choice<one_alias>(*got, "option") == std::optional<std::string>{"123"});
}

CLAPP_TEST("possible_values.rs::aliases") {
    const outcome got = clapp::parse(three_aliases_cmd, raw_args{"pv", "--option", "2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_choice<three_aliases>(*got, "option") == std::optional<std::string>{"2"});
}

CLAPP_TEST("possible_values.rs::ignore_case") {
    const outcome got = clapp::parse(folding, raw_args{"pv", "--option", "TeSt123"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_choice<two_values>(*got, "option") == std::optional<std::string>{"TeSt123"});
}

CLAPP_TEST("possible_values.rs::ignore_case_fail") {
    const outcome got = clapp::parse(not_folding, raw_args{"pv", "--option", "TeSt123"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

CLAPP_TEST("possible_values.rs::ignore_case_multiple") {
    const outcome got =
            clapp::parse(folding_many, raw_args{"pv", "--option", "TeSt123", "teST123", "tESt321"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(many_choices<two_values>(*got, "option") ==
                std::vector<std::string>{"TeSt123", "teST123", "tESt321"});
}

CLAPP_TEST("possible_values.rs::ignore_case_multiple_fail") {
    const outcome got = clapp::parse(not_folding_many,
                                     raw_args{"pv", "--option", "test123", "teST123", "test321"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}
