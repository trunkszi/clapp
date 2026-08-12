#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
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

    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        Identical to the helper in conformance_help_test.cpp; see the note there.
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
     * \brief The page that actually reaches the terminal, driven through `clapp::parse()`.
     *
     * AUDIT NOTE (adversarial fidelity pass). `page()` above applies the
     * `use_long && long_help_exists()` collapse *in this file*, so `page(spec, true)` and
     * `page(spec, false)` are equal by construction of the helper — the test's own
     * arithmetic, not clapp's. The claim in the header comment ("an implementation that
     * wires `--help` straight to the long renderer passes every case that only checks
     * `-h`") is therefore not held by `page()` at all: the collapse under test lives in
     * `render_help_text()` (parser/parse.hpp), on the parse path, and only a parse-path
     * comparison can see it. The kind-only `error_kind::display_help` check the file had
     * is exactly the weakened shape this project has shipped before. Both `-h` and
     * `--help` now go through `clapp::parse()` and the whole page is compared, which is
     * also what clap's own `utils::assert_output` harness does.
     */
    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // clap's `Arg::new("foo")` with neither `short` nor `long` is a positional whose index
    // clap assigns.
    //
    // clapp does not require an explicit positional index. With GCC 16.1.0, the `given`
    //     fixture below with `.index(1)` dropped renders `Usage: myapp [foo]` and the same
    // `Arguments:` block, byte for byte. An argument with neither short nor long is
    //     auto-positional in clapp exactly as in clap.
    // The `.index()` calls are kept because they are harmless and explicit.
    //
    // Each `freeze()` below IS the port of the corresponding `#[should_panic]`'s negation: it
    // is `consteval`, so if `check_help_expected()` ever started rejecting one of these three
    // accepted shapes, this file would stop compiling rather than stop passing.
    // ---------------------------------------------------------------------------

    /** clap's `help_required_and_given_for_subcommand`. */
    consteval command_spec make_given_for_subcommand() {
        command_builder app("myapp");
        std::move(app)
                .help_expected()
                .arg(arg_builder("foo").index(1).help("It does foo stuff"))
                .subcommand(command_builder{"bar"}
                                    .arg(arg_builder("create").index(1).help("creates bar"))
                                    .arg(arg_builder("delete").index(2).help("deletes bar")));
        return app.freeze();
    }
    constexpr command_spec given_for_subcommand = make_given_for_subcommand();

    /** clap's `help_required_and_given`. */
    consteval command_spec make_given() {
        command_builder app("myapp");
        std::move(app).help_expected().arg(arg_builder("foo").index(1).help("It does foo stuff"));
        return app.freeze();
    }
    constexpr command_spec given = make_given();

    /** clap's `help_required_and_no_args` — no author-declared argument at all. */
    consteval command_spec make_no_args() {
        command_builder app("myapp");
        std::move(app).help_expected();
        return app.freeze();
    }
    constexpr command_spec no_args = make_no_args();

    // The setting is on, and it is global: the child inherited it. Pinned on the frozen tree
    // because it has no rendered consequence — its only effect is to reject, and the shape
    // that would be rejected cannot be written in this file without stopping the build.
    // Asserted on BOTH sides, per CLAUDE.md trap 10's rule about flags with no accessor: the
    // parent that set it and the child that was never told.
    static_assert(given_for_subcommand.is_help_expected_set());
    static_assert(given.is_help_expected_set());
    static_assert(no_args.is_help_expected_set());
    static_assert(given_for_subcommand.has_subcommand("bar"));
    static_assert(given_for_subcommand.find_subcommand("bar")->is_help_expected_set());
    // The generated `help` subcommand inherits it as well, and freezes — which is the
    // no-argument case again, one level down.
    static_assert(given_for_subcommand.find_subcommand("help")->is_help_expected_set());

    // Neither `-h` nor `--help` has anything long to show, so the two are the same screen.
    // This is what makes asserting both meaningful rather than redundant: it is a claim, and
    // it is the claim that fails first if the injected `--help` ever grows a long form.
    static_assert(!clapp::long_help_exists(given_for_subcommand));
    static_assert(!clapp::long_help_exists(given));
    static_assert(!clapp::long_help_exists(no_args));

}  // namespace

// ---------------------------------------------------------------------------
// help.rs::help_required_and_given_for_subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::help_required_and_given_for_subcommand") {
    // clap's assertion, verbatim: `try_get_matches_from(empty_args()).unwrap()`.
    const outcome got = clapp::parse(given_for_subcommand, raw_args{});
    CLAPP_CHECK(got.has_value());

    // Measured from clap 4.6.5. `  bar   ` carries three trailing spaces because `bar`
    // has no `about` and the help column is still reserved — clap pads the row rather
    // than trimming it, and so must clapp.
    const std::string_view expected = "Usage: myapp [foo] [COMMAND]\n"
                                      "\n"
                                      "Commands:\n"
                                      "  bar   \n"
                                      "  help  Print this message or the help of the given "
                                      "subcommand(s)\n"
                                      "\n"
                                      "Arguments:\n"
                                      "  [foo]  It does foo stuff\n"
                                      "\n"
                                      "Options:\n"
                                      "  -h, --help  Print help\n";
    CLAPP_CHECK(same(page(given_for_subcommand, false), expected));
    CLAPP_CHECK(same(page(given_for_subcommand, true), expected));

    // The same two screens through the parse path, which is where clapp's own
    // `use_long &&= long_help_exists()` collapse lives. Measured on clap 4.6.5:
    // `try_get_matches_from(["myapp", "--help"])` renders the SHORT screen here, byte
    // for byte identical to `-h`, even though `render_long_help()` on this command
    // differs (it forces next-line layout for `[foo]` and `-h, --help`).
    const outcome short_form = clapp::parse(given_for_subcommand, raw_args{"myapp", "-h"});
    CLAPP_CHECK(!short_form.has_value());
    CLAPP_CHECK(short_form.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(short_form), expected));

    const outcome long_form = clapp::parse(given_for_subcommand, raw_args{"myapp", "--help"});
    CLAPP_CHECK(!long_form.has_value());
    CLAPP_CHECK(long_form.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(long_form), expected));
}

