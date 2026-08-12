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

    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        See the fuller note in conformance_hidden_args_test.cpp.
     */
    std::string page(const command_spec& cmd, bool long_form) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd)})
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
    // clap's `common()` pins `utils::FULL_TEMPLATE` so that `{name} {version}` is on the
    // page at all — the default template shows `about`, not the version. Every fixture below
    // shares it, which is what makes the first line of each expected screen meaningful.
    // ---------------------------------------------------------------------------

    inline constexpr std::string_view full_template =
            "{before-help}{name} {version}\n{author-with-newline}{about-with-newline}\n"
            "{usage-heading} {usage}\n\n{all-args}{after-help}";

    consteval command_spec make_no_version() {
        command_builder app("foo");
        std::move(app).help_template(full_template);
        return app.freeze();
    }
    constexpr command_spec no_version = make_no_version();

    consteval command_spec make_with_version() {
        command_builder app("foo");
        std::move(app).help_template(full_template).version("3.0");
        return app.freeze();
    }
    constexpr command_spec with_version = make_with_version();

    consteval command_spec make_with_long_version() {
        command_builder app("foo");
        std::move(app).help_template(full_template).long_version("3.0 (abcdefg)");
        return app.freeze();
    }
    constexpr command_spec with_long_version = make_with_long_version();

    consteval command_spec make_with_both() {
        command_builder app("foo");
        std::move(app).help_template(full_template).version("3.0").long_version("3.0 (abcdefg)");
        return app.freeze();
    }
    constexpr command_spec with_both = make_with_both();

    /** clap's `with_subcommand()`: two levels, so propagation has somewhere to fail to reach. */
    consteval command_spec make_with_subcommand() {
        command_builder app("foo");
        std::move(app)
                .help_template(full_template)
                .version("3.0")
                .subcommand(command_builder("bar").subcommand(command_builder("baz")));
        return app.freeze();
    }
    constexpr command_spec with_subcommand = make_with_subcommand();

    consteval command_spec make_propagating() {
        command_builder app("foo");
        std::move(app)
                .help_template(full_template)
                .version("3.0")
                .propagate_version()
                .subcommand(command_builder("bar").subcommand(command_builder("baz")));
        return app.freeze();
    }
    constexpr command_spec propagating = make_propagating();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    // ---------------------------------------------------------------------------

    // The flag is injected exactly when there is something for it to print. Both sides are
    // asserted: a predicate that answered "no" for everything would satisfy the first line
    // alone.
    static_assert(!no_version.has_arg("version"));
    static_assert(with_version.has_arg("version"));
    static_assert(with_long_version.has_arg("version"));

    // Propagation reaches the grandchild, not only the child.
    static_assert(!with_subcommand.find_subcommand("bar")->has_arg("version"));
    static_assert(propagating.find_subcommand("bar")->has_arg("version"));
    static_assert(propagating.find_subcommand("bar")->find_subcommand("baz")->has_arg("version"));

}  // namespace

// ---------------------------------------------------------------------------
// render_version() — the five fallback cases
// ---------------------------------------------------------------------------

CLAPP_TEST("version.rs::version_short_flag_with_version") {
    CLAPP_CHECK(same(clapp::render_version(with_version, false).to_string(), "foo 3.0\n"));
}

CLAPP_TEST("version.rs::version_long_flag_with_version") {
    // Only `version()` was set, so the long form falls back onto it.
    CLAPP_CHECK(same(clapp::render_version(with_version, true).to_string(), "foo 3.0\n"));
}

CLAPP_TEST("version.rs::version_short_flag_with_long_version") {
    // ...and the fallback runs the other way too.
    CLAPP_CHECK(same(clapp::render_version(with_long_version, false).to_string(),
                     "foo 3.0 (abcdefg)\n"));
}

CLAPP_TEST("version.rs::version_long_flag_with_long_version") {
    CLAPP_CHECK(same(clapp::render_version(with_long_version, true).to_string(),
                     "foo 3.0 (abcdefg)\n"));
}

