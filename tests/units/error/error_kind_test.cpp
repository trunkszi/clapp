#include <clapp/error/error_kind.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace {

    using clapp::all_error_kinds;
    using clapp::describe;
    using clapp::error_kind;
    using clapp::error_kind_count;
    using clapp::exit_code_of;
    using clapp::is_display;
    using clapp::name_of;
    using clapp::use_stderr;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // The set of kinds
    // ---------------------------------------------------------------------------

    // clap has seventeen; clapp has sixteen. The missing one is InvalidUtf8, which clapp
    // reports as value_validation carrying a parse_error of kind invalid_utf8 — recorded
    // as an intentional difference on clapp::error_kind.
    static_assert(error_kind_count == 16);
    static_assert(all_error_kinds.size() == error_kind_count);

    // Enumerator zero is invalid_value, matching clap's declaration order. Nothing depends
    // on a value-initialized error_kind being meaningful, but the order does matter: it is
    // the order the two enumerations are read side by side in.
    static_assert(all_error_kinds.front() == error_kind::invalid_value);
    static_assert(all_error_kinds.back() == error_kind::format);
    static_assert(static_cast<std::size_t>(error_kind::invalid_value) == 0);

    consteval bool every_kind_appears_exactly_once() {
        for (std::size_t value = 0; value < error_kind_count; ++value) {
            std::size_t seen = 0;
            for (const error_kind kind : all_error_kinds) {
                if (static_cast<std::size_t>(kind) == value) ++seen;
            }
            if (seen != 1) return false;
        }
        return true;
    }

    static_assert(every_kind_appears_exactly_once());

    // ---------------------------------------------------------------------------
    // Routing and exit codes
    // ---------------------------------------------------------------------------

    // The three display kinds, spelled out. These are the assertions the file comment is
    // about; the pair to compare is display_help against
    // display_help_on_missing_argument_or_subcommand, which a plausible bug — "all the
    // display kinds are successes" — collapses into one.
    static_assert(is_display(error_kind::display_help));
    static_assert(is_display(error_kind::display_help_on_missing_argument_or_subcommand));
    static_assert(is_display(error_kind::display_version));

    static_assert(!use_stderr(error_kind::display_help));
    static_assert(!use_stderr(error_kind::display_version));
    static_assert(use_stderr(error_kind::display_help_on_missing_argument_or_subcommand));

    static_assert(exit_code_of(error_kind::display_help) == 0);
    static_assert(exit_code_of(error_kind::display_version) == 0);
    static_assert(exit_code_of(error_kind::display_help_on_missing_argument_or_subcommand) == 2);

    // io and format are not display kinds even though they carry no description of their
    // own: they are ordinary failures that originate outside the parser.
    static_assert(!is_display(error_kind::io));
    static_assert(!is_display(error_kind::format));
    static_assert(exit_code_of(error_kind::io) == 2);
    static_assert(exit_code_of(error_kind::format) == 2);

    // Every kind answers both questions, and the two agree: stdout implies success and
    // stderr implies the usage code, with no third possibility.
    consteval bool routing_is_total_and_consistent() {
        std::size_t to_stdout = 0;
        for (const error_kind kind : all_error_kinds) {
            const int code = exit_code_of(kind);
            if (code != 0 && code != 2) return false;
            if (use_stderr(kind) != (code == 2)) return false;
            if (!use_stderr(kind)) ++to_stdout;
        }
        return to_stdout == 2;  // exactly display_help and display_version
    }

    static_assert(routing_is_total_and_consistent());

    // ---------------------------------------------------------------------------
    // describe() and name_of()
    // ---------------------------------------------------------------------------

    // The wording is clap's, verbatim: a user moving between the two libraries reads the
    // same sentence.
    static_assert(describe(error_kind::invalid_value) ==
                  "one of the values isn't valid for an argument"sv);
    static_assert(describe(error_kind::unknown_argument) == "unexpected argument found"sv);
    static_assert(describe(error_kind::invalid_subcommand) == "unrecognized subcommand"sv);
    static_assert(describe(error_kind::no_equals) ==
                  "equal is needed when assigning values to one of the arguments"sv);
    static_assert(describe(error_kind::value_validation) ==
                  "invalid value for one of the arguments"sv);
    static_assert(describe(error_kind::too_many_values) ==
                  "unexpected value for an argument found"sv);
    static_assert(describe(error_kind::too_few_values) == "more values required for an argument"sv);
    static_assert(describe(error_kind::wrong_number_of_values) ==
                  "too many or too few values for an argument"sv);
    static_assert(describe(error_kind::argument_conflict) ==
                  "an argument cannot be used with one or more of the other specified arguments"sv);
    static_assert(describe(error_kind::missing_required_argument) ==
                  "one or more required arguments were not provided"sv);
    static_assert(describe(error_kind::missing_subcommand) ==
                  "a subcommand is required but one was not provided"sv);

    // The five without a sentence of their own. For the display kinds the message is the
    // help or version text; for io and format it is the system's own message, attached as
    // the error's source.
    static_assert(describe(error_kind::display_help) == std::nullopt);
    static_assert(describe(error_kind::display_help_on_missing_argument_or_subcommand) ==
                  std::nullopt);
    static_assert(describe(error_kind::display_version) == std::nullopt);
    static_assert(describe(error_kind::io) == std::nullopt);
    static_assert(describe(error_kind::format) == std::nullopt);

    consteval bool describe_is_present_exactly_where_expected() {
        std::size_t described = 0;
        for (const error_kind kind : all_error_kinds) {
            const bool has = describe(kind).has_value();
            const bool wants =
                    !is_display(kind) && kind != error_kind::io && kind != error_kind::format;
            if (has != wants) return false;
            if (has) ++described;
        }
        return described == 11;
    }

    static_assert(describe_is_present_exactly_where_expected());

    // Kebab-case, matching name_of(clapp::arg_action) and clapp::naming::kebab. Names are
    // non-empty and distinct — the distinctness half is what catches a copy-pasted arm.
    static_assert(name_of(error_kind::invalid_value) == "invalid-value"sv);
    static_assert(name_of(error_kind::wrong_number_of_values) == "wrong-number-of-values"sv);
    static_assert(name_of(error_kind::display_help_on_missing_argument_or_subcommand) ==
                  "display-help-on-missing-argument-or-subcommand"sv);
    static_assert(name_of(error_kind::io) == "io"sv);

    consteval bool names_are_nonempty_and_distinct() {
        for (std::size_t i = 0; i < all_error_kinds.size(); ++i) {
            if (name_of(all_error_kinds[i]).empty()) return false;
            for (std::size_t j = i + 1; j < all_error_kinds.size(); ++j) {
                if (name_of(all_error_kinds[i]) == name_of(all_error_kinds[j])) return false;
            }
        }
        return true;
    }

    static_assert(names_are_nonempty_and_distinct());

    // The two spellings must not be confusable either: describe() is a sentence and
    // name_of() is an identifier, and no kind may answer the same for both.
    consteval bool name_and_description_never_coincide() {
        for (const error_kind kind : all_error_kinds) {
            if (describe(kind) == std::optional<std::string_view>{name_of(kind)}) return false;
        }
        return true;
    }

    static_assert(name_and_description_never_coincide());

    // ---------------------------------------------------------------------------
    // Runtime mirror
    // ---------------------------------------------------------------------------

    CLAPP_TEST("error_kind: the enumeration is complete and duplicate-free") {
        CLAPP_CHECK(every_kind_appears_exactly_once());
        CLAPP_CHECK(all_error_kinds.size() == 16);
    }

    CLAPP_TEST("error_kind: routing is total, and only two kinds reach stdout") {
        CLAPP_CHECK(routing_is_total_and_consistent());

        std::size_t stdout_kinds = 0;
        for (const error_kind kind : all_error_kinds) {
            if (!use_stderr(kind)) ++stdout_kinds;
        }
        CLAPP_CHECK(stdout_kinds == 2);
    }

    CLAPP_TEST("error_kind: help-on-missing-argument is an error, not a success") {
        // The one clap behaviour the design sketch gets wrong. Stated twice on purpose.
        CLAPP_CHECK(use_stderr(error_kind::display_help_on_missing_argument_or_subcommand));
        CLAPP_CHECK(exit_code_of(error_kind::display_help_on_missing_argument_or_subcommand) == 2);
        CLAPP_CHECK(exit_code_of(error_kind::display_help) == 0);
    }

    CLAPP_TEST("error_kind: every kind has a spelling and the five documented gaps") {
        CLAPP_CHECK(describe_is_present_exactly_where_expected());
        CLAPP_CHECK(names_are_nonempty_and_distinct());
        for (const error_kind kind : all_error_kinds) {
            CLAPP_CHECK(!name_of(kind).empty());
        }
    }

}  // namespace
