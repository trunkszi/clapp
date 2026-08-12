#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
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
    using clapp::help_style;
    using clapp::value_range;

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        Same helper, and the same reasoning, as conformance_help_test.cpp.
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
    // Value domains — clap's `opt3_vals` / `pos3_vals`
    //
    // See DIVERGENCE 2 above: clap writes these as string arrays on the argument.
    // ---------------------------------------------------------------------------

    enum class opt3_vals { fast, slow };
    enum class pos3_vals { vi, emacs };

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    /**
     * clap's `utils::FULL_TEMPLATE`, byte for byte.
     *
     * It is the reason `complex_help_output` prints an author line at all: the default
     * layout has no `{author-with-newline}` in it, so an author set on the command is
     * invisible until a template asks for one.
     */
    constexpr std::string_view full_template = "{before-help}{name} {version}\n"
                                               "{author-with-newline}{about-with-newline}\n"
                                               "{usage-heading} {usage}\n"
                                               "\n"
                                               "{all-args}{after-help}";

    /**
     * clap's shared `utils::complex_app()`.
     *
     * Transcribed rather than shared with conformance_tests_test.cpp for the reason that
     * file already states: each conformance file is self-contained, so a change made for
     * one file's sake cannot silently rewrite another's expectations. Two additions over
     * that copy, both of which only this file needs — `help_template(FULL_TEMPLATE)` and
     * the two value domains.
     */
    consteval command_spec make_complex_app() {
        command_builder app("clap-test");
        std::move(app)
                .version("v1.4.8")
                .about("tests clap library")
                .author("Kevin K. <kbknapp@gmail.com>")
                .help_template(full_template)
                .arg(arg_builder("option")
                             .short_('o')
                             .long_("option")
                             .value_name("opt")
                             .help("tests options")
                             .required(false)
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("positional").index(1).help("tests positionals"))
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("tests flags")
                             .action(arg_action::count)
                             .global())
                .arg(arg_builder("flag2")
                             .short_('F')
                             .help("tests flags with exclusions")
                             .conflicts_with("flag")
                             .requires_("long-option-2")
                             .action(arg_action::set_true))
                .arg(arg_builder("long-option-2")
                             .long_("long-option-2")
                             .value_name("option2")
                             .help("tests long options with exclusions")
                             .conflicts_with("option")
                             .requires_("positional2")
                             .action(arg_action::set))
                .arg(arg_builder("positional2").index(2).help("tests positionals with exclusions"))
                .arg(arg_builder("option3")
                             .short_('O')
                             .long_("option3")
                             .value_name("option3")
                             .help("specific vals")
                             .value_parser<opt3_vals>()
                             .action(arg_action::set))
                .arg(arg_builder("positional3")
                             .index(3)
                             .help("tests specific values")
                             .value_parser<pos3_vals>()
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("multvals")
                             .long_("multvals")
                             .help("Tests multiple values, not mult occs")
                             .value_names({"one", "two"}))
                .arg(arg_builder("multvalsmo")
                             .long_("multvalsmo")
                             .help("Tests multiple values, and mult occs")
                             .value_names({"one", "two"})
                             .action(arg_action::append))
                .arg(arg_builder("minvals2")
                             .long_("minvals2")
                             .value_name("minvals")
                             .help("Tests 2 min vals")
                             .num_args(value_range::at_least(2)))
                .arg(arg_builder("maxvals3")
                             .long_("maxvals3")
                             .value_name("maxvals")
                             .help("Tests 3 max vals")
                             .num_args(value_range{1, 3}))
                .arg(arg_builder("optvaleq")
                             .long_("optvaleq")
                             .value_name("optval")
                             .help("Tests optional value, require = sign")
                             .num_args(value_range{0, 1})
                             .require_equals())
                .arg(arg_builder("optvalnoeq")
                             .long_("optvalnoeq")
                             .value_name("optval")
                             .help("Tests optional value")
                             .num_args(value_range{0, 1}))
                .subcommand(command_builder("subcmd")
                                    .about("tests subcommands")
                                    .version("0.1")
                                    .author("Kevin K. <kbknapp@gmail.com>")
                                    .help_template(full_template)
                                    .arg(arg_builder("option")
                                                 .short_('o')
                                                 .long_("option")
                                                 .value_name("scoption")
                                                 .help("tests options")
                                                 .num_args(value_range::at_least(1)))
                                    .arg(arg_builder("subcmdarg")
                                                 .short_('s')
                                                 .long_("subcmdarg")
                                                 .value_name("subcmdarg")
                                                 .help("tests other args")
                                                 .action(arg_action::set))
                                    .arg(arg_builder("scpositional")
                                                 .index(1)
                                                 .help("tests positionals")));
        return app.freeze();
    }
    constexpr command_spec complex_app = make_complex_app();

    /** clap's `no_wrap_default_help`: `Command::new("ctest").version("1.0").term_width(0)`. */
    consteval command_spec make_no_wrap_default() {
        command_builder app("ctest");
        std::move(app).version("1.0").term_width(0);
        return app.freeze();
    }
    constexpr command_spec no_wrap_default = make_no_wrap_default();

    /**
     * clap's `no_wrap_help`: the whole page is supplied by the author, so nothing about the
     * command but `override_help()` can reach the output.
     */
    constexpr std::string_view multi_sc_help = "tests subcommands\n"
                                               "\n"
                                               "Usage: ctest subcmd multi [OPTIONS]\n"
                                               "\n"
                                               "Options:\n"
                                               "  -f, --flag                  tests flags\n"
                                               "  -o, --option <scoption>...  tests options\n"
                                               "  -h, --help                  Print help\n"
                                               "  -V, --version               Print version\n";

    consteval command_spec make_no_wrap_override() {
        command_builder app("ctest");
        std::move(app).term_width(0).override_help(multi_sc_help);
        return app.freeze();
    }
    constexpr command_spec no_wrap_override = make_no_wrap_override();

    /** clap's `unwrapped_help`, at `term_width(68)`. */
    consteval command_spec make_unwrapped() {
        command_builder app("test");
        std::move(app)
                .term_width(68)
                .arg(arg_builder("all")
                             .short_('a')
                             .long_("all")
                             .action(arg_action::set_true)
                             .help("Also do versioning for private crates (will not be published)"))
                .arg(arg_builder("exact")
                             .long_("exact")
                             .action(arg_action::set_true)
                             .help("Specify inter dependency version numbers exactly with `=`"))
                .arg(arg_builder("no_git_commit")
                             .long_("no-git-commit")
                             .action(arg_action::set_true)
                             .help("Do not commit version changes"))
                .arg(arg_builder("no_git_push")
                             .long_("no-git-push")
                             .action(arg_action::set_true)
                             .help("Do not push generated commit and tags to git remote"))
                .subcommand(command_builder("sub1").about(
                        "One two three four five six seven eight nine ten eleven twelve "
                        "thirteen fourteen fifteen"));
        return app.freeze();
    }
    constexpr command_spec unwrapped = make_unwrapped();

    /**
     * clap's `ripgrep_usage` override string. The leading `rg` sits on the first line and
     * every continuation carries the seven spaces the author typed — that indentation is
     * part of the data, not something the renderer supplies.
     */
    constexpr std::string_view ripgrep_override_usage =
            "rg [OPTIONS] <pattern> [<path> ...]\n"
            "       rg [OPTIONS] [-e PATTERN | -f FILE ]... [<path> ...]\n"
            "       rg [OPTIONS] --files [<path> ...]\n"
            "       rg [OPTIONS] --type-list";

    consteval command_spec make_ripgrep() {
        command_builder app("ripgrep");
        std::move(app).version("0.5").override_usage(ripgrep_override_usage);
        return app.freeze();
    }
    constexpr command_spec ripgrep = make_ripgrep();

    /**
     * clap's `ripgrep_usage_using_templates`. Rust's `"\<newline>` escape strips the newline
     * and the following indentation, so clap's override string is byte-identical to the one
     * above; only the template differs.
     */
    consteval command_spec make_ripgrep_tmpl() {
        command_builder app("ripgrep");
        std::move(app)
                .version("0.5")
                .override_usage(ripgrep_override_usage)
                .help_template("{bin} {version}\n"
                               "\n"
                               "Usage: {usage}\n"
                               "\n"
                               "Options:\n"
                               "{options}");
        return app.freeze();
    }
    constexpr command_spec ripgrep_tmpl = make_ripgrep_tmpl();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    // ---------------------------------------------------------------------------

    // None of these six commands has any long-form content, so `-h` and `--help` are the
    // same page for every one of them. Asserting it here is what lets each case below assert
    // once instead of twice, and it is the thing that would change first if a fixture grew a
    // `long_about` by accident.
    static_assert(!clapp::long_help_exists(complex_app));
    static_assert(!clapp::long_help_exists(no_wrap_default));
    static_assert(!clapp::long_help_exists(unwrapped));
    static_assert(!clapp::long_help_exists(ripgrep));
    static_assert(!clapp::long_help_exists(ripgrep_tmpl));

    // DIVERGENCE 1, pinned rather than described. clap would report `Some(0)`; clapp reports
    // "unset", so the two `no_wrap_*` cases below do NOT exercise "never wrap". If anyone
    // ever removes the length sentinel, this line fails first and points at the two cases
    // whose expectations should then be re-derived from clap.
    static_assert(no_wrap_default.get_term_width() == std::optional<std::size_t>{});
    static_assert(no_wrap_override.get_term_width() == std::optional<std::size_t>{});

    // `unwrapped_help`'s width, by contrast, IS expressible and is the whole point of the
    // case — every wrap position in its expected page is a function of this number.
    static_assert(unwrapped.get_term_width() == std::optional<std::size_t>{68});

    // The template is what makes `complex_help_output` print an author line; the default
    // layout never would.
    static_assert(complex_app.get_help_template().has_value());
    static_assert(complex_app.get_author() == std::string_view{"Kevin K. <kbknapp@gmail.com>"});

    // `override_help` short-circuits the whole renderer, which is the only reason
    // `no_wrap_help` can assert a page listing arguments the command does not have.
    static_assert(no_wrap_override.get_override_help().has_value());
    static_assert(!no_wrap_override.has_arg("flag"));

}  // namespace

