#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
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

    // ---------------------------------------------------------------------------
    // Fixtures — mixed argument types
    //
    // clap writes these with the `arg!` macro: `arg!(-b --both "Both long and short")` is a
    // flag (SetTrue), `arg!(<POSITIONAL> "Positional")` is a required positional whose value
    // name defaults to its id. clapp has no `arg!`, so the three properties the macro implies
    // are spelled out; nothing else differs.
    // ---------------------------------------------------------------------------

    /**
     * \param positional_name  clap's `<POSITIONAL>` vs `<S>` — the only difference between
     *                         `mixed_argument_types` and
     *                         `mixed_argument_types_short_positional`.
     * \param positional_help  clap's `"Positional"` vs `"Short positional"`.
     * \param with_short       Whether `-b, --both` is declared at all —
     *                         `mixed_argument_types_no_short` drops it.
     */
    consteval command_spec make_mixed(std::string_view positional_name,
                                      std::string_view positional_help,
                                      bool with_short) {
        command_builder app("myprog");
        std::move(app).about("mixed arguments").next_help_heading("Mixed");
        if (with_short)
            std::move(app).arg(arg_builder("both")
                                       .short_('b')
                                       .long_("both")
                                       .action(arg_action::set_true)
                                       .help("Both long and short"));
        std::move(app)
                .arg(arg_builder("long")
                             .long_("long")
                             .action(arg_action::set_true)
                             .help("Long only"))
                .arg(arg_builder(positional_name).index(1).required().help(positional_help));
        return app.freeze();
    }
    constexpr command_spec mixed           = make_mixed("POSITIONAL", "Positional", true);
    constexpr command_spec mixed_short_pos = make_mixed("S", "Short positional", true);
    constexpr command_spec mixed_no_short  = make_mixed("POSITIONAL", "Positional", false);

    // ---------------------------------------------------------------------------
    // Fixtures — allow_missing_positional
    // ---------------------------------------------------------------------------

    consteval command_spec make_missing_final_required() {
        command_builder app("test");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("arg1").index(1))
                .arg(arg_builder("arg2").index(2).required());
        return app.freeze();
    }
    constexpr command_spec missing_final_required = make_missing_final_required();

    consteval command_spec make_missing_final_multiple() {
        command_builder app("test");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("foo").index(1))
                .arg(arg_builder("bar").index(2))
                .arg(arg_builder("baz")
                             .index(3)
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec missing_final_multiple = make_missing_final_multiple();

    // ---------------------------------------------------------------------------
    // Fixture — Append reaches the dotted rendering
    //
    // clap writes `.action(Set)` and then `.action(Append)`; the last call wins, so the arg's
    // action is Append. Writing only `.action(Append)` here is the same command.
    // ---------------------------------------------------------------------------

    consteval command_spec make_dotted_append(bool named) {
        command_builder app("test");
        arg_builder one("foo");
        std::move(one)
                .index(1)
                .required()
                .action(arg_action::append)
                .num_args(value_range::at_least(1));
        if (named) std::move(one).value_name("BAR");
        std::move(app).arg(std::move(one));
        return app.freeze();
    }
    constexpr command_spec dotted_append_plain = make_dotted_append(false);
    constexpr command_spec dotted_append_named = make_dotted_append(true);

    // ---------------------------------------------------------------------------
    // Fixture — next_line_help, shared by all four next_line_* cases
    //
    // clap's four cases build exactly this, differing only in `term_width` and in the help
    // text. Note the three per-argument / per-subcommand `next_line_help` settings: the point
    // of the case is that the command-level `true` wins over the per-argument `false`, so all
    // three rows render identically.
    // ---------------------------------------------------------------------------

    consteval command_spec
    make_next_line(std::size_t width, std::string_view value_name, std::string_view text) {
        command_builder app("test");
        std::move(app)
                .term_width(width)
                .next_line_help()
                .arg(arg_builder("default")
                             .long_("default")
                             .value_name(value_name)
                             .help(text)
                             .long_help(text)
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("next_line_help_false")
                             .long_("next_line_help_false")
                             .next_line_help(false)
                             .value_name(value_name)
                             .help(text)
                             .long_help(text)
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("next_line_help_true")
                             .long_("next_line_help_true")
                             .next_line_help(true)
                             .value_name(value_name)
                             .help(text)
                             .long_help(text)
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .subcommand(command_builder("default").about(text).long_about(text))
                .subcommand(command_builder("next_line_help_false")
                                    .next_line_help(false)
                                    .about(text)
                                    .long_about(text))
                .subcommand(command_builder("next_line_help_true")
                                    .next_line_help(true)
                                    .about(text)
                                    .long_about(text));
        return app.freeze();
    }

    constexpr std::string_view wrapped_text =
            "Also do versioning for private crates (will not be published)\n"
            "\n"
            "Specify inter dependency version numbers exactly with `=`\n"
            "\n"
            "Do not commit version changes\n"
            "\n"
            "Do not push generated commit and tags to git remote\n";

    constexpr command_spec next_line_short   = make_next_line(120, "V", "Hello");
    constexpr command_spec next_line_wrapped = make_next_line(67, "SOME_LONG_VALUE", wrapped_text);

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // These are the preconditions the expected screens below depend on. If any of them
    // regressed, the byte comparisons would fail for a reason unrelated to what they test,
    // so they are asserted separately and at compile time.
    // ---------------------------------------------------------------------------

    // `long_help` on the three arguments is what makes `--help` a different screen from `-h`;
    // without it `page(..., true)` would collapse back to the short one and the two halves of
    // each next_line_* case would be identical.
    static_assert(clapp::long_help_exists(next_line_short));
    static_assert(clapp::long_help_exists(next_line_wrapped));

    // The mixed_argument_types family has no long content at all, so its single expected
    // screen is what BOTH `-h` and `--help` print.
    static_assert(!clapp::long_help_exists(mixed));
    static_assert(!clapp::long_help_exists(mixed_short_pos));
    static_assert(!clapp::long_help_exists(mixed_no_short));

    // The explicit widths clap sets. Without them the four next_line_* screens would wrap at
    // clapp::default_terminal_width.
    static_assert(next_line_short.get_term_width() == std::optional<std::size_t>{120});
    static_assert(next_line_wrapped.get_term_width() == std::optional<std::size_t>{67});

    // `allow_missing_positional` is what admits `[arg1] <arg2>`; the usage line below is a
    // consequence of it, so pin that the setting actually reached the frozen spec.
    static_assert(missing_final_required.is_allow_missing_positional_set());
    static_assert(missing_final_multiple.is_allow_missing_positional_set());

    // The command-level flag is what puts every description on its own line.
    static_assert(next_line_short.is_next_line_help_set());
    static_assert(next_line_wrapped.is_next_line_help_set());

    // HONEST NOTE ON WHAT THE `next_line_help_false` ROW DOES *NOT* PROVE.
    // clapp stores `next_line_help` as one bit (arg.hpp:1816 forwards to
    // `setting(arg_setting::next_line_help, yes)`), so `next_line_help(false)` CLEARS the bit
    // and is indistinguishable from never having called it — as the two assertions below
    // show, the `default` arg and the `next_line_help_false` arg reach `freeze()` in exactly
    // the same state. clap 4.6.5 is built the same way (`ArgSettings` is a bitflag set and
    // the template asks `use_long || arg.is_next_line_help_set() || cmd.is_next_line_help_set()`),
    // so the behaviours agree — but the agreement is by construction on both sides, and this
    // case cannot distinguish "the command-level true wins" from "the per-arg false was never
    // stored". Only the `next_line_help_true` row below carries information.
    static_assert(!next_line_short.find_arg("default")->is_next_line_help_set());
    static_assert(!next_line_short.find_arg("next_line_help_false")->is_next_line_help_set());
    static_assert(next_line_short.find_arg("next_line_help_true")->is_next_line_help_set());

}  // namespace

// ---------------------------------------------------------------------------
// Mixed argument types — a positional sharing a custom heading with options
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::mixed_argument_types") {
    // `<POSITIONAL>` (12) is wider than `-b, --both` (10), so the positional sets the
    // section's column. Note the positional row starts at column 2 and the long-only
    // option at column 6: the short-flag gutter is not given to positionals.
    CLAPP_CHECK(same(page(mixed, true),
                     "mixed arguments\n"
                     "\n"
                     "Usage: myprog [OPTIONS] <POSITIONAL>\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"
                     "\n"
                     "Mixed:\n"
                     "  -b, --both    Both long and short\n"
                     "      --long    Long only\n"
                     "  <POSITIONAL>  Positional\n"));
}