CLAPP_TEST("help.rs::help_required_and_given_for_subcommand (the subcommand's own page)") {
    // The half the case is named for. `bar`'s two positionals are documented for a reason
    // no top-level check would need, and this is where that documentation surfaces.
    const std::string_view expected = "Usage: myapp bar [create] [delete]\n"
                                      "\n"
                                      "Arguments:\n"
                                      "  [create]  creates bar\n"
                                      "  [delete]  deletes bar\n"
                                      "\n"
                                      "Options:\n"
                                      "  -h, --help  Print help\n";
    const command_spec& bar         = *given_for_subcommand.find_subcommand("bar");
    CLAPP_CHECK(same(page(bar, false, "myapp bar"), expected));
    CLAPP_CHECK(same(page(bar, true, "myapp bar"), expected));

    // Through the parse path, where the usage prefix `myapp bar` has to be assembled by
    // clapp rather than handed to the renderer by this test. Both forms measured on
    // clap 4.6.5 (`["myapp","bar","-h"]` and `["myapp","bar","--help"]`) — identical.
    const outcome bar_short = clapp::parse(given_for_subcommand, raw_args{"myapp", "bar", "-h"});
    CLAPP_CHECK(!bar_short.has_value());
    CLAPP_CHECK(bar_short.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(bar_short), expected));

    const outcome bar_long = clapp::parse(given_for_subcommand, raw_args{"myapp", "bar", "--help"});
    CLAPP_CHECK(!bar_long.has_value());
    CLAPP_CHECK(bar_long.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(bar_long), expected));

    // And `myapp bar` parses with no arguments, exactly as the root does.
    CLAPP_CHECK(clapp::parse(given_for_subcommand, raw_args{"", "bar"}).has_value());
}

// ---------------------------------------------------------------------------
// help.rs::help_required_and_given
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::help_required_and_given") {
    const outcome got = clapp::parse(given, raw_args{});
    CLAPP_CHECK(got.has_value());

    const std::string_view expected = "Usage: myapp [foo]\n"
                                      "\n"
                                      "Arguments:\n"
                                      "  [foo]  It does foo stuff\n"
                                      "\n"
                                      "Options:\n"
                                      "  -h, --help  Print help\n";
    CLAPP_CHECK(same(page(given, false), expected));
    CLAPP_CHECK(same(page(given, true), expected));

    // The help flag still works on a `help_expected()` command — it is an argument like
    // any other and the setting did not disturb it. The kind alone does not say that:
    // it has to be the whole screen, and it has to be the SHORT screen for `--help`.
    const outcome asked_short = clapp::parse(given, raw_args{"myapp", "-h"});
    CLAPP_CHECK(!asked_short.has_value());
    CLAPP_CHECK(asked_short.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(asked_short), expected));

    const outcome asked = clapp::parse(given, raw_args{"myapp", "--help"});
    CLAPP_CHECK(!asked.has_value());
    CLAPP_CHECK(asked.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(asked), expected));
}

// ---------------------------------------------------------------------------
// help.rs::help_required_and_no_args
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::help_required_and_no_args") {
    // The case that proves the injected `--help` satisfies its own rule: this command's
    // only argument is the one clapp generated, and `freeze()` accepted it.
    const outcome got = clapp::parse(no_args, raw_args{});
    CLAPP_CHECK(got.has_value());

    const std::string_view expected = "Usage: myapp\n"
                                      "\n"
                                      "Options:\n"
                                      "  -h, --help  Print help\n";
    CLAPP_CHECK(same(page(no_args, false), expected));
    CLAPP_CHECK(same(page(no_args, true), expected));

    // The injected `--help` is also the only thing that can FIRE here, so the parse-path
    // comparison is the one that shows the injected flag is wired, not merely counted by
    // `check_help_expected()`. Measured on clap 4.6.5: same screen for both forms.
    const outcome injected_short = clapp::parse(no_args, raw_args{"myapp", "-h"});
    CLAPP_CHECK(!injected_short.has_value());
    CLAPP_CHECK(injected_short.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(injected_short), expected));

    const outcome injected_long = clapp::parse(no_args, raw_args{"myapp", "--help"});
    CLAPP_CHECK(!injected_long.has_value());
    CLAPP_CHECK(injected_long.error().kind() == error_kind::display_help);
    CLAPP_CHECK(same(message_of(injected_long), expected));
}
