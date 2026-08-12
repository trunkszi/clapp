/**
 * \file
 * \brief clapp::error_kind and the routing helpers (stream, exit code, description).
 */

#pragma once


#include <array>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <optional>
#include <string_view>

namespace clapp {

    /**
     * \brief Why a parse ended the way it did. clap's `ErrorKind`.
     *
     * Sixteen enumerators in clap's declaration order.
     *
     * \warning **clap's `InvalidUtf8` has no counterpart here.** clapp::os_str keeps
     *          ill-formed bytes losslessly; clapp::value_parser reports the failure as
     *          clapp::parse_error_kind::invalid_utf8, which reaches the user as
     *          error_kind::value_validation. Ports of clap's `utf8.rs` therefore expect
     *          `value_validation` where clap expects `InvalidUtf8`.
     */
    enum class error_kind : std::uint8_t {
        /** An argument with a fixed set of possible values got something else. */
        invalid_value,
        /** A flag, option or positional that the command does not define. */
        unknown_argument,
        /** A subcommand close enough to a real one to name; else unknown_argument. */
        invalid_subcommand,
        /** `--opt value` where the argument was configured `require_equals`. */
        no_equals,
        /** A value that the argument's clapp::value_parser rejected. */
        value_validation,
        /** More values than `num_args` allows. */
        too_many_values,
        /** Fewer values than `num_args` requires. */
        too_few_values,
        /** A count of values that `num_args` does not allow, in either direction. */
        wrong_number_of_values,
        /** Two conflicting arguments, or one that appeared twice when it may not. */
        argument_conflict,
        /** A `required` argument that never appeared. */
        missing_required_argument,
        /** `subcommand_required` with no subcommand given. */
        missing_subcommand,
        /** Not a failure: `--help`. Help text on **stdout**, exit **0**. */
        display_help,
        /**
         * `arg_required_else_help` with nothing given. Help text but still a failure:
         * **stderr**, exit **2**. See the enum \warning.
         */
        display_help_on_missing_argument_or_subcommand,
        /** Not a failure: `--version`. **stdout**, exit **0**. */
        display_version,
        /** An I/O failure, typically while writing the message itself. */
        io,
        /** A formatting failure. clap's `ErrorKind::Format`. */
        format,
    };

    /** \brief How many clapp::error_kind values there are. */
    inline constexpr std::size_t error_kind_count = 16;

    /** \brief Every clapp::error_kind, in declaration order. */
    inline constexpr std::array<error_kind, error_kind_count> all_error_kinds{
            error_kind::invalid_value,
            error_kind::unknown_argument,
            error_kind::invalid_subcommand,
            error_kind::no_equals,
            error_kind::value_validation,
            error_kind::too_many_values,
            error_kind::too_few_values,
            error_kind::wrong_number_of_values,
            error_kind::argument_conflict,
            error_kind::missing_required_argument,
            error_kind::missing_subcommand,
            error_kind::display_help,
            error_kind::display_help_on_missing_argument_or_subcommand,
            error_kind::display_version,
            error_kind::io,
            error_kind::format,
    };

    /**
     * \brief Kebab-cased spelling of \p kind (diagnostics and tests).
     * \param kind The kind to spell.
     * \return View into a string literal, valid for the program lifetime.
     * \note Kebab-case (not clap's Rust identifiers), matching clapp::naming::kebab.
     */
    [[nodiscard]] constexpr std::string_view name_of(error_kind kind) noexcept {
        switch (kind) {
        case error_kind::invalid_value:
            return "invalid-value";
        case error_kind::unknown_argument:
            return "unknown-argument";
        case error_kind::invalid_subcommand:
            return "invalid-subcommand";
        case error_kind::no_equals:
            return "no-equals";
        case error_kind::value_validation:
            return "value-validation";
        case error_kind::too_many_values:
            return "too-many-values";
        case error_kind::too_few_values:
            return "too-few-values";
        case error_kind::wrong_number_of_values:
            return "wrong-number-of-values";
        case error_kind::argument_conflict:
            return "argument-conflict";
        case error_kind::missing_required_argument:
            return "missing-required-argument";
        case error_kind::missing_subcommand:
            return "missing-subcommand";
        case error_kind::display_help:
            return "display-help";
        case error_kind::display_help_on_missing_argument_or_subcommand:
            return "display-help-on-missing-argument-or-subcommand";
        case error_kind::display_version:
            return "display-version";
        case error_kind::io:
            return "io";
        case error_kind::format:
            return "format";
        }
        return {};
    }

