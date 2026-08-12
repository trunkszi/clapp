#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/value_source.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_condition;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::group_builder;
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

    /**
     * \brief Whole-block comparison against clap's expected string, printing both on a
     *        mismatch. `opt_without_value_fail` needs it because the interesting part is an
     *        ABSENCE — that error carries no `Usage:` line — and no substring check can
     *        assert that something is missing.
     */
    bool same_block(const outcome& got, std::string_view want) {
        const std::string text = message_of(got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
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

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_defaulted_opt() {
        command_builder app("df");
        std::move(app).arg(arg_builder("o").short_('o').value_name("opt").default_value("default"));
        return app.freeze();
    }
    constexpr command_spec defaulted_opt = make_defaulted_opt();

    consteval command_spec make_defaulted_long() {
        command_builder app("df");
        std::move(app).arg(
                arg_builder("opt").long_("opt").value_name("FILE").default_value("default"));
        return app.freeze();
    }
    constexpr command_spec defaulted_long = make_defaulted_long();

    consteval command_spec make_defaulted_positional() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg").index(1).default_value("default"));
        return app.freeze();
    }
    constexpr command_spec defaulted_positional = make_defaulted_positional();

    // The conditional-default matrix. `opt` is watched; `arg` carries the rules.
    consteval command_spec make_if_present_no_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE").required())
                .arg(arg_builder("arg").index(1).default_value_if(
                        "opt",
                        arg_condition::present(),
                        std::optional<std::string_view>{"default"}));
        return app.freeze();
    }
    constexpr command_spec if_present_no_default = make_if_present_no_default();

    consteval command_spec make_if_present_optional_watch() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value_if(
                        "opt",
                        arg_condition::present(),
                        std::optional<std::string_view>{"default"}));
        return app.freeze();
    }
    constexpr command_spec if_present_optional_watch = make_if_present_optional_watch();

    consteval command_spec make_if_present_with_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value("first").default_value_if(
                        "opt",
                        arg_condition::present(),
                        std::optional<std::string_view>{"default"}));
        return app.freeze();
    }
    constexpr command_spec if_present_with_default = make_if_present_with_default();

    consteval command_spec make_if_value_no_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value_if("opt", "value", "default"));
        return app.freeze();
    }
    constexpr command_spec if_value_no_default = make_if_value_no_default();

    consteval command_spec make_if_some_no_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value_if("opt", "some", "default"));
        return app.freeze();
    }
    constexpr command_spec if_some_no_default = make_if_some_no_default();

    consteval command_spec make_if_some_with_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value("first").default_value_if(
                        "opt", "some", "default"));
        return app.freeze();
    }
    constexpr command_spec if_some_with_default = make_if_some_with_default();

    // The `std::nullopt` form, which REMOVES the default rather than replacing it.
    consteval command_spec make_unset_no_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value_if(
                        "opt", arg_condition::equal_to("value"), std::nullopt));
        return app.freeze();
    }
    constexpr command_spec unset_no_default = make_unset_no_default();

    consteval command_spec make_unset_with_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("arg").index(1).default_value("default").default_value_if(
                        "opt", arg_condition::equal_to("value"), std::nullopt));
        return app.freeze();
    }
    constexpr command_spec unset_with_default = make_unset_with_default();

    // Two rules, evaluated in declaration order.
    consteval command_spec make_two_rules() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .arg(arg_builder("arg")
                             .index(1)
                             .default_value("first")
                             .default_value_if("opt",
                                               arg_condition::equal_to("some"),
                                               std::optional<std::string_view>{"default"})
                             .default_value_if("flag",
                                               arg_condition::present(),
                                               std::optional<std::string_view>{"flg"}));
        return app.freeze();
    }
    constexpr command_spec two_rules = make_two_rules();

    consteval command_spec make_two_rules_second_unsets() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .arg(arg_builder("arg")
                             .index(1)
                             .default_value("first")
                             .default_value_if("opt",
                                               arg_condition::equal_to("some"),
                                               std::optional<std::string_view>{"default"})
                             .default_value_if("flag", arg_condition::present(), std::nullopt));
        return app.freeze();
    }
    constexpr command_spec two_rules_second_unsets = make_two_rules_second_unsets();

    // The multi-valued half of the matrix.
    consteval command_spec make_multi_if_plain() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("args")
                             .long_("args")
                             .num_args(value_range::exactly(2))
                             .default_values_if(
                                     "opt", arg_condition::equal_to("value"), {"df1", "df2"}));
        return app.freeze();
    }
    constexpr command_spec multi_if_plain = make_multi_if_plain();

    consteval command_spec make_multi_if_with_default() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("args")
                             .long_("args")
                             .num_args(value_range::exactly(2))
                             .default_values({"first", "second"})
                             .default_values_if(
                                     "opt", arg_condition::equal_to("value"), {"df1", "df2"}));
        return app.freeze();
    }
    constexpr command_spec multi_if_with_default = make_multi_if_with_default();

    consteval command_spec make_multi_two_rules() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").value_name("FILE"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .arg(arg_builder("args")
                             .long_("args")
                             .num_args(value_range::exactly(2))
                             .default_values({"first", "second"})
                             .default_values_if(
                                     "opt", arg_condition::equal_to("some"), {"d1", "d2"})
                             .default_values_if("flag", arg_condition::present(), {"d3", "d4"}));
        return app.freeze();
    }
    constexpr command_spec multi_two_rules = make_multi_two_rules();

    consteval command_spec make_multi_defaults() {
        command_builder app("diff");
        std::move(app).arg(arg_builder("files")
                                   .long_("files")
                                   .num_args(value_range::exactly(2))
                                   .default_values({"old", "new"}));
        return app.freeze();
    }
    constexpr command_spec multi_defaults = make_multi_defaults();

    // clap's `opt_without_value_fail`: a short option with a default AND a trailing `-o`
    // that supplies nothing. The default must NOT rescue it — an option that was spelled but
    // left empty is an error, which is the one cell where "has a default" and "was supplied"
    // come apart in the opposite direction from every other case in this file.
    consteval command_spec make_defaulted_needs_value() {
        command_builder app("df");
        std::move(app).arg(arg_builder("o")
                                   .short_('o')
                                   .value_name("opt")
                                   .help("some opt")
                                   .action(arg_action::set)
                                   .default_value("default"));
        return app.freeze();
    }
    constexpr command_spec defaulted_needs_value = make_defaulted_needs_value();

    // Interaction with the validator.
    consteval command_spec make_conditional_reqs() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("target")
                             .action(arg_action::set)
                             .default_value("file")
                             .long_("target"))
                .arg(arg_builder("input").action(arg_action::set).required().long_("input"))
                .arg(arg_builder("output")
                             .action(arg_action::set)
                             .required_if_eq("target", "file")
                             .long_("output"));
        return app.freeze();
    }
    constexpr command_spec conditional_reqs = make_conditional_reqs();

    consteval command_spec make_smart_usage() {
        command_builder app("bug");
        std::move(app)
                .arg(arg_builder("foo")
                             .long_("config")
                             .action(arg_action::set)
                             .default_value("bar"))
                .arg(arg_builder("input").index(1).required());
        return app.freeze();
    }
    constexpr command_spec smart_usage = make_smart_usage();

    consteval command_spec make_required_group_with_default() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("arg").index(1).default_value("value"))
                .group(group_builder("group").args({"arg"}).required());
        return app.freeze();
    }
    constexpr command_spec required_group_with_default = make_required_group_with_default();

    consteval command_spec make_required_arg_with_default() {
        command_builder app("test");
        std::move(app).arg(arg_builder("arg").index(1).required().default_value("value"));
        return app.freeze();
    }
    constexpr command_spec required_arg_with_default = make_required_arg_with_default();

    consteval command_spec make_exit_code() {
        command_builder app("hello");
        std::move(app).arg(arg_builder("exit-code")
                                   .long_("exit-code")
                                   .action(arg_action::set)
                                   .num_args(value_range::exactly(1))
                                   .default_value("0"));
        return app.freeze();
    }
    constexpr command_spec exit_code = make_exit_code();

    consteval command_spec make_delimited_default() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .help("multiple options")
                                   .value_delimiter(';')
                                   .default_value("first;second"));
        return app.freeze();
    }
    constexpr command_spec delimited_default = make_delimited_default();

    consteval command_spec make_delimited_missing() {
        command_builder app("program");
        std::move(app).arg(
                arg_builder("option")
                        .long_("option")
                        .value_delimiter(';')
                        .num_args(value_range::optional())
                        .default_missing_values({"value1;value2;value3", "value4;value5"}));
        return app.freeze();
    }
    constexpr command_spec delimited_missing = make_delimited_missing();

    consteval command_spec make_default_vs_trailing() {
        command_builder app("test");
        std::move(app)
                .dont_delimit_trailing_values()
                .arg(arg_builder("pos").index(1).required())
                .arg(arg_builder("flag").long_("flag").default_value("one,two").value_delimiter(
                        ','));
        return app.freeze();
    }
    constexpr command_spec default_vs_trailing = make_default_vs_trailing();

    static_assert(defaulted_opt.find_arg("o")->get_default_values().size() == 1);
    static_assert(if_present_with_default.find_arg("arg")->get_default_values_ifs().size() == 1);
    static_assert(two_rules.find_arg("arg")->get_default_values_ifs().size() == 2);
    static_assert(
            !two_rules_second_unsets.find_arg("arg")->get_default_values_ifs()[1].has_values());
    static_assert(multi_defaults.find_arg("files")->get_default_values().size() == 2);

}  // namespace

