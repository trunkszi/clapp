#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#    include <stdlib.h>  // unsetenv is POSIX, not in <cstdlib>
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

    using outcome = std::expected<arg_matches, error>;

    void clear_env(const char* name) {
#ifndef _WIN32
        ::unsetenv(name);
#else
        static_cast<void>(name);
#endif
    }

    std::vector<std::string> many_strings(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const std::optional<clapp::values_ref<std::string>> found =
                matches.get_many<std::string>(id);
        if (!found.has_value()) return out;
        for (const std::string& one : *found) out.push_back(one);
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_long_flags() {
        command_builder app("mo_flags_long");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("multflag")
                             .long_("multflag")
                             .help("allowed multiple flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("flag")
                             .long_("flag")
                             .help("disallowed multiple flag")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec long_flags = make_long_flags();

    consteval command_spec make_short_flags() {
        command_builder app("mo_flags_short");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("multflag")
                             .short_('m')
                             .long_("multflag")
                             .help("allowed multiple flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("disallowed multiple flag")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec short_flags = make_short_flags();

    consteval command_spec make_positional() {
        command_builder app("test");
        std::move(app).arg(arg_builder("multi")
                                   .index(1)
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec positional = make_positional();

    consteval command_spec make_counter() {
        command_builder app("mo_flags_large_qty");
        std::move(app).arg(arg_builder("multflag")
                                   .short_('m')
                                   .long_("multflag")
                                   .help("allowed multiple flag")
                                   .action(arg_action::count));
        return app.freeze();
    }
    constexpr command_spec counter = make_counter();

    // clap's `mo_before_env`: `.env()` declared BEFORE `.action()`.
    consteval command_spec make_before_env() {
        command_builder app("mo_before_env");
        std::move(app).arg(arg_builder("verbose")
                                   .env("CLAPP_CONF_MO_VERBOSE")
                                   .short_('v')
                                   .long_("verbose")
                                   .action(arg_action::count));
        return app.freeze();
    }
    constexpr command_spec before_env = make_before_env();

    // clap's `mo_after_env`: the same three calls, `.env()` last.
    consteval command_spec make_after_env() {
        command_builder app("mo_after_env");
        std::move(app).arg(arg_builder("verbose")
                                   .short_('v')
                                   .long_("verbose")
                                   .action(arg_action::count)
                                   .env("CLAPP_CONF_MO_VERBOSE"));
        return app.freeze();
    }
    constexpr command_spec after_env = make_after_env();

    static_assert(long_flags.is_args_override_self());
    static_assert(counter.find_arg("multflag")->get_action() == arg_action::count);
    static_assert(before_env.find_arg("verbose")->get_env() ==
                  after_env.find_arg("verbose")->get_env());
    static_assert(before_env.find_arg("verbose")->get_action() ==
                  after_env.find_arg("verbose")->get_action());

}  // namespace

// ---------------------------------------------------------------------------
// A repeated flag under args_override_self
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_occurrences.rs::multiple_occurrences_of_flags_long") {
    const outcome got =
            clapp::parse(long_flags, raw_args{"", "--multflag", "--flag", "--multflag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("multflag"));
    CLAPP_CHECK(got->get_flag("multflag"));
    CLAPP_CHECK(got->contains_id("flag"));
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("multiple_occurrences.rs::multiple_occurrences_of_flags_short") {
    const outcome got = clapp::parse(short_flags, raw_args{"", "-m", "-f", "-m"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("multflag"));
    CLAPP_CHECK(got->get_flag("multflag"));
    CLAPP_CHECK(got->contains_id("flag"));
    CLAPP_CHECK(got->get_flag("flag"));
}

// ---------------------------------------------------------------------------
// Absent, one, many
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_occurrences.rs::multiple_occurrences_of_positional") {
    const outcome none = clapp::parse(positional, raw_args{"test"});
    CLAPP_CHECK(none.has_value());
    CLAPP_CHECK(!none->contains_id("multi"));
    CLAPP_CHECK(!none->get_many<std::string>("multi").has_value());

    const outcome one = clapp::parse(positional, raw_args{"test", "one"});
    CLAPP_CHECK(one.has_value());
    CLAPP_CHECK(one->contains_id("multi"));
    CLAPP_CHECK(many_strings(*one, "multi") == std::vector<std::string>{"one"});

    const outcome four = clapp::parse(positional, raw_args{"test", "one", "two", "three", "four"});
    CLAPP_CHECK(four.has_value());
    CLAPP_CHECK(four->contains_id("multi"));
    CLAPP_CHECK(many_strings(*four, "multi") ==
                std::vector<std::string>{"one", "two", "three", "four"});
}

// ---------------------------------------------------------------------------
// Counting saturates
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_occurrences.rs::multiple_occurrences_of_flags_large_quantity") {
    const auto repeated = [](std::size_t times) {
        std::vector<std::string> argv{""};
        for (std::size_t i = 0; i < times; ++i) argv.emplace_back("-m");
        return raw_args(std::from_range, argv);
    };

    const outcome got = clapp::parse(counter, repeated(200));
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("multflag"));
    CLAPP_CHECK(got->get_count("multflag") == 200);

    const outcome saturated = clapp::parse(counter, repeated(500));
    CLAPP_CHECK(saturated.has_value());
    CLAPP_CHECK(saturated->contains_id("multflag"));
    CLAPP_CHECK(saturated->get_count("multflag") == 255);
}

// ---------------------------------------------------------------------------
// Builder order does not matter
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_occurrences.rs::multiple_occurrences_of_before_env") {
    clear_env("CLAPP_CONF_MO_VERBOSE");
    CLAPP_CHECK(clapp::parse(before_env, raw_args{""})->get_count("verbose") == 0);
    CLAPP_CHECK(clapp::parse(before_env, raw_args{"", "-v"})->get_count("verbose") == 1);
    CLAPP_CHECK(clapp::parse(before_env, raw_args{"", "-vv"})->get_count("verbose") == 2);
    CLAPP_CHECK(clapp::parse(before_env, raw_args{"", "-vvv"})->get_count("verbose") == 3);
}

CLAPP_TEST("multiple_occurrences.rs::multiple_occurrences_of_after_env") {
    clear_env("CLAPP_CONF_MO_VERBOSE");
    CLAPP_CHECK(clapp::parse(after_env, raw_args{""})->get_count("verbose") == 0);
    CLAPP_CHECK(clapp::parse(after_env, raw_args{"", "-v"})->get_count("verbose") == 1);
    CLAPP_CHECK(clapp::parse(after_env, raw_args{"", "-vv"})->get_count("verbose") == 2);
    CLAPP_CHECK(clapp::parse(after_env, raw_args{"", "-vvv"})->get_count("verbose") == 3);
}
