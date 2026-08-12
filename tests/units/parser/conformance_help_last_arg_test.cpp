#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

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
     *        Identical to conformance_help_test.cpp's helper of the same name.
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
    // Fixtures
    //
    // The four `last_*` commands are one shape with two independent switches, exactly as
    // clap's four cases are: whether the `last()` argument is required, and which of the two
    // subcommand-negation settings (if either) is on. They are built from one function so that
    // nothing but those switches can differ between them — if the fixtures were written out
    // four times, a stray `.help()` in one of them would look like a renderer difference.
    // ---------------------------------------------------------------------------

    /** How the fixture treats subcommands — clap's two settings plus "no subcommand at all". */
    enum class sc_mode { none, negates_reqs, args_conflict };

    consteval command_spec make_last(bool args_required, sc_mode mode) {
        command_builder app("last");
        std::move(app).version("0.1");
        if (mode == sc_mode::negates_reqs) std::move(app).subcommand_negates_reqs();
        if (mode == sc_mode::args_conflict) std::move(app).args_conflicts_with_subcommands();

        std::move(app)
                .arg(arg_builder("TARGET").required().help("some"))
                .arg(arg_builder("CORPUS").help("some"));

        arg_builder args("ARGS");
        std::move(args)
                .action(arg_action::set)
                .num_args(value_range::at_least(1))
                .last()
                .help("some");
        if (args_required) std::move(args).required();
        std::move(app).arg(std::move(args));

        if (mode != sc_mode::none) std::move(app).subcommand(command_builder("test").about("some"));
        return app.freeze();
    }

    constexpr command_spec last_mult         = make_last(false, sc_mode::none);
    constexpr command_spec last_mult_req     = make_last(true, sc_mode::none);
    constexpr command_spec last_mult_req_sc  = make_last(true, sc_mode::negates_reqs);
    constexpr command_spec last_mult_with_sc = make_last(false, sc_mode::args_conflict);

    /**
     * clap's `sc_negates_reqs` and `args_negate_sc` differ in more than the setting — the
     * first has a *required* `--opt` and no `--flag`, the second has an optional `--opt` plus
     * a `--flag` — so they are built separately, as clap builds them.
     */
    consteval command_spec make_sc_negates_reqs() {
        command_builder app("prog");
        std::move(app)
                .version("1.0")
                .subcommand_negates_reqs()
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .value_name("FILE")
                             .help("tests options")
                             .required()
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("PATH").help("help"))
                .subcommand(command_builder("test"));
        return app.freeze();
    }
    constexpr command_spec sc_negates_reqs = make_sc_negates_reqs();

    /**
     * clap's `args_negate_sc`, and — with `hidden_sc` true and the setting dropped —
     * `issue_1046_hide_scs`. The two commands are otherwise the same, which is what makes the
     * second one a test of hiding rather than of layout. clap's only other difference is the
     * PATH help text (`"help"` vs `"some"`); it is carried faithfully.
     */
    consteval command_spec make_args_negate_sc(bool hidden_sc) {
        command_builder app("prog");
        std::move(app).version("1.0");
        if (!hidden_sc) std::move(app).args_conflicts_with_subcommands();
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .help("testing flags"))
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .value_name("FILE")
                             .help("tests options")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("PATH").help(hidden_sc ? "some" : "help"))
                .subcommand(command_builder("test").hide(hidden_sc));
        return app.freeze();
    }
    constexpr command_spec args_negate_sc = make_args_negate_sc(false);
    constexpr command_spec hide_scs_1046  = make_args_negate_sc(true);

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // These pin the *builder* half of the fork the screens below test, so that a failure in
    // one of the CLAPP_TESTs can be attributed to the renderer rather than to the setting
    // never having been recorded. In particular: args_conflicts_with_subcommands() implies
    // subcommand_negates_reqs() (command.hpp:2043), so the two are NOT interchangeable and the
    // implication only runs one way.
    // ---------------------------------------------------------------------------

    static_assert(sc_negates_reqs.is_subcommand_negates_reqs_set());
    static_assert(!sc_negates_reqs.is_args_conflicts_with_subcommands_set());
    static_assert(args_negate_sc.is_args_conflicts_with_subcommands_set());
    static_assert(args_negate_sc.is_subcommand_negates_reqs_set());  // implied, not typed

    // `issue_1046_hide_scs` sets neither — its second usage line is absent because the
    // subcommand is hidden, not because a negation setting removed it.
    static_assert(!hide_scs_1046.is_subcommand_negates_reqs_set());
    static_assert(!hide_scs_1046.is_args_conflicts_with_subcommands_set());

    // None of these seven commands has any long-form content, so `--help` and `-h` must print
    // the same screen. That is why every case below can be written with one expectation.
    // All seven, not four: the claim is "every case below may be written with a single
    // expectation", and it is only backed for the specs it actually names.
    static_assert(!clapp::long_help_exists(last_mult));
    static_assert(!clapp::long_help_exists(last_mult_req));
    static_assert(!clapp::long_help_exists(last_mult_req_sc));
    static_assert(!clapp::long_help_exists(last_mult_with_sc));
    static_assert(!clapp::long_help_exists(sc_negates_reqs));
    static_assert(!clapp::long_help_exists(args_negate_sc));
    static_assert(!clapp::long_help_exists(hide_scs_1046));

}  // namespace

