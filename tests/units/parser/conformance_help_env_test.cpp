#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::help_style;
    using clapp::value_range;

    /**
     * \brief A clapp::env_lookup that reports `ENVVAR=MYVAL` and nothing else.
     *
     * clap's cases mutate the real process environment; this replaces that with an input.
     * It is `constexpr`-callable, which is what lets the same fixtures be reached from a
     * `static_assert` in tests/units/output/help_test.cpp.
     */
    struct fake_env {
        [[nodiscard]] constexpr std::optional<std::string_view>
        operator()(std::string_view name) const noexcept {
            if (name == "ENVVAR") return std::string_view{"MYVAL"};
            return std::nullopt;
        }
    };

    static_assert(clapp::env_lookup<fake_env>);

    /**
     * \brief The page `--help` prints, with clap's `use_long` collapse applied and the
     *        environment supplied. See the fuller note in conformance_hidden_args_test.cpp.
     */
    std::string page(const command_spec& cmd, bool long_form) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd)},
                                  fake_env{})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // clap writes eight nearly identical commands out longhand. One builder over the three
    // axes that actually vary keeps the differences visible; the axes are named rather than
    // passed as bare bools at the call sites below.
    // ---------------------------------------------------------------------------

    consteval command_spec make_env_cmd(bool hide_env, bool hide_env_values, bool as_flag) {
        command_builder app("ctest");
        arg_builder cafe("cafe");
        std::move(cafe).short_('c').long_("cafe");
        if (as_flag) {
            std::move(cafe).action(arg_action::set_true);
        } else {
            std::move(cafe)
                    .value_name("FILE")
                    .action(arg_action::set)
                    .num_args(value_range::exactly(1));
        }
        if (hide_env) std::move(cafe).hide_env();
        if (hide_env_values) std::move(cafe).hide_env_values();
        std::move(cafe).env("ENVVAR").help("A coffeehouse, coffee shop, or cafe.");
        std::move(app).version("0.1").arg(std::move(cafe));
        return app.freeze();
    }

    constexpr command_spec hide_env_opt       = make_env_cmd(true, false, false);
    constexpr command_spec show_env_opt       = make_env_cmd(false, false, false);
    constexpr command_spec hide_env_vals_opt  = make_env_cmd(false, true, false);
    constexpr command_spec hide_env_flag      = make_env_cmd(true, false, true);
    constexpr command_spec show_env_flag      = make_env_cmd(false, false, true);
    constexpr command_spec hide_env_vals_flag = make_env_cmd(false, true, true);

    // The switches survive freeze(), and each command carries only the one it asked for.
    // Both sides of each are asserted, so an accessor stuck at `false` cannot pass.
    static_assert(hide_env_opt.find_arg("cafe")->is_hide_env_set());
    static_assert(!show_env_opt.find_arg("cafe")->is_hide_env_set());
    static_assert(hide_env_vals_opt.find_arg("cafe")->is_hide_env_values_set());
    static_assert(!show_env_opt.find_arg("cafe")->is_hide_env_values_set());

    // Declaring `env()` is not a long-help trigger, so all eight screens are the compact
    // layout even under `--help`. This is what makes the expected strings below two-column.
    static_assert(!clapp::long_help_exists(show_env_opt));

}  // namespace

// ---------------------------------------------------------------------------
// The option shape — `-c, --cafe <FILE>`
// ---------------------------------------------------------------------------

CLAPP_TEST("help_env.rs::hide_env") {
    CLAPP_CHECK(same(page(hide_env_opt, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or cafe.\n"
                     "  -h, --help         Print help\n"
                     "  -V, --version      Print version\n"));
}

CLAPP_TEST("help_env.rs::show_env") {
    CLAPP_CHECK(
            same(page(show_env_opt, true),
                 "Usage: ctest [OPTIONS]\n"
                 "\n"
                 "Options:\n"
                 "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or cafe. [env: ENVVAR=MYVAL]\n"
                 "  -h, --help         Print help\n"
                 "  -V, --version      Print version\n"));
}

CLAPP_TEST("help_env.rs::hide_env_vals") {
    // The variable's name survives; only its value is suppressed.
    CLAPP_CHECK(same(page(hide_env_vals_opt, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or cafe. [env: ENVVAR]\n"
                     "  -h, --help         Print help\n"
                     "  -V, --version      Print version\n"));
}

CLAPP_TEST("help_env.rs::show_env_vals") {
    // clap's `show_env_vals` builds the same command as `show_env` — neither switch set —
    // and asserts the same screen. Kept as its own case because it is the control the
    // other three are read against.
    CLAPP_CHECK(
            same(page(show_env_opt, true),
                 "Usage: ctest [OPTIONS]\n"
                 "\n"
                 "Options:\n"
                 "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or cafe. [env: ENVVAR=MYVAL]\n"
                 "  -h, --help         Print help\n"
                 "  -V, --version      Print version\n"));
}

// ---------------------------------------------------------------------------
// The flag shape — `-c, --cafe`, which takes no value and so measures a shorter column
// ---------------------------------------------------------------------------

CLAPP_TEST("help_env.rs::hide_env_flag") {
    CLAPP_CHECK(same(page(hide_env_flag, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe     A coffeehouse, coffee shop, or cafe.\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help_env.rs::show_env_flag") {
    CLAPP_CHECK(same(page(show_env_flag, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe     A coffeehouse, coffee shop, or cafe. [env: ENVVAR=MYVAL]\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help_env.rs::hide_env_vals_flag") {
    CLAPP_CHECK(same(page(hide_env_vals_flag, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe     A coffeehouse, coffee shop, or cafe. [env: ENVVAR]\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help_env.rs::show_env_vals_flag") {
    CLAPP_CHECK(same(page(show_env_flag, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe     A coffeehouse, coffee shop, or cafe. [env: ENVVAR=MYVAL]\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

// ---------------------------------------------------------------------------
// The seam itself
// ---------------------------------------------------------------------------

CLAPP_TEST("help_env.rs::an_unset_variable_renders_an_empty_value") {
    // Not one of clap's cases, and it is here because the seam has a second side that
    // clap's fixtures cannot reach: clapp::detail::no_env reports every variable as
    // unset, which is the environment a constant expression has. clap prints `[env: VAR=]`
    // for an unset variable, so the default lookup must too — otherwise every
    // `static_assert` over a page with an `env()` argument would be pinning a screen that
    // no real process ever sees.
    CLAPP_CHECK(same(clapp::render_help(show_env_opt, help_style{}).to_string(),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or cafe. [env: ENVVAR=]\n"
                     "  -h, --help         Print help\n"
                     "  -V, --version      Print version\n"));
}
