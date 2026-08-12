#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#    include <stdlib.h>  // setenv is POSIX, not in <cstdlib>
#endif

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    void set_env(const char* name, const char* value) {
#ifndef _WIN32
        ::setenv(name, value, 1);
#else
        static_cast<void>(name);
        static_cast<void>(value);
#endif
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    /**
     * \brief `one_string` at the end of a subcommand path, with a missing level reported as
     *        `std::nullopt` rather than a crash.
     */
    std::optional<std::string> one_string_at(const arg_matches& root,
                                             std::initializer_list<std::string_view> path,
                                             std::string_view id) {
        const arg_matches* here = &root;
        for (const std::string_view step : path) {
            here = here->subcommand_matches(step);
            if (here == nullptr) return std::nullopt;
        }
        return one_string(*here, id);
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    // clap's `issue_1076`.
    consteval command_spec make_myprog() {
        command_builder app("myprog");
        std::move(app)
                .arg(arg_builder("GLOBAL_ARG")
                             .long_("global-arg")
                             .help("Specifies something needed by the subcommands")
                             .global()
                             .action(arg_action::set)
                             .default_value("default_value"))
                .arg(arg_builder("GLOBAL_FLAG")
                             .long_("global-flag")
                             .help("Specifies something needed by the subcommands")
                             .global()
                             .action(arg_action::set))
                .subcommand(command_builder("outer").subcommand(command_builder("inner")));
        return app.freeze();
    }
    constexpr command_spec myprog = make_myprog();

    // clap's `propagate_global_arg_in_subcommand_to_subsubcommand_1385`: the global is
    // declared on `sub1`, not on the root.
    consteval command_spec make_sub_global() {
        command_builder app("foo");
        std::move(app).subcommand(
                command_builder("sub1")
                        .arg(arg_builder("arg1").long_("arg1").action(arg_action::set).global())
                        .subcommand(command_builder("sub1a")));
        return app.freeze();
    }
    constexpr command_spec sub_global = make_sub_global();

    // clap's `propagate_global_arg_to_subcommand_in_subsubcommand_2053`: globals at two
    // levels at once.
    consteval command_spec make_two_levels() {
        command_builder app("opts");
        std::move(app)
                .arg(arg_builder("global-flag")
                             .long_("global-flag")
                             .action(arg_action::set_true)
                             .global())
                .arg(arg_builder("global-str")
                             .long_("global-str")
                             .value_name("str")
                             .action(arg_action::set)
                             .global())
                .subcommand(command_builder("test")
                                    .arg(arg_builder("sub-flag")
                                                 .long_("sub-flag")
                                                 .action(arg_action::set_true)
                                                 .global())
                                    .arg(arg_builder("sub-str")
                                                 .long_("sub-str")
                                                 .value_name("str")
                                                 .action(arg_action::set)
                                                 .global())
                                    .subcommand(command_builder("test")));
        return app.freeze();
    }
    constexpr command_spec two_levels = make_two_levels();

    // clap's `global_arg_available_in_subcommand`: one global, one deliberately not.
    consteval command_spec make_opt_in() {
        command_builder app("opt");
        std::move(app)
                .args({arg_builder("global").global().long_("global").action(arg_action::set_true),
                       arg_builder("not").global(false).long_("not").action(arg_action::set_true)})
                .subcommand(command_builder("ping"));
        return app.freeze();
    }
    constexpr command_spec opt_in = make_opt_in();

    // clap's `deeply_nested_discovery`: one global per level, four levels deep.
    consteval command_spec make_nested() {
        command_builder app("a");
        std::move(app)
                .arg(arg_builder("long-a").long_("long-a").global().action(arg_action::set_true))
                .subcommand(command_builder("b")
                                    .arg(arg_builder("long-b").long_("long-b").global().action(
                                            arg_action::set_true))
                                    .subcommand(command_builder("c")
                                                        .arg(arg_builder("long-c")
                                                                     .long_("long-c")
                                                                     .global()
                                                                     .action(arg_action::set_true))
                                                        .subcommand(command_builder("d"))));
        return app.freeze();
    }
    constexpr command_spec nested = make_nested();

    // clap's `global_overrides_default`.
    consteval command_spec make_defaulted() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("name")
                             .long_("name")
                             .global()
                             .action(arg_action::set)
                             .default_value("from_default"))
                .subcommand(command_builder("sub"));
        return app.freeze();
    }
    constexpr command_spec defaulted = make_defaulted();

    // clap's `global_overrides_env`. The variable is namespaced because ctest runs test
    // binaries concurrently.
    consteval command_spec make_from_env() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("name")
                             .long_("name")
                             .global()
                             .action(arg_action::set)
                             .env("CLAPP_CONF_GLOBAL_OVERRIDES_ENV"))
                .subcommand(command_builder("sub"));
        return app.freeze();
    }
    constexpr command_spec from_env = make_from_env();

    static_assert(sub_global.find_subcommand("sub1")->find_arg("arg1")->is_global_set());
    static_assert(!sub_global.has_arg("arg1"));
    static_assert(opt_in.find_arg("global")->is_global_set());
    static_assert(!opt_in.find_arg("not")->is_global_set());
    static_assert(two_levels.find_subcommand("test")->find_arg("sub-str")->is_global_set());

}  // namespace

