#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/output/help.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/util/id.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::command_builder;
    using clapp::command_setting;
    using clapp::command_spec;
    using clapp::help_style;
    using clapp::long_help_exists;
    using clapp::render_help;
    using clapp::render_version;
    using clapp::style_class;
    using clapp::styled_str;
    using clapp::styles;
    using clapp::value_range;

    // -----------------------------------------------------------------------
    // Helpers
    //
    // Every fixture is built and frozen inside the consteval function that asserts on
    // it, as in tests/units/output/usage_test.cpp: a frozen command_spec at namespace
    // scope would cost .rodata in a binary that only ever reads it at compile time.
    // -----------------------------------------------------------------------

    /**
     * The page as clapp::render_help() renders it — clap's `Command::render_help()` /
     * `render_long_help()`, which honour `use_long` literally.
     */
    [[nodiscard]] consteval std::string
    page_of(const command_spec& spec, bool use_long = false, std::string_view usage_name = {}) {
        return render_help(spec, help_style{.use_long = use_long, .usage_name = usage_name})
                .to_string();
    }

    /**
     * The page a `-h` or `--help` on the command line produces — clap's
     * `Command::write_help_err`, which first collapses `use_long` against
     * clapp::long_help_exists(). This is what clap's own tests compare against, because
     * they drive the parser rather than the renderer.
     */
    [[nodiscard]] consteval std::string
    help_flag_page(const command_spec& spec, bool use_long, std::string_view usage_name = {}) {
        return page_of(spec, use_long && long_help_exists(spec), usage_name);
    }

    /**
     * A `constexpr` clapp::env_lookup with one variable set.
     *
     * The reason clap's help_env.rs can be a compile-time test here at all: clap's
     * versions call `env::set_var("ENVVAR", "MYVAL")` and are marked `unsafe`, because
     * clap reads the environment in `Command::build`. clapp reads it at *render* time
     * through this seam, so the test never touches the process.
     */
    struct fake_env {
        [[nodiscard]] constexpr std::optional<std::string_view>
        operator()(std::string_view name) const {
            if (name == "ENVVAR") return std::string_view{"MYVAL"};
            return std::nullopt;
        }
    };

    static_assert(clapp::env_lookup<fake_env>);

    /**
     * The negative half of the concept check. A concept satisfied by everything passes
     * the positive half, which is the failure mode this rules out.
     */
    struct not_a_lookup {
        [[nodiscard]] constexpr int operator()(std::string_view) const { return 0; }
    };

    static_assert(!clapp::env_lookup<not_a_lookup>);
    static_assert(!clapp::env_lookup<int>);

    [[nodiscard]] consteval std::string env_page_of(const command_spec& spec) {
        return render_help(spec, help_style{}, fake_env{}).to_string();
    }

    // -----------------------------------------------------------------------
    // Enumerations used as value parsers
    //
    // Two, because the long-help `Possible values:` block is only reached when at least
    // one value carries help of its own (clap's `use_long_pv`), and the short-help
    // `[possible values: a, b]` tail is only reached when none does.
    // -----------------------------------------------------------------------

    /**
     * clap's `value_parser(["fast", "slow"])`: names only, so short help lists them
     * inline and long help still does.
     */
    enum class speed { fast, slow };

    /**
     * clap's `help_enum_arg_with_no_description` list: one bare value, one with help,
     * one hidden and containing a space — which is also the escaping case, since
     * `Escape` quotes anything with whitespace in it.
     */
    enum class mode {
        fast,
        slow[[= clapp::value{.help = "slower than fast"}]],
        secret_speed[[= clapp::value{.name = "secret speed", .hide = true}]],
    };

    /** A deliberately narrow possible-value row for palette-aware wrapping. */
    enum class tight_mode {
        abc[[= clapp::value{.help = "help words"}]],
    };

    // =======================================================================
    // 1. The two-column layout — clap tests/builder/help.rs
    // =======================================================================

    /**
     * clap `args_with_last_usage`, verbatim.
     *
     * The load-bearing case for the alignment arithmetic: five named arguments of four
     * different shapes (short+long+value, short+long only, positional, `last()`
     * positional) all have to reach the same description column, and that column is
     * `longest + TAB_WIDTH * 2` where `longest` charges an argument with a long option
     * four extra cells for the `-s, ` it may not have.
     */
    consteval bool args_with_last_usage() {
        command_builder cmd("flamegraph");
        std::move(cmd)
                .version("0.1")
                .arg(arg_builder("verbose")
                             .help("Prints out more stuff.")
                             .short_('v')
                             .long_("verbose")
                             .action(arg_action::set_true))
                .arg(arg_builder("timeout")
                             .help("Timeout in seconds.")
                             .short_('t')
                             .long_("timeout")
                             .value_name("SECONDS"))
                .arg(arg_builder("frequency")
                             .help("The sampling frequency.")
                             .short_('f')
                             .long_("frequency")
                             .value_name("HERTZ"))
                .arg(arg_builder("binary path")
                             .help("The path of the binary to be profiled. for a binary.")
                             .value_name("BINFILE"))
                .arg(arg_builder("pass through args")
                             .help("Any arguments you wish to pass to the being profiled.")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .last()
                             .value_name("ARGS"))
                .setting(command_setting::disable_help_subcommand);
        return help_flag_page(cmd.freeze(), true) ==
               "Usage: flamegraph [OPTIONS] [BINFILE] [-- <ARGS>...]\n"
               "\n"
               "Arguments:\n"
               "  [BINFILE]  The path of the binary to be profiled. for a binary.\n"
               "  [ARGS]...  Any arguments you wish to pass to the being profiled.\n"
               "\n"
               "Options:\n"
               "  -v, --verbose            Prints out more stuff.\n"
               "  -t, --timeout <SECONDS>  Timeout in seconds.\n"
               "  -f, --frequency <HERTZ>  The sampling frequency.\n"
               "  -h, --help               Print help\n"
               "  -V, --version            Print version\n";
    }

    static_assert(args_with_last_usage(),
                  "clapp: the help column is longest + TAB_WIDTH * 2, and an argument "
                  "with a long option is charged SHORT_SIZE for the '-s, ' it may not "
                  "have. If this fails, align_to_about() and write_args() disagree.");

    /**
     * clap `req_last_arg_usage`, verbatim. Positionals sort by declared index, not by
     * the alphabetical key the named arguments use.
     */
    consteval bool req_last_arg_usage() {
        command_builder cmd("example");
        std::move(cmd)
                .version("1.0")
                .arg(arg_builder("FIRST")
                             .help("First")
                             .num_args(value_range::at_least(1))
                             .required())
                .arg(arg_builder("SECOND")
                             .help("Second")
                             .num_args(value_range::at_least(1))
                             .required()
                             .last())
                .setting(command_setting::disable_help_subcommand);
        return help_flag_page(cmd.freeze(), true) == "Usage: example <FIRST>... -- <SECOND>...\n"
                                                     "\n"
                                                     "Arguments:\n"
                                                     "  <FIRST>...   First\n"
                                                     "  <SECOND>...  Second\n"
                                                     "\n"
                                                     "Options:\n"
                                                     "  -h, --help     Print help\n"
                                                     "  -V, --version  Print version\n";
    }

    static_assert(req_last_arg_usage());

    /**
     * Declaration order, not alphabetical order.
     *
     * clapp::command_builder::arg() numbers each named argument from a cursor that
     * starts at 0 — clap's `current_disp_ord: Some(0)` — while the injected `--help`
     * and `--version` are pushed past it and keep the 999 default. The tie-break inside
     * one display order is clap's `option_sort_key`, which is why the two injected
     * flags come out as `-h` then `-V` rather than in the order they were injected.
     */
    consteval bool declaration_order_beats_the_alphabet() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("zebra").long_("zebra").action(arg_action::set_true).help("z"))
                .arg(arg_builder("apple").long_("apple").action(arg_action::set_true).help("a"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "Usage: test [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "      --zebra  z\n"
                                        "      --apple  a\n"
                                        "  -h, --help   Print help\n";
    }

    static_assert(declaration_order_beats_the_alphabet(),
                  "clapp: arguments list in declaration order. If this fails, "
                  "command_builder's display-order cursor stopped numbering and every "
                  "help screen silently re-sorted itself by option letter.");

    /**
     * clap's `option_sort_key` within one display order, which is a *string* compare
     * and not a "shorts first" rule.
     *
     * A short option keys as its lowercase letter plus `'0'` when it was lowercase and
     * `'1'` when it was uppercase, so `-c` sorts immediately before `-C`. A long-only
     * option keys as its spelling, so `--aaa` lands before both — clap's own comment
     * ("Example order: -a, -b, -B, -s, --select-file, --select-folder, -x") reads as
     * though shorts came first only because every long in that example starts with a
     * letter that puts it there anyway. Reachable here because all three arguments
     * share display order 4.
     */
    consteval bool equal_display_orders_use_claps_key() {
        command_builder cmd("test");
        std::move(cmd)
                .next_display_order(4)
                .arg(arg_builder("upper").short_('C').action(arg_action::set_true).help("C"))
                .next_display_order(4)
                .arg(arg_builder("lower").short_('c').action(arg_action::set_true).help("c"))
                .next_display_order(4)
                .arg(arg_builder("named").long_("aaa").action(arg_action::set_true).help("a"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "Usage: test [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "      --aaa   a\n"
                                        "  -c          c\n"
                                        "  -C          C\n"
                                        "  -h, --help  Print help\n";
    }

    static_assert(equal_display_orders_use_claps_key());

    /**
     * An argument with no help still pads out to the column, so the next row lines up.
     * clap's `flatten_basic` pins the same trailing spaces.
     */
    consteval bool an_empty_help_still_pads_its_column() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("quiet").long_("quiet").action(arg_action::set_true))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) ==
               "Usage: test [OPTIONS]\n"
               "\n"
               "Options:\n"
               "      --quiet  \n"  // trailing spaces are clap's, not an accident
               "  -h, --help   Print help\n";
    }

    static_assert(an_empty_help_still_pads_its_column());

    // =======================================================================
    // 2. before_help / after_help / about — clap tests/builder/help.rs
    // =======================================================================

    consteval command_builder clap_test_before_after(bool with_long) {
        command_builder cmd("clap-test");
        std::move(cmd)
                .version("v1.4.8")
                .about("tests clap library")
                .before_help("some text that comes before the help")
                .after_help("some text that comes after the help")
                .setting(command_setting::disable_help_subcommand);
        if (with_long) {
            std::move(cmd)
                    .before_long_help("some longer text that comes before the help")
                    .after_long_help("some longer text that comes after the help");
        }
        return cmd;
    }

    /**
     * clap `after_and_before_help_output`, verbatim — and the same page for `-h` and
     * `--help`, because with no long form anywhere `--help` collapses onto `-h`.
     */
    consteval bool after_and_before_help_output() {
        const command_spec spec    = clap_test_before_after(false).freeze();
        const std::string expected = "some text that comes before the help\n"
                                     "\n"
                                     "tests clap library\n"
                                     "\n"
                                     "Usage: clap-test\n"
                                     "\n"
                                     "Options:\n"
                                     "  -h, --help     Print help\n"
                                     "  -V, --version  Print version\n"
                                     "\n"
                                     "some text that comes after the help\n";
        return help_flag_page(spec, false) == expected && help_flag_page(spec, true) == expected;
    }

    static_assert(after_and_before_help_output());

    /**
     * clap `after_and_before_long_help_output`, verbatim. Now the two screens differ in
     * four ways at once: the long/short prose swaps, every description moves to its own
     * line, a blank line separates the rows, and `-h`'s own help text gains the "(see
     * more with '--help')" hint.
     */
    consteval bool after_and_before_long_help_output() {
        const command_spec spec = clap_test_before_after(true).freeze();
        return help_flag_page(spec, true) == "some longer text that comes before the help\n"
                                             "\n"
                                             "tests clap library\n"
                                             "\n"
                                             "Usage: clap-test\n"
                                             "\n"
                                             "Options:\n"
                                             "  -h, --help\n"
                                             "          Print help (see a summary with '-h')\n"
                                             "\n"
                                             "  -V, --version\n"
                                             "          Print version\n"
                                             "\n"
                                             "some longer text that comes after the help\n" &&
               help_flag_page(spec, false) ==
                       "some text that comes before the help\n"
                       "\n"
                       "tests clap library\n"
                       "\n"
                       "Usage: clap-test\n"
                       "\n"
                       "Options:\n"
                       "  -h, --help     Print help (see more with '--help')\n"
                       "  -V, --version  Print version\n"
                       "\n"
                       "some text that comes after the help\n";
    }

    static_assert(after_and_before_long_help_output());

    /**
     * Short help reads `about` and does **not** fall back to `long_about`; long help
     * does fall back to `about`. The asymmetry is clap's `write_about`, and it exists
     * because a `long_about` is allowed to be a page.
     */
    consteval bool short_help_never_falls_back_to_long_about() {
        command_builder cmd("test");
        std::move(cmd)
                .long_about("the long story")
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        const command_spec spec = cmd.freeze();
        return page_of(spec, false).starts_with("Usage: test") &&
               page_of(spec, true).starts_with("the long story\n\nUsage: test");
    }

    static_assert(short_help_never_falls_back_to_long_about());

    /**
     * An absent `about` must not leave the blank line `{about-with-newline}` puts under
     * it; that is what clap's `StyledStr::trim_start_lines` is for, and it removes
     * exactly one blank line, exactly once.
     */
    consteval bool an_absent_about_leaves_no_blank_first_line() {
        command_builder cmd("test");
        std::move(cmd)
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "Usage: test\n"
                                        "\n"
                                        "Options:\n"
                                        "  -h, --help  Print help\n";
    }

    static_assert(an_absent_about_leaves_no_blank_first_line());

    /** `{n}` is clap's newline escape, honoured in `about` and in an argument's help. */
    consteval bool the_newline_var_is_expanded() {
        command_builder cmd("test");
        std::move(cmd)
                .about("first{n}second")
                .arg(arg_builder("opt").long_("opt").action(arg_action::set_true).help("one{n}two"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "first\n"
                                        "second\n"
                                        "\n"
                                        "Usage: test [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "      --opt   one\n"
                                        "              two\n"
                                        "  -h, --help  Print help\n";
    }

    static_assert(the_newline_var_is_expanded());

    // =======================================================================
    // 3. Sections and headings — clap tests/builder/help.rs
    // =======================================================================

    /**
     * clap `multiple_custom_help_headers`, verbatim.
     *
     * Four things at once, and each of them is a separate rule:
     *
     *   - custom headings print **after** `Options:`, in the order they were first
     *     declared (NETWORKING, OVERRIDE SPECIAL, SPECIAL), not sorted;
     *   - `next_help_heading()` covers the arguments added after it and nothing else;
     *   - an argument's own `help_heading()` overrides it;
     *   - `help_heading("")` is clap's `help_heading(None)` and sends the argument back
     *     to the default `Options:` section — which is exactly the case
     *     clapp::detail::effective_help_heading() exists for, because clapp's frozen
     *     `arg_spec` reports it as `optional{""}` rather than as `nullopt`.
     */
    consteval bool multiple_custom_help_headers() {
        command_builder cmd("test");
        std::move(cmd)
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
                .next_help_heading("SPECIAL")
                .arg(arg_builder("birthday-song")
                             .short_('b')
                             .long_("birthday-song")
                             .value_name("song")
                             .action(arg_action::set)
                             .help("Change which song is played for birthdays")
                             .required()
                             .help_heading("OVERRIDE SPECIAL"))
                .arg(arg_builder("style")
                             .long_("style")
                             .value_name("style")
                             .action(arg_action::set)
                             .help("Choose musical style to play the song")
                             .help_heading(""))
                .arg(arg_builder("birthday-song-volume")
                             .short_('v')
                             .long_("birthday-song-volume")
                             .value_name("volume")
                             .action(arg_action::set)
                             .help("Change the volume of the birthday song")
                             .required())
                .next_help_heading("")
                .arg(arg_builder("server-addr")
                             .short_('a')
                             .long_("server-addr")
                             .action(arg_action::set_true)
                             .help("Set server address")
                             .help_heading("NETWORKING"))
                .arg(arg_builder("speed")
                             .long_("speed")
                             .short_('s')
                             .value_name("SPEED")
                             .value_parser<speed>()
                             .help("How fast?")
                             .action(arg_action::set))
                .setting(command_setting::disable_help_subcommand);
        return help_flag_page(cmd.freeze(), true) ==
               "does stuff\n"
               "\n"
               "Usage: test [OPTIONS] --fake <some> <val> --birthday-song <song> "
               "--birthday-song-volume <volume>\n"
               "\n"
               "Options:\n"
               "  -f, --fake <some> <val>  some help\n"
               "      --style <style>      Choose musical style to play the song\n"
               "  -s, --speed <SPEED>      How fast? [possible values: fast, slow]\n"
               "  -h, --help               Print help\n"
               "  -V, --version            Print version\n"
               "\n"
               "NETWORKING:\n"
               "  -n, --no-proxy     Do not use system proxy settings\n"
               "  -a, --server-addr  Set server address\n"
               "\n"
               "OVERRIDE SPECIAL:\n"
               "  -b, --birthday-song <song>  Change which song is played for birthdays\n"
               "\n"
               "SPECIAL:\n"
               "  -v, --birthday-song-volume <volume>  Change the volume of the birthday "
               "song\n";
    }

    static_assert(multiple_custom_help_headers(),
                  "clapp: custom help headings print after Options: in first-declared "
                  "order, and help_heading(\"\") means the default section. If only the "
                  "last clause fails, effective_help_heading() stopped folding clapp's "
                  "present-but-empty heading onto clap's None.");

    /** Each section's column is measured within that section, not across the page. */
    consteval bool each_section_measures_its_own_column() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("a-very-long-option-name")
                             .long_("a-very-long-option-name")
                             .action(arg_action::set_true)
                             .help("wide"))
                .next_help_heading("OTHER")
                .arg(arg_builder("x").short_('x').action(arg_action::set_true).help("narrow"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "Usage: test [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "      --a-very-long-option-name  wide\n"
                                        "  -h, --help                     Print help\n"
                                        "\n"
                                        "OTHER:\n"
                                        "  -x  narrow\n";
    }

    static_assert(each_section_measures_its_own_column());

    // =======================================================================
    // 4. hide / hide_short_help / hide_long_help
    // =======================================================================

    /**
     * clap's `should_show_arg`: `hide()` wins outright, while the other two are
     * per-screen and are what make `-h` and `--help` list different arguments.
     */
    consteval bool the_two_screens_list_different_arguments() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("visible")
                             .long_("visible")
                             .action(arg_action::set_true)
                             .help("seen"))
                .arg(arg_builder("gone")
                             .long_("gone")
                             .action(arg_action::set_true)
                             .help("never")
                             .hide())
                .arg(arg_builder("shortonly")
                             .long_("short-only")
                             .action(arg_action::set_true)
                             .help("only in long help")
                             .hide_short_help())
                .arg(arg_builder("longonly")
                             .long_("long-only")
                             .action(arg_action::set_true)
                             .help("only in short help")
                             .hide_long_help())
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        const command_spec spec = cmd.freeze();
        return page_of(spec, false) == "Usage: test [OPTIONS]\n"
                                       "\n"
                                       "Options:\n"
                                       "      --visible    seen\n"
                                       "      --long-only  only in short help\n"
                                       "  -h, --help       Print help (see more with '--help')\n" &&
               page_of(spec, true) == "Usage: test [OPTIONS]\n"
                                      "\n"
                                      "Options:\n"
                                      "      --visible\n"
                                      "          seen\n"
                                      "\n"
                                      "      --short-only\n"
                                      "          only in long help\n"
                                      "\n"
                                      "  -h, --help\n"
                                      "          Print help (see a summary with '-h')\n";
    }

    static_assert(the_two_screens_list_different_arguments());

    // =======================================================================
    // 5. possible values, defaults, aliases — the bracketed tail
    // =======================================================================

    consteval bool help_wrapping_uses_the_commands_effective_palette() {
        const auto build = [](styles palette) {
            command_builder cmd("probe");
            std::move(cmd)
                    .term_width(15)
                    .styles(palette)
                    .arg(arg_builder("mode")
                                 .long_("mode")
                                 .action(arg_action::set)
                                 .value_parser<tight_mode>())
                    .setting(command_setting::disable_help_subcommand)
                    .setting(command_setting::disable_version_flag);
            return cmd.freeze();
        };

        const command_spec styled = build(styles::styled());
        const command_spec plain  = build(styles::plain());
        return page_of(styled, true) == "Usage: probe [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "      --mode <mode>\n"
                                        "          Possible values:\n"
                                        "          - abc\n"
                                        "            :\n"
                                        "            help\n"
                                        "            words\n"
                                        "\n"
                                        "  -h, --help\n"
                                        "          Print\n"
                                        "          help\n"
                                        "          (see\n"
                                        "          a\n"
                                        "          summary\n"
                                        "          with\n"
                                        "          '-h')\n" &&
               page_of(plain, true) == "Usage: probe [OPTIONS]\n"
                                       "\n"
                                       "Options:\n"
                                       "      --mode <mode>\n"
                                       "          Possible values:\n"
                                       "          - abc:\n"
                                       "            help\n"
                                       "            words\n"
                                       "\n"
                                       "  -h, --help\n"
                                       "          Print\n"
                                       "          help\n"
                                       "          (see\n"
                                       "          a\n"
                                       "          summary\n"
                                       "          with\n"
                                       "          '-h')\n";
    }

    static_assert(help_wrapping_uses_the_commands_effective_palette());

    /**
     * clap `help_enum_arg_with_no_description`, verbatim.
     *
     * The long-form `Possible values:` block, which only appears when at least one
     * value carries help of its own. Three details are pinned here and all three are
     * clap's: a hidden value is dropped, a value with no help gets no `: ` at all, and
     * the `[default: …]` tail is separated from the block by a line that contains
     * nothing but the column indent.
     */
    consteval bool help_enum_arg_with_no_description() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("config")
                             .action(arg_action::set)
                             .short_('c')
                             .long_("config")
                             .value_name("MODE")
                             .value_parser<mode>()
                             .default_value("fast"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return help_flag_page(cmd.freeze(), true) ==
               "Usage: test [OPTIONS]\n"
               "\n"
               "Options:\n"
               "  -c, --config <MODE>\n"
               "          Possible values:\n"
               "          - fast\n"
               "          - slow: slower than fast\n"
               "          \n"  // clap's own trailing indent; see the case name
               "          [default: fast]\n"
               "\n"
               "  -h, --help\n"
               "          Print help (see a summary with '-h')\n";
    }

    static_assert(help_enum_arg_with_no_description(),
                  "clapp: the long-form Possible values: block is clap's, indented "
                  "column and blank-but-indented separator line included.");

    /** The same argument on `-h`: one line, values inline, defaults first. */
    consteval bool short_help_lists_values_inline() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("config")
                             .action(arg_action::set)
                             .short_('c')
                             .long_("config")
                             .value_name("MODE")
                             .value_parser<mode>()
                             .default_value("fast"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze(), false) ==
               "Usage: test [OPTIONS]\n"
               "\n"
               "Options:\n"
               "  -c, --config <MODE>  [default: fast] [possible values: fast, slow]\n"
               "  -h, --help           Print help (see more with '--help')\n";
    }

    static_assert(short_help_lists_values_inline());

    /** The three `hide_*` switches, each suppressing its own annotation and nothing else. */
    consteval bool each_hide_switch_suppresses_only_its_own_tail() {
        const auto build = [](bool hide_pv, bool hide_default) {
            command_builder cmd("test");
            arg_builder one("config");
            std::move(one)
                    .action(arg_action::set)
                    .long_("config")
                    .value_name("MODE")
                    .value_parser<speed>()
                    .default_value("fast");
            if (hide_pv) std::move(one).hide_possible_values();
            if (hide_default) std::move(one).hide_default_value();
            std::move(cmd)
                    .arg(std::move(one))
                    .setting(command_setting::disable_help_subcommand)
                    .setting(command_setting::disable_version_flag);
            return cmd.freeze();
        };
        return page_of(build(false, false))
                       .contains("[default: fast] [possible values: fast, slow]") &&
               page_of(build(true, false)).contains("--config <MODE>  [default: fast]\n") &&
               !page_of(build(true, false)).contains("[possible values:") &&
               page_of(build(false, true)).contains("[possible values: fast, slow]") &&
               !page_of(build(false, true)).contains("[default:")
               // Not `!contains("[")`: the usage line's own `[OPTIONS]` would satisfy that
               // for the wrong reason, and would keep satisfying it if both tails came back.
               && !page_of(build(true, true)).contains("[default:") &&
               !page_of(build(true, true)).contains("[possible values:");
    }

    static_assert(each_hide_switch_suppresses_only_its_own_tail());

    /**
     * A default value containing whitespace is quoted, because `[default: a b]` reads
     * as two values. clap's `Escape`.
     */
    consteval bool a_spaced_default_is_quoted() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("out")
                             .long_("out")
                             .action(arg_action::set)
                             .default_value("two words"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()).contains("[default: \"two words\"]");
    }

    static_assert(a_spaced_default_is_quoted());

    /**
     * Visible aliases, singular and plural, on an argument and on a subcommand. clap's
     * `pluralize` and its ordering: short flags, then long flags, then name aliases.
     */
    consteval bool aliases_are_listed_and_pluralized() {
        command_builder sub("sub");
        std::move(sub).about("a sub").visible_alias("alt").visible_short_flag_alias('S');

        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .action(arg_action::set_true)
                             .visible_alias("option")
                             .visible_short_alias('O')
                             .help("an option"))
                .arg(arg_builder("one")
                             .long_("one")
                             .action(arg_action::set_true)
                             .visible_alias("uno")
                             .help("just one"))
                .subcommand(std::move(sub))
                .setting(command_setting::disable_version_flag);
        const std::string page = page_of(cmd.freeze());
        return page.contains("an option [aliases: -O, --option]") &&
               page.contains("just one [alias: --uno]") &&
               page.contains("a sub [aliases: -S, alt]");
    }

    static_assert(aliases_are_listed_and_pluralized());

    /** A hidden alias stays hidden: it is hidden from *help*, not from the parser. */
    consteval bool hidden_aliases_are_not_listed() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("opt")
                             .long_("opt")
                             .action(arg_action::set_true)
                             .alias("secret")
                             .help("an option"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return !page_of(cmd.freeze()).contains("alias");
    }

    static_assert(hidden_aliases_are_not_listed());

    // =======================================================================
    // 6. The environment seam — clap tests/builder/help_env.rs
    // =======================================================================

    consteval command_spec ctest_env(bool hide_env, bool hide_env_values, bool flag) {
        arg_builder cafe("cafe");
        std::move(cafe).short_('c').long_("cafe").env("ENVVAR").help(
                "A coffeehouse, coffee shop, or café.");
        if (flag) {
            std::move(cafe).action(arg_action::set_true);
        } else {
            std::move(cafe).value_name("FILE").action(arg_action::set);
        }
        if (hide_env) std::move(cafe).hide_env();
        if (hide_env_values) std::move(cafe).hide_env_values();

        command_builder cmd("ctest");
        std::move(cmd)
                .version("0.1")
                .arg(std::move(cafe))
                .setting(command_setting::disable_help_subcommand);
        return cmd.freeze();
    }

    /**
     * clap's `SHOW_ENV`, `HIDE_ENV`, `HIDE_ENV_VALS` and `SHOW_ENV_FLAG`, verbatim.
     *
     * `hide_env()` drops the whole annotation; `hide_env_values()` keeps the variable's
     * name and drops only its contents. Two switches, two different jobs — collapsing
     * them is the mistake this case exists to catch.
     */
    consteval bool env_annotations_match_clap() {
        return env_page_of(ctest_env(false, false, false)) ==
                       "Usage: ctest [OPTIONS]\n"
                       "\n"
                       "Options:\n"
                       "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or café. "
                       "[env: ENVVAR=MYVAL]\n"
                       "  -h, --help         Print help\n"
                       "  -V, --version      Print version\n" &&
               env_page_of(ctest_env(true, false, false)) ==
                       "Usage: ctest [OPTIONS]\n"
                       "\n"
                       "Options:\n"
                       "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or café.\n"
                       "  -h, --help         Print help\n"
                       "  -V, --version      Print version\n" &&
               env_page_of(ctest_env(false, true, false)) ==
                       "Usage: ctest [OPTIONS]\n"
                       "\n"
                       "Options:\n"
                       "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or café. "
                       "[env: ENVVAR]\n"
                       "  -h, --help         Print help\n"
                       "  -V, --version      Print version\n" &&
               env_page_of(ctest_env(false, false, true)) ==
                       "Usage: ctest [OPTIONS]\n"
                       "\n"
                       "Options:\n"
                       "  -c, --cafe     A coffeehouse, coffee shop, or café. "
                       "[env: ENVVAR=MYVAL]\n"
                       "  -h, --help     Print help\n"
                       "  -V, --version  Print version\n";
    }

    static_assert(env_annotations_match_clap());

    /**
     * With no lookup the variable reads as unset, which is `[env: ENVVAR=]` — exactly
     * what clap prints for a process that does not have it set. The default is not a
     * stub.
     */
    consteval bool an_unset_variable_renders_as_clap_does() {
        return page_of(ctest_env(false, false, false)).contains("[env: ENVVAR=]");
    }

    static_assert(an_unset_variable_renders_as_clap_does());

    // =======================================================================
    // 7. The template engine — clap tests/builder/template_help.rs
    // =======================================================================

    consteval command_builder my_app() {
        command_builder test("test");
        std::move(test)
                .about("does testing things")
                .arg(arg_builder("list")
                             .short_('l')
                             .long_("list")
                             .help("lists test values")
                             .action(arg_action::set_true));
        command_builder cmd("MyApp");
        std::move(cmd)
                .version("1.0")
                .author("Kevin K. <kbknapp@gmail.com>")
                .about("Does awesome things")
                .arg(arg_builder("config")
                             .short_('c')
                             .long_("config")
                             .value_name("FILE")
                             .help("Sets a custom config file")
                             .action(arg_action::set))
                .arg(arg_builder("output").required().help("Sets an optional output file"))
                .arg(arg_builder("d")
                             .short_('d')
                             .action(arg_action::count)
                             .help("Turn debugging information on"))
                .subcommand(std::move(test));
        return cmd;
    }

    consteval command_spec templated(std::string_view tmpl) {
        command_builder cmd = my_app();
        std::move(cmd).help_template(tmpl);
        return cmd.freeze();
    }

    /** clap `with_template` / `SIMPLE_TEMPLATE`, verbatim. */
    consteval bool with_template() {
        return help_flag_page(templated("{bin} {version}\n{author}\n{about}\n\n"
                                        "Usage: {usage}\n\n{all-args}"),
                              true) ==
               "MyApp 1.0\n"
               "Kevin K. <kbknapp@gmail.com>\n"
               "Does awesome things\n"
               "\n"
               "Usage: MyApp [OPTIONS] <output> [COMMAND]\n"
               "\n"
               "Commands:\n"
               "  test  does testing things\n"
               "  help  Print this message or the help of the given subcommand(s)\n"
               "\n"
               "Arguments:\n"
               "  <output>  Sets an optional output file\n"
               "\n"
               "Options:\n"
               "  -c, --config <FILE>  Sets a custom config file\n"
               "  -d...                Turn debugging information on\n"
               "  -h, --help           Print help\n"
               "  -V, --version        Print version\n";
    }

    static_assert(with_template());

    /**
     * clap `custom_template` / `CUSTOM_TEMPL_HELP`, verbatim. `{options}`,
     * `{positionals}` and `{subcommands}` write only their own rows, with the caller
     * supplying the headings — which is why the three sections here run together with
     * no blank line, unlike `{all-args}` above.
     */
    consteval bool custom_template() {
        return help_flag_page(templated("{bin} {version}\n{author}\n{about}\n\n"
                                        "Usage: {usage}\n\nOptions:\n{options}\n"
                                        "Arguments:\n{positionals}\nCommands:\n{subcommands}"),
                              true) ==
               "MyApp 1.0\n"
               "Kevin K. <kbknapp@gmail.com>\n"
               "Does awesome things\n"
               "\n"
               "Usage: MyApp [OPTIONS] <output> [COMMAND]\n"
               "\n"
               "Options:\n"
               "  -c, --config <FILE>  Sets a custom config file\n"
               "  -d...                Turn debugging information on\n"
               "  -h, --help           Print help\n"
               "  -V, --version        Print version\n"
               "Arguments:\n"
               "  <output>  Sets an optional output file\n"
               "Commands:\n"
               "  test  does testing things\n"
               "  help  Print this message or the help of the given subcommand(s)\n";
    }

    static_assert(custom_template());

    consteval command_spec plain_template(std::string_view tmpl) {
        command_builder cmd("MyApp");
        std::move(cmd)
                .version("1.0")
                .author("Kevin K. <kbknapp@gmail.com>")
                .about("Does awesome things")
                .help_template(tmpl);
        return cmd.freeze();
    }

    /**
     * clap `template_notag`, `template_unknowntag` and `template_author_version`,
     * verbatim. An unrecognised tag round-trips **with** its braces, which is how an
     * author sees the typo.
     */
    consteval bool template_literals_and_unknown_tags() {
        return page_of(plain_template("test no tag test")) == "test no tag test\n" &&
               page_of(plain_template("test {unknown_tag} test")) == "test {unknown_tag} test\n" &&
               page_of(plain_template("{author}\n{version}\n{about}\n{bin}")) ==
                       "Kevin K. <kbknapp@gmail.com>\n1.0\nDoes awesome things\nMyApp\n";
    }

    static_assert(template_literals_and_unknown_tags());

    /** The `-section` and `-with-newline` variants, and `{tab}`. */
    consteval bool the_spacing_variants_differ() {
        return page_of(plain_template("[{about}]")) == "[Does awesome things]\n" &&
               page_of(plain_template("[{about-with-newline}]")) == "[Does awesome things\n]\n" &&
               page_of(plain_template("[{about-section}]")) == "[\nDoes awesome things\n]\n" &&
               page_of(plain_template("[{author-section}]")) ==
                       "[\nKevin K. <kbknapp@gmail.com>\n]\n" &&
               page_of(plain_template("[{tab}]")) == "[  ]\n" &&
               page_of(plain_template("{usage-heading} x")) == "Usage: x\n";
    }

    static_assert(the_spacing_variants_differ());

    /**
     * A `{` with no `}` swallows the rest of that segment, silently. Rust's
     * `split('{')` plus `split_once('}')` does this, and reproducing it is the point:
     * a template engine that guessed differently would render a different page for the
     * same input.
     */
    consteval bool an_unclosed_brace_swallows_its_segment() {
        return page_of(plain_template("head {about} tail {oops")) == "head Does awesome "
                                                                     "things tail\n";
    }

    static_assert(an_unclosed_brace_swallows_its_segment());

    /**
     * clapp cannot express clap's `help_template("")`.
     *
     * clapp::command_spec stores prose as a pointer plus a **length sentinel** — zero
     * means "never set" — because GCC 16.1.0 under `-fsanitize=null` will not fold a
     * pointer comparison inside a constant expression (see the length-sentinel note on
     * clapp::arg_id). An empty template is therefore indistinguishable from no
     * template, and the automatic layout runs instead of clap's bare `"\n"`. This assertion
     * keeps the deliberate divergence visible.
     */
    consteval bool an_empty_template_falls_back_to_the_default_layout() {
        return page_of(plain_template("")).starts_with("Does awesome things\n\nUsage: MyApp");
    }

    static_assert(an_empty_template_falls_back_to_the_default_layout());

    // =======================================================================
    // 8. Subcommands
    // =======================================================================

    /**
     * The `Commands:` table, its heading override, and the fact that the injected
     * `help` subcommand sorts last because it keeps display order 999.
     */
    consteval bool subcommands_list_in_declaration_order() {
        command_builder first("beta");
        std::move(first).about("second alphabetically, first declared");
        command_builder second("alpha");
        std::move(second).about("first alphabetically, second declared");

        command_builder cmd("test");
        std::move(cmd)
                .subcommand(std::move(first))
                .subcommand(std::move(second))
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) ==
               "Usage: test [COMMAND]\n"
               "\n"
               "Commands:\n"
               "  beta   second alphabetically, first declared\n"
               "  alpha  first alphabetically, second declared\n"
               "  help   Print this message or the help of the given subcommand(s)\n"
               "\n"
               "Options:\n"
               "  -h, --help  Print help\n";
    }

    static_assert(subcommands_list_in_declaration_order());

    consteval bool the_subcommand_heading_can_be_overridden() {
        command_builder sub("sub");
        std::move(sub).about("a sub");
        command_builder cmd("test");
        std::move(cmd)
                .subcommand_help_heading("APPLETS")
                .subcommand(std::move(sub))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()).contains("APPLETS:\n  sub  a sub\n");
    }

    static_assert(the_subcommand_heading_can_be_overridden());

    /** A subcommand selected by a flag shows both spellings in the table. */
    consteval bool flag_subcommands_show_their_flags() {
        command_builder sub("sub");
        std::move(sub).about("a sub").short_flag('S').long_flag("subbie");
        command_builder cmd("test");
        std::move(cmd)
                .subcommand(std::move(sub))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()).contains("  sub, -S, --subbie  a sub\n");
    }

    static_assert(flag_subcommands_show_their_flags());

    /**
     * A hidden subcommand is not listed, and a command whose only subcommand is hidden
     * grows no `Commands:` section at all.
     */
    consteval bool a_hidden_subcommand_removes_its_section() {
        command_builder sub("sub");
        std::move(sub).about("a sub").hide();
        command_builder cmd("test");
        std::move(cmd)
                .subcommand(std::move(sub))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return !page_of(cmd.freeze()).contains("Commands:");
    }

    static_assert(a_hidden_subcommand_removes_its_section());

    /**
     * clap `very_large_display_order` (tests/builder/display_order.rs), verbatim except
     * for the sentinel — clap uses `usize::MAX` and clapp uses 2^53-1, which sorts the
     * same way and avoids a literal that overflows on a 32-bit `std::size_t`.
     */
    consteval bool very_large_display_order() {
        command_builder sub("sub");
        std::move(sub).display_order(9007199254740991ULL);
        command_builder cmd("test");
        std::move(cmd).subcommand(std::move(sub)).setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) ==
               "Usage: test [COMMAND]\n"
               "\n"
               "Commands:\n"
               "  help  Print this message or the help of the given subcommand(s)\n"
               "  sub   \n"  // clap pins these trailing spaces too
               "\n"
               "Options:\n"
               "  -h, --help  Print help\n";
    }

    static_assert(very_large_display_order());

    /**
     * clap `flatten_basic` (tests/builder/help.rs), verbatim.
     *
     * `flatten_help` replaces the `Commands:` table with one section per subcommand,
     * headed by the path the user would have to type. That path is clap's
     * `Command::usage_name`, computed here rather than stored, because ADR-0005 freezes
     * the tree before it has one.
     */
    consteval bool flatten_basic() {
        command_builder sub("test");
        std::move(sub).about("test command").arg(arg_builder("child").long_("child"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(sub))
                .setting(command_setting::disable_version_flag);
        return help_flag_page(cmd.freeze(), false) ==
               "parent command\n"
               "\n"
               "Usage: parent [OPTIONS]\n"
               "       parent test [OPTIONS]\n"
               "       parent help [COMMAND]...\n"
               "\n"
               "Options:\n"
               "      --parent <parent>  \n"
               "  -h, --help             Print help\n"
               "\n"
               "parent test:\n"
               "test command\n"
               "      --child <child>  \n"
               "  -h, --help           Print help\n"
               "\n"
               "parent help:\n"
               "Print this message or the help of the given subcommand(s)\n"
               "  [COMMAND]...  Print help for the subcommand(s)\n";
    }

    static_assert(flatten_basic(),
                  "clapp: flatten_help heads each section with the subcommand's usage "
                  "name. If only the headings are wrong, write_flat_subcommands() and "
                  "usage_renderer::subcommand_usage_name() disagree about the path.");

    /**
     * clap `flatten_recursive` (tests/builder/help.rs), reduced to the smallest shape
     * that reaches the recursion — **and pinned to clapp's output, which differs from
     * clap's on two lines.**
     *
     * clap prints `parent child1 help [COMMAND]` with no argument row; clapp prints
     * `parent child1 help [COMMAND]...` and the row `[COMMAND]...  Print help for the
     * subcommand(s)`. The cause is clap's lazy build: only the level being rendered
     * runs `_build_self(false)`, which is what adds the injected `help` subcommand's
     * `[COMMAND]...` positional, so a `help` reached by flatten *recursion* was never
     * built. clapp freezes the whole tree at once (ADR-0005) and has no unbuilt level,
     * so it reports what is really there — and it really is there: `parent child1 help
     * grandchild1` prints grandchild1's help in **both** implementations.
     *
     * Everything else on the page is clap's, byte for byte, including the top-level
     * `parent help [COMMAND]...` — which clap *does* print with the row, since that one
     * is the level it built. The two spellings sitting three lines apart in clap's own
     * expected output is the tell.
     *
     * This case exists because the recursion at help.hpp's write_flat_subcommands() and
     * its twin in usage.hpp had no test at all: `flatten_basic` never recurses.
     */
    consteval bool flatten_recursive_shows_the_nested_help_positional() {
        command_builder grandchild("grandchild1");
        std::move(grandchild).about("grandchild1 command");
        command_builder child("child1");
        std::move(child)
                .flatten_help()
                .about("child1 command")
                .arg(arg_builder("child").long_("child1"))
                .subcommand(std::move(grandchild));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(child))
                .setting(command_setting::disable_version_flag);
        return help_flag_page(cmd.freeze(), false) ==
               "parent command\n"
               "\n"
               "Usage: parent [OPTIONS]\n"
               "       parent child1 [OPTIONS]\n"
               "       parent child1 grandchild1\n"
               // clap: "       parent child1 help [COMMAND]\n"
               "       parent child1 help [COMMAND]...\n"
               "       parent help [COMMAND]...\n"
               "\n"
               "Options:\n"
               "      --parent <parent>  \n"
               "  -h, --help             Print help\n"
               "\n"
               "parent child1:\n"
               "child1 command\n"
               "      --child1 <child>  \n"
               "  -h, --help            Print help\n"
               "\n"
               "parent child1 grandchild1:\n"
               "grandchild1 command\n"
               "  -h, --help  Print help\n"
               "\n"
               "parent child1 help:\n"
               "Print this message or the help of the given subcommand(s)\n"
               // clap stops here for the nested help; the next line is clapp's.
               "  [COMMAND]...  Print help for the subcommand(s)\n"
               "\n"
               "parent help:\n"
               "Print this message or the help of the given subcommand(s)\n"
               "  [COMMAND]...  Print help for the subcommand(s)\n";
    }

    static_assert(flatten_recursive_shows_the_nested_help_positional(),
                  "clapp: flatten_help recurses into a child that is itself flatten_help. "
                  "If this fails, check the divergence note first — the two lines that "
                  "differ from clap are deliberate and documented.");

    // =======================================================================
    // 9. Wrapping and next_line_help
    // =======================================================================

    /**
     * `term_width()` wraps the about line, and pushes a description that no longer fits
     * beside its option onto its own line — clap's `force_next_line`, whose middle test
     * is "the option column takes more than 40% of the terminal".
     */
    consteval bool a_narrow_terminal_wraps_and_breaks() {
        command_builder cmd("test");
        std::move(cmd)
                .term_width(40)
                .about("a rather long description that should be wrapped at forty columns")
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .value_name("V")
                             .action(arg_action::set)
                             .help("this description is also long enough to need wrapping"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "a rather long description that should be\n"
                                        "wrapped at forty columns\n"
                                        "\n"
                                        "Usage: test [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "  -o, --opt <V>\n"
                                        "          this description is also long\n"
                                        "          enough to need wrapping\n"
                                        "  -h, --help\n"
                                        "          Print help\n";
    }

    static_assert(a_narrow_terminal_wraps_and_breaks());

    /**
     * One argument asking for its own line puts the whole section on its own lines, so
     * that the section stays a column rather than becoming two shapes.
     */
    consteval bool next_line_help_is_a_section_wide_decision() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .value_name("V")
                             .action(arg_action::set)
                             .next_line_help()
                             .help("a description"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "Usage: test [OPTIONS]\n"
                                        "\n"
                                        "Options:\n"
                                        "  -o, --opt <V>\n"
                                        "          a description\n"
                                        "  -h, --help\n"
                                        "          Print help\n";
    }

    static_assert(next_line_help_is_a_section_wide_decision());

    /**
     * `max_term_width` bounds the *detected* width and deliberately does not bound an
     * explicit `term_width` — clapp::resolve_wrap_width's rule, reached through
     * clapp::help_style::detected_width.
     */
    consteval bool max_term_width_bounds_only_the_detected_width() {
        const auto build = [](bool explicit_width) {
            command_builder cmd("test");
            std::move(cmd)
                    .max_term_width(30)
                    .about("aaaa bbbb cccc dddd eeee ffff gggg hhhh")
                    .setting(command_setting::disable_help_subcommand)
                    .setting(command_setting::disable_version_flag);
            if (explicit_width) std::move(cmd).term_width(80);
            return cmd.freeze();
        };
        const command_spec capped    = build(false);
        const command_spec explicit_ = build(true);
        return render_help(capped, help_style{.detected_width = 200})
                       .to_string()
                       .starts_with("aaaa bbbb cccc dddd eeee ffff\ngggg hhhh\n") &&
               render_help(explicit_, help_style{.detected_width = 200})
                       .to_string()
                       .starts_with("aaaa bbbb cccc dddd eeee ffff gggg hhhh\n");
    }

    static_assert(max_term_width_bounds_only_the_detected_width());

    /**
     * A wide character counts two cells, so wrapping a CJK about line breaks earlier
     * than a byte count would. clapp::display_width does the counting; this pins that
     * the renderer asks it rather than `size()`.
     */
    consteval bool wrapping_counts_cells_not_bytes() {
        command_builder cmd("test");
        std::move(cmd)
                .term_width(10)
                .about("中文 中文 中文")
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()).starts_with("中文 中文\n中文\n");
    }

    static_assert(wrapping_counts_cells_not_bytes());

    // =======================================================================
    // 10. override_help, version, and the long-help gate
    // =======================================================================

    /**
     * `override_help()` short-circuits everything, template included, and still gets the
     * trailing newline clap's `write_help` always appends.
     */
    consteval bool override_help_short_circuits() {
        command_builder cmd("test");
        std::move(cmd)
                .about("ignored")
                .help_template("also ignored")
                .override_help("my own help")
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return page_of(cmd.freeze()) == "my own help\n";
    }

    static_assert(override_help_short_circuits());

    /**
     * clap `Command::_render_version`. Each form falls back to the other's text, and
     * the name is `display_name` falling back to `name` — never `bin_name`.
     */
    consteval bool version_lines_match_clap() {
        command_builder both("prog");
        std::move(both).version("1.0").long_version("1.0 (deadbeef)");
        const command_spec with_both = both.freeze();

        command_builder shortonly("prog");
        std::move(shortonly).version("1.0");
        const command_spec with_short = shortonly.freeze();

        command_builder named("prog");
        std::move(named).display_name("Prog!").bin_name("prog-1.0").version("1.0");
        const command_spec with_display = named.freeze();

        command_builder bare("prog");
        const command_spec with_none = bare.freeze();

        return render_version(with_both, false).to_string() == "prog 1.0\n" &&
               render_version(with_both, true).to_string() == "prog 1.0 (deadbeef)\n" &&
               render_version(with_short, true).to_string() == "prog 1.0\n" &&
               render_version(with_display, false).to_string() == "Prog! 1.0\n" &&
               render_version(with_none, false).to_string() == "prog\n";
    }

    static_assert(version_lines_match_clap());

    /**
     * clapp::long_help_exists() — the predicate that decides whether `--help` differs
     * from `-h` at all. All four sources count, and the mistake worth catching is
     * consulting only the obvious one.
     */
    consteval bool long_help_exists_reads_all_four_sources() {
        const auto bare = [] {
            command_builder cmd("test");
            std::move(cmd)
                    .arg(arg_builder("a").long_("a").action(arg_action::set_true).help("a"))
                    .setting(command_setting::disable_help_subcommand);
            return cmd.freeze();
        };
        const auto with_long_about = [] {
            command_builder cmd("test");
            std::move(cmd).long_about("more").setting(command_setting::disable_help_subcommand);
            return cmd.freeze();
        };
        const auto with_after_long = [] {
            command_builder cmd("test");
            std::move(cmd).after_long_help("more").setting(
                    command_setting::disable_help_subcommand);
            return cmd.freeze();
        };
        const auto with_arg_long_help = [] {
            command_builder cmd("test");
            std::move(cmd)
                    .arg(arg_builder("a")
                                 .long_("a")
                                 .action(arg_action::set_true)
                                 .help("a")
                                 .long_help("aaa"))
                    .setting(command_setting::disable_help_subcommand);
            return cmd.freeze();
        };
        const auto with_valued_pv = [] {
            command_builder cmd("test");
            std::move(cmd)
                    .arg(arg_builder("a").long_("a").action(arg_action::set).value_parser<mode>())
                    .setting(command_setting::disable_help_subcommand);
            return cmd.freeze();
        };
        const auto with_bare_pv = [] {
            command_builder cmd("test");
            std::move(cmd)
                    .arg(arg_builder("a").long_("a").action(arg_action::set).value_parser<speed>())
                    .setting(command_setting::disable_help_subcommand);
            return cmd.freeze();
        };
        return !long_help_exists(bare()) && long_help_exists(with_long_about()) &&
               long_help_exists(with_after_long()) && long_help_exists(with_arg_long_help()) &&
               long_help_exists(with_valued_pv())
               // A possible value with no help of its own does not create a long form:
               // `[possible values: fast, slow]` fits on one line.
               && !long_help_exists(with_bare_pv());
    }

    static_assert(long_help_exists_reads_all_four_sources());

    /**
     * The builder-side twin and the spec-side function must give the same verdict.
     *
     * clapp::command_builder::long_help_exists() decides what the *injected* `--help`
     * says; clapp::long_help_exists(const command_spec&) decides whether `--help`
     * collapses onto `-h`. Since M5 both are clapp::detail::long_help_exists_over(), but
     * the two callers feed it from different places — one from private members before
     * freeze(), one from accessors after — so the plumbing can still drift. Asserting
     * the *observable* pair closes that: a two-tier label promising a screen that is
     * never printed is the failure this rules out, and it is invisible to every other
     * case in this file.
     */
    consteval bool the_two_long_help_gates_agree() {
        const auto page_says_see_more = [](const command_spec& spec) {
            return page_of(spec, false).contains("Print help (see more with '--help')");
        };
        const auto has_long = [] {
            command_builder cmd("test");
            std::move(cmd)
                    .arg(arg_builder("a")
                                 .long_("a")
                                 .action(arg_action::set_true)
                                 .help("a")
                                 .long_help("aaa"))
                    .setting(command_setting::disable_help_subcommand)
                    .setting(command_setting::disable_version_flag);
            return cmd.freeze();
        }();
        const auto has_none = [] {
            command_builder cmd("test");
            std::move(cmd)
                    .arg(arg_builder("a").long_("a").action(arg_action::set_true).help("a"))
                    .setting(command_setting::disable_help_subcommand)
                    .setting(command_setting::disable_version_flag);
            return cmd.freeze();
        }();
        // Both sides, on both answers: an implementation that always says "yes" or always
        // says "no" passes half of this.
        return long_help_exists(has_long) && page_says_see_more(has_long) &&
               !long_help_exists(has_none) && !page_says_see_more(has_none);
    }

    static_assert(the_two_long_help_gates_agree(),
                  "clapp: the injected --help's label and the --help/-h collapse are the "
                  "same question. If they disagree, detail::long_help_exists_over() has "
                  "two callers feeding it different arguments.");

    // =======================================================================
    // 12b. User-supplied text never carries an escape sequence out
    // =======================================================================

    /**
     * A raw ANSI escape in `about` / `help` / `version` is removed, not passed through.
     *
     * clap answers this at the stream — anstream's `StripStream` when colour is off —
     * so an escape survives clap's `StyledStr` and disappears on the way out. clapp
     * answers it at the producer, because clapp::styled_str's documented invariant is
     * "no ANSI bytes" and because clapp::render_plain() must stay an exact identity on
     * content.
     *
     * The DECSCUSR sequence (`ESC [ 1 SP q`) is in here on purpose: its intermediate
     * byte is a `U+0020`, so before M5 fixed clapp::detail::space_word_end() the wrapper
     * could break a line in the middle of it and leave the terminal inside a control
     * sequence. Removing it makes that unreachable from a help screen; the wrapper fix
     * makes it unreachable from clapp::wrap() as well.
     */
    consteval bool user_escapes_never_reach_the_page() {
        command_builder cmd("demo");
        std::move(cmd)
                .version("1.0 \x1B[31mRED\x1B[0m")
                .about("about \x1B[31mRED\x1B[0m and \x1B[1 q cursor")
                .arg(arg_builder("lvl")
                             .long_("lvl")
                             .action(arg_action::set)
                             .help("help \x1B]0;title with spaces\x07 done"))
                .setting(command_setting::disable_help_subcommand);
        const command_spec spec = cmd.freeze();

        const std::string page    = page_of(spec);
        const std::string version = render_version(spec, false).to_string();
        for (const char byte : page) {
            if (byte == '\x1B') return false;
        }
        for (const char byte : version) {
            if (byte == '\x1B') return false;
        }
        // The prose either side of the sequence survives — this is a strip, not a truncate.
        return page.contains("about RED and  cursor") && page.contains("help  done") &&
               version == std::string_view{"demo 1.0 RED\n"};
    }

    static_assert(user_escapes_never_reach_the_page(),
                  "clapp: a help page must never carry an escape sequence clapp did not "
                  "generate — with colour off it corrupts the output, and split by the "
                  "wrapper it wedges the terminal.");

    // =======================================================================
    // 11. Styling
    //
    // Comparing to_string() is blind to clapp::style_class, so a renderer that pushed
    // every fragment as `plain` would pass every case above. These do not.
    // =======================================================================

    consteval bool fragments_carry_their_semantic_class() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("out")
                             .short_('o')
                             .long_("out")
                             .value_name("FILE")
                             .action(arg_action::set)
                             .default_value("a.out")
                             .help("where to write"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        const styled_str page = render_help(cmd.freeze());
        return page.text_of(style_class::header) == "Options:" &&
               page.text_of(style_class::usage) == "Usage:" &&
               page.text_of(style_class::literal).contains("-o") &&
               page.text_of(style_class::literal).contains("--out") &&
               page.text_of(style_class::placeholder).contains("<FILE>") &&
               page.text_of(style_class::context).contains("[default: ") &&
               page.text_of(style_class::context_value) == "a.out";
    }

    static_assert(fragments_carry_their_semantic_class(),
                  "clapp: help fragments carry a style_class. A to_string() comparison "
                  "cannot see this, which is why it is asserted separately.");

    consteval bool a_custom_heading_is_a_header_fragment() {
        command_builder cmd("test");
        std::move(cmd)
                .next_help_heading("NETWORKING")
                .arg(arg_builder("port").long_("port").action(arg_action::set_true).help("p"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return render_help(cmd.freeze()).text_of(style_class::header) == "Options:NETWORKING:";
    }

    static_assert(a_custom_heading_is_a_header_fragment());

    // =======================================================================
    // 12. Shape invariants
    // =======================================================================

    /**
     * clap's `write_help` ends with `trim_end()` then one `"\n"`, so every page ends in
     * exactly one newline no matter which sections were empty.
     */
    consteval bool every_page_ends_in_exactly_one_newline() {
        const auto ends_once = [](const std::string& page) {
            return page.size() >= 2 && page.back() == '\n' && page[page.size() - 2] != '\n';
        };
        command_builder empty("test");
        std::move(empty)
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag)
                .setting(command_setting::disable_help_flag);
        command_builder full("test");
        std::move(full).about("a").after_help("z").arg(
                arg_builder("o").long_("o").action(arg_action::set_true).help("o"));
        return ends_once(page_of(empty.freeze())) && ends_once(page_of(full.freeze())) &&
               ends_once(page_of(full.freeze(), true));
    }

    static_assert(every_page_ends_in_exactly_one_newline());

    /**
     * A command with nothing to list takes clap's `DEFAULT_NO_ARGS_TEMPLATE`, which is
     * shorter by the blank line and the whole `{all-args}` block.
     */
    consteval bool a_command_with_nothing_to_list_takes_the_short_template() {
        command_builder cmd("test");
        std::move(cmd)
                .about("does nothing")
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag)
                .setting(command_setting::disable_help_flag);
        return page_of(cmd.freeze()) == "does nothing\n\nUsage: test\n";
    }

    static_assert(a_command_with_nothing_to_list_takes_the_short_template());

    /**
     * `usage_name` reaches the `Usage:` line and nothing else — the path a subcommand's
     * help has to be told, because a frozen clapp::command_spec cannot know it.
     */
    consteval bool usage_name_names_the_path() {
        command_builder cmd("clone");
        std::move(cmd)
                .about("clone a repository")
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        const command_spec spec = cmd.freeze();
        return page_of(spec).contains("Usage: clone\n") &&
               page_of(spec, false, "git clone").contains("Usage: git clone\n");
    }

    static_assert(usage_name_names_the_path());

}  // namespace