CLAPP_TEST("help.rs::mixed_argument_types_short_positional") {
    // Same command with a 3-wide placeholder: now `-b, --both` sets the column and the
    // positional is the one that gets padded. Together with the case above this proves
    // the column is a max over both row kinds rather than being taken from either.
    CLAPP_CHECK(same(page(mixed_short_pos, true),
                     "mixed arguments\n"
                     "\n"
                     "Usage: myprog [OPTIONS] <S>\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"
                     "\n"
                     "Mixed:\n"
                     "  -b, --both  Both long and short\n"
                     "      --long  Long only\n"
                     "  <S>         Short positional\n"));
}

CLAPP_TEST("help.rs::mixed_argument_types_no_short") {
    // Nothing in the `Mixed:` section has a short spelling, and `--long` STILL gets the
    // 6-column indent — the gutter is decided over the whole command (where `-h` lives),
    // not per section. This case and `mixed_argument_types` differ only by `-b, --both`.
    CLAPP_CHECK(same(page(mixed_no_short, true),
                     "mixed arguments\n"
                     "\n"
                     "Usage: myprog [OPTIONS] <POSITIONAL>\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"
                     "\n"
                     "Mixed:\n"
                     "      --long    Long only\n"
                     "  <POSITIONAL>  Positional\n"));
}