// ---------------------------------------------------------------------------
// The widest page clap owns
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::complex_help_output") {
    // Five arity placeholders on one page, and the option column is measured across all
    // of them: `--minvals2 <minvals> <minvals>...` is the longest, so every description
    // in the block hangs off its width. The two possible-value lists are inline, because
    // no value carries help of its own.
    CLAPP_CHECK(same(
            page(complex_app, true),
            "clap-test v1.4.8\n"
            "Kevin K. <kbknapp@gmail.com>\n"
            "tests clap library\n"
            "\n"
            "Usage: clap-test [OPTIONS] [positional] [positional2] [positional3]... [COMMAND]\n"
            "\n"
            "Commands:\n"
            "  subcmd  tests subcommands\n"
            "  help    Print this message or the help of the given subcommand(s)\n"
            "\n"
            "Arguments:\n"
            "  [positional]      tests positionals\n"
            "  [positional2]     tests positionals with exclusions\n"
            "  [positional3]...  tests specific values [possible values: vi, emacs]\n"
            "\n"
            "Options:\n"
            "  -o, --option <opt>...                  tests options\n"
            "  -f, --flag...                          tests flags\n"
            "  -F                                     tests flags with exclusions\n"
            "      --long-option-2 <option2>          tests long options with exclusions\n"
            "  -O, --option3 <option3>                specific vals [possible values: fast, "
            "slow]\n"
            "      --multvals <one> <two>             Tests multiple values, not mult occs\n"
            "      --multvalsmo <one> <two>           Tests multiple values, and mult occs\n"
            "      --minvals2 <minvals> <minvals>...  Tests 2 min vals\n"
            "      --maxvals3 <maxvals>...            Tests 3 max vals\n"
            "      --optvaleq[=<optval>]              Tests optional value, require = sign\n"
            "      --optvalnoeq [<optval>]            Tests optional value\n"
            "  -h, --help                             Print help\n"
            "  -V, --version                          Print version\n"));
}