// ===========================================================================
// Runtime
//
// Everything above is a compile-time conclusion; these report it, and cover the one
// entry point that cannot cross the consteval boundary.
// ===========================================================================

CLAPP_TEST("render_help lays out two columns as clap does") {
    CLAPP_CHECK(args_with_last_usage());
    CLAPP_CHECK(req_last_arg_usage());
    CLAPP_CHECK(declaration_order_beats_the_alphabet());
    CLAPP_CHECK(equal_display_orders_use_claps_key());
    CLAPP_CHECK(an_empty_help_still_pads_its_column());
}

CLAPP_TEST("render_help places before/after help and about") {
    CLAPP_CHECK(after_and_before_help_output());
    CLAPP_CHECK(after_and_before_long_help_output());
    CLAPP_CHECK(short_help_never_falls_back_to_long_about());
    CLAPP_CHECK(an_absent_about_leaves_no_blank_first_line());
    CLAPP_CHECK(the_newline_var_is_expanded());
}

CLAPP_TEST("render_help groups by help_heading") {
    CLAPP_CHECK(multiple_custom_help_headers());
    CLAPP_CHECK(each_section_measures_its_own_column());
}

CLAPP_TEST("render_help honours the three hide switches") {
    CLAPP_CHECK(the_two_screens_list_different_arguments());
    CLAPP_CHECK(each_hide_switch_suppresses_only_its_own_tail());
    CLAPP_CHECK(hidden_aliases_are_not_listed());
    CLAPP_CHECK(a_hidden_subcommand_removes_its_section());
}

