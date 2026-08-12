#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <cstddef>
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

    // ---------------------------------------------------------------------------
    // The screen a user actually sees
    // ---------------------------------------------------------------------------

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *
     * This is clapp::detail::render_help_text() minus its terminal probe: that function
     * reaches clapp::render_help_for_terminal(), whose width depends on whether ctest is
     * attached to a tty and on `COLUMNS`, and a snapshot test cannot be allowed to depend
     * on either. Leaving `detected_width` empty pins clapp::default_terminal_width (100),
     * which is also the width clap's own tests run at.
     *
     * \note The `use_long && long_help_exists()` collapse is the whole reason this helper
     *       exists rather than a bare clapp::render_help() call. See the \warning on
     *       clapp::render_help(): passing `use_long` through unmodified renders a screen
     *       `--help` never produces.
     */
    std::string page(const command_spec& cmd, bool long_form, std::string_view usage_name = {}) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd),
                                             .usage_name = usage_name})
                .to_string();
    }

    /**
     * \brief Compare and, on mismatch, print both sides so the failure is readable.
     *
     * CLAPP_CHECK reports only the source text of its expression, which for a page
     * comparison is a wall of escaped newlines. Printing the two pages makes a one-column
     * misalignment visible at a glance.
     */
    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_hide_args() {
        command_builder app("test");
        std::move(app)
                .author("Kevin K.")
                .about("tests stuff")
                .version("1.4")
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("some flag")
                             .action(arg_action::set_true)
                             .hide())
                .arg(arg_builder("flag2")
                             .short_('F')
                             .long_("flag2")
                             .help("some other flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option")
                             .long_("option")
                             .value_name("opt")
                             .help("some option")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("DUMMY").index(1).hide());
        return app.freeze();
    }
    constexpr command_spec hide_args_cmd = make_hide_args();

    /** clap's `hide_short_args` / `hide_short_args_long_help`: one shared command, two screens. */
    consteval command_spec make_hide_short_args() {
        command_builder app("test");
        std::move(app)
                .about("hides short args")
                .author("Steve P.")
                .version("2.31.2")
                .arg(arg_builder("cfg")
                             .short_('c')
                             .long_("config")
                             .hide_short_help()
                             .action(arg_action::set_true)
                             .help("Some help text describing the --config arg"))
                .arg(arg_builder("visible")
                             .short_('v')
                             .long_("visible")
                             .action(arg_action::set_true)
                             .help("This text should be visible"));
        return app.freeze();
    }
    constexpr command_spec hide_short_cmd = make_hide_short_args();

    /** clap's `hide_long_args` / `hide_long_args_short_help`: the mirror image. */
    consteval command_spec make_hide_long_args() {
        command_builder app("test");
        std::move(app)
                .about("hides long args")
                .author("Steve P.")
                .version("2.31.2")
                .arg(arg_builder("cfg")
                             .short_('c')
                             .long_("config")
                             .hide_long_help()
                             .action(arg_action::set_true)
                             .help("Some help text describing the --config arg"))
                .arg(arg_builder("visible")
                             .short_('v')
                             .long_("visible")
                             .action(arg_action::set_true)
                             .help("This text should be visible"));
        return app.freeze();
    }
    constexpr command_spec hide_long_cmd = make_hide_long_args();

    consteval command_spec make_hide_pos_args() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .arg(arg_builder("pos").index(1).help("some pos").hide())
                .arg(arg_builder("another").index(2).help("another pos"));
        return app.freeze();
    }
    constexpr command_spec hide_pos_cmd = make_hide_pos_args();

    consteval command_spec make_hide_subcmds() {
        command_builder app("test");
        std::move(app).version("1.4").subcommand(command_builder("sub").hide());
        return app.freeze();
    }
    constexpr command_spec hide_subcmds_cmd = make_hide_subcmds();

    /**
     * clap's `hide_opt_args_only`: the help and version flags are the author's own, hidden,
     * so the automatic ones must not come back.
     */
    consteval command_spec make_hide_opt_only() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .after_help("After help")
                .disable_help_flag()
                .disable_version_flag()
                .arg(arg_builder("help").short_('h').long_("help").action(arg_action::help).hide())
                .arg(arg_builder("version")
                             .short_('v')
                             .long_("version")
                             .action(arg_action::set_true)
                             .hide())
                .arg(arg_builder("option")
                             .long_("option")
                             .value_name("opt")
                             .help("some option")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .hide());
        return app.freeze();
    }
    constexpr command_spec hide_opt_only_cmd = make_hide_opt_only();

    consteval command_spec make_hide_pos_only() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .after_help("After help")
                .disable_help_flag()
                .disable_version_flag()
                .arg(arg_builder("help").short_('h').long_("help").action(arg_action::help).hide())
                .arg(arg_builder("version")
                             .short_('v')
                             .long_("version")
                             .action(arg_action::set_true)
                             .hide())
                .arg(arg_builder("pos").index(1).help("some pos").hide());
        return app.freeze();
    }
    constexpr command_spec hide_pos_only_cmd = make_hide_pos_only();

    consteval command_spec make_hide_subcmds_only() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .after_help("After help")
                .disable_help_flag()
                .disable_version_flag()
                .arg(arg_builder("help").short_('h').long_("help").action(arg_action::help).hide())
                .arg(arg_builder("version")
                             .short_('v')
                             .long_("version")
                             .action(arg_action::set_true)
                             .hide())
                .subcommand(command_builder("sub").hide());
        return app.freeze();
    }
    constexpr command_spec hide_subcmds_only_cmd = make_hide_subcmds_only();

    /**
     * clap's `value_parser([PossibleValue::new("fast"), PossibleValue::new("slow").help(…)])`,
     * expressed the way clapp enumerates a domain.
     */
    enum class pv_speed { fast, slow[[= clapp::value{.help = "not as fast"}]] };

    consteval command_spec make_hidden_pv() {
        command_builder app("ctest");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .hide()
                                   .action(arg_action::set)
                                   .num_args(value_range::exactly(1))
                                   .value_parser<pv_speed>());
        return app.freeze();
    }
    constexpr command_spec hidden_pv_cmd = make_hidden_pv();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // These decide the screens above, so a change here explains a diff there rather than
    // leaving a wall of text to be read by eye.
    // ---------------------------------------------------------------------------

    static_assert(hide_args_cmd.find_arg("flag")->is_hide_set());
    static_assert(!hide_args_cmd.find_arg("flag2")->is_hide_set());

    // Neither switch is set anywhere in `hide_args`, and no possible value carries help, so
    // `--help` collapses onto `-h` and the compact layout is what a user sees. This is the
    // fact that makes the `hide_args` expectation below a two-column screen.
    static_assert(!clapp::long_help_exists(hide_args_cmd));

    // ...whereas either hide switch makes the two screens genuinely different.
    static_assert(clapp::long_help_exists(hide_short_cmd));
    static_assert(clapp::long_help_exists(hide_long_cmd));

    // The regression this file fixed: `sub` is hidden and only the injected `help` is left,
    // which must not count. Asserted on the populated side too, so the predicate cannot be
    // satisfied by a command that simply has no subcommands.
    static_assert(hide_subcmds_cmd.has_subcommands());
    static_assert(!hide_subcmds_cmd.has_visible_subcommands());
    static_assert(!hide_subcmds_only_cmd.has_visible_subcommands());

    // A hidden argument's possible values are invisible to long_help_exists(), which is the
    // whole point of `hidden_arg_with_possible_value_with_help`.
    static_assert(!clapp::long_help_exists(hidden_pv_cmd));

}  // namespace

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

