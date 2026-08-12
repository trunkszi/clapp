/**
 * \file
 * \brief clapp::value_hint helpers: name_of, parse_value_hint, is_path, trailing-var checks.
 */

#pragma once

#include <clapp/detail/std_meta.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/util/str.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace clapp {

    /**
     * \brief Every value_hint in clap's declaration order (possible_values / completion table).
     * \note Shell support is uneven (path hints are the most portable; fish skips positionals).
     */
    inline constexpr std::array<value_hint, 13> all_value_hints{value_hint::unknown,
                                                                value_hint::other,
                                                                value_hint::any_path,
                                                                value_hint::file_path,
                                                                value_hint::dir_path,
                                                                value_hint::executable_path,
                                                                value_hint::command_name,
                                                                value_hint::command_string,
                                                                value_hint::command_with_arguments,
                                                                value_hint::username,
                                                                value_hint::hostname,
                                                                value_hint::url,
                                                                value_hint::email_address};

    /**
     * \brief Kebab-case spelling of \p hint (`"any-path"`).
     * \return View into a string literal.
     * \note parse_value_hint() also accepts clap's squashed form (`"anypath"`).
     */
    [[nodiscard]] constexpr std::string_view name_of(value_hint hint) noexcept {
        switch (hint) {
        case value_hint::unknown:
            return "unknown";
        case value_hint::other:
            return "other";
        case value_hint::any_path:
            return "any-path";
        case value_hint::file_path:
            return "file-path";
        case value_hint::dir_path:
            return "dir-path";
        case value_hint::executable_path:
            return "executable-path";
        case value_hint::command_name:
            return "command-name";
        case value_hint::command_string:
            return "command-string";
        case value_hint::command_with_arguments:
            return "command-with-arguments";
        case value_hint::username:
            return "username";
        case value_hint::hostname:
            return "hostname";
        case value_hint::url:
            return "url";
        case value_hint::email_address:
            return "email-address";
        }
        return {};
    }

    namespace detail {

        /**
         * \brief Equality ignoring ASCII case and `-` / `_` (AnyPath / anypath / any-path).
         */
        [[nodiscard]] constexpr bool equals_ignore_separators(std::string_view a,
                                                              std::string_view b) noexcept {
            constexpr auto is_separator = [](char c) noexcept { return c == '-' || c == '_'; };

            std::size_t i = 0;
            std::size_t j = 0;
            while (true) {
                while (i < a.size() && is_separator(a[i])) ++i;
                while (j < b.size() && is_separator(b[j])) ++j;
                if (i == a.size() || j == b.size()) break;
                if (to_lower(a[i]) != to_lower(b[j])) return false;
                ++i;
                ++j;
            }
            return i == a.size() && j == b.size();
        }

    }  // namespace detail

    /**
     * \brief Parse a value_hint from its spelling.
     * \param text Spelling to recognize.
     * \return The hint, or nullopt if unknown.
     * \note More permissive than clap FromStr: also accepts kebab/snake/Pascal forms.
     */
    [[nodiscard]] constexpr std::optional<value_hint>
    parse_value_hint(std::string_view text) noexcept {
        for (const value_hint hint : all_value_hints) {
            if (detail::equals_ignore_separators(text, name_of(hint))) return hint;
        }
        return std::nullopt;
    }

    /**
     * \brief Whether \p hint is a filesystem-path completer (any/file/dir/executable_path).
     */
    [[nodiscard]] constexpr bool is_path(value_hint hint) noexcept {
        switch (hint) {
        case value_hint::any_path:
        case value_hint::file_path:
        case value_hint::dir_path:
        case value_hint::executable_path:
            return true;
        case value_hint::unknown:
        case value_hint::other:
        case value_hint::command_name:
        case value_hint::command_string:
        case value_hint::command_with_arguments:
        case value_hint::username:
        case value_hint::hostname:
        case value_hint::url:
        case value_hint::email_address:
            return false;
        }
        return false;
    }

    /**
     * \brief Whether \p hint constrains argument configuration.
     * \return True only for value_hint::command_with_arguments.
     * \warning command_with_arguments is not inert: needs a positional with
     *          num_args at_least(1) and command trailing_var_arg. Without those,
     *          completion and parse silently diverge (e.g. `-la` parsed as our flags).
     *          command_of checks this combination at consteval.
     */
    [[nodiscard]] constexpr bool requires_trailing_var_arg(value_hint hint) noexcept {
        return hint == value_hint::command_with_arguments;
    }

    namespace detail {

        // Reflection turns this into a real check: adding an enumerator to
        // clapp::value_hint without adding it to all_value_hints would otherwise drop it
        // silently from the possible-value list and from parse_value_hint().
        static_assert(std::meta::enumerators_of(^^value_hint).size() == all_value_hints.size(),
                      "clapp: all_value_hints must list every clapp::value_hint.");

        static_assert(value_hint{} == value_hint::unknown,
                      "clapp: a value-initialized value_hint must mean 'no hint given', "
                      "matching clap's `#[default] Unknown`.");

        // Every spelling of a hint must round-trip, and clap's own squashed input must
        // keep working now that the canonical form is kebab-cased.
        static_assert(parse_value_hint("any-path") == value_hint::any_path);
        static_assert(parse_value_hint("any_path") == value_hint::any_path);
        static_assert(parse_value_hint("AnyPath") == value_hint::any_path);
        static_assert(parse_value_hint("anypath") == value_hint::any_path);
        static_assert(parse_value_hint("any path") == std::nullopt);

        /** \brief Verify that every canonical value-hint name parses back to its value. */
        consteval bool value_hint_names_round_trip() {
            for (const value_hint hint : all_value_hints) {
                if (parse_value_hint(name_of(hint)) != hint) return false;
            }
            return true;
        }

        static_assert(value_hint_names_round_trip(),
                      "clapp: name_of() and parse_value_hint() must be inverses. A "
                      "duplicated or misspelled name makes one hint unreachable.");

    }  // namespace detail

}  // namespace clapp
