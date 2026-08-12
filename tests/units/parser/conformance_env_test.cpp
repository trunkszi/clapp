#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/value_source.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#    include <stdlib.h>  // setenv / unsetenv are POSIX, not in <cstdlib>
#endif

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::raw_args;
    using clapp::value_range;
    using clapp::value_source;

    using outcome = std::expected<arg_matches, error>;

    void set_env(const char* name, const char* value) {
#ifndef _WIN32
        ::setenv(name, value, 1);
#else
        static_cast<void>(name);
        static_cast<void>(value);
#endif
    }

    void clear_env(const char* name) {
#ifndef _WIN32
        ::unsetenv(name);
#else
        static_cast<void>(name);
#endif
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

    // clap's `value_parser(["env"])`, expressed the way clapp enumerates a domain.
    enum class only_env { env };

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_positional_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_BASIC")
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec positional_env = make_positional_env();

    consteval command_spec make_bool_env() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("present")
                             .short_('p')
                             .env("CLAPP_CONF_ENV_FLAG_TRUE")
                             .action(arg_action::set_true))
                .arg(arg_builder("negated")
                             .short_('n')
                             .env("CLAPP_CONF_ENV_FLAG_FALSE")
                             .action(arg_action::set_true))
                .arg(arg_builder("absent")
                             .short_('a')
                             .env("CLAPP_CONF_ENV_FLAG_ABSENT")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec bool_env = make_bool_env();

    consteval command_spec make_absent_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_NONE")
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec absent_env = make_absent_env();

    consteval command_spec make_absent_env_no_value() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg").index(1).help("some opt").env("CLAPP_CONF_ENV_NONE"));
        return app.freeze();
    }
    constexpr command_spec absent_env_no_value = make_absent_env_no_value();

    consteval command_spec make_env_and_default() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_WITH_DEFAULT")
                                   .action(arg_action::set)
                                   .default_value("default"));
        return app.freeze();
    }
    constexpr command_spec env_and_default = make_env_and_default();

    consteval command_spec make_option_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .long_("arg")
                                   .value_name("FILE")
                                   .help("some arg")
                                   .env("CLAPP_CONF_ENV_OVERRIDE")
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec option_env = make_option_env();

    consteval command_spec make_positional_override() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_POS_OVERRIDE")
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec positional_override = make_positional_override();

    consteval command_spec make_delimited_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_DELIMITED")
                                   .action(arg_action::set)
                                   .value_delimiter(',')
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec delimited_env = make_delimited_env();

    consteval command_spec make_undelimited_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_UNDELIMITED")
                                   .action(arg_action::set)
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec undelimited_env = make_undelimited_env();

    consteval command_spec make_enumerated_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_ENUMERATED")
                                   .action(arg_action::set)
                                   .value_parser<only_env>());
        return app.freeze();
    }
    constexpr command_spec enumerated_env = make_enumerated_env();

    consteval command_spec make_typed_env() {
        command_builder app("df");
        std::move(app).arg(arg_builder("arg")
                                   .index(1)
                                   .help("some opt")
                                   .env("CLAPP_CONF_ENV_TYPED")
                                   .action(arg_action::set)
                                   .value_parser<int>());
        return app.freeze();
    }
    constexpr command_spec typed_env = make_typed_env();

    static_assert(positional_env.find_arg("arg")->get_env() ==
                  std::optional<std::string_view>{"CLAPP_CONF_ENV_BASIC"});
    static_assert(!positional_env.find_arg("arg")->get_env()->empty());

}  // namespace

// ---------------------------------------------------------------------------
// The environment fills an absent argument
// ---------------------------------------------------------------------------

CLAPP_TEST("env.rs::env") {
    set_env("CLAPP_CONF_ENV_BASIC", "env");
    const outcome got = clapp::parse(positional_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::env_variable});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"env"});
}

CLAPP_TEST("env.rs::positionals") {
    // Same shape as `env`, kept separate because clap keeps it separate: a positional
    // reaches the environment wave through a different branch from an option.
    set_env("CLAPP_CONF_ENV_BASIC", "env");
    const outcome got = clapp::parse(positional_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::env_variable});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"env"});
}