CLAPP_TEST("render_help writes the bracketed tail") {
    CLAPP_CHECK(help_enum_arg_with_no_description());
    CLAPP_CHECK(short_help_lists_values_inline());
    CLAPP_CHECK(a_spaced_default_is_quoted());
    CLAPP_CHECK(aliases_are_listed_and_pluralized());
}

CLAPP_TEST("render_help reads Arg::env through the injected lookup") {
    CLAPP_CHECK(env_annotations_match_clap());
    CLAPP_CHECK(an_unset_variable_renders_as_clap_does());
}

CLAPP_TEST("the help_template engine expands clap's placeholders") {
    CLAPP_CHECK(with_template());
    CLAPP_CHECK(custom_template());
    CLAPP_CHECK(template_literals_and_unknown_tags());
    CLAPP_CHECK(the_spacing_variants_differ());
    CLAPP_CHECK(an_unclosed_brace_swallows_its_segment());
    CLAPP_CHECK(an_empty_template_falls_back_to_the_default_layout());
}

CLAPP_TEST("render_help lists subcommands") {
    CLAPP_CHECK(subcommands_list_in_declaration_order());
    CLAPP_CHECK(the_subcommand_heading_can_be_overridden());
    CLAPP_CHECK(flag_subcommands_show_their_flags());
    CLAPP_CHECK(very_large_display_order());
    CLAPP_CHECK(flatten_basic());
}