// ---------------------------------------------------------------------------
// term_width(0) — see DIVERGENCE 1
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::no_wrap_default_help") {
    CLAPP_CHECK(same(page(no_wrap_default, true),
                     "Usage: ctest\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help.rs::no_wrap_help") {
    // The command has no arguments beyond the injected pair and no subcommands at all;
    // the page below is entirely the author's string. That is the assertion: an
    // `override_help()` reaches the output without the renderer touching a byte of it —
    // no re-wrapping, no re-alignment, no appended `Options:`. Only the trailing newline
    // is the renderer's, and the input already ends in one.
    CLAPP_CHECK(same(page(no_wrap_override, true), multi_sc_help));
}

// ---------------------------------------------------------------------------
// Wrapping at a fixed width
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::unwrapped_help") {
    // Two blocks, two left margins, one width. `sub1`'s about wraps after "eleven" at
    // column 8; the options wrap after "will" at column 23. A single global hanging
    // indent gets one of the two right and looks correct doing it.
    CLAPP_CHECK(same(page(unwrapped, true),
                     "Usage: test [OPTIONS] [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  sub1  One two three four five six seven eight nine ten eleven\n"
                     "        twelve thirteen fourteen fifteen\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -a, --all            Also do versioning for private crates (will\n"
                     "                       not be published)\n"
                     "      --exact          Specify inter dependency version numbers\n"
                     "                       exactly with `=`\n"
                     "      --no-git-commit  Do not commit version changes\n"
                     "      --no-git-push    Do not push generated commit and tags to git\n"
                     "                       remote\n"
                     "  -h, --help           Print help\n"));
}