// ---------------------------------------------------------------------------
// allow_missing_positional
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::missing_positional_final_required") {
    // `[arg1] <arg2>`: the brackets come from `required()`, not from the position.
    CLAPP_CHECK(same(page(missing_final_required, true),
                     "Usage: test [arg1] <arg2>\n"
                     "\n"
                     "Arguments:\n"
                     "  [arg1]  \n"
                     "  <arg2>  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("help.rs::missing_positional_final_multiple") {
    // The ellipsis sits outside the bracket: `[baz]...`, never `[baz...]`.
    CLAPP_CHECK(same(page(missing_final_multiple, true),
                     "Usage: test [foo] [bar] [baz]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [foo]     \n"
                     "  [bar]     \n"
                     "  [baz]...  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

CLAPP_TEST("help.rs::positional_multiple_occurrences_is_dotted") {
    // Append, not Set: the dotted rendering follows the arity, not the action. Compare
    // conformance_help_test.cpp's `positional_multiple_values_is_dotted`, which is the
    // Set half of the same pair and expects the same two screens.
    CLAPP_CHECK(same(page(dotted_append_plain, true),
                     "Usage: test <foo>...\n"
                     "\n"
                     "Arguments:\n"
                     "  <foo>...  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
    CLAPP_CHECK(same(page(dotted_append_named, true),
                     "Usage: test <BAR>...\n"
                     "\n"
                     "Arguments:\n"
                     "  <BAR>...  \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}

// ---------------------------------------------------------------------------
// next_line_help
//
// clap runs each of these commands twice, `-h` then `--help`, and both screens are
// asserted here. The bin name is `myprog` while the command is named `test`, exactly as
// clap's `assert_output(cmd, "myprog -h", ..)` does.
// ---------------------------------------------------------------------------

namespace {

    /** The `-h` screen shared by `next_line_command_short` and `next_line_arg_short`. */
    constexpr std::string_view next_line_short_h =
            "Usage: myprog [OPTIONS] [COMMAND]\n"
            "\n"
            "Commands:\n"
            "  default\n"
            "          Hello\n"
            "  next_line_help_false\n"
            "          Hello\n"
            "  next_line_help_true\n"
            "          Hello\n"
            "  help\n"
            "          Print this message or the help of the given subcommand(s)\n"
            "\n"
            "Options:\n"
            "      --default <V>\n"
            "          Hello\n"
            "      --next_line_help_false <V>\n"
            "          Hello\n"
            "      --next_line_help_true <V>\n"
            "          Hello\n"
            "  -h, --help\n"
            "          Print help (see more with '--help')\n";

    /**
     * The `--help` screen: same rows, plus a blank line after each argument, and the
     * reversed cross-reference on the help flag.
     */
    constexpr std::string_view next_line_short_long =
            "Usage: myprog [OPTIONS] [COMMAND]\n"
            "\n"
            "Commands:\n"
            "  default\n"
            "          Hello\n"
            "  next_line_help_false\n"
            "          Hello\n"
            "  next_line_help_true\n"
            "          Hello\n"
            "  help\n"
            "          Print this message or the help of the given subcommand(s)\n"
            "\n"
            "Options:\n"
            "      --default <V>\n"
            "          Hello\n"
            "\n"
            "      --next_line_help_false <V>\n"
            "          Hello\n"
            "\n"
            "      --next_line_help_true <V>\n"
            "          Hello\n"
            "\n"
            "  -h, --help\n"
            "          Print help (see a summary with '-h')\n";

// The wrapped body, once. Every row in the two wrapped screens repeats it verbatim, and
// the blank lines between paragraphs carry the 10-column indent — that trailing
// whitespace is clap's, verified byte for byte against help.rs:4487.
#define CLAPP_WRAPPED_BODY                                                                         \
    "          Also do versioning for private crates (will not be\n"                               \
    "          published)\n"                                                                       \
    "          \n"                                                                                 \
    "          Specify inter dependency version numbers exactly with `=`\n"                        \
    "          \n"                                                                                 \
    "          Do not commit version changes\n"                                                    \
    "          \n"                                                                                 \
    "          Do not push generated commit and tags to git remote\n"

    constexpr std::string_view next_line_wrapped_h =
            "Usage: myprog [OPTIONS] [COMMAND]\n"
            "\n"
            "Commands:\n"
            "  default\n" CLAPP_WRAPPED_BODY "  next_line_help_false\n" CLAPP_WRAPPED_BODY
            "  next_line_help_true\n" CLAPP_WRAPPED_BODY "  help\n"
            "          Print this message or the help of the given subcommand(s)\n"
            "\n"
            "Options:\n"
            "      --default <SOME_LONG_VALUE>\n" CLAPP_WRAPPED_BODY
            "      --next_line_help_false <SOME_LONG_VALUE>\n" CLAPP_WRAPPED_BODY
            "      --next_line_help_true <SOME_LONG_VALUE>\n" CLAPP_WRAPPED_BODY "  -h, --help\n"
            "          Print help (see more with '--help')\n";

    constexpr std::string_view next_line_wrapped_long =
            "Usage: myprog [OPTIONS] [COMMAND]\n"
            "\n"
            "Commands:\n"
            "  default\n" CLAPP_WRAPPED_BODY "  next_line_help_false\n" CLAPP_WRAPPED_BODY
            "  next_line_help_true\n" CLAPP_WRAPPED_BODY "  help\n"
            "          Print this message or the help of the given subcommand(s)\n"
            "\n"
            "Options:\n"
            "      --default <SOME_LONG_VALUE>\n" CLAPP_WRAPPED_BODY "\n"
            "      --next_line_help_false <SOME_LONG_VALUE>\n" CLAPP_WRAPPED_BODY "\n"
            "      --next_line_help_true <SOME_LONG_VALUE>\n" CLAPP_WRAPPED_BODY "\n"
            "  -h, --help\n"
            "          Print help (see a summary with '-h')\n";

#undef CLAPP_WRAPPED_BODY

}  // namespace

CLAPP_TEST("help.rs::next_line_command_short") {
    CLAPP_CHECK(same(page(next_line_short, false, "myprog"), next_line_short_h));
    CLAPP_CHECK(same(page(next_line_short, true, "myprog"), next_line_short_long));
}

CLAPP_TEST("help.rs::next_line_arg_short") {
    // Byte-identical to `next_line_command_short` in clap 4.6.5 (help.rs:4346 vs 4250);
    // kept as its own case so this file's list matches clap's one-for-one.
    CLAPP_CHECK(same(page(next_line_short, false, "myprog"), next_line_short_h));
    CLAPP_CHECK(same(page(next_line_short, true, "myprog"), next_line_short_long));
}

CLAPP_TEST("help.rs::next_line_command_wrapped") {
    CLAPP_CHECK(same(page(next_line_wrapped, false, "myprog"), next_line_wrapped_h));
    CLAPP_CHECK(same(page(next_line_wrapped, true, "myprog"), next_line_wrapped_long));
}

CLAPP_TEST("help.rs::next_line_arg_wrapped") {
    // Byte-identical to `next_line_command_wrapped` in clap 4.6.5 (help.rs:4629 vs 4442).
    CLAPP_CHECK(same(page(next_line_wrapped, false, "myprog"), next_line_wrapped_h));
    CLAPP_CHECK(same(page(next_line_wrapped, true, "myprog"), next_line_wrapped_long));
}
