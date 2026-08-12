#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    /**
     * clap's three `parent_cmd_req_in_usage_*` cases share one command verbatim.
     *
     * `Arg::new("TARGET").required(true)` and `Arg::new("ARGS").action(Set).required(true)`
     * are both POSITIONALS in clap — neither has a `short`/`long`, and `Set` is already the
     * default action, so the `.action(ArgAction::Set)` in clap's source changes nothing about
     * this arg and is reproduced here only for fidelity. clapp requires the index to be
     * explicit, hence `.index(1)` / `.index(2)`; declaration order fixes it in clap.
     */
    consteval command_spec make_parent() {
        command_builder app("parent");
        std::move(app)
                .version("0.1")
                .arg(arg_builder("TARGET").index(1).required().help("some"))
                .arg(arg_builder("ARGS")
                             .index(2)
                             .required()
                             .help("some")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .subcommand(command_builder("test").about("some"));
        return app.freeze();
    }
    constexpr command_spec parent = make_parent();

    /**
     * clap's `ctest` for both negative cases. \p conflicts picks which of the two settings
     * is under test; everything else is identical, which is the point (see the file header).
     */
    consteval command_spec make_ctest(bool conflicts) {
        command_builder app("ctest");
        std::move(app).arg(arg_builder("input").index(1).required());
        if (conflicts)
            std::move(app).args_conflicts_with_subcommands();
        else
            std::move(app).subcommand_negates_reqs();
        std::move(app).subcommand(command_builder("subcmd"));
        return app.freeze();
    }
    constexpr command_spec negates_reqs   = make_ctest(false);
    constexpr command_spec conflicts_reqs = make_ctest(true);

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // The suppression rule reads two flags, and `freeze()` couples them. Pin the coupling
    // here so that a failure in the two negative cases below can be read as a *renderer*
    // failure rather than a builder one.
    // ---------------------------------------------------------------------------

    // The positive fixture has neither flag: this is why its child usage splices.
    static_assert(!parent.is_subcommand_negates_reqs_set());
    static_assert(!parent.is_args_conflicts_with_subcommands_set());

    // `subcommand_negates_reqs()` alone does NOT imply the conflicts flag...
    static_assert(negates_reqs.is_subcommand_negates_reqs_set());
    static_assert(!negates_reqs.is_args_conflicts_with_subcommands_set());

    // ...but `args_conflicts_with_subcommands()` DOES imply negates — command.hpp:2042. So
    // the two negative cases exercise the two arms of the `||` in
    // `subcommand_usage_name()`, not the same arm twice.
    static_assert(conflicts_reqs.is_subcommand_negates_reqs_set());
    static_assert(conflicts_reqs.is_args_conflicts_with_subcommands_set());

    // The parent's required set is what gets spliced; both must be required, or the expected
    // `Usage: parent <TARGET> <ARGS> test` would be right for the wrong reason.
    static_assert(parent.has_arg("TARGET"));
    static_assert(parent.has_arg("ARGS"));
    static_assert(parent.find_arg("TARGET")->is_required_set());
    static_assert(parent.find_arg("ARGS")->is_required_set());
    static_assert(parent.has_subcommand("test"));

    // Neither child has any long content, so `-h` and `--help` must render the same page —
    // the reason clap's three positive cases can assert one expected string across a
    // `--help` flag, a `help` subcommand (long form) and a bare `render_help()`.
    static_assert(!clapp::long_help_exists(*parent.find_subcommand("test")));
    static_assert(!clapp::long_help_exists(*negates_reqs.find_subcommand("subcmd")));

    /** The one page all three positive cases must produce. */
    constexpr std::string_view parent_child_page = "some\n"
                                                   "\n"
                                                   "Usage: parent <TARGET> <ARGS> test\n"
                                                   "\n"
                                                   "Options:\n"
                                                   "  -h, --help  Print help\n";

    /**
     * The one page both negative cases must produce. Note the absent leading `about` block:
     * clap's `subcmd` has none, so the page starts straight at `Usage:`.
     */
    constexpr std::string_view ctest_child_page = "Usage: ctest subcmd\n"
                                                  "\n"
                                                  "Options:\n"
                                                  "  -h, --help  Print help\n";

}  // namespace