// ---------------------------------------------------------------------------
// The plain default
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::opts") {
    const outcome got = clapp::parse(defaulted_opt, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("o"));
    CLAPP_CHECK(got->value_source("o") == std::optional<value_source>{value_source::default_value});
    CLAPP_CHECK(one_string(*got, "o") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_vals.rs::default_has_index") {
    // A defaulted argument still gets a position, so `index_of` is not nullopt.
    const outcome got = clapp::parse(defaulted_opt, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->index_of("o") == std::optional<std::size_t>{1});
}

CLAPP_TEST("default_vals.rs::opt_user_override") {
    const outcome got = clapp::parse(defaulted_long, raw_args{"", "--opt", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("opt") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"value"});
}

CLAPP_TEST("default_vals.rs::positionals") {
    const outcome got = clapp::parse(defaulted_positional, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::default_value});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_vals.rs::positional_user_override") {
    const outcome got = clapp::parse(defaulted_positional, raw_args{"", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"value"});
}

CLAPP_TEST("default_vals.rs::multiple_defaults") {
    const outcome got = clapp::parse(multi_defaults, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == std::vector<std::string>{"old", "new"});
}

CLAPP_TEST("default_vals.rs::multiple_defaults_override") {
    const outcome got = clapp::parse(multi_defaults, raw_args{"", "--files", "other", "mine"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == std::vector<std::string>{"other", "mine"});
}

CLAPP_TEST("default_vals.rs::issue_1050_num_vals_and_defaults") {
    const outcome got = clapp::parse(exit_code, raw_args{"hello", "--exit-code=1"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "exit-code") == std::optional<std::string>{"1"});
}

// ---------------------------------------------------------------------------
// default_value_if, on presence
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::default_if_arg_present_no_default") {
    const outcome got = clapp::parse(if_present_no_default, raw_args{"", "--opt", "some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_no_default_user_override") {
    const outcome got =
            clapp::parse(if_present_optional_watch, raw_args{"", "--opt", "some", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_no_arg_with_default") {
    // The condition did not fire; the plain default answers.
    const outcome got = clapp::parse(if_present_with_default, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"first"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_default") {
    // It fired; the conditional default OUTRANKS the plain one.
    const outcome got = clapp::parse(if_present_with_default, raw_args{"", "--opt", "some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_default_user_override") {
    const outcome got =
            clapp::parse(if_present_with_default, raw_args{"", "--opt", "some", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_no_arg_with_default_user_override") {
    const outcome got = clapp::parse(if_present_with_default, raw_args{"", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

// ---------------------------------------------------------------------------
// default_value_if, on a value
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_no_default") {
    const outcome got = clapp::parse(if_value_no_default, raw_args{"", "--opt", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_no_default_fail") {
    // The WRONG value: no rule fires and there is no plain default, so `arg` is absent —
    // not present-and-empty.
    const outcome got = clapp::parse(if_value_no_default, raw_args{"", "--opt", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("arg"));
    CLAPP_CHECK(!one_string(*got, "arg").has_value());
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_no_default_user_override") {
    const outcome got = clapp::parse(if_some_no_default, raw_args{"", "--opt", "some", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_no_arg_with_default") {
    const outcome got = clapp::parse(if_some_with_default, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"first"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_no_arg_with_default_fail") {
    const outcome got = clapp::parse(if_some_with_default, raw_args{"", "--opt", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"first"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_with_default") {
    const outcome got = clapp::parse(if_some_with_default, raw_args{"", "--opt", "some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_with_value_with_default_user_override") {
    const outcome got = clapp::parse(if_some_with_default, raw_args{"", "--opt", "some", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

CLAPP_TEST("default_vals.rs::default_if_arg_present_no_arg_with_value_with_default_user_override") {
    const outcome got = clapp::parse(if_some_with_default, raw_args{"", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

CLAPP_TEST(
        "default_vals.rs::default_if_arg_present_no_arg_with_value_with_default_user_override_fail") {
    const outcome got = clapp::parse(if_some_with_default, raw_args{"", "--opt", "value", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

// ---------------------------------------------------------------------------
// Unsetting the default
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::no_default_if_arg_present_with_value_no_default") {
    const outcome got = clapp::parse(unset_no_default, raw_args{"", "--opt", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("arg"));
}

CLAPP_TEST("default_vals.rs::no_default_if_arg_present_with_value_with_default") {
    // The rule fires and its value list is EMPTY, which removes the plain default. An
    // implementation that reads "no values" as "nothing to do" leaves "default" here.
    const outcome got = clapp::parse(unset_with_default, raw_args{"", "--opt", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("arg"));
    CLAPP_CHECK(!one_string(*got, "arg").has_value());
}

CLAPP_TEST("default_vals.rs::no_default_if_arg_present_with_value_with_default_user_override") {
    const outcome got = clapp::parse(unset_with_default, raw_args{"", "--opt", "value", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"other"});
}

CLAPP_TEST("default_vals.rs::no_default_if_arg_present_no_arg_with_value_with_default") {
    // The rule did NOT fire, so the plain default survives.
    const outcome got = clapp::parse(unset_with_default, raw_args{"", "--opt", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

// ---------------------------------------------------------------------------
// Several rules, in declaration order
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::default_ifs_arg_present") {
    const outcome got = clapp::parse(two_rules, raw_args{"", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"flg"});
}

CLAPP_TEST("default_vals.rs::no_default_ifs_arg_present") {
    const outcome got = clapp::parse(two_rules_second_unsets, raw_args{"", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("arg"));
}

CLAPP_TEST("default_vals.rs::default_ifs_arg_present_user_override") {
    const outcome got = clapp::parse(two_rules, raw_args{"", "--flag", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"value"});
}

CLAPP_TEST("default_vals.rs::default_ifs_arg_present_order") {
    // BOTH conditions hold. The FIRST-declared rule wins; reversing the loop yields
    // "flg" and passes every single-condition case above.
    const outcome got = clapp::parse(two_rules, raw_args{"", "--opt=some", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"default"});
}

// ---------------------------------------------------------------------------
// The multi-valued half of the matrix
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_no_default") {
    const outcome got = clapp::parse(multi_if_plain, raw_args{"", "--opt", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"df1", "df2"});
}

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_no_default_fail") {
    const outcome got = clapp::parse(multi_if_plain, raw_args{"", "--opt", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("args"));
}

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_no_default_user_override") {
    const outcome got =
            clapp::parse(multi_if_plain, raw_args{"", "--opt", "value", "--args", "old", "new"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"old", "new"});
}

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_no_arg_with_default") {
    const outcome got = clapp::parse(multi_if_with_default, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"first", "second"});
}

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_no_arg_with_default_fail") {
    const outcome got = clapp::parse(multi_if_with_default, raw_args{"", "--opt", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"first", "second"});
}

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_with_default") {
    const outcome got = clapp::parse(multi_if_with_default, raw_args{"", "--opt", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"df1", "df2"});
}

CLAPP_TEST("default_vals.rs::default_values_if_arg_present_with_value_with_default_user_override") {
    const outcome got = clapp::parse(multi_if_with_default,
                                     raw_args{"", "--opt", "value", "--args", "other1", "other2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"other1", "other2"});
}

CLAPP_TEST("default_vals.rs::default_values_ifs_arg_present") {
    const outcome got = clapp::parse(multi_two_rules, raw_args{"", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"d3", "d4"});
}

CLAPP_TEST("default_vals.rs::default_values_ifs_arg_present_order") {
    const outcome got = clapp::parse(multi_two_rules, raw_args{"", "--opt=some", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"d1", "d2"});
}

CLAPP_TEST("default_vals.rs::default_values_ifs_arg_present_user_override") {
    const outcome got =
            clapp::parse(multi_two_rules, raw_args{"", "--flag", "--args", "value1", "value2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"value1", "value2"});
}

// ---------------------------------------------------------------------------
// Delimiters apply to defaults
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::with_value_delimiter") {
    const outcome got = clapp::parse(delimited_default, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == std::vector<std::string>{"first", "second"});
}

CLAPP_TEST("default_vals.rs::missing_with_value_delimiter") {
    // Two entries, each split by `;`, flattened into five values.
    const outcome got = clapp::parse(delimited_missing, raw_args{"program", "--option"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") ==
                std::vector<std::string>{"value1", "value2", "value3", "value4", "value5"});
}

CLAPP_TEST("default_vals.rs::default_independent_of_trailing") {
    // `dont_delimit_trailing_values` must not reach a DEFAULT: the same two values with
    // and without a `--` on the command line.
    const outcome plain = clapp::parse(default_vs_trailing, raw_args{"program", "here"});
    CLAPP_CHECK(plain.has_value());
    CLAPP_CHECK(one_string(*plain, "pos") == std::optional<std::string>{"here"});
    CLAPP_CHECK(raw_of(*plain, "flag") == std::vector<std::string>{"one", "two"});

    const outcome escaped = clapp::parse(default_vs_trailing, raw_args{"program", "--", "here"});
    CLAPP_CHECK(escaped.has_value());
    CLAPP_CHECK(one_string(*escaped, "pos") == std::optional<std::string>{"here"});
    CLAPP_CHECK(raw_of(*escaped, "flag") == std::vector<std::string>{"one", "two"});
}

// ---------------------------------------------------------------------------
// A default satisfies nothing
// ---------------------------------------------------------------------------

CLAPP_TEST("default_vals.rs::conditional_reqs_pass") {
    // `--target` is satisfied by its DEFAULT `"file"`, and that still fires
    // `required_if_eq("target", "file")` — the validator runs after the defaults wave.
    const outcome got = clapp::parse(conditional_reqs,
                                     raw_args{"test", "--input", "some", "--output", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "output") == std::optional<std::string>{"other"});
    CLAPP_CHECK(one_string(*got, "input") == std::optional<std::string>{"some"});
}

CLAPP_TEST("default_vals.rs::default_vals_donnot_show_in_smart_usage") {
    const outcome got = clapp::parse(smart_usage, raw_args{"bug"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(message_of(got) == "error: the following required arguments were not provided:\n"
                                   "  <input>\n"
                                   "\n"
                                   "Usage: bug <input>\n"
                                   "\n"
                                   "For more information, try '--help'.\n");
}

CLAPP_TEST("default_vals.rs::required_groups_with_default_values") {
    // The group is required and its only member is defaulted: a default does not occupy
    // a group, so the empty command line still fails.
    CLAPP_CHECK(!clapp::parse(required_group_with_default, raw_args{"test"}).has_value());

    const outcome supplied = clapp::parse(required_group_with_default, raw_args{"test", "value"});
    CLAPP_CHECK(supplied.has_value());
    CLAPP_CHECK(supplied->contains_id("arg"));
    CLAPP_CHECK(supplied->contains_id("group"));
}

CLAPP_TEST("default_vals.rs::required_args_with_default_values") {
    CLAPP_CHECK(!clapp::parse(required_arg_with_default, raw_args{"test"}).has_value());

    const outcome supplied = clapp::parse(required_arg_with_default, raw_args{"test", "value"});
    CLAPP_CHECK(supplied.has_value());
    CLAPP_CHECK(supplied->contains_id("arg"));
}

// ---------------------------------------------------------------------------
// The user-override column of the multi-valued matrix
//
// Every cell above answers "which default fires". These four answer "does anything on
// the command line outrank whichever one fired", and they are a different branch: the
// single-valued `default_value_if` path and the multi-valued `default_values_if` path do
// not share the code that decides an explicit occurrence wins. The file's own header
// advertises "the command line outranks both" as a pinned rule; without these it was
// pinned only on the single-valued half.
// ---------------------------------------------------------------------------

CLAPP_TEST(
        "default_vals.rs::default_values_if_arg_present_no_arg_with_value_with_default_user_override") {
    // No `--opt` at all, so the plain default is what would fire — and `--args` beats it.
    const outcome got =
            clapp::parse(multi_if_with_default, raw_args{"", "--args", "other1", "other2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("args"));
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"other1", "other2"});
}

CLAPP_TEST(
        "default_vals.rs::default_values_if_arg_present_no_arg_with_value_with_default_user_override_fail") {
    // `--opt some` does NOT match the `value` condition, so again the plain default is
    // the one in play — and again `--args` beats it. The "fail" in clap's name refers to
    // the condition failing, not to the parse.
    const outcome got = clapp::parse(multi_if_with_default,
                                     raw_args{"", "--opt", "some", "--args", "other1", "other2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"other1", "other2"});
}

CLAPP_TEST(
        "default_vals.rs::no_default_values_if_arg_present_with_value_with_default_user_override") {
    // The condition DOES match this time, so `df1 df2` is what would fire — and `--args`
    // beats that too. This is the cell that separates "outranks the plain default" from
    // "outranks any default": an implementation that special-cases only the former passes
    // the two above and fails here.
    const outcome got = clapp::parse(multi_if_with_default,
                                     raw_args{"", "--opt", "value", "--args", "other1", "other2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"other1", "other2"});
}

CLAPP_TEST("default_vals.rs::no_default_values_if_arg_present_no_arg_with_value_with_default") {
    // Nothing on the command line for `args`, and a condition that does not match: the
    // plain default. The negative control for the three above.
    const outcome got = clapp::parse(multi_if_with_default, raw_args{"", "--opt", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("args"));
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"first", "second"});
}

CLAPP_TEST("default_vals.rs::opt_without_value_fail") {
    // A default does not stand in for a value the user started to supply and did not
    // finish. Whole block: the sentence names the argument in its `-o <opt>` form, and
    // there is no `Usage:` line here at all — clap prints only the try-help line for this
    // one, and that absence is as much a part of the contract as the sentence.
    const outcome got = clapp::parse(defaulted_needs_value, raw_args{"", "-o"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
    CLAPP_CHECK(same_block(got,
                           "error: a value is required for '-o <opt>' but none was supplied\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}
