#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <cstddef>
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
    using clapp::group_builder;
    using clapp::help_style;
    using clapp::value_range;

    /// \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
    ///        Same helper, same reason, as conformance_help_test.cpp.
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
    // clap writes `value_parser(["Nearest", …])`; clapp enumerates a domain as a type. The
    // enumerator identifiers must be renamed because clapp's default is
    // `clapp::rename(identifier, naming::kebab)` — `nearest`, not `Nearest` — and clap's
    // expected screen spells them capitalised.
    // ---------------------------------------------------------------------------

    enum class filter_kind {
        nearest[[= clapp::value{.name = "Nearest"}]],
        linear[[= clapp::value{.name = "Linear"}]],
        cubic[[= clapp::value{.name = "Cubic"}]],
        gaussian[[= clapp::value{.name = "Gaussian"}]],
        lanczos3[[= clapp::value{.name = "Lanczos3"}]],
    };

    // ---------------------------------------------------------------------------
    // Fixtures — issue_688_hide_pos_vals
    //
    // Three commands, one expected screen. `long_text` is clap's help string with its `\`
    // line continuations already collapsed (Rust drops the newline and the following
    // indentation), so the prose is a single space-separated run.
    // ---------------------------------------------------------------------------

    constexpr std::string_view filter_help_with_list =
            "Sets the filter, or sampling method, to use for interpolation when resizing the "
            "particle images. The default is Linear (Bilinear). [possible values: Nearest, Linear, "
            "Cubic, Gaussian, Lanczos3]";

    constexpr std::string_view filter_help_plain =
            "Sets the filter, or sampling method, to use for interpolation when resizing the "
            "particle images. The default is Linear (Bilinear).";

    /// clap's `app1`: the domain exists, the annotation is suppressed command-wide, and the
    /// author has written the list into the help text by hand.
    consteval command_spec make_688_hidden() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(120).hide_possible_values().arg(
                arg_builder("filter")
                        .help(filter_help_with_list)
                        .long_("filter")
                        .value_parser<filter_kind>()
                        .action(arg_action::set)
                        .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec cmd_688_hidden = make_688_hidden();

    /// clap's `app2`: the domain exists and the annotation is generated. The author writes
    /// nothing, and the screen must come out identical to `app1`'s.
    consteval command_spec make_688_generated() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(120).arg(
                arg_builder("filter")
                        .help(filter_help_plain)
                        .long_("filter")
                        .value_parser<filter_kind>()
                        .action(arg_action::set)
                        .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec cmd_688_generated = make_688_generated();

    /// clap's `app3`: no domain at all, list written by hand. Nothing to hide, nothing to
    /// generate, and still the same screen.
    consteval command_spec make_688_prose() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(120).arg(
                arg_builder("filter")
                        .help(filter_help_with_list)
                        .long_("filter")
                        .action(arg_action::set)
                        .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec cmd_688_prose = make_688_prose();

    // ---------------------------------------------------------------------------
    // Fixtures — the remaining eight cases
    // ---------------------------------------------------------------------------

    consteval command_spec make_702() {
        command_builder app("myapp");
        std::move(app)
                .version("1.0")
                .author("foo")
                .about("bar")
                .arg(arg_builder("arg1").index(1).help("some option"))
                .arg(arg_builder("arg2")
                             .index(2)
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .help("some option"))
                .arg(arg_builder("some")
                             .help("some option")
                             .short_('s')
                             .long_("some")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("other")
                             .help("some other option")
                             .short_('o')
                             .long_("other")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("label")
                             .help("a label")
                             .short_('l')
                             .long_("label")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec cmd_702 = make_702();

    /// clap's `issue_1052_require_delim_help`. The delimiter must not reach the placeholder:
    /// two value names render space-separated whatever splits the input.
    consteval command_spec make_1052() {
        command_builder app("test");
        std::move(app)
                .author("Kevin K.")
                .about("tests stuff")
                .version("1.3")
                .arg(arg_builder("fake")
                             .short_('f')
                             .long_("fake")
                             .help("some help")
                             .required()
                             .value_names({"some", "val"})
                             .action(arg_action::set)
                             .value_delimiter(':'));
        return app.freeze();
    }
    constexpr command_spec cmd_1052 = make_1052();

    /// clap's `issue_1487`. Both members are positionals; the required group collapses them
    /// into one usage slot while both keep their own table rows.
    consteval command_spec make_1487() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("arg1").index(1).group("group1"))
                .arg(arg_builder("arg2").index(2).group("group1"))
                .group(group_builder("group1").args({"arg1", "arg2"}).required());
        return app.freeze();
    }
    constexpr command_spec cmd_1487 = make_1487();

    consteval command_spec make_1642() {
        command_builder app("prog");
        std::move(app).arg(
                arg_builder("cfg")
                        .long_("config")
                        .action(arg_action::set_true)
                        .long_help("The config file used by the myprog must be in JSON format\n"
                                   "with only valid keys and may not contain other nonsense\n"
                                   "that cannot be read by this program. Obviously I'm going on\n"
                                   "and on, so I'll stop now."));
        return app.freeze();
    }
    constexpr command_spec cmd_1642 = make_1642();

    /// clap's `issue_1794_usage`. The command is called `hello` and the binary `deno`; the
    /// usage line must say `deno`, which is the whole point of the case.
    consteval command_spec make_1794() {
        command_builder app("hello");
        std::move(app)
                .bin_name("deno")
                .arg(arg_builder("option1").long_("option1").action(arg_action::set_true))
                .arg(arg_builder("pos1")
                             .index(1)
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .group(group_builder("arg1").args({"pos1", "option1"}).required())
                .arg(arg_builder("pos2")
                             .index(2)
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec cmd_1794 = make_1794();

    /// clap's `dont_strip_padding_issue_5083`. `{subcommands}` alone, and three of the four
    /// rows have nothing in the description column.
    consteval command_spec make_5083() {
        command_builder app("test");
        std::move(app)
                .help_template("{subcommands}")
                .subcommand(command_builder("one"))
                .subcommand(command_builder("two"))
                .subcommand(command_builder("three"));
        return app.freeze();
    }
    constexpr command_spec cmd_5083 = make_5083();

    /// clap's `custom_help_headers_hide_args`.
    ///
    /// The declaration order matters and is clap's: `NETWORKING` is opened first and every
    /// argument that lands in it hides from the short screen, so on `-h` the section must not
    /// be emitted at all — not emitted empty.
    consteval command_spec make_custom_hide() {
        command_builder app("blorp");
        std::move(app)
                .author("Will M.")
                .about("does stuff")
                .version("1.4")
                .next_help_heading("NETWORKING")
                .arg(arg_builder("no-proxy")
                             .short_('n')
                             .long_("no-proxy")
                             .help("Do not use system proxy settings")
                             .action(arg_action::set_true)
                             .hide_short_help())
                .next_help_heading("SPECIAL")
                .arg(arg_builder("song")
                             .short_('b')
                             .long_("song")
                             .value_name("song")
                             .help("Change which song is played for birthdays")
                             .required()
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .help_heading("OVERRIDE SPECIAL"))
                .arg(arg_builder("song-volume")
                             .short_('v')
                             .long_("song-volume")
                             .value_name("volume")
                             .help("Change the volume of the birthday song")
                             .required()
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .next_help_heading("")
                .arg(arg_builder("server-addr")
                             .short_('a')
                             .long_("server-addr")
                             .help("Set server address")
                             .action(arg_action::set_true)
                             .help_heading("NETWORKING")
                             .hide_short_help());
        return app.freeze();
    }
    constexpr command_spec custom_hide = make_custom_hide();

    /// clap's `setup_aliases()`, plus its `.term_width(80)`.
    ///
    /// Eleven aliases on `dest`, six of them visible; sixteen on `rev`, nine visible. The
    /// hidden ones exist so that a renderer which prints `get_aliases()` instead of
    /// `get_visible_aliases()` produces a visibly longer list rather than an identical one.
    consteval command_spec make_aliases() {
        command_builder app("ctest");
        std::move(app)
                .version("0.1")
                .term_width(80)
                .arg(arg_builder("dest")
                             .short_('d')
                             .long_("destination")
                             .value_name("FILE")
                             .help("File to save into")
                             .long_help("The Filepath to save into the result")
                             .short_alias('q')
                             .short_aliases({'w', 'e'})
                             .alias("arg-alias")
                             .aliases({"do-stuff", "do-tests"})
                             .visible_short_alias('t')
                             .visible_short_aliases({'i', 'o'})
                             .visible_alias("file")
                             .visible_aliases({"into", "to"})
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .subcommand(command_builder("rev")
                                    .short_flag('r')
                                    .long_flag("inplace")
                                    .about("In place")
                                    .long_about("Change mode to work in place on source")
                                    .alias("subc-alias")
                                    .aliases({"subc-do-stuff", "subc-do-tests"})
                                    .short_flag_alias('j')
                                    .short_flag_aliases({'k', 'l'})
                                    .long_flag_alias("subc-long-flag-alias")
                                    .long_flag_aliases({"subc-long-do-stuff", "subc-long-do-tests"})
                                    .visible_alias("source")
                                    .visible_aliases({"from", "onsource"})
                                    .visible_short_flag_alias('s')
                                    .visible_short_flag_aliases({'f', 'g'})
                                    .visible_long_flag_alias("origin")
                                    .visible_long_flag_aliases({"path", "tryfrom"})
                                    .arg(arg_builder("input").index(1).value_name("INPUT").help(
                                            "The source file")));
        return app.freeze();
    }
    constexpr command_spec aliases_cmd = make_aliases();

    /// clap's help.rs `hide_args` (help.rs:1619).
    ///
    /// `arg!(-f --flag "testing flags")` is a flag, so `set_true`; `arg!(-o --opt <FILE>
    /// "tests options")` takes exactly one value; `Arg::new("pos").hide(true)` is a
    /// positional, which clapp indexes explicitly because clapp has no implicit index.
    ///
    /// NOT the same command as `conformance_hidden_args_test.cpp`'s `hide_args_cmd`, which
    /// ports clap's *hidden_args.rs* case of the same name: that one is `test`, has an
    /// author and an about, four arguments of which two are hidden, and expects a page with
    /// a leading `tests stuff` line. Nothing but the function name is shared.
    consteval command_spec make_help_hide_args() {
        command_builder app("prog");
        std::move(app)
                .version("1.0")
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("testing flags")
                             .action(arg_action::set_true))
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .value_name("FILE")
                             .help("tests options")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("pos").index(1).hide());
        return app.freeze();
    }
    constexpr command_spec help_hide_args_cmd = make_help_hide_args();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // These are the preconditions the expected screens below silently depend on. Asserting
    // them here means a fixture that stops expressing clap's command fails on its own line
    // rather than as a puzzling diff.
    // ---------------------------------------------------------------------------

    // The three `issue_688` commands wrap at 120, not at clapp::default_terminal_width — the
    // expected screen's line breaks are meaningless without this.
    static_assert(cmd_688_hidden.get_term_width() == std::optional<std::size_t>{120});
    static_assert(cmd_688_generated.get_term_width() == std::optional<std::size_t>{120});
    static_assert(cmd_688_prose.get_term_width() == std::optional<std::size_t>{120});
    static_assert(aliases_cmd.get_term_width() == std::optional<std::size_t>{80});

    // None of the `issue_688` variants has long content, so `--help` and `-h` are the same
    // page — which is why one expected string serves all three.
    static_assert(!clapp::long_help_exists(cmd_688_hidden));
    static_assert(!clapp::long_help_exists(cmd_688_generated));
    static_assert(!clapp::long_help_exists(cmd_688_prose));

    // `long_help` on `cfg` is what makes issue 1642 a long screen at all.
    static_assert(clapp::long_help_exists(cmd_1642));

    // ...and neither issue 1487 nor issue 1794 has any, so their help flags read
    // "Print help" rather than cross-referencing the other screen.
    static_assert(!clapp::long_help_exists(cmd_1487));
    static_assert(!clapp::long_help_exists(cmd_1794));

    // `custom_help_headers_hide_args` and the alias cases both have long content, so `-h`
    // and `--help` genuinely differ and asserting only one of them would prove half a rule.
    static_assert(clapp::long_help_exists(custom_hide));
    static_assert(clapp::long_help_exists(aliases_cmd));

    // `hide_args` asserts an ABSENCE, so the fixture must be shown to contain the thing that
    // is absent from the page. A `make_help_hide_args()` that dropped `.arg(pos)` altogether
    // renders the identical screen and the case would pass while testing nothing — the
    // vacuous shape traps 15 to 17 are all instances of. Both halves are stated: the
    // positional is THERE, and it is hidden. `has_arg` is the predicate form, never a null
    // comparison, per trap 10.
    static_assert(help_hide_args_cmd.has_arg("pos"));
    static_assert(help_hide_args_cmd.find_arg("pos")->is_hide_set());
    static_assert(!help_hide_args_cmd.find_arg("flag")->is_hide_set());
    static_assert(!help_hide_args_cmd.find_arg("opt")->is_hide_set());

    // Nothing in it is long, so `--help` collapses onto `-h` and clap's compact two-column
    // page is what the case compares against.
    static_assert(!clapp::long_help_exists(help_hide_args_cmd));

    // No `term_width`, unlike every other fixture in this file: clap's expected page for
    // this case is narrow enough that nothing wraps at clapp::default_terminal_width. Pinned
    // because a stray width here would re-flow the page and the failure would read as a
    // hiding bug.
    static_assert(help_hide_args_cmd.get_term_width() == std::nullopt);

}  // namespace

// ---------------------------------------------------------------------------
// issue_688 — three routes to one screen
// ---------------------------------------------------------------------------

constexpr std::string_view expected_688 =
        "Usage: ctest [OPTIONS]\n"
        "\n"
        "Options:\n"
        "      --filter <filter>  Sets the filter, or sampling method, to use for interpolation "
        "when resizing the particle\n"
        "                         images. The default is Linear (Bilinear). [possible values: "
        "Nearest, Linear, Cubic, Gaussian,\n"
        "                         Lanczos3]\n"
        "  -h, --help             Print help\n"
        "  -V, --version          Print version\n";

CLAPP_TEST("help.rs::issue_688_hide_pos_vals (hide_possible_values + hand-written list)") {
    CLAPP_CHECK(same(page(cmd_688_hidden, true), expected_688));
}

// WAS A DIVERGENCE, NOW CLOSED — see the file header.
//
// This is the only one of the three `issue_688` variants whose `[possible values: …]` is
// GENERATED, so it is the only one whose description is more than one clapp::styled_str
// fragment. It used to be pinned against its own expected string, identical to
// `expected_688` except for one trailing space after `Gaussian,`, on the grounds that a
// break landing on the first word of a fragment could not retract the space that ended
// the previous one. clapp::wrap(const styled_str&, std::size_t) now wraps the whole
// message as one stream, so the fragment boundary is not a wrapping boundary and the
// space is retracted like any other. All three variants assert `expected_688`, which is
// clap's screen unmodified — which is also what the header claims they should, since the
// three differ only in where the text came from.
CLAPP_TEST("help.rs::issue_688_hide_pos_vals (generated list)") {
    CLAPP_CHECK(same(page(cmd_688_generated, true), expected_688));
}

CLAPP_TEST("help.rs::issue_688_hide_pos_vals (no value domain, hand-written list)") {
    CLAPP_CHECK(same(page(cmd_688_prose, true), expected_688));
}

// ---------------------------------------------------------------------------
// Arity, delimiters
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::issue_702_multiple_values") {
    CLAPP_CHECK(same(page(cmd_702, true),
                     "bar\n"
                     "\n"
                     "Usage: myapp [OPTIONS] [arg1] [arg2]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [arg1]     some option\n"
                     "  [arg2]...  some option\n"
                     "\n"
                     "Options:\n"
                     "  -s, --some <some>       some option\n"
                     "  -o, --other <other>     some other option\n"
                     "  -l, --label <label>...  a label\n"
                     "  -h, --help              Print help\n"
                     "  -V, --version           Print version\n"));
}

CLAPP_TEST("help.rs::issue_1052_require_delim_help") {
    CLAPP_CHECK(same(page(cmd_1052, true),
                     "tests stuff\n"
                     "\n"
                     "Usage: test --fake <some> <val>\n"
                     "\n"
                     "Options:\n"
                     "  -f, --fake <some> <val>  some help\n"
                     "  -h, --help               Print help\n"
                     "  -V, --version            Print version\n"));
}

// ---------------------------------------------------------------------------
// Required groups in the usage line
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::issue_1487") {
    // Both members are positionals, so `<arg1|arg2>` replaces two usage slots with one
    // while the `Arguments:` table keeps both rows — each with the trailing padding an
    // empty description leaves behind.
    CLAPP_CHECK(same(page(cmd_1487, false, "ctest"),
                     "Usage: ctest <arg1|arg2>\n"
                     "\n"
                     "Arguments:\n"
                     "  [arg1]  \n"
                     "  [arg2]  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("help.rs::issue_1794_usage") {
    // The group mixes a positional with a flag, and `pos2` — outside the group — still
    // gets its own optional slot after it. The usage name is the *bin* name, not the
    // command name.
    CLAPP_CHECK(same(page(cmd_1794, true),
                     "Usage: deno <pos1|--option1> [pos2]\n"
                     "\n"
                     "Arguments:\n"
                     "  [pos1]  \n"
                     "  [pos2]  \n"
                     "\n"
                     "Options:\n"
                     "      --option1  \n"
                     "  -h, --help     Print help\n"));
}

// ---------------------------------------------------------------------------
// Long help layout
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::issue_1642_long_help_spacing") {
    // Four source lines arrive as four output lines: the author's own breaks survive
    // re-wrapping, and the blank line between entries is the long screen's separator.
    CLAPP_CHECK(same(page(cmd_1642, true),
                     "Usage: prog [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --config\n"
                     "          The config file used by the myprog must be in JSON format\n"
                     "          with only valid keys and may not contain other nonsense\n"
                     "          that cannot be read by this program. Obviously I'm going on\n"
                     "          and on, so I'll stop now.\n"
                     "\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"));
}

// ---------------------------------------------------------------------------
// Padding
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::dont_strip_padding_issue_5083") {
    // Three of the four rows are a name followed by nothing. clap keeps the padding that
    // would have preceded a description; a right-trimming renderer emits "  one\n".
    CLAPP_CHECK(same(page(cmd_5083, true),
                     "  one    \n"
                     "  two    \n"
                     "  three  \n"
                     "  help   Print this message or the help of the given subcommand(s)\n"));
}

// ---------------------------------------------------------------------------
// Headings: overridden, cleared, and emptied by hiding
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::custom_help_headers_hide_args") {
    // `NETWORKING` is opened first and both of its members hide from the short screen, so
    // the heading must not be emitted. `song` escapes `SPECIAL` into `OVERRIDE SPECIAL`,
    // which therefore sorts before it — section order is first-appearance order of the
    // heading, not declaration order of the arguments.
    CLAPP_CHECK(same(page(custom_hide, false, "test"),
                     "does stuff\n"
                     "\n"
                     "Usage: test [OPTIONS] --song <song> --song-volume <volume>\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help (see more with '--help')\n"
                     "  -V, --version  Print version\n"
                     "\n"
                     "OVERRIDE SPECIAL:\n"
                     "  -b, --song <song>  Change which song is played for birthdays\n"
                     "\n"
                     "SPECIAL:\n"
                     "  -v, --song-volume <volume>  Change the volume of the birthday song\n"));
}

// ---------------------------------------------------------------------------
// Visible aliases
// ---------------------------------------------------------------------------

// WAS BLOCKED BY BOTH HALVES OF THE OLD PER-FRAGMENT WRAP, AND IS THE CASE THAT PROVES
// THEY WERE ONE DEFECT. This screen carried both symptoms on two different rows, which is
// why it was the last one to come back and why it is worth reading before touching
// clapp::wrap(const styled_str&, std::size_t) again.
//
//   the `rev` row — a word split across a fragment boundary was measured in halves.
//   `--tryfrom` fits the 58-cell description column exactly (49 + 9 == 58) and the
//   separator `", "` that follows it does not, so the break landed BETWEEN them and put a
//   comma on column 23 of a help page:
//
//     clap    rev, -r, --inplace  In place [aliases: -s, -f, -g, --origin, --path,
//                                 --tryfrom, source, from, onsource]
//     was     rev, -r, --inplace  In place [aliases: -s, -f, -g, --origin, --path, --tryfrom
//                                 , source, from, onsource]
//
//   the `-d` row — a break landing on the first word of a fragment could not retract the
//   trailing space of the previous one. The description column is 52 cells; `--file, `
//   ends at 48 and `--into, ` would end at 56, so the line ended in a space clap does not
//   emit.
//
// THE NOTE THAT USED TO SIT HERE SAID THE SECOND HALF COULD NOT BE FIXED — that it was
// clap's own `wrap_styled` behaviour and that closing it would move clapp off a pinned
// clap output. That was wrong, and the measurement that shows it is not in this file:
// clap's `wrap_styled` keeps the space only because the test formats a NON-EMPTY
// `anstyle::Style` into its input, so `StyledStr::iter_text()` really does see seven
// runs. The styles clap builds THESE lists from — `context` and `context_value` — are
// empty even with colour forced on (measured against clap 4.6.5, rustc 1.98.0-nightly:
// `Styles::styled().get_context()` is `Style { fg: None, bg: None, underline: None,
// effects: Effects() }`), so `format!("{ctx}, {ctx:#}")` is literally `", "`,
// `iter_text()` yields one run spanning the whole `[aliases: …]` list, and clap's word
// extents and retraction both cross what clapp was calling a boundary. Both halves were
// the same defect — clapp fragmenting where clap does not — and one change closed both.
// (An "0 escapes with colour ON" figure used to appear here as corroboration; it was
// withdrawn on 2026-08-11 because it was read through clap's colour-unaware `Display`
// and would report 0 for any styling at all. See the file header.)
CLAPP_TEST("help.rs::visible_aliases_with_short_help") {
    CLAPP_CHECK(same(page(aliases_cmd, false),
                     "Usage: ctest [OPTIONS] [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  rev, -r, --inplace  In place [aliases: -s, -f, -g, --origin, --path,\n"
                     "                      --tryfrom, source, from, onsource]\n"
                     "  help                Print this message or the help of the given "
                     "subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -d, --destination <FILE>  File to save into [aliases: -t, -i, -o, --file,\n"
                     "                            --into, --to]\n"
                     "  -h, --help                Print help (see more with '--help')\n"
                     "  -V, --version             Print version\n"));
}

// The long screen is the control for the short one above: its `Options:` half puts the
// argument's alias list in its own paragraph, where it fits on one line and never reaches
// the wrapper at all, so that half already matched clap byte for byte while the short
// screen's `-d` row did not. Only the `Commands:` `rev` row — the same row, the same
// 58-cell column, the same list — was ever wrong here. Keeping both screens is what
// separates "the alias list is built right" from "the alias list is wrapped right"; a
// change that broke only the wrapper would leave this case's `Options:` half green.
CLAPP_TEST("help.rs::visible_aliases_with_long_help") {
    // The subcommand table is identical on both screens — clap defers a subcommand's
    // `long_about` to `ctest rev --help` rather than inlining it — while the argument's
    // alias list moves into its own paragraph.
    CLAPP_CHECK(same(page(aliases_cmd, true),
                     "Usage: ctest [OPTIONS] [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  rev, -r, --inplace  In place [aliases: -s, -f, -g, --origin, --path,\n"
                     "                      --tryfrom, source, from, onsource]\n"
                     "  help                Print this message or the help of the given "
                     "subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -d, --destination <FILE>\n"
                     "          The Filepath to save into the result\n"
                     "          \n"
                     "          [aliases: -t, -i, -o, --file, --into, --to]\n"
                     "\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"
                     "\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

// ---------------------------------------------------------------------------
// Hidden arguments on the help screen
// ---------------------------------------------------------------------------

// NOT A DUPLICATE OF `hidden_args.rs::hide_args`, and the prefix is the whole reason this
// case is written out under its own name. clap's suite has two functions called
// `hide_args`: help.rs:1619 (this one, command `prog`, one hidden positional) and
// hidden_args.rs:18 (command `test`, an author, an about, and TWO hidden arguments, one of
// them a flag). Their expected pages share no line but `  -h, --help` and `  -V,
// --version`. For a whole milestone this file's absence was invisible because a
// name-keyed coverage check found the hidden_args.rs port and stopped looking.
CLAPP_TEST("help.rs::hide_args") {
    // What the page proves, in the order it proves it:
    //   * `Usage: prog [OPTIONS]` — the hidden positional does not reach the usage line, so
    //     there is no `[pos]`, and `[OPTIONS]` is still there because two options remain;
    //   * there is NO `Arguments:` section at all. The only positional is hidden, and its
    //     heading must go with it rather than being emitted and then left empty;
    //   * the description column is 20 (`  -o, --opt <FILE>  `), computed from the widest
    //     VISIBLE argument. `pos` is 5 columns and could not have widened it, which is why
    //     this case does not also pin what hidden_args.rs's `hide_args` pins — that a
    //     hidden argument keeps its column budget. Two cases, two properties.
    CLAPP_CHECK(same(page(help_hide_args_cmd, true),
                     "Usage: prog [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -f, --flag        testing flags\n"
                     "  -o, --opt <FILE>  tests options\n"
                     "  -h, --help        Print help\n"
                     "  -V, --version     Print version\n"));
}