// ---------------------------------------------------------------------------
// The parent's requirements reach the child's usage line — three routes
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::parent_cmd_req_in_usage_with_help_flag") {
    const outcome got = clapp::parse(parent, raw_args{"parent", "test", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), parent_child_page));
}

CLAPP_TEST("help.rs::parent_cmd_req_in_usage_with_help_subcommand") {
    const outcome got = clapp::parse(parent, raw_args{"parent", "help", "test"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), parent_child_page));
}

CLAPP_TEST("help.rs::parent_cmd_req_in_usage_with_render_help") {
    // clap: `cmd.build(); cmd.find_subcommand_mut("test").unwrap().render_help()`.
    // `build()` stamps `usage_name` onto the child; ADR-0005 freezes clapp's tree, so the
    // string is computed instead of stored. `child_usage_name()` is the same function the
    // parse loop calls (parse.hpp:757), which is what makes this a port of clap's case
    // rather than a hand-written expectation.
    const std::string usage_name = clapp::detail::child_usage_name(
            parent, parent.get_name(), *parent.find_subcommand("test"));
    CLAPP_CHECK(same(usage_name, "parent <TARGET> <ARGS> test"));

    const std::string help =
            clapp::render_help(*parent.find_subcommand("test"),
                               help_style{.use_long = false, .usage_name = usage_name})
                    .to_string();
    CLAPP_CHECK(same(help, parent_child_page));
}

// ---------------------------------------------------------------------------
// ...unless reaching the subcommand cancels them — two settings, two arms
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::parent_cmd_req_ignored_when_negates_reqs") {
    const outcome got = clapp::parse(negates_reqs, raw_args{"ctest", "subcmd", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), ctest_child_page));
}

CLAPP_TEST("help.rs::parent_cmd_req_ignored_when_conflicts") {
    const outcome got = clapp::parse(conflicts_reqs, raw_args{"ctest", "subcmd", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got), ctest_child_page));
}

// ---------------------------------------------------------------------------
// The negative direction of the same rule
//
// Trap 17's lesson, applied here: half of a wrong answer still looks right. A renderer
// that NEVER splices the parent's requirements passes both negative cases above, and a
// renderer that ALWAYS splices passes all three positive ones. Only asserting that the
// same shape of command produces two DIFFERENT usage lines depending on one flag can
// separate them — so pin the difference itself, on the same child name and the same
// parent requirement.
// ---------------------------------------------------------------------------

namespace {

    /**
     * `ctest` with a required positional and neither suppression flag — the control for the
     * two negative cases. Same name, same child, same requirement; only the flag differs.
     */
    consteval command_spec make_ctest_plain() {
        command_builder app("ctest");
        std::move(app)
                .arg(arg_builder("input").index(1).required())
                .subcommand(command_builder("subcmd"));
        return app.freeze();
    }
    constexpr command_spec plain_reqs = make_ctest_plain();

}  // namespace

CLAPP_TEST("help.rs::parent_cmd_req_ignored_when_negates_reqs (differential control)") {
    const outcome got = clapp::parse(plain_reqs, raw_args{"ctest", "subcmd", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    // The SAME command minus the flag splices `<input>` in. If this line ever equals
    // `ctest_child_page`, the two cases above have stopped testing anything.
    //
    // This expectation is not clapp's output written down — it is clap's. Measured
    // 2026-08-10 against clap 4.6.5 from /Users/quincy/Desktop/clap as a local dependency,
    // running clap's own `cmd.build()` + `render_help()` on exactly
    // this command; clap prints `Usage: ctest <input> subcmd` and clapp reproduces it byte
    // for byte. The same run re-confirmed `parent_child_page` above.
    CLAPP_CHECK(same(message_of(got),
                     "Usage: ctest <input> subcmd\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
    CLAPP_CHECK(message_of(got) != ctest_child_page);
}