CLAPP_TEST("env.rs::env_bool_literal") {
    // clap uses FalseyValueParser here; clapp's default bool parser already accepts all
    // twelve spellings case-insensitively, so "On" and "nO" are the test as written.
    set_env("CLAPP_CONF_ENV_FLAG_TRUE", "On");
    set_env("CLAPP_CONF_ENV_FLAG_FALSE", "nO");
    clear_env("CLAPP_CONF_ENV_FLAG_ABSENT");

    const outcome got = clapp::parse(bool_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("present"));
    CLAPP_CHECK(!got->get_flag("negated"));
    CLAPP_CHECK(!got->get_flag("absent"));
}

// ---------------------------------------------------------------------------
// Absence stays absence
// ---------------------------------------------------------------------------

CLAPP_TEST("env.rs::no_env") {
    clear_env("CLAPP_CONF_ENV_NONE");
    const outcome got = clapp::parse(absent_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("arg"));
    CLAPP_CHECK(!got->value_source("arg").has_value());
    CLAPP_CHECK(!one_string(*got, "arg").has_value());
}

CLAPP_TEST("env.rs::no_env_no_takes_value") {
    clear_env("CLAPP_CONF_ENV_NONE");
    const outcome got = clapp::parse(absent_env_no_value, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("arg"));
    CLAPP_CHECK(!got->value_source("arg").has_value());
}

// ---------------------------------------------------------------------------
// Precedence
// ---------------------------------------------------------------------------

CLAPP_TEST("env.rs::with_default") {
    // The environment beats `default_value` — the whole reason the waves are ordered.
    set_env("CLAPP_CONF_ENV_WITH_DEFAULT", "env");
    const outcome got = clapp::parse(env_and_default, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::env_variable});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"env"});
}

CLAPP_TEST("env.rs::opt_user_override") {
    set_env("CLAPP_CONF_ENV_OVERRIDE", "env");
    const outcome got = clapp::parse(option_env, raw_args{"", "--arg", "opt"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"opt"});
    // clap issue #1835: the environment's value must not be left behind in the list.
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"opt"});
}

CLAPP_TEST("env.rs::positionals_user_override") {
    set_env("CLAPP_CONF_ENV_POS_OVERRIDE", "env");
    const outcome got = clapp::parse(positional_override, raw_args{"", "opt"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->value_source("arg") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"opt"});
}

// ---------------------------------------------------------------------------
// The value takes the same road as a typed one
// ---------------------------------------------------------------------------

CLAPP_TEST("env.rs::multiple_one") {
    set_env("CLAPP_CONF_ENV_DELIMITED", "env");
    const outcome got = clapp::parse(delimited_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"env"});
}

CLAPP_TEST("env.rs::multiple_three") {
    set_env("CLAPP_CONF_ENV_DELIMITED", "env1,env2,env3");
    const outcome got = clapp::parse(delimited_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"env1", "env2", "env3"});
}

CLAPP_TEST("env.rs::multiple_no_delimiter") {
    // Without a delimiter the whole variable is ONE value, spaces and all.
    set_env("CLAPP_CONF_ENV_UNDELIMITED", "env1 env2 env3");
    const outcome got = clapp::parse(undelimited_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"env1 env2 env3"});
}

CLAPP_TEST("env.rs::possible_value") {
    set_env("CLAPP_CONF_ENV_ENUMERATED", "env");
    const outcome got = clapp::parse(enumerated_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"env"});
}

CLAPP_TEST("env.rs::not_possible_value") {
    // Outside the parser's domain: an ERROR, not a silent acceptance. This is the case
    // that catches storing the environment's bytes without running them through react().
    set_env("CLAPP_CONF_ENV_ENUMERATED", "nope");
    const outcome got = clapp::parse(enumerated_env, raw_args{""});
    CLAPP_CHECK(!got.has_value());
}

CLAPP_TEST("env.rs::value_parser_output") {
    set_env("CLAPP_CONF_ENV_TYPED", "42");
    const outcome got = clapp::parse(typed_env, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_one<int>("arg").has_value());
    if (got.has_value() && got->get_one<int>("arg").has_value())
        CLAPP_CHECK(**got->get_one<int>("arg") == 42);
}

CLAPP_TEST("env.rs::value_parser_invalid") {
    set_env("CLAPP_CONF_ENV_TYPED", "env");
    const outcome got = clapp::parse(typed_env, raw_args{""});
    CLAPP_CHECK(!got.has_value());
}
