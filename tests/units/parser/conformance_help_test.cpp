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

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
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

    // ---------------------------------------------------------------------------
    // Value domains
    //
    // clap writes `value_parser(["fast", "slow"])` and `PossibleValue::new(..).help(..)`;
    // clapp enumerates a domain as a type, and the per-value name, help and hide flag are
    // annotations. The three enums differ only in which of those three are present, which is
    // what separates the four possible-value renderings.
    // ---------------------------------------------------------------------------

    /** Names only: the short screen lists them inline and the long screen still does. */
    enum class speed { fast, slow };

    /** One hidden value, which must be absent from an otherwise identical inline list. */
    enum class speed_secret {
        fast,
        slow,
        secret_speed[[= clapp::value{.name = "secret speed", .hide = true}]],
    };

    /**
     * One value carrying help, which switches the whole argument to a bullet list *and*
     * makes `--help` a different screen from `-h`.
     */
    enum class speed_help {
        fast,
        slow[[= clapp::value{.help = "not as fast"}]],
        secret_speed[[= clapp::value{.name = "secret speed", .hide = true}]],
    };

    /** clap's `["normal", " ", "\n", "\t", "other"]` — the escaping case. */
    enum class ws_vals {
        normal,
        sp[[= clapp::value{.name = " "}]],
        nl[[= clapp::value{.name = "\n"}]],
        tab[[= clapp::value{.name = "\t"}]],
        other,
    };

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_hide_pv() {
        command_builder app("ctest");
        std::move(app)
                .version("0.1")
                .arg(arg_builder("pos")
                             .short_('p')
                             .long_("pos")
                             .value_name("VAL")
                             .value_parser<speed>()
                             .help("Some vals")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("cafe")
                             .short_('c')
                             .long_("cafe")
                             .value_name("FILE")
                             .hide_possible_values()
                             .value_parser<speed>()
                             .help("A coffeehouse, coffee shop, or café.")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec hide_pv = make_hide_pv();

    consteval command_spec make_hide_single_pv() {
        command_builder app("ctest");
        std::move(app)
                .version("0.1")
                .arg(arg_builder("pos")
                             .short_('p')
                             .long_("pos")
                             .value_name("VAL")
                             .value_parser<speed_secret>()
                             .help("Some vals")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("cafe")
                             .short_('c')
                             .long_("cafe")
                             .value_name("FILE")
                             .help("A coffeehouse, coffee shop, or café.")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec hide_single_pv = make_hide_single_pv();

    consteval command_spec make_pv_with_help() {
        command_builder app("ctest");
        std::move(app)
                .version("0.1")
                .arg(arg_builder("pos")
                             .short_('p')
                             .long_("pos")
                             .value_name("VAL")
                             .value_parser<speed_help>()
                             .help("Some vals")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("cafe")
                             .short_('c')
                             .long_("cafe")
                             .value_name("FILE")
                             .help("A coffeehouse, coffee shop, or café.")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec pv_with_help = make_pv_with_help();

    consteval command_spec make_hidden_pv() {
        command_builder app("ctest");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .hide_possible_values()
                                   .value_parser<speed_help>()
                                   .action(arg_action::set)
                                   .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec hidden_pv = make_hidden_pv();

    /**
     * clap's `hide_default_val`, both halves. \p hide also switches the help text, exactly
     * as clap does — the point is that the two produce the same screen.
     */
    consteval command_spec make_hide_default(bool hide) {
        command_builder app("default");
        arg_builder one("argument");
        std::move(one)
                .help(hide ? "Pass an argument to the program. [default: default-argument]"
                           : "Pass an argument to the program.")
                .long_("arg")
                .action(arg_action::set)
                .num_args(value_range::exactly(1))
                .default_value("default-argument");
        if (hide) std::move(one).hide_default_value();
        std::move(app).version("0.1").term_width(120).arg(std::move(one));
        return app.freeze();
    }
    constexpr command_spec hide_default_hidden = make_hide_default(true);
    constexpr command_spec hide_default_shown  = make_hide_default(false);

    consteval command_spec make_empty_default() {
        command_builder app("default");
        std::move(app).version("0.1").term_width(120).arg(
                arg_builder("argument")
                        .help("Pass an argument to the program.")
                        .long_("arg")
                        .action(arg_action::set)
                        .num_args(value_range::exactly(1))
                        .default_value(""));
        return app.freeze();
    }
    constexpr command_spec empty_default = make_empty_default();

    consteval command_spec make_escaped_ws() {
        command_builder app("default");
        std::move(app).version("0.1").term_width(120).arg(
                arg_builder("argument")
                        .help("Pass an argument to the program.")
                        .long_("arg")
                        .action(arg_action::set)
                        .num_args(value_range::exactly(1))
                        .default_value("\n")
                        .value_parser<ws_vals>());
        return app.freeze();
    }
    constexpr command_spec escaped_ws = make_escaped_ws();

    consteval command_spec make_1364() {
        command_builder app("demo");
        std::move(app)
                .arg(arg_builder("foo").short_('f').action(arg_action::set_true))
                .arg(arg_builder("baz")
                             .short_('z')
                             .value_name("BAZ")
                             .hide_short_help()
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("files")
                             .index(1)
                             .value_name("FILES")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec cmd_1364 = make_1364();

    consteval command_spec make_short_with_value() {
        command_builder app("demo");
        std::move(app).arg(arg_builder("baz")
                                   .short_('z')
                                   .value_name("BAZ")
                                   .help("Short only")
                                   .help_heading("Baz")
                                   .action(arg_action::set)
                                   .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec short_with_value = make_short_with_value();

    consteval command_spec make_short_with_count() {
        command_builder app("demo");
        std::move(app).arg(arg_builder("baz")
                                   .short_('z')
                                   .action(arg_action::count)
                                   .help("Short only")
                                   .help_heading("Baz"));
        return app.freeze();
    }
    constexpr command_spec short_with_count = make_short_with_count();

    consteval command_spec make_custom_headers() {
        command_builder app("blorp");
        std::move(app)
                .author("Will M.")
                .about("does stuff")
                .version("1.4")
                .arg(arg_builder("fake")
                             .short_('f')
                             .long_("fake")
                             .help("some help")
                             .required()
                             .value_names({"some", "val"})
                             .action(arg_action::set)
                             .value_delimiter(':'))
                .next_help_heading("NETWORKING")
                .arg(arg_builder("no-proxy")
                             .short_('n')
                             .long_("no-proxy")
                             .action(arg_action::set_true)
                             .help("Do not use system proxy settings"))
                .arg(arg_builder("port").long_("port").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec custom_headers = make_custom_headers();

    consteval command_spec make_custom_heading_pos() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .arg(arg_builder("gear").index(1).help("Which gear"))
                .next_help_heading("NETWORKING")
                .arg(arg_builder("speed").index(2).help("How fast"));
        return app.freeze();
    }
    constexpr command_spec custom_heading_pos = make_custom_heading_pos();

    consteval command_spec make_only_heading_opts() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .disable_version_flag()
                .disable_help_flag()
                .arg(arg_builder("help").long_("help").action(arg_action::help).hide())
                .next_help_heading("NETWORKING")
                .arg(arg_builder("speed")
                             .short_('s')
                             .long_("speed")
                             .value_name("SPEED")
                             .help("How fast")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec only_heading_opts = make_only_heading_opts();

    consteval command_spec make_only_heading_pos() {
        command_builder app("test");
        std::move(app)
                .version("1.4")
                .disable_version_flag()
                .disable_help_flag()
                .arg(arg_builder("help").long_("help").action(arg_action::help).hide())
                .next_help_heading("NETWORKING")
                .arg(arg_builder("speed").index(1).help("How fast"));
        return app.freeze();
    }
    constexpr command_spec only_heading_pos = make_only_heading_pos();

    consteval command_spec make_2508() {
        command_builder app("my_app");
        std::move(app)
                .arg(arg_builder("some_arg")
                             .long_("some_arg")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(2)))
                .arg(arg_builder("some_arg_issue")
                             .long_("some_arg_issue")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(2))
                             .value_name("ARG"));
        return app.freeze();
    }
    constexpr command_spec cmd_2508 = make_2508();

    consteval command_spec make_dotted(bool named) {
        command_builder app("test");
        arg_builder one("foo");
        std::move(one)
                .index(1)
                .required()
                .action(arg_action::set)
                .num_args(value_range::at_least(1));
        if (named) std::move(one).value_name("BAR");
        std::move(app).arg(std::move(one));
        return app.freeze();
    }
    constexpr command_spec dotted_plain = make_dotted(false);
    constexpr command_spec dotted_named = make_dotted(true);

    consteval command_spec make_too_few_names() {
        command_builder app("test");
        std::move(app).arg(arg_builder("foo")
                                   .long_("foo")
                                   .required()
                                   .action(arg_action::set)
                                   .num_args(value_range::exactly(3))
                                   .value_names({"one", "two"}));
        return app.freeze();
    }
    constexpr command_spec too_few_names = make_too_few_names();

    consteval command_spec make_partial_names() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("example")
                                   .long_("example")
                                   .num_args(value_range{1, 2})
                                   .action(arg_action::set)
                                   .help("Takes one or two values")
                                   .value_names({"FOO", "BAR"}));
        return app.freeze();
    }
    constexpr command_spec partial_names = make_partial_names();

    consteval command_spec make_after_help_no_args() {
        command_builder app("myapp");
        std::move(app).version("1.0").disable_help_flag().disable_version_flag().after_help(
                "This is after help.");
        return app.freeze();
    }
    constexpr command_spec after_help_no_args = make_after_help_no_args();

    consteval command_spec make_897() {
        command_builder app("ctest");
        std::move(app).version("0.1").subcommand(command_builder("foo")
                                                         .version("0.1")
                                                         .about("About foo")
                                                         .long_about("Long about foo"));
        return app.freeze();
    }
    constexpr command_spec cmd_897 = make_897();

    consteval command_spec make_usage_order() {
        command_builder app("order");
        std::move(app)
                .arg(arg_builder("a").short_('a').action(arg_action::set_true))
                .arg(arg_builder("B").short_('B').action(arg_action::set_true))
                .arg(arg_builder("b").short_('b').action(arg_action::set_true))
                .arg(arg_builder("save").short_('s').action(arg_action::set_true))
                .arg(arg_builder("select_file").long_("select_file").action(arg_action::set_true))
                .arg(arg_builder("select_folder")
                             .long_("select_folder")
                             .action(arg_action::set_true))
                .arg(arg_builder("x").short_('x').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec usage_order = make_usage_order();

    consteval command_spec make_prefer_about() {
        command_builder app("about-in-subcommands-list");
        std::move(app).subcommand(
                command_builder("sub").long_about("long about sub").about("short about sub"));
        return app.freeze();
    }
    constexpr command_spec prefer_about = make_prefer_about();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    // ---------------------------------------------------------------------------

    // A value carrying help is what makes `--help` a different screen; names alone are not.
    static_assert(!clapp::long_help_exists(hide_pv));
    static_assert(!clapp::long_help_exists(hide_single_pv));
    static_assert(clapp::long_help_exists(pv_with_help));

    // ...and hiding the values takes that back, which is why `hidden_pv` renders compact
    // even though its domain is the same as `pv_with_help`'s.
    static_assert(!clapp::long_help_exists(hidden_pv));

    // `long_about` on the subcommand is what splits issue 897 into two screens.
    static_assert(clapp::long_help_exists(*cmd_897.find_subcommand("foo")));

    // The explicit `term_width` the three `default`-command cases rely on: without it the
    // annotation tails would wrap at clapp::default_terminal_width and the expected lines
    // below would be wrong for a reason unrelated to what they test.
    static_assert(hide_default_shown.get_term_width() == std::optional<std::size_t>{120});

}  // namespace

// ---------------------------------------------------------------------------
// Possible values — four renderings
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::hide_possible_vals") {
    CLAPP_CHECK(same(page(hide_pv, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -p, --pos <VAL>    Some vals [possible values: fast, slow]\n"
                     "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or café.\n"
                     "  -h, --help         Print help\n"
                     "  -V, --version      Print version\n"));
}

CLAPP_TEST("help.rs::hide_single_possible_val") {
    // `secret speed` is hidden and simply is not there; the list is otherwise identical
    // to the one above, which is what makes this a test of hiding rather than of listing.
    CLAPP_CHECK(same(page(hide_single_pv, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -p, --pos <VAL>    Some vals [possible values: fast, slow]\n"
                     "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or café.\n"
                     "  -h, --help         Print help\n"
                     "  -V, --version      Print version\n"));
}

CLAPP_TEST("help.rs::possible_vals_with_help") {
    // One value with help turns the inline list into a bullet list, puts every
    // description on its own line, and cross-references `-h`.
    CLAPP_CHECK(same(page(pv_with_help, true),
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -p, --pos <VAL>\n"
                     "          Some vals\n"
                     "\n"
                     "          Possible values:\n"
                     "          - fast\n"
                     "          - slow: not as fast\n"
                     "\n"
                     "  -c, --cafe <FILE>\n"
                     "          A coffeehouse, coffee shop, or café.\n"
                     "\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"
                     "\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

CLAPP_TEST("help.rs::hidden_possible_vals") {
    // Same domain as `possible_vals_with_help`, but hidden — so no list, no long screen,
    // and the row keeps its padded empty description.
    CLAPP_CHECK(same(page(hidden_pv, true),
                     "Usage: ctest [pos]\n"
                     "\n"
                     "Arguments:\n"
                     "  [pos]  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::hide_default_val") {
    // Both halves, against one expected screen: writing the default by hand and hiding
    // the automatic one is indistinguishable from letting the automatic one speak.
    const std::string_view expected =
            "Usage: default [OPTIONS]\n"
            "\n"
            "Options:\n"
            "      --arg <argument>  Pass an argument to the program. [default: default-argument]\n"
            "  -h, --help            Print help\n"
            "  -V, --version         Print version\n";
    CLAPP_CHECK(same(page(hide_default_hidden, true), expected));
    CLAPP_CHECK(same(page(hide_default_shown, true), expected));
}

CLAPP_TEST("help.rs::empty_default_value") {
    // `[default: ""]`, not `[default: ]`.
    CLAPP_CHECK(same(page(empty_default, true),
                     "Usage: default [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --arg <argument>  Pass an argument to the program. [default: \"\"]\n"
                     "  -h, --help            Print help\n"
                     "  -V, --version         Print version\n"));
}

CLAPP_TEST("help.rs::escaped_whitespace_values") {
    // Now clap's bytes exactly, trailing space and all — meaning WITHOUT one.
    //
    // Until 2026-08-10 this case was pinned clapp-side with a trailing space before the
    // newline after `"\t",`, and named itself a divergence on the theory that clapp's
    // annotations are styled where clap's are plain. That theory was wrong, and only a
    // measurement could show it: clap's `context` and `context_value` styles are EMPTY,
    // even under `Styles::styled()`, so clap's help page carries zero escape sequences
    // and its `StyledStr::wrap` sees one text run. clapp's styled_str is escape-free by
    // construction too — it was clapp's *wrapper* that treated every fragment boundary
    // as a word boundary, losing the space retraction clap performs.
    CLAPP_CHECK(same(page(escaped_ws, true),
                     "Usage: default [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --arg <argument>  Pass an argument to the program. "
                     "[default: \"\\n\"] [possible values: normal, \" \", \"\\n\", \"\\t\",\n"
                     "                        other]\n"
                     "  -h, --help            Print help\n"
                     "  -V, --version         Print version\n"));
}

// ---------------------------------------------------------------------------
// Arity and placeholders
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::issue_2508_number_of_values_with_single_value_name") {
    // One name serving a count of two repeats the name; no name at all repeats the id.
    CLAPP_CHECK(same(page(cmd_2508, true),
                     "Usage: my_app [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --some_arg <some_arg> <some_arg>  \n"
                     "      --some_arg_issue <ARG> <ARG>      \n"
                     "  -h, --help                            Print help\n"));
}

CLAPP_TEST("help.rs::positional_multiple_values_is_dotted") {
    CLAPP_CHECK(same(page(dotted_plain, true),
                     "Usage: test <foo>...\n"
                     "\n"
                     "Arguments:\n"
                     "  <foo>...  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
    CLAPP_CHECK(same(page(dotted_named, true),
                     "Usage: test <BAR>...\n"
                     "\n"
                     "Arguments:\n"
                     "  <BAR>...  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("help.rs::too_few_value_names_is_dotted") {
    // Three values, two names: the last name takes the ellipsis rather than a third
    // placeholder being invented.
    CLAPP_CHECK(same(page(too_few_names, true),
                     "Usage: test --foo <one> <two>...\n"
                     "\n"
                     "Options:\n"
                     "      --foo <one> <two>...  \n"
                     "  -h, --help                Print help\n"));
}

CLAPP_TEST("help.rs::partially_optional_value_names") {
    // `1..=2` brackets only the optional one.
    CLAPP_CHECK(same(page(partial_names, true),
                     "Usage: prog [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --example <FOO> [BAR]  Takes one or two values\n"
                     "  -h, --help                 Print help\n"));
}

CLAPP_TEST("help.rs::short_with_value") {
    CLAPP_CHECK(same(page(short_with_value, false),
                     "Usage: demo [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"
                     "\n"
                     "Baz:\n"
                     "  -z <BAZ>  Short only\n"));
}

CLAPP_TEST("help.rs::short_with_count") {
    // A counted flag is `-z...`, with no value placeholder at all.
    CLAPP_CHECK(same(page(short_with_count, false),
                     "Usage: demo [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"
                     "\n"
                     "Baz:\n"
                     "  -z...  Short only\n"));
}

CLAPP_TEST("help.rs::issue_1364_no_short_options") {
    // `-z` hides from the short screen; `-f` stays with an empty, padded description.
    CLAPP_CHECK(same(page(cmd_1364, false),
                     "Usage: demo [OPTIONS] [FILES]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [FILES]...  \n"
                     "\n"
                     "Options:\n"
                     "  -f          \n"
                     "  -h, --help  Print help (see more with '--help')\n"));
}

// ---------------------------------------------------------------------------
// Custom headings
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::custom_headers_headers") {
    // The `NETWORKING:` block is measured on its own, so its column is narrower than the
    // `Options:` block above it. `--port` has no description and no padding follows it.
    CLAPP_CHECK(same(page(custom_headers, true, "test"),
                     "does stuff\n"
                     "\n"
                     "Usage: test [OPTIONS] --fake <some> <val>\n"
                     "\n"
                     "Options:\n"
                     "  -f, --fake <some> <val>  some help\n"
                     "  -h, --help               Print help\n"
                     "  -V, --version            Print version\n"
                     "\n"
                     "NETWORKING:\n"
                     "  -n, --no-proxy  Do not use system proxy settings\n"
                     "      --port\n"));
}

CLAPP_TEST("help.rs::custom_heading_pos") {
    // A POSITIONAL under a custom heading leaves `Arguments:` entirely.
    CLAPP_CHECK(same(page(custom_heading_pos, true),
                     "Usage: test [gear] [speed]\n"
                     "\n"
                     "Arguments:\n"
                     "  [gear]  Which gear\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"
                     "\n"
                     "NETWORKING:\n"
                     "  [speed]  How fast\n"));
}

CLAPP_TEST("help.rs::only_custom_heading_opts_no_args") {
    // Every default-section member is hidden, so `Options:` must not be emitted empty.
    CLAPP_CHECK(same(page(only_heading_opts, true),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "NETWORKING:\n"
                     "  -s, --speed <SPEED>  How fast\n"));
}

CLAPP_TEST("help.rs::only_custom_heading_pos_no_args") {
    CLAPP_CHECK(same(page(only_heading_pos, true),
                     "Usage: test [speed]\n"
                     "\n"
                     "NETWORKING:\n"
                     "  [speed]  How fast\n"));
}

// ---------------------------------------------------------------------------
// about / long_about, ordering, and the no-args template
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::show_short_about_issue_897") {
    CLAPP_CHECK(same(page(*cmd_897.find_subcommand("foo"), false, "ctest foo"),
                     "About foo\n"
                     "\n"
                     "Usage: ctest foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help (see more with '--help')\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help.rs::show_long_about_issue_897") {
    // Same subcommand: `long_about`, next-line layout, and the reversed cross-reference.
    CLAPP_CHECK(same(page(*cmd_897.find_subcommand("foo"), true, "ctest foo"),
                     "Long about foo\n"
                     "\n"
                     "Usage: ctest foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"
                     "\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

CLAPP_TEST("help.rs::prefer_about_over_long_about_in_subcommands_list") {
    // The table shows `about` even though `long_about` exists — the detail is deferred
    // to `cmd sub --help`.
    CLAPP_CHECK(same(page(prefer_about, true),
                     "Usage: about-in-subcommands-list [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  sub   short about sub\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("help.rs::option_usage_order") {
    // Declaration order, and `-B` before `-b`: the sort is ASCII, not case-insensitive.
    CLAPP_CHECK(same(page(usage_order, true),
                     "Usage: order [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -a                   \n"
                     "  -B                   \n"
                     "  -b                   \n"
                     "  -s                   \n"
                     "      --select_file    \n"
                     "      --select_folder  \n"
                     "  -x                   \n"
                     "  -h, --help           Print help\n"));
}

CLAPP_TEST("help.rs::after_help_no_args") {
    // clap's DEFAULT_NO_ARGS_TEMPLATE: no `Options:` block, and no blank line where one
    // would have been.
    CLAPP_CHECK(same(page(after_help_no_args, true),
                     "Usage: myapp\n"
                     "\n"
                     "This is after help.\n"));
}