// Covers clap's `version_short_flag_with_both` and `version_long_flag_with_both` in one
// case: the two clap functions differ only in which of `-V` / `--version` they send, and
// splitting them here would assert the same command twice. Both spellings are driven
// below, so a name diff against clap's file finds both names on this line.
CLAPP_TEST("version.rs::version_short_and_long_flag_with_both") {
    // The case that proves the two are not merged: same command, two different lines.
    CLAPP_CHECK(same(clapp::render_version(with_both, false).to_string(), "foo 3.0\n"));
    CLAPP_CHECK(same(clapp::render_version(with_both, true).to_string(), "foo 3.0 (abcdefg)\n"));
}

// ---------------------------------------------------------------------------
// The parse side
// ---------------------------------------------------------------------------

CLAPP_TEST("version.rs::version_short_flag_no_version") {
    const outcome got = clapp::parse(no_version, raw_args{"foo", "-V"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("version.rs::version_long_flag_no_version") {
    const outcome got = clapp::parse(no_version, raw_args{"foo", "--version"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("version.rs::version_flag_with_version_reports_display_version") {
    // The kind, the exit code and the stream are the contract callers branch on.
    const outcome got = clapp::parse(with_version, raw_args{"foo", "--version"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_version);
    CLAPP_CHECK(!got.has_value() && got.error().exit_code() == 0);
    CLAPP_CHECK(!got.has_value() && !got.error().use_stderr());
}

CLAPP_TEST("version.rs::no_propagation_by_default_long") {
    const outcome got = clapp::parse(with_subcommand, raw_args{"foo", "bar", "--version"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("version.rs::no_propagation_by_default_short") {
    const outcome got = clapp::parse(with_subcommand, raw_args{"foo", "bar", "-V"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("version.rs::propagate_version_long") {
    const outcome got = clapp::parse(propagating, raw_args{"foo", "bar", "--version"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_version);
}

CLAPP_TEST("version.rs::propagate_version_short") {
    const outcome got = clapp::parse(propagating, raw_args{"foo", "bar", "-V"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_version);
}

// ---------------------------------------------------------------------------
// The help screens — eight cases, four commands times two flags
// ---------------------------------------------------------------------------

CLAPP_TEST("version.rs::help_short_flag_no_version") {
    // `foo ` keeps its trailing space: `{version}` substituted nothing and the template
    // engine does not trim the line it sat on.
    CLAPP_CHECK(same(page(no_version, false),
                     "foo \n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("version.rs::help_long_flag_no_version") {
    CLAPP_CHECK(same(page(no_version, true),
                     "foo \n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("version.rs::help_short_flag_with_version") {
    // The `-V, --version` row is now the longest, so every description moves right.
    CLAPP_CHECK(same(page(with_version, false),
                     "foo 3.0\n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("version.rs::help_long_flag_with_version") {
    CLAPP_CHECK(same(page(with_version, true),
                     "foo 3.0\n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("version.rs::help_short_flag_with_long_version") {
    // `{version}` on the help page is NOT keyed on which flag asked: clap's
    // `write_version` prefers `long_version` here for both `-h` and `--help`.
    CLAPP_CHECK(same(page(with_long_version, false),
                     "foo 3.0 (abcdefg)\n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("version.rs::help_long_flag_with_long_version") {
    CLAPP_CHECK(same(page(with_long_version, true),
                     "foo 3.0 (abcdefg)\n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("version.rs::help_short_flag_with_both") {
    // With both set the page shows the SHORT one, on both screens — the opposite of
    // render_version(long_form: true). That asymmetry is clap's and is the reason these
    // two cases exist alongside the render_version() ones above.
    CLAPP_CHECK(same(page(with_both, false),
                     "foo 3.0\n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("version.rs::help_long_flag_with_both") {
    CLAPP_CHECK(same(page(with_both, true),
                     "foo 3.0\n"
                     "\n"
                     "Usage: foo\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}
