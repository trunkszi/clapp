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
     * \brief The page `--help` prints, with clap's `use_long` collapse applied.
     *        See the fuller note in conformance_hidden_args_test.cpp.
     */
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

    /**
     * clap declares the same four arguments in every case here; only the ordering controls
     * around them change. Factoring them out makes those controls the only thing that
     * differs between the fixtures, which is the point of the file.
     *
     * \param app The command under construction, already carrying its ordering controls.
     */
    consteval void add_four_args(command_builder& app) {
        std::move(app)
                .arg(arg_builder("flag_b")
                             .long_("flag_b")
                             .help("first flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_b")
                             .long_("option_b")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("first option"))
                .arg(arg_builder("flag_a")
                             .long_("flag_a")
                             .help("second flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_a")
                             .long_("option_a")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("second option"));
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_no_derive_order() {
        command_builder app("test");
        std::move(app).version("1.2").next_display_order(std::nullopt);
        add_four_args(app);
        return app.freeze();
    }
    constexpr command_spec no_derive_order = make_no_derive_order();

    consteval command_spec make_derive_order() {
        command_builder app("test");
        std::move(app).version("1.2");
        add_four_args(app);
        return app.freeze();
    }
    constexpr command_spec derive_order = make_derive_order();

    /**
     * clap's `derive_order_no_next_order`: no cursor, and the four arguments declared in a
     * different order than `add_four_args` uses, so alphabetical and declaration order are
     * distinguishable.
     */
    consteval command_spec make_no_next_order() {
        command_builder app("test");
        std::move(app)
                .version("1.2")
                .next_display_order(std::nullopt)
                .arg(arg_builder("flag_a")
                             .long_("flag_a")
                             .help("first flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_a")
                             .long_("option_a")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("first option"))
                .arg(arg_builder("flag_b")
                             .long_("flag_b")
                             .help("second flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_b")
                             .long_("option_b")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("second option"));
        return app.freeze();
    }
    constexpr command_spec no_next_order = make_no_next_order();

    consteval command_spec make_next_order() {
        command_builder app("test");
        std::move(app)
                .version("1.2")
                .next_display_order(10000)
                .arg(arg_builder("flag_a")
                             .long_("flag_a")
                             .help("second flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_a")
                             .long_("option_a")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("second option"))
                .next_display_order(10)
                .arg(arg_builder("flag_b")
                             .long_("flag_b")
                             .help("first flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_b")
                             .long_("option_b")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("first option"));
        return app.freeze();
    }
    constexpr command_spec next_order = make_next_order();

    consteval command_spec make_sc_propagate() {
        command_builder sub("sub");
        std::move(sub).version("1.2");
        add_four_args(sub);
        command_builder app("test");
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }
    constexpr command_spec sc_propagate = make_sc_propagate();

    consteval command_spec make_sc_explicit_order() {
        command_builder sub("sub");
        std::move(sub)
                .version("1.2")
                .arg(arg_builder("flag_b")
                             .long_("flag_b")
                             .help("first flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("option_b")
                             .long_("option_b")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("first option"))
                .arg(arg_builder("flag_a")
                             .long_("flag_a")
                             .help("second flag")
                             .display_order(0)
                             .action(arg_action::set_true))
                .arg(arg_builder("option_a")
                             .long_("option_a")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help("second option"));
        command_builder app("test");
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }
    constexpr command_spec sc_explicit_order = make_sc_explicit_order();

    consteval command_spec make_subcmd_alpha_order() {
        command_builder app("test");
        std::move(app)
                .version("1")
                .next_display_order(std::nullopt)
                .subcommand(command_builder("b1").about("blah b1").arg(
                        arg_builder("test").short_('t').action(arg_action::set_true)))
                .subcommand(command_builder("a1").about("blah a1").arg(
                        arg_builder("roster").short_('r').action(arg_action::set_true)));
        return app.freeze();
    }
    constexpr command_spec subcmd_alpha_order = make_subcmd_alpha_order();

    consteval command_spec make_subcmd_decl_order() {
        command_builder app("test");
        std::move(app)
                .version("1")
                .subcommand(command_builder("b1").about("blah b1").arg(
                        arg_builder("test").short_('t').action(arg_action::set_true)))
                .subcommand(command_builder("a1").about("blah a1").arg(
                        arg_builder("roster").short_('r').action(arg_action::set_true)));
        return app.freeze();
    }
    constexpr command_spec subcmd_decl_order = make_subcmd_decl_order();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // The sort key itself, asserted where it is set. A screen in the wrong order is then a
    // renderer bug rather than a builder bug, and vice versa — which is the whole reason
    // these are here and not left implicit in the expected strings.
    // ---------------------------------------------------------------------------

    // With a cursor, the author's arguments are numbered from 0 in declaration order and the
    // injected flags keep the default that sorts them last.
    static_assert(derive_order.find_arg("flag_b")->get_display_order() == 0);
    static_assert(derive_order.find_arg("option_b")->get_display_order() == 1);
    static_assert(derive_order.find_arg("flag_a")->get_display_order() == 2);
    static_assert(derive_order.find_arg("option_a")->get_display_order() == 3);
    static_assert(derive_order.find_arg("help")->get_display_order() >
                  derive_order.find_arg("option_a")->get_display_order());

    // Without one, every argument shares the default, so only the name can break the tie —
    // including for the injected flags, which is what interleaves them.
    static_assert(no_derive_order.find_arg("flag_a")->get_display_order() ==
                  no_derive_order.find_arg("help")->get_display_order());
    static_assert(no_derive_order.find_arg("version")->get_display_order() ==
                  no_derive_order.find_arg("option_b")->get_display_order());

    // The cursor moving backwards is visible in the key, not only in the screen.
    static_assert(next_order.find_arg("flag_a")->get_display_order() == 10000);
    static_assert(next_order.find_arg("flag_b")->get_display_order() == 10);

    // An explicit order is not overwritten by the cursor that would otherwise have numbered
    // it — the case the \note on next_display_order() warns is indistinguishable at 999.
    static_assert(
            sc_explicit_order.find_subcommand("sub")->find_arg("flag_a")->get_display_order() == 0);

}  // namespace

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

CLAPP_TEST("derive_order.rs::no_derive_order") {
    // Alphabetical, with `-h` and `-V` sorted into the list rather than appended to it.
    CLAPP_CHECK(same(page(no_derive_order, true),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --flag_a               second flag\n"
                     "      --flag_b               first flag\n"
                     "  -h, --help                 Print help\n"
                     "      --option_a <option_a>  second option\n"
                     "      --option_b <option_b>  first option\n"
                     "  -V, --version              Print version\n"));
}

CLAPP_TEST("derive_order.rs::derive_order") {
    // Declaration order, injected flags last.
    CLAPP_CHECK(same(page(derive_order, true),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --flag_b               first flag\n"
                     "      --option_b <option_b>  first option\n"
                     "      --flag_a               second flag\n"
                     "      --option_a <option_a>  second option\n"
                     "  -h, --help                 Print help\n"
                     "  -V, --version              Print version\n"));
}

CLAPP_TEST("derive_order.rs::derive_order_next_order") {
    // The pair declared second prints first, and the injected flags land between the
    // two groups because 999 sits between 10 and 10000.
    CLAPP_CHECK(same(page(next_order, true),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --flag_b               first flag\n"
                     "      --option_b <option_b>  first option\n"
                     "  -h, --help                 Print help\n"
                     "  -V, --version              Print version\n"
                     "      --flag_a               second flag\n"
                     "      --option_a <option_a>  second option\n"));
}

CLAPP_TEST("derive_order.rs::derive_order_no_next_order") {
    CLAPP_CHECK(same(page(no_next_order, true),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --flag_a               first flag\n"
                     "      --flag_b               second flag\n"
                     "  -h, --help                 Print help\n"
                     "      --option_a <option_a>  first option\n"
                     "      --option_b <option_b>  second option\n"
                     "  -V, --version              Print version\n"));
}

// ---------------------------------------------------------------------------
// Inside a subcommand — same rules, and a usage line that names the path
// ---------------------------------------------------------------------------

CLAPP_TEST("derive_order.rs::derive_order_subcommand_propagate") {
    CLAPP_CHECK(same(page(*sc_propagate.find_subcommand("sub"), true, "test sub"),
                     "Usage: test sub [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --flag_b               first flag\n"
                     "      --option_b <option_b>  first option\n"
                     "      --flag_a               second flag\n"
                     "      --option_a <option_a>  second option\n"
                     "  -h, --help                 Print help\n"
                     "  -V, --version              Print version\n"));
}

CLAPP_TEST("derive_order.rs::derive_order_subcommand_propagate_with_explicit_display_order") {
    // `flag_a` asked for 0 and jumps the queue; the other three keep their relative order.
    CLAPP_CHECK(same(page(*sc_explicit_order.find_subcommand("sub"), true, "test sub"),
                     "Usage: test sub [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --flag_a               second flag\n"
                     "      --flag_b               first flag\n"
                     "      --option_b <option_b>  first option\n"
                     "      --option_a <option_a>  second option\n"
                     "  -h, --help                 Print help\n"
                     "  -V, --version              Print version\n"));
}

// ---------------------------------------------------------------------------
// The subcommand list obeys the same two rules
// ---------------------------------------------------------------------------

CLAPP_TEST("derive_order.rs::subcommand_sorted_display_order") {
    CLAPP_CHECK(same(page(subcmd_alpha_order, true),
                     "Usage: test [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  a1    blah a1\n"
                     "  b1    blah b1\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("derive_order.rs::subcommand_derived_display_order") {
    CLAPP_CHECK(same(page(subcmd_decl_order, true),
                     "Usage: test [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  b1    blah b1\n"
                     "  a1    blah a1\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}
