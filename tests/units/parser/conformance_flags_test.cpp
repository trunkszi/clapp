#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    /**
     * \brief Whole-block comparison against clap's expected string, printing both on a
     *        mismatch. Used wherever clap itself compares the whole block: a substring check
     *        would also pass on a message whose `Usage:` line or `tip:` line has gone
     *        missing, which is exactly the regression these cases exist to catch.
     */
    bool same_block(const outcome& got, std::string_view want) {
        const std::string text = message_of(got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_two_flags() {
        command_builder app("flag");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("some flag")
                             .action(arg_action::set_true))
                .arg(arg_builder("color")
                             .short_('c')
                             .long_("color")
                             .help("some other flag")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec two_flags = make_two_flags();

    consteval command_spec make_three_flags() {
        command_builder app("multe_flags");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true))
                .arg(arg_builder("debug").short_('d').long_("debug").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec three_flags = make_three_flags();

    consteval command_spec make_repeatable() {
        command_builder app("opts");
        std::move(app).args_override_self().arg(
                arg_builder("o").short_('o').help("some flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec repeatable = make_repeatable();

    consteval command_spec make_rainbow() {
        command_builder app("flag");
        std::move(app).arg(arg_builder("rainbow").long_("rainbow").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec rainbow = make_rainbow();

    // clap's `mycat`: one positional and one flag whose long spelling looks like data.
    consteval command_spec make_mycat() {
        command_builder app("mycat");
        std::move(app)
                .arg(arg_builder("filename").index(1))
                .arg(arg_builder("a-flag").long_("a-flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec mycat = make_mycat();

    consteval command_spec make_required_positional() {
        command_builder app("test");
        std::move(app).arg(arg_builder("arg").index(1).action(arg_action::set).required());
        return app.freeze();
    }
    constexpr command_spec required_positional = make_required_positional();

    // A SetTrue flag that also accepts an optional value.
    consteval command_spec make_optional_value() {
        command_builder app("flag");
        std::move(app).arg(arg_builder("flag")
                                   .short_('f')
                                   .long_("flag")
                                   .help("some flag")
                                   .action(arg_action::set_true)
                                   .num_args(value_range::optional()));
        return app.freeze();
    }
    constexpr command_spec optional_value = make_optional_value();

    static_assert(two_flags.find_arg("flag")->get_action() == arg_action::set_true);
    static_assert(!two_flags.find_arg("flag")->get_num_args().takes_values());
    static_assert(optional_value.find_arg("flag")->get_num_args() == value_range::optional());
    static_assert(mycat.find_arg("filename")->get_index() == std::optional<std::size_t>{1});

    // clap's two scale fixtures, built here rather than pasted as 200 literals.
    std::vector<std::string> separate_flags(std::size_t count) {
        std::vector<std::string> out;
        out.emplace_back("");
        for (std::size_t i = 0; i < count; ++i) out.emplace_back("-o");
        return out;
    }

    std::vector<std::string> clustered_flags(std::size_t clusters, std::size_t per_cluster) {
        std::vector<std::string> out;
        out.emplace_back("");
        std::string one{"-"};
        one.append(per_cluster, 'o');
        for (std::size_t i = 0; i < clusters; ++i) out.push_back(one);
        return out;
    }

}  // namespace

// ---------------------------------------------------------------------------
// The four spellings
// ---------------------------------------------------------------------------

CLAPP_TEST("flags.rs::flag_using_short") {
    const outcome got = clapp::parse(two_flags, raw_args{"", "-f", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(got->get_flag("color"));
}

CLAPP_TEST("flags.rs::flag_using_long") {
    const outcome got = clapp::parse(two_flags, raw_args{"", "--flag", "--color"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(got->get_flag("color"));
}

CLAPP_TEST("flags.rs::flag_using_mixed") {
    const outcome short_then_long = clapp::parse(two_flags, raw_args{"", "-f", "--color"});
    CLAPP_CHECK(short_then_long.has_value());
    CLAPP_CHECK(short_then_long->get_flag("flag"));
    CLAPP_CHECK(short_then_long->get_flag("color"));

    const outcome long_then_short = clapp::parse(two_flags, raw_args{"", "--flag", "-c"});
    CLAPP_CHECK(long_then_short.has_value());
    CLAPP_CHECK(long_then_short->get_flag("flag"));
    CLAPP_CHECK(long_then_short->get_flag("color"));
}

CLAPP_TEST("flags.rs::multiple_flags_in_single") {
    const outcome got = clapp::parse(three_flags, raw_args{"", "-fcd"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(got->get_flag("debug"));
}

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

CLAPP_TEST("flags.rs::lots_o_flags_sep") {
    // clap's fixture is 213 separate `-o` tokens under args_override_self.
    const raw_args line{std::from_range, separate_flags(213)};
    const outcome got = clapp::parse(repeatable, line);
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("o"));
    CLAPP_CHECK(got->get_flag("o"));
}

CLAPP_TEST("flags.rs::lots_o_flags_combined") {
    // Four 64-letter clusters and one 41-letter cluster: 297 occurrences in 5 tokens.
    std::vector<std::string> line = clustered_flags(4, 64);
    line.push_back(std::string{"-"} + std::string(41, 'o'));
    const outcome got = clapp::parse(repeatable, raw_args{std::from_range, line});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("o"));
    CLAPP_CHECK(got->get_flag("o"));
}

// ---------------------------------------------------------------------------
// A flag is not an option
// ---------------------------------------------------------------------------

CLAPP_TEST("flags.rs::flag_using_long_with_literals") {
    const outcome got = clapp::parse(rainbow, raw_args{"", "--rainbow=false"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::too_many_values);
}

CLAPP_TEST("flags.rs::unexpected_value_error") {
    // Whole block, as clap compares it. Note the usage line names `--a-flag` rather than
    // `[OPTIONS]`: the offending argument is promoted into the usage, which a substring
    // check on `"foo"` and `"--a-flag"` cannot distinguish from the generic line.
    const outcome got = clapp::parse(mycat, raw_args{"mycat", "--a-flag=foo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::too_many_values);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected value 'foo' for '--a-flag' found; no more were "
                           "expected\n"
                           "\n"
                           "Usage: mycat --a-flag [filename]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// `--` and unknown spellings
// ---------------------------------------------------------------------------

CLAPP_TEST("flags.rs::issue_1284_argument_in_flag_style") {
    // After `--`, a flag spelling is data — and the flag it spells stays unset.
    const outcome escaped = clapp::parse(mycat, raw_args{"", "--", "--another-flag"});
    CLAPP_CHECK(escaped.has_value());
    CLAPP_CHECK(one_string(*escaped, "filename") == std::optional<std::string>{"--another-flag"});

    const outcome flagged = clapp::parse(mycat, raw_args{"", "--a-flag"});
    CLAPP_CHECK(flagged.has_value());
    CLAPP_CHECK(flagged->get_flag("a-flag"));

    const outcome escaped_known = clapp::parse(mycat, raw_args{"", "--", "--a-flag"});
    CLAPP_CHECK(escaped_known.has_value());
    CLAPP_CHECK(one_string(*escaped_known, "filename") == std::optional<std::string>{"--a-flag"});
    CLAPP_CHECK(!escaped_known->get_flag("a-flag"));

    // The error half, compared whole. The `tip:` line is the interesting part — it tells
    // the user the escape they just failed to type — and a `find("--another-flag")`
    // check passes with it deleted, because the token appears in the first line too.
    const outcome unknown = clapp::parse(mycat, raw_args{"mycat", "--another-flag"});
    CLAPP_CHECK(!unknown.has_value());
    CLAPP_CHECK(kind_of(unknown) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(unknown,
                           "error: unexpected argument '--another-flag' found\n"
                           "\n"
                           "  tip: to pass '--another-flag' as a value, use "
                           "'-- --another-flag'\n"
                           "\n"
                           "Usage: mycat [OPTIONS] [filename]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("flags.rs::issue_2308_multiple_dashes") {
    // Five dashes are not four dashes plus a flag, and the tip has to quote the token
    // back verbatim rather than a normalised form of it.
    const outcome got = clapp::parse(required_positional, raw_args{"test", "-----"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected argument '-----' found\n"
                           "\n"
                           "  tip: to pass '-----' as a value, use '-- -----'\n"
                           "\n"
                           "Usage: test <arg>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// A flag that also takes a value
// ---------------------------------------------------------------------------

CLAPP_TEST("flags.rs::optional_value") {
    const outcome absent = clapp::parse(optional_value, raw_args{""});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!absent->get_flag("flag"));

    const outcome bare = clapp::parse(optional_value, raw_args{"", "-f"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(bare->get_flag("flag"));

    // The flag was present and its value says otherwise. `get_flag` reports the VALUE.
    const outcome said_false = clapp::parse(optional_value, raw_args{"", "-f", "false"});
    CLAPP_CHECK(said_false.has_value());
    CLAPP_CHECK(!said_false->get_flag("flag"));

    const outcome said_true = clapp::parse(optional_value, raw_args{"", "-f", "true"});
    CLAPP_CHECK(said_true.has_value());
    CLAPP_CHECK(said_true->get_flag("flag"));
}