// ---------------------------------------------------------------------------
// The frozen tree is reusable
// ---------------------------------------------------------------------------

CLAPP_TEST("global_args.rs::issue_1076") {
    // clap's version proves `Command::build` is idempotent across three
    // `try_get_matches_from_mut` calls. clapp's spec is immutable, so the same claim is
    // that three parses of it agree — including on the propagated default.
    for (int round = 0; round < 3; ++round) {
        const outcome got = clapp::parse(myprog, raw_args{"myprog"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(one_string(*got, "GLOBAL_ARG") == std::optional<std::string>{"default_value"});
        CLAPP_CHECK(!got->contains_id("GLOBAL_FLAG"));
    }
}

// ---------------------------------------------------------------------------
// A global declared below the root
// ---------------------------------------------------------------------------

CLAPP_TEST("global_args.rs::propagate_global_arg_in_subcommand_to_subsubcommand_1385") {
    const outcome got = clapp::parse(sub_global, raw_args{"foo", "sub1", "--arg1", "v1", "sub1a"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string_at(*got, {"sub1", "sub1a"}, "arg1") == std::optional<std::string>{"v1"});
}

CLAPP_TEST("global_args.rs::propagate_global_arg_to_subcommand_in_subsubcommand_2053") {
    const outcome got = clapp::parse(two_levels,
                                     raw_args{"cmd",
                                              "test",
                                              "test",
                                              "--global-flag",
                                              "--global-str",
                                              "hello",
                                              "--sub-flag",
                                              "--sub-str",
                                              "world"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string_at(*got, {"test"}, "sub-str") == std::optional<std::string>{"world"});
    // The root's own global made the same trip; asserted here because clap's fixture
    // declares it and then never looks.
    CLAPP_CHECK(one_string(*got, "global-str") == std::optional<std::string>{"hello"});
}

CLAPP_TEST("global_args.rs::global_arg_available_in_subcommand") {
    const outcome got = clapp::parse(opt_in, raw_args{"opt", "ping", "--global"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("global"));
    const arg_matches* ping = got->subcommand_matches("ping");
    CLAPP_CHECK(ping != nullptr);
    CLAPP_CHECK(ping->get_flag("global"));
    // `global(false)` is not merely the absence of the flag. `not` is not even a *valid
    // id* at this level — `try_contains_id` reports `unknown_argument` rather than
    // `false`, and the non-`try` spelling would abort. That is a stronger statement than
    // clap's (`ArgMatches::contains_id` there simply returns false), and it is the
    // observable clapp offers.
    CLAPP_CHECK(!ping->try_contains_id("not").has_value());
    // At the root the same id is valid, present (its `set_true` default was injected)
    // and false — which is what makes the child's answer a statement about propagation
    // rather than about the flag never having been set.
    CLAPP_CHECK(got->try_contains_id("not") == std::expected<bool, clapp::matches_error>{true});
    CLAPP_CHECK(!got->get_flag("not"));
}

CLAPP_TEST("global_args.rs::deeply_nested_discovery") {
    const outcome got =
            clapp::parse(nested, raw_args{"a", "b", "c", "d", "--long-a", "--long-b", "--long-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("long-a"));

    const arg_matches* b = got->subcommand_matches("b");
    CLAPP_CHECK(b != nullptr);
    CLAPP_CHECK(b->get_flag("long-b"));

    const arg_matches* c = b->subcommand_matches("c");
    CLAPP_CHECK(c != nullptr);
    CLAPP_CHECK(c->get_flag("long-c"));
}

// ---------------------------------------------------------------------------
// A global beats the weaker sources, from either side of the subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("global_args.rs::global_overrides_default") {
    CLAPP_CHECK(one_string(*clapp::parse(defaulted, raw_args{"test"}), "name") ==
                std::optional<std::string>{"from_default"});
    CLAPP_CHECK(one_string(*clapp::parse(defaulted, raw_args{"test", "--name", "from_arg"}),
                           "name") == std::optional<std::string>{"from_arg"});
    CLAPP_CHECK(one_string(*clapp::parse(defaulted, raw_args{"test", "--name", "from_arg", "sub"}),
                           "name") == std::optional<std::string>{"from_arg"});
    // The upward direction: the root had already defaulted `name` when the child set it.
    CLAPP_CHECK(one_string(*clapp::parse(defaulted, raw_args{"test", "sub", "--name", "from_arg"}),
                           "name") == std::optional<std::string>{"from_arg"});
}

CLAPP_TEST("global_args.rs::global_overrides_env") {
    set_env("CLAPP_CONF_GLOBAL_OVERRIDES_ENV", "from_env");

    CLAPP_CHECK(one_string(*clapp::parse(from_env, raw_args{"test"}), "name") ==
                std::optional<std::string>{"from_env"});
    CLAPP_CHECK(one_string(*clapp::parse(from_env, raw_args{"test", "--name", "from_arg"}),
                           "name") == std::optional<std::string>{"from_arg"});
    CLAPP_CHECK(one_string(*clapp::parse(from_env, raw_args{"test", "--name", "from_arg", "sub"}),
                           "name") == std::optional<std::string>{"from_arg"});
    CLAPP_CHECK(one_string(*clapp::parse(from_env, raw_args{"test", "sub", "--name", "from_arg"}),
                           "name") == std::optional<std::string>{"from_arg"});
}