CLAPP_TEST("render_help wraps to the terminal") {
    CLAPP_CHECK(a_narrow_terminal_wraps_and_breaks());
    CLAPP_CHECK(next_line_help_is_a_section_wide_decision());
    CLAPP_CHECK(max_term_width_bounds_only_the_detected_width());
    CLAPP_CHECK(wrapping_counts_cells_not_bytes());
}

CLAPP_TEST("render_version and the long-help gate match clap") {
    CLAPP_CHECK(override_help_short_circuits());
    CLAPP_CHECK(version_lines_match_clap());
    CLAPP_CHECK(long_help_exists_reads_all_four_sources());
    CLAPP_CHECK(the_two_long_help_gates_agree());
}

CLAPP_TEST("user-supplied escape sequences are stripped from the page") {
    CLAPP_CHECK(user_escapes_never_reach_the_page());
}

CLAPP_TEST("flatten_help recurses into a flattened child") {
    CLAPP_CHECK(flatten_recursive_shows_the_nested_help_positional());
}

CLAPP_TEST("help fragments carry a style_class") {
    CLAPP_CHECK(fragments_carry_their_semantic_class());
    CLAPP_CHECK(a_custom_heading_is_a_header_fragment());
}

CLAPP_TEST("every page ends in exactly one newline") {
    CLAPP_CHECK(every_page_ends_in_exactly_one_newline());
    CLAPP_CHECK(a_command_with_nothing_to_list_takes_the_short_template());
    CLAPP_CHECK(usage_name_names_the_path());
}

CLAPP_TEST("render_help_for_terminal probes the terminal and the environment") {
    // The impure entry point, and the only thing in this file that cannot be a
    // static_assert. It must agree with the pure one whenever the width is pinned, and
    // it must produce a well-formed page whatever the terminal running the test says.
    static constexpr command_spec spec = [] {
        command_builder inner("test");
        std::move(inner)
                .term_width(60)
                .about("a demo")
                .arg(arg_builder("opt").long_("opt").action(arg_action::set_true).help("an option"))
                .setting(command_setting::disable_help_subcommand)
                .setting(command_setting::disable_version_flag);
        return inner.freeze();
    }();

    // term_width() pins the width, so terminal detection cannot change the answer.
    const std::string probed = clapp::render_help_for_terminal(spec).to_string();
    const std::string pure   = clapp::render_help(spec).to_string();
    CLAPP_CHECK(probed == pure);
    CLAPP_CHECK(probed.contains("Usage: test [OPTIONS]"));
    CLAPP_CHECK(!probed.empty() && probed.back() == '\n');
}