    /**
     * \brief One-line description of \p kind when it has one. clap's `ErrorKind::as_str`.
     * \param kind The kind to describe.
     * \return The sentence, or `nullopt` for the three display kinds, io, and format.
     */
    [[nodiscard]] constexpr std::optional<std::string_view> describe(error_kind kind) noexcept {
        switch (kind) {
        case error_kind::invalid_value:
            return "one of the values isn't valid for an argument";
        case error_kind::unknown_argument:
            return "unexpected argument found";
        case error_kind::invalid_subcommand:
            return "unrecognized subcommand";
        case error_kind::no_equals:
            return "equal is needed when assigning values to one of the arguments";
        case error_kind::value_validation:
            return "invalid value for one of the arguments";
        case error_kind::too_many_values:
            return "unexpected value for an argument found";
        case error_kind::too_few_values:
            return "more values required for an argument";
        case error_kind::wrong_number_of_values:
            return "too many or too few values for an argument";
        case error_kind::argument_conflict:
            return "an argument cannot be used with one or more of the other specified "
                   "arguments";
        case error_kind::missing_required_argument:
            return "one or more required arguments were not provided";
        case error_kind::missing_subcommand:
            return "a subcommand is required but one was not provided";
        case error_kind::display_help:
        case error_kind::display_help_on_missing_argument_or_subcommand:
        case error_kind::display_version:
        case error_kind::io:
        case error_kind::format:
            return std::nullopt;
        }
        return std::nullopt;
    }

    /**
     * \brief Whether \p kind carries a rendered document rather than a diagnostic.
     *
     * True for the three `display_*` kinds; the renderer passes the message through
     * without prefixing `error:`.
     *
     * \warning This is **not** "did the program succeed". Use use_stderr() or
     *          exit_code_of(); display_help_on_missing_argument_or_subcommand is
     *          `true` here and still exits 2.
     */
    [[nodiscard]] constexpr bool is_display(error_kind kind) noexcept {
        return kind == error_kind::display_help ||
               kind == error_kind::display_help_on_missing_argument_or_subcommand ||
               kind == error_kind::display_version;
    }

    /**
     * \brief Whether the message for \p kind belongs on stderr.
     * \param kind The kind to route.
     * \return `false` (stdout) only for display_help and display_version; everything
     *         else — including display_help_on_missing_argument_or_subcommand — is stderr.
     */
    [[nodiscard]] constexpr bool use_stderr(error_kind kind) noexcept {
        return !(kind == error_kind::display_help || kind == error_kind::display_version);
    }

    /**
     * \brief Process exit status for \p kind: `0` on stdout, `2` otherwise.
     * \param kind The kind to score.
     * \note Defined via use_stderr() so the two cannot disagree on
     *       display_help_on_missing_argument_or_subcommand.
     */
    [[nodiscard]] constexpr int exit_code_of(error_kind kind) noexcept {
        return use_stderr(kind) ? 2 : 0;
    }

    namespace detail {

        static_assert(all_error_kinds.size() == error_kind_count);

        // Exhaustive + duplicate-free without reflecting the enum (avoids pulling
        // reflection into every error consumer).
        /** \brief Verify that the error-kind table is exhaustive and duplicate-free. */
        consteval bool all_error_kinds_is_a_set() {
            for (std::size_t i = 0; i < all_error_kinds.size(); ++i) {
                for (std::size_t j = i + 1; j < all_error_kinds.size(); ++j) {
                    if (all_error_kinds[i] == all_error_kinds[j]) return false;
                }
            }
            for (std::size_t value = 0; value < error_kind_count; ++value) {
                bool found = false;
                for (const error_kind kind : all_error_kinds) {
                    if (static_cast<std::size_t>(kind) == value) found = true;
                }
                if (!found) return false;
            }
            return true;
        }

        static_assert(all_error_kinds_is_a_set(),
                      "clapp: all_error_kinds must list every clapp::error_kind exactly "
                      "once, with contiguous underlying values.");

        // Pin routing: display_help_on_missing_argument_or_subcommand must stay exit 2.
        static_assert(!use_stderr(error_kind::display_help));
        static_assert(!use_stderr(error_kind::display_version));
        static_assert(use_stderr(error_kind::display_help_on_missing_argument_or_subcommand));
        static_assert(exit_code_of(error_kind::display_help) == 0);
        static_assert(exit_code_of(error_kind::display_version) == 0);
        static_assert(exit_code_of(error_kind::display_help_on_missing_argument_or_subcommand) ==
                      2);
        static_assert(exit_code_of(error_kind::unknown_argument) == 2);

        /** \brief Verify that every diagnostic kind has exactly the expected description. */
        consteval bool describe_covers_every_kind() {
            for (const error_kind kind : all_error_kinds) {
                const bool has_sentence = describe(kind).has_value();
                const bool wants_sentence =
                        !is_display(kind) && kind != error_kind::io && kind != error_kind::format;
                if (has_sentence != wants_sentence) return false;
                if (name_of(kind).empty()) return false;
            }
            return true;
        }

        static_assert(describe_covers_every_kind());

    }  // namespace detail

}  // namespace clapp
