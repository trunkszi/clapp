#include <clapp/error/error.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using clapp::all_error_kinds;
    using clapp::context_kind;
    using clapp::context_value;
    using clapp::context_value_kind;
    using clapp::cow_str;
    using clapp::error;
    using clapp::error_format;
    using clapp::error_kind;
    using clapp::style_class;
    using clapp::styled_str;
    using namespace std::string_view_literals;

    /**
     * Borrowed names, the shape the parser will actually use: everything here would come
     * out of a frozen command_spec and live in .rodata for the life of the process.
     */
    constexpr std::vector<cow_str> borrowed(std::initializer_list<std::string_view> names) {
        std::vector<cow_str> out;
        for (const std::string_view name : names) out.push_back(cow_str::borrowed(name));
        return out;
    }

    // ---------------------------------------------------------------------------
    // Every kind: constructible, routed, and scored
    // ---------------------------------------------------------------------------

    consteval bool every_kind_is_constructible_and_routed() {
        for (const error_kind kind : all_error_kinds) {
            const error err{kind};
            if (err.kind() != kind) return false;
            if (err.use_stderr() != clapp::use_stderr(kind)) return false;
            if (err.exit_code() != clapp::exit_code_of(kind)) return false;
            if (err.has_message()) return false;
            if (err.has_source() || err.has_help_flag()) return false;
            if (!err.context_entries().empty()) return false;
            // Every kind renders *something*: an error that printed nothing would exit 2
            // in silence.
            if (err.render().empty()) return false;
        }
        return true;
    }

    static_assert(every_kind_is_constructible_and_routed());

    // The routing rule spelled out on the type, not only on the free function.
    static_assert(error{error_kind::display_help}.exit_code() == 0);
    static_assert(!error{error_kind::display_help}.use_stderr());
    static_assert(error{error_kind::display_version}.exit_code() == 0);
    static_assert(error{error_kind::display_help_on_missing_argument_or_subcommand}.exit_code() ==
                  2);
    static_assert(error{error_kind::display_help_on_missing_argument_or_subcommand}.use_stderr());
    static_assert(error{error_kind::unknown_argument}.exit_code() == 2);
    static_assert(error{error_kind::io}.exit_code() == 2);

    // ---------------------------------------------------------------------------
    // Context round-trips
    // ---------------------------------------------------------------------------

    consteval bool context_round_trips() {
        error err{error_kind::invalid_value};
        err.insert(context_kind::invalid_arg, context_value::string(cow_str::borrowed("--color")));
        err.insert(context_kind::actual_num_values, context_value::number(3));

        if (!err.has_context(context_kind::invalid_arg)) return false;
        if (err.has_context(context_kind::usage)) return false;
        if (err.context_ref(context_kind::invalid_arg).as_string() != "--color"sv) return false;
        if (err.context_ref(context_kind::actual_num_values).as_number() != 3) return false;
        if (err.context(context_kind::usage) != std::nullopt) return false;
        if (err.context_entries().size() != 2) return false;

        // Re-inserting replaces rather than duplicating.
        err.insert(context_kind::invalid_arg, context_value::string(cow_str::borrowed("--colour")));
        if (err.context_entries().size() != 2) return false;
        if (err.context_ref(context_kind::invalid_arg).as_string() != "--colour"sv) return false;

        if (!err.erase_context(context_kind::invalid_arg)) return false;
        if (err.erase_context(context_kind::invalid_arg)) return false;
        return err.context_entries().size() == 1;
    }

    static_assert(context_round_trips());

    // context_ref() answers a reference, so an absent lookup has to be told apart from a
    // present-but-`none` one with has_context(). Both sides asserted: an implementation
    // that returned the shared empty value for a *present* none would pass the first half.
    consteval bool absent_and_present_none_are_distinguishable() {
        error err{error_kind::argument_conflict};
        err.insert(context_kind::prior_arg, context_value::none());

        return err.has_context(context_kind::prior_arg) &&
               err.context_ref(context_kind::prior_arg).kind() == context_value_kind::none &&
               !err.has_context(context_kind::invalid_arg) &&
               err.context_ref(context_kind::invalid_arg).kind() == context_value_kind::none;
    }

    static_assert(absent_and_present_none_are_distinguishable());

    // The rvalue overloads exist so a factory can be written as one expression; they must
    // produce the same object as the lvalue ones.
    consteval bool rvalue_and_lvalue_builders_agree() {
        error built_stepwise{error_kind::no_equals};
        built_stepwise.insert(context_kind::invalid_arg,
                              context_value::string(cow_str::borrowed("--color")));
        built_stepwise.set_help_flag(cow_str::borrowed("--help"));

        const error built_inline =
                error{error_kind::no_equals}
                        .insert(context_kind::invalid_arg,
                                context_value::string(cow_str::borrowed("--color")))
                        .set_help_flag(cow_str::borrowed("--help"));

        return built_stepwise == built_inline;
    }

    static_assert(rvalue_and_lvalue_builders_agree());

    // ---------------------------------------------------------------------------
    // The named constructors, and what they render
    // ---------------------------------------------------------------------------

    consteval bool unknown_argument_renders_like_clap() {
        const error err = error::unknown_argument(
                cow_str::borrowed("--verbose"), std::nullopt, std::nullopt, false, std::nullopt);
        const styled_str message = err.render();
        return err.kind() == error_kind::unknown_argument &&
               message.to_string() == "error: unexpected argument '--verbose' found\n" &&
               message.text_of(style_class::error) == "error:" &&
               message.text_of(style_class::invalid) == "--verbose" &&
               err.context_ref(context_kind::invalid_arg).as_string() == "--verbose"sv;
    }

    static_assert(unknown_argument_renders_like_clap());

    consteval bool the_help_flag_adds_claps_closing_line() {
        error err = error::unknown_argument(
                cow_str::borrowed("--verbose"), std::nullopt, std::nullopt, false, std::nullopt);
        err.set_help_flag(cow_str::borrowed("--help"));
        const styled_str message = err.render();
        return message.to_string() ==
                       "error: unexpected argument '--verbose' found\n\nFor more information, "
                       "try '--help'.\n" &&
               message.text_of(style_class::literal) == "--help" && err.has_help_flag();
    }

    static_assert(the_help_flag_adds_claps_closing_line());

    consteval bool a_trailing_value_gets_the_double_dash_tip() {
        const error err = error::unknown_argument(
                cow_str::borrowed("--verbose"), std::nullopt, std::nullopt, true, std::nullopt);
        return err.render().to_string() ==
               "error: unexpected argument '--verbose' found\n\n  tip: to pass '--verbose' as a "
               "value, use '-- --verbose'\n";
    }

    static_assert(a_trailing_value_gets_the_double_dash_tip());

    // A flag that exists only under a subcommand becomes a tip; a flag that exists here
    // becomes suggested_arg and is rendered by the did-you-mean path instead. Both arms of
    // clap's `match did_you_mean`.
    consteval bool a_flag_under_a_subcommand_becomes_a_tip() {
        const error err = error::unknown_argument(cow_str::borrowed("--verbose"),
                                                  cow_str::borrowed("--verbose"),
                                                  cow_str::borrowed("build"),
                                                  false,
                                                  std::nullopt);
        return err.render().to_string() ==
                       "error: unexpected argument '--verbose' found\n\n  tip: 'build --verbose' "
                       "exists\n" &&
               !err.has_context(context_kind::suggested_arg);
    }

    static_assert(a_flag_under_a_subcommand_becomes_a_tip());

    consteval bool a_local_flag_becomes_a_did_you_mean() {
        const error err = error::unknown_argument(cow_str::borrowed("--bar"),
                                                  cow_str::borrowed("--baz"),
                                                  std::nullopt,
                                                  false,
                                                  std::nullopt);
        return err.context_ref(context_kind::suggested_arg).as_string() == "--baz"sv &&
               err.render().to_string() ==
                       "error: unexpected argument '--bar' found\n\n  tip: a similar argument "
                       "exists: '--baz'\n";
    }

    static_assert(a_local_flag_becomes_a_did_you_mean());

    // invalid_value computes its own suggestion, exactly as clap does inside its
    // constructor. "gren" scores 0.93 against "green" and 0.72 against "red", so the
    // suggestion is the higher of two candidates that both clear the threshold — a pair
    // that a "first match wins" bug would get wrong.
    consteval bool invalid_value_suggests_the_best_candidate() {
        const error err = error::invalid_value(cow_str::borrowed("--color"),
                                               cow_str::borrowed("gren"),
                                               borrowed({"red", "green", "blue"}));
        return err.kind() == error_kind::invalid_value &&
               err.context_ref(context_kind::suggested_value).as_string() == "green"sv &&
               err.context_ref(context_kind::valid_value).as_strings().size() == 3 &&
               err.render().to_string() ==
                       "error: invalid value 'gren' for '--color'\n  [possible values: red, "
                       "green, blue]\n\n  tip: a similar value exists: 'green'\n";
    }

    static_assert(invalid_value_suggests_the_best_candidate());

    // Nothing close enough: no suggestion context at all, and the possible-values list is
    // still printed.
    consteval bool invalid_value_suggests_nothing_when_nothing_is_close() {
        const error err = error::invalid_value(cow_str::borrowed("--speed"),
                                               cow_str::borrowed("other"),
                                               borrowed({"fast", "slow"}));
        return !err.has_context(context_kind::suggested_value) &&
               err.render().to_string() ==
                       "error: invalid value 'other' for '--speed'\n  [possible values: fast, "
                       "slow]\n";
    }

    static_assert(invalid_value_suggests_nothing_when_nothing_is_close());

    // The suggestion must not alias the value list it came from: best_match() returns a
    // view into its input, and the list is moved into the context immediately afterwards.
    // Owning it is the fix; this asserts the fix is in place.
    consteval bool the_suggestion_owns_its_bytes() {
        const error err            = error::invalid_value(cow_str::borrowed("--color"),
                                                          cow_str::borrowed("gren"),
                                                          borrowed({"red", "green", "blue"}));
        const context_value& value = err.context_ref(context_kind::suggested_value);
        return value.kind() == context_value_kind::string && value.to_string() == "green";
    }

    static_assert(the_suggestion_owns_its_bytes());

    consteval bool empty_value_says_a_value_is_required() {
        const error err = error::empty_value(cow_str::borrowed("--name"), {});
        return err.kind() == error_kind::invalid_value &&
               err.render().to_string() ==
                       "error: a value is required for '--name' but none was supplied\n";
    }

    static_assert(empty_value_says_a_value_is_required());

    // A possible value that is empty or contains whitespace is quoted, as clap's Escape
    // does. Without it, `[possible values: a b, ]` reads as three values.
    consteval bool possible_values_are_escaped() {
        const error err = error::invalid_value(
                cow_str::borrowed("--v"), cow_str::borrowed("zzz"), borrowed({"a b", ""}));
        return err.render().contains(R"([possible values: "a b", ""])");
    }

    static_assert(possible_values_are_escaped());

    consteval bool argument_conflict_names_the_other_argument() {
        const error err = error::argument_conflict(
                cow_str::borrowed("--debug"), borrowed({"--color"}), std::nullopt);
        return err.kind() == error_kind::argument_conflict &&
               err.context_ref(context_kind::prior_arg).kind() == context_value_kind::string &&
               err.render().to_string() ==
                       "error: the argument '--debug' cannot be used with '--color'\n";
    }

    static_assert(argument_conflict_names_the_other_argument());

    // One conflicting argument is a String, several are a Strings, none is a None — clap's
    // `match others.len()`. All three render differently.
    consteval bool argument_conflict_collapses_by_count() {
        const error many = error::argument_conflict(
                cow_str::borrowed("--debug"), borrowed({"--color", "--quiet"}), std::nullopt);
        const error none = error::argument_conflict(cow_str::borrowed("--debug"), {}, std::nullopt);
        return many.context_ref(context_kind::prior_arg).kind() == context_value_kind::strings &&
               many.render().to_string() ==
                       "error: the argument '--debug' cannot be used with:\n  --color\n  "
                       "--quiet\n" &&
               none.context_ref(context_kind::prior_arg).kind() == context_value_kind::none &&
               none.render().to_string() ==
                       "error: the argument '--debug' cannot be used with one or more of the "
                       "other specified arguments\n";
    }

    static_assert(argument_conflict_collapses_by_count());

    // An argument in conflict with itself is a repeat, not a conflict, and clap words it
    // differently. This is the pair a naive port collapses.
    consteval bool an_argument_conflicting_with_itself_is_a_repeat() {
        const error repeat = error::argument_conflict(
                cow_str::borrowed("--debug"), borrowed({"--debug"}), std::nullopt);
        return repeat.render().to_string() ==
               "error: the argument '--debug' cannot be used multiple times\n";
    }

    static_assert(an_argument_conflicting_with_itself_is_a_repeat());

    consteval bool subcommand_conflict_names_the_subcommand() {
        const error err = error::subcommand_conflict(
                cow_str::borrowed("build"), borrowed({"--color"}), std::nullopt);
        return err.kind() == error_kind::argument_conflict &&
               err.render().to_string() ==
                       "error: the subcommand 'build' cannot be used with '--color'\n";
    }

    static_assert(subcommand_conflict_names_the_subcommand());

    consteval bool no_equals_asks_for_the_equals_sign() {
        const error err = error::no_equals(cow_str::borrowed("--color"), std::nullopt);
        return err.kind() == error_kind::no_equals &&
               err.render().to_string() ==
                       "error: equal sign is needed when assigning values to '--color'\n";
    }

    static_assert(no_equals_asks_for_the_equals_sign());

    consteval bool missing_required_argument_lists_them_and_prints_usage() {
        const error err = error::missing_required_argument(
                borrowed({"--name <NAME>"}),
                styled_str{style_class::usage, "Usage: prog --name <NAME>"});
        return err.kind() == error_kind::missing_required_argument &&
               err.render().to_string() ==
                       "error: the following required arguments were not provided:\n  --name "
                       "<NAME>\n\nUsage: prog --name <NAME>\n" &&
               err.context_ref(context_kind::usage).kind() == context_value_kind::styled;
    }

    static_assert(missing_required_argument_lists_them_and_prints_usage());

    consteval bool missing_subcommand_lists_the_available_ones() {
        const error err = error::missing_subcommand(
                cow_str::borrowed("prog"), borrowed({"add", "remove"}), std::nullopt);
        return err.kind() == error_kind::missing_subcommand &&
               err.render().to_string() ==
                       "error: 'prog' requires a subcommand but one was not provided\n  "
                       "[subcommands: add, remove]\n";
    }

    static_assert(missing_subcommand_lists_the_available_ones());

    consteval bool invalid_subcommand_suggests_the_nearest_name() {
        const error err = error::invalid_subcommand(
                cow_str::borrowed("confi"), borrowed({"config"}), std::nullopt);
        return err.kind() == error_kind::invalid_subcommand &&
               err.render().to_string() ==
                       "error: unrecognized subcommand 'confi'\n\n  tip: a similar subcommand "
                       "exists: 'config'\n";
    }

    static_assert(invalid_subcommand_suggests_the_nearest_name());

    // Two candidates get the plural wording; clap's did_you_mean switches on the count.
    consteval bool two_candidates_read_as_plural() {
        const error err = error::invalid_subcommand(
                cow_str::borrowed("confi"), borrowed({"config", "confirm"}), std::nullopt);
        return err.render().contains("some similar subcommands exist: 'config', 'confirm'");
    }

    static_assert(two_candidates_read_as_plural());

    consteval bool unrecognized_subcommand_has_nothing_to_suggest() {
        const error err = error::unrecognized_subcommand(cow_str::borrowed("foo"), std::nullopt);
        return err.kind() == error_kind::invalid_subcommand &&
               !err.has_context(context_kind::suggested_subcommand) &&
               err.render().to_string() == "error: unrecognized subcommand 'foo'\n";
    }

    static_assert(unrecognized_subcommand_has_nothing_to_suggest());

    consteval bool too_many_values_names_the_extra_one() {
        const error err = error::too_many_values(
                cow_str::borrowed("--opt"), cow_str::borrowed("extra"), std::nullopt);
        return err.kind() == error_kind::too_many_values &&
               err.render().to_string() ==
                       "error: unexpected value 'extra' for '--opt' found; no more were "
                       "expected\n";
    }

    static_assert(too_many_values_names_the_extra_one());

    // The verb agrees with the count: clap's singular_or_plural. Both sides asserted,
    // because "was"/"were" is exactly the kind of thing that gets hard-coded.
    consteval bool too_few_values_counts_and_conjugates() {
        const error plural = error::too_few_values(cow_str::borrowed("--opt"), 3, 2, std::nullopt);
        const error singular =
                error::too_few_values(cow_str::borrowed("--opt"), 3, 1, std::nullopt);
        return plural.kind() == error_kind::too_few_values &&
               plural.context_ref(context_kind::min_values).as_number() == 3 &&
               plural.context_ref(context_kind::actual_num_values).as_number() == 2 &&
               plural.render().to_string() ==
                       "error: 3 values required by '--opt'; only 2 were provided\n" &&
               singular.render().to_string() ==
                       "error: 3 values required by '--opt'; only 1 was provided\n";
    }

    static_assert(too_few_values_counts_and_conjugates());

    consteval bool wrong_number_of_values_counts_and_conjugates() {
        const error err =
                error::wrong_number_of_values(cow_str::borrowed("--opt"), 2, 1, std::nullopt);
        return err.kind() == error_kind::wrong_number_of_values &&
               err.context_ref(context_kind::expected_num_values).as_number() == 2 &&
               err.render().to_string() ==
                       "error: 2 values required for '--opt' but 1 was provided\n";
    }

    static_assert(wrong_number_of_values_counts_and_conjugates());

    // The seam clapp::parse_error was designed for: the reason is a string_view into
    // static storage, so it is borrowed rather than copied.
    consteval bool value_validation_appends_the_cause() {
        const error err =
                error::value_validation(cow_str::borrowed("--num"),
                                        cow_str::borrowed("abc"),
                                        cow_str::borrowed("invalid digit found in string"));
        return err.kind() == error_kind::value_validation && err.has_source() &&
               err.source() == "invalid digit found in string"sv &&
               err.render().to_string() ==
                       "error: invalid value 'abc' for '--num': invalid digit found in "
                       "string\n";
    }

    static_assert(value_validation_appends_the_cause());

    consteval bool unnecessary_double_dash_explains_the_fix() {
        const error err = error::unnecessary_double_dash(cow_str::borrowed("add"), std::nullopt);
        return err.kind() == error_kind::unknown_argument &&
               err.render().to_string() ==
                       "error: unexpected argument 'add' found\n\n  tip: subcommand 'add' "
                       "exists; to use it, remove the '--' before it\n";
    }

    static_assert(unnecessary_double_dash_explains_the_fix());

    // io and format have no description of their own, so the attached cause *is* the
    // message. Without the source they fall back to "unknown cause" rather than printing
    // an empty line.
    consteval bool io_and_format_speak_through_their_source() {
        return error::io(cow_str::borrowed("Broken pipe")).render().to_string() ==
                       "error: Broken pipe\n" &&
               error::format(cow_str::borrowed("bad format")).render().to_string() ==
                       "error: bad format\n" &&
               error{error_kind::io}.render().to_string() == "error: unknown cause\n";
    }

    static_assert(io_and_format_speak_through_their_source());

    // ---------------------------------------------------------------------------
    // The three display kinds
    // ---------------------------------------------------------------------------

    // A formatted message is passed through byte for byte, whatever error_format asks for.
    // Help output that arrives with "error:" glued to the front is the failure this guards,
    // and it is a failure the type system would not catch.
    consteval bool display_kinds_render_verbatim() {
        const styled_str help{style_class::header, "Usage: prog [OPTIONS]"};
        const styled_str version{"prog 1.2.3"};

        const error on_request = error::display_help(help);
        const error on_missing = error::display_help_error(help);
        const error versioned  = error::display_version(version);

        return on_request.render() == help && on_request.render(error_format::kind_only) == help &&
               on_request.kind() == error_kind::display_help && on_request.exit_code() == 0 &&
               !on_request.use_stderr()

               && on_missing.render() == help &&
               on_missing.kind() == error_kind::display_help_on_missing_argument_or_subcommand
               // Same text, opposite routing. The pair a "display_* means success" bug
               // collapses.
               && on_missing.exit_code() == 2 && on_missing.use_stderr()

               && versioned.render() == version && versioned.exit_code() == 0 &&
               !versioned.use_stderr() && versioned.render().to_string() == "prog 1.2.3";
    }

    static_assert(display_kinds_render_verbatim());

    // ---------------------------------------------------------------------------
    // raw() and the two formatters
    // ---------------------------------------------------------------------------

    consteval bool raw_wraps_the_message_but_ignores_context() {
        error err = error::raw(error_kind::invalid_value, cow_str::borrowed("custom message"));
        err.insert(context_kind::invalid_arg,
                   context_value::string(cow_str::borrowed("--ignored")));
        err.set_help_flag(cow_str::borrowed("--help"));

        return err.has_message() &&
               err.render().to_string() ==
                       "error: custom message\n\nFor more information, try '--help'.\n" &&
               !err.render().contains("--ignored");
    }

    static_assert(raw_wraps_the_message_but_ignores_context());

    // KindFormatter: the kind's own sentence, no context, no usage, no closing line.
    consteval bool kind_only_drops_every_detail() {
        error err = error::unknown_argument(cow_str::borrowed("--verbose"),
                                            std::nullopt,
                                            std::nullopt,
                                            true,
                                            styled_str{style_class::usage, "Usage: prog"});
        err.set_help_flag(cow_str::borrowed("--help"));

        const styled_str terse = err.render(error_format::kind_only);
        const styled_str rich  = err.render();

        return terse.to_string() == "error: unexpected argument found\n" &&
               !terse.contains("tip:") && !terse.contains("Usage:") && rich.contains("--verbose") &&
               rich.contains("tip:") && rich.contains("Usage: prog");
    }

    static_assert(kind_only_drops_every_detail());

    // Without the context it needs, the rich renderer falls back to the generic sentence
    // instead of printing a half-built one. Every arm of write_dynamic_context has this
    // guard; here it is checked for all sixteen kinds at once.
    consteval bool a_bare_error_falls_back_to_the_generic_sentence() {
        for (const error_kind kind : all_error_kinds) {
            const error err{kind};
            if (clapp::is_display(kind)) continue;  // no message at all was supplied
            const std::string message = err.render().to_string();
            const auto sentence       = clapp::describe(kind);
            // Built with push_back: std::string{string_view} and operator+ both reach
            // libstdc++'s _M_mutate, which is not a constant expression under
            // -fsanitize=null (CLAUDE.md trap 10).
            std::string expected;
            for (const char byte : "error: "sv) expected.push_back(byte);
            for (const char byte : sentence.value_or("unknown cause"sv)) expected.push_back(byte);
            expected.push_back('\n');
            if (message != expected) return false;
        }
        return true;
    }

    static_assert(a_bare_error_falls_back_to_the_generic_sentence());

    // ---------------------------------------------------------------------------
    // Usage
    // ---------------------------------------------------------------------------

    // The usage line keeps its own styling when it is spliced into the message: it arrives
    // already rendered from clapp::output and must not be flattened to plain text.
    consteval bool usage_is_appended_with_its_styling_intact() {
        styled_str usage;
        usage.push(style_class::usage, "Usage:").push_plain(" ").push(style_class::literal, "prog");

        const error err          = error::no_equals(cow_str::borrowed("--color"), usage);
        const styled_str message = err.render();

        return message.to_string() ==
                       "error: equal sign is needed when assigning values to '--color'\n\nUsage: "
                       "prog\n" &&
               message.text_of(style_class::usage) == "Usage:" &&
               message.text_of(style_class::literal) == "prog";
    }

    static_assert(usage_is_appended_with_its_styling_intact());

    // ---------------------------------------------------------------------------
    // Runtime mirror
    // ---------------------------------------------------------------------------
    //
    // These report the compile-time conclusions and cover what cannot cross the consteval
    // boundary: an error that outlives the expression building it, and a context borrowing
    // a runtime string.

    CLAPP_TEST("error: every kind is constructible, routed and scored") {
        CLAPP_CHECK(every_kind_is_constructible_and_routed());

        std::size_t to_stdout = 0;
        for (const error_kind kind : all_error_kinds) {
            const error err{kind};
            CLAPP_CHECK(err.kind() == kind);
            CLAPP_CHECK(err.exit_code() == (err.use_stderr() ? 2 : 0));
            CLAPP_CHECK(!err.render().empty());
            if (!err.use_stderr()) ++to_stdout;
        }
        CLAPP_CHECK(to_stdout == 2);
    }

    CLAPP_TEST("error: survives being returned, with its context intact") {
        const error err = [] {
            return error::invalid_value(cow_str::borrowed("--color"),
                                        cow_str::borrowed("gren"),
                                        borrowed({"red", "green", "blue"}));
        }();

        CLAPP_CHECK(err.kind() == clapp::error_kind::invalid_value);
        // The safe idiom, and the one this suite demonstrates on purpose. context() returns
        // a COPY; as_string() borrows from whatever it is called on, so
        // `const auto v = err.context(k)->as_string();` leaves v pointing at a dead
        // temporary — reproduced as an ASan stack-use-after-scope, with no diagnostic from
        // either supported compiler. Bind the copy first, or go through context_ref().
        const std::optional<context_value> invalid_arg = err.context(context_kind::invalid_arg);
        const std::optional<std::string_view> arg_text = invalid_arg->as_string();
        CLAPP_CHECK(arg_text == "--color"sv);
        CLAPP_CHECK(err.context_ref(context_kind::suggested_value).as_string() == "green"sv);
        CLAPP_CHECK(err.context(context_kind::valid_value)->to_string() == "red, green, blue");
        CLAPP_CHECK(err.render().to_string() ==
                    "error: invalid value 'gren' for '--color'\n  [possible values: red, green, "
                    "blue]\n\n  tip: a similar value exists: 'green'\n");
    }

    // clap's Error::invalid_value / empty_value / value_validation take no `usage`
    // parameter at all, so the rendered message carries no `Usage:` block and
    // get(ContextKind::Usage) answers None. The three siblings that DO take one
    // (too_many_values, too_few_values, wrong_number_of_values) are the contrast that
    // makes this a rule rather than an oversight, so both halves are asserted together.
    consteval bool the_value_errors_carry_no_usage() {
        const error bad = error::invalid_value(
                cow_str::borrowed("--c"), cow_str::borrowed("blue"), borrowed({"true", "false"}));
        const error empty = error::empty_value(cow_str::borrowed("--o <o>"), {});
        const error rejected =
                error::value_validation(cow_str::borrowed("--c <c>"),
                                        cow_str::borrowed("abc"),
                                        cow_str::borrowed("invalid digit found in string"));
        styled_str line;
        line.push_plain("Usage: test");
        const error counted =
                error::too_few_values(cow_str::borrowed("--o <o>"), 2, 1, std::optional{line});

        return !bad.has_context(context_kind::usage) && !empty.has_context(context_kind::usage) &&
               !rejected.has_context(context_kind::usage) && !bad.render().contains("Usage:") &&
               !rejected.render().contains("Usage:") && counted.has_context(context_kind::usage) &&
               counted.render().contains("Usage: test");
    }

    static_assert(the_value_errors_carry_no_usage());

    CLAPP_TEST("error: context borrowed from a runtime string is not copied") {
        const std::string name = "--verbose";
        const error err        = error::unknown_argument(
                cow_str::borrowed(name), std::nullopt, std::nullopt, false, std::nullopt);

        const context_value value = *err.context(context_kind::invalid_arg);
        CLAPP_CHECK(value.as_string()->data() == name.data());
        CLAPP_CHECK(err.render().contains("'--verbose'"));
    }

    CLAPP_TEST("error: the messages match clap's wording") {
        CLAPP_CHECK(unknown_argument_renders_like_clap());
        CLAPP_CHECK(the_help_flag_adds_claps_closing_line());
        CLAPP_CHECK(a_trailing_value_gets_the_double_dash_tip());
        CLAPP_CHECK(a_flag_under_a_subcommand_becomes_a_tip());
        CLAPP_CHECK(a_local_flag_becomes_a_did_you_mean());
        CLAPP_CHECK(invalid_value_suggests_the_best_candidate());
        CLAPP_CHECK(invalid_value_suggests_nothing_when_nothing_is_close());
        CLAPP_CHECK(empty_value_says_a_value_is_required());
        CLAPP_CHECK(possible_values_are_escaped());
        CLAPP_CHECK(argument_conflict_names_the_other_argument());
        CLAPP_CHECK(argument_conflict_collapses_by_count());
        CLAPP_CHECK(an_argument_conflicting_with_itself_is_a_repeat());
        CLAPP_CHECK(subcommand_conflict_names_the_subcommand());
        CLAPP_CHECK(no_equals_asks_for_the_equals_sign());
        CLAPP_CHECK(missing_required_argument_lists_them_and_prints_usage());
        CLAPP_CHECK(missing_subcommand_lists_the_available_ones());
        CLAPP_CHECK(invalid_subcommand_suggests_the_nearest_name());
        CLAPP_CHECK(two_candidates_read_as_plural());
        CLAPP_CHECK(unrecognized_subcommand_has_nothing_to_suggest());
        CLAPP_CHECK(too_many_values_names_the_extra_one());
        CLAPP_CHECK(too_few_values_counts_and_conjugates());
        CLAPP_CHECK(wrong_number_of_values_counts_and_conjugates());
        CLAPP_CHECK(value_validation_appends_the_cause());
        CLAPP_CHECK(the_value_errors_carry_no_usage());
        CLAPP_CHECK(unnecessary_double_dash_explains_the_fix());
        CLAPP_CHECK(io_and_format_speak_through_their_source());
    }

    CLAPP_TEST("error: help and version are control flow, not failure") {
        CLAPP_CHECK(display_kinds_render_verbatim());

        const styled_str help{style_class::header, "Usage: prog"};
        const error on_request = error::display_help(help);
        const error on_missing = error::display_help_error(help);

        CLAPP_CHECK(on_request.render().to_string() == on_missing.render().to_string());
        CLAPP_CHECK(on_request.exit_code() == 0);
        CLAPP_CHECK(on_missing.exit_code() == 2);
        CLAPP_CHECK(!on_request.use_stderr());
        CLAPP_CHECK(on_missing.use_stderr());
    }

    CLAPP_TEST("error: context round-trips and the two formatters differ") {
        CLAPP_CHECK(context_round_trips());
        CLAPP_CHECK(absent_and_present_none_are_distinguishable());
        CLAPP_CHECK(rvalue_and_lvalue_builders_agree());
        CLAPP_CHECK(raw_wraps_the_message_but_ignores_context());
        CLAPP_CHECK(kind_only_drops_every_detail());
        CLAPP_CHECK(a_bare_error_falls_back_to_the_generic_sentence());
        CLAPP_CHECK(usage_is_appended_with_its_styling_intact());
    }

    CLAPP_TEST("error: copies compare equal and are independent") {
        error original   = error::no_equals(cow_str::borrowed("--color"), std::nullopt);
        const error copy = original;
        CLAPP_CHECK(copy == original);

        original.insert(context_kind::usage,
                        context_value::styled(styled_str{style_class::usage, "Usage: prog"}));
        CLAPP_CHECK(copy != original);
        CLAPP_CHECK(!copy.has_context(context_kind::usage));
        CLAPP_CHECK(original.render().contains("Usage: prog"));
        CLAPP_CHECK(!copy.render().contains("Usage: prog"));
    }

}  // namespace