CLAPP_TEST("hidden_args.rs::hide_args") {
    CLAPP_CHECK(same(page(hide_args_cmd, true),
                     "tests stuff\n"
                     "\n"
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -F, --flag2         some other flag\n"
                     "      --option <opt>  some option\n"
                     "  -h, --help          Print help\n"
                     "  -V, --version       Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_short_args") {
    CLAPP_CHECK(same(page(hide_short_cmd, false),
                     "hides short args\n"
                     "\n"
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -v, --visible  This text should be visible\n"
                     "  -h, --help     Print help (see more with '--help')\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_short_args_long_help") {
    CLAPP_CHECK(same(page(hide_short_cmd, true),
                     "hides short args\n"
                     "\n"
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --config\n"
                     "          Some help text describing the --config arg\n"
                     "\n"
                     "  -v, --visible\n"
                     "          This text should be visible\n"
                     "\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"
                     "\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_long_args") {
    CLAPP_CHECK(same(page(hide_long_cmd, true),
                     "hides long args\n"
                     "\n"
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -v, --visible\n"
                     "          This text should be visible\n"
                     "\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"
                     "\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_long_args_short_help") {
    CLAPP_CHECK(same(page(hide_long_cmd, false),
                     "hides long args\n"
                     "\n"
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --config   Some help text describing the --config arg\n"
                     "  -v, --visible  This text should be visible\n"
                     "  -h, --help     Print help (see more with '--help')\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_pos_args") {
    CLAPP_CHECK(same(page(hide_pos_cmd, true),
                     "Usage: test [another]\n"
                     "\n"
                     "Arguments:\n"
                     "  [another]  another pos\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_subcmds") {
    // The `[COMMAND]` slot is absent from the usage line as well as the section: one
    // predicate decides both, which is why the regression showed up twice.
    CLAPP_CHECK(same(page(hide_subcmds_cmd, true),
                     "Usage: test\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("hidden_args.rs::hide_opt_args_only") {
    CLAPP_CHECK(same(page(hide_opt_only_cmd, true),
                     "Usage: test\n"
                     "\n"
                     "After help\n"));
}

CLAPP_TEST("hidden_args.rs::hide_pos_args_only") {
    CLAPP_CHECK(same(page(hide_pos_only_cmd, true),
                     "Usage: test\n"
                     "\n"
                     "After help\n"));
}

CLAPP_TEST("hidden_args.rs::hide_subcmds_only") {
    CLAPP_CHECK(same(page(hide_subcmds_only_cmd, true),
                     "Usage: test\n"
                     "\n"
                     "After help\n"));
}

CLAPP_TEST("hidden_args.rs::hidden_arg_with_possible_value_with_help") {
    // `slow` carries help, which normally appends "(see more with '--help')" to the help
    // flag. `pos` is hidden, so nothing may change.
    CLAPP_CHECK(same(page(hidden_pv_cmd, true),
                     "Usage: ctest\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}