// ---------------------------------------------------------------------------
// override_usage(), through both layouts
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::ripgrep_usage") {
    // The automatic layout: `Usage:` is the renderer's heading, everything after it is
    // the author's four lines, indentation included. Note the command has neither
    // arguments nor subcommands of its own — had the generator been consulted, the usage
    // would read `Usage: ripgrep` and nothing else.
    CLAPP_CHECK(same(page(ripgrep, true),
                     "Usage: rg [OPTIONS] <pattern> [<path> ...]\n"
                     "       rg [OPTIONS] [-e PATTERN | -f FILE ]... [<path> ...]\n"
                     "       rg [OPTIONS] --files [<path> ...]\n"
                     "       rg [OPTIONS] --type-list\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help.rs::ripgrep_usage_using_templates") {
    // The template layout, same override. Three separate things are pinned here that the
    // case above cannot see: `{usage}` yields the BODY only (the `Usage: ` prefix is the
    // template author's own literal), `{bin}` falls back to the command's name because
    // `bin_name` was never set, and `{options}` emits rows with no heading of its own —
    // `Options:` on the line above is again the author's.
    CLAPP_CHECK(same(page(ripgrep_tmpl, true),
                     "ripgrep 0.5\n"
                     "\n"
                     "Usage: rg [OPTIONS] <pattern> [<path> ...]\n"
                     "       rg [OPTIONS] [-e PATTERN | -f FILE ]... [<path> ...]\n"
                     "       rg [OPTIONS] --files [<path> ...]\n"
                     "       rg [OPTIONS] --type-list\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}