// ---------------------------------------------------------------------------
// last() in the usage line
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::last_arg_mult_usage") {
    // Optional `last()`: the brackets wrap the separator AND the value — `[-- <ARGS>...]`,
    // never `-- [<ARGS>...]`. The table row is `[ARGS]...`, i.e. optional-with-ellipsis,
    // which is a different spelling of the same fact.
    CLAPP_CHECK(same(page(last_mult, true),
                     "Usage: last <TARGET> [CORPUS] [-- <ARGS>...]\n"
                     "\n"
                     "Arguments:\n"
                     "  <TARGET>   some\n"
                     "  [CORPUS]   some\n"
                     "  [ARGS]...  some\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help.rs::last_arg_mult_usage_req") {
    // The same command with `.required()` added to ARGS and nothing else changed: the
    // brackets disappear from the usage line and the table row flips to `<ARGS>...`.
    CLAPP_CHECK(same(page(last_mult_req, true),
                     "Usage: last <TARGET> [CORPUS] -- <ARGS>...\n"
                     "\n"
                     "Arguments:\n"
                     "  <TARGET>   some\n"
                     "  [CORPUS]   some\n"
                     "  <ARGS>...  some\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help.rs::last_arg_mult_usage_req_with_sc") {
    // `subcommand_negates_reqs`: the second line keeps both positionals but demotes
    // `<TARGET>` to `[TARGET]`, and drops the `last()` argument entirely — a subcommand
    // invocation cannot also carry the `--` tail. Note the table still says `<TARGET>`:
    // requiredness is a property of the argument, bracketing a property of the line.
    CLAPP_CHECK(same(page(last_mult_req_sc, true),
                     "Usage: last <TARGET> [CORPUS] -- <ARGS>...\n"
                     "       last [TARGET] [CORPUS] <COMMAND>\n"
                     "\n"
                     "Commands:\n"
                     "  test  some\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Arguments:\n"
                     "  <TARGET>   some\n"
                     "  [CORPUS]   some\n"
                     "  <ARGS>...  some\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("help.rs::last_arg_mult_usage_with_sc") {
    // `args_conflicts_with_subcommands`: the second line drops the positionals too, so it
    // is bare `last <COMMAND>`. Compare with the case directly above — same command shape,
    // same subcommand, one different setting, and the whole second line changes.
    CLAPP_CHECK(same(page(last_mult_with_sc, true),
                     "Usage: last <TARGET> [CORPUS] [-- <ARGS>...]\n"
                     "       last <COMMAND>\n"
                     "\n"
                     "Commands:\n"
                     "  test  some\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Arguments:\n"
                     "  <TARGET>   some\n"
                     "  [CORPUS]   some\n"
                     "  [ARGS]...  some\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

// ---------------------------------------------------------------------------
// The two negation settings, without last()
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::sc_negates_reqs") {
    // A *required* option vanishes from the subcommand line while the optional positional
    // survives it — the setting negates requirements, it does not hide arguments. `test`
    // has no `about`, so its cell is empty and the two trailing spaces after it are the
    // padding to the description column, which clap emits too.
    CLAPP_CHECK(same(page(sc_negates_reqs, true),
                     "Usage: prog --opt <FILE> [PATH]\n"
                     "       prog [PATH] <COMMAND>\n"
                     "\n"
                     "Commands:\n"
                     "  test  \n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Arguments:\n"
                     "  [PATH]  help\n"
                     "\n"
                     "Options:\n"
                     "  -o, --opt <FILE>  tests options\n"
                     "  -h, --help        Print help\n"
                     "  -V, --version     Print version\n"));
}

CLAPP_TEST("help.rs::args_negate_sc") {
    // The same two-line shape, but the second line is bare: `args_conflicts_with_subcommands`
    // removes `[PATH]` as well, which `sc_negates_reqs` above keeps.
    CLAPP_CHECK(same(page(args_negate_sc, true),
                     "Usage: prog [OPTIONS] [PATH]\n"
                     "       prog <COMMAND>\n"
                     "\n"
                     "Commands:\n"
                     "  test  \n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Arguments:\n"
                     "  [PATH]  help\n"
                     "\n"
                     "Options:\n"
                     "  -f, --flag        testing flags\n"
                     "  -o, --opt <FILE>  tests options\n"
                     "  -h, --help        Print help\n"
                     "  -V, --version     Print version\n"));
}

CLAPP_TEST("help.rs::issue_1046_hide_scs") {
    // Hiding the only user subcommand removes three things at once: the second usage line,
    // the whole `Commands:` block (including the automatic `help` entry, which must not
    // survive alone), and the `[COMMAND]` slot from the first usage line. A renderer that
    // decides on `[COMMAND]` from "does this command have subcommands" rather than from
    // "does it have any *visible* subcommands" prints `Usage: prog [OPTIONS] [PATH] [COMMAND]`
    // here and is otherwise indistinguishable. clapp gets it right at
    // include/clapp/output/usage.hpp:982, `!cmd_->has_visible_subcommands()`.
    CLAPP_CHECK(same(page(hide_scs_1046, true),
                     "Usage: prog [OPTIONS] [PATH]\n"
                     "\n"
                     "Arguments:\n"
                     "  [PATH]  some\n"
                     "\n"
                     "Options:\n"
                     "  -f, --flag        testing flags\n"
                     "  -o, --opt <FILE>  tests options\n"
                     "  -h, --help        Print help\n"
                     "  -V, --version     Print version\n"));
}
