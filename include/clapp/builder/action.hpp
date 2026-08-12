/**
 * \file
 * \brief clapp::arg_action alias and tables: takes_values, default/max num_args, defaults.
 */

#pragma once

#include <clapp/builder/value_range.hpp>
#include <clapp/detail/std_meta.hpp>
#include <clapp/meta/annotations.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace clapp {
    /**
     * \brief Behaviour when an argument is matched. Alias of clapp::action (clap ArgAction).
     *
     * Maps clap's nine variants 1:1 (`set`, `append`, `set_true`/`false`, `count`,
     * `help`/`help_short`/`help_long`, `version`) plus clapp's infer sentinel.
     *
     * \warning arg_action::infer is not one of clap's nine. It is enumerator zero
     *          ("deduce from field type"); value-init is infer, not Set. Downstream of
     *          command_of must use a resolved action — is_resolved() / #resolved_actions.
     *          Query answers for infer are not a substitute for resolving it.
     */
    using arg_action = action;

    /**
     * \brief Accumulation type for count actions. clap's `CountType` (`std::uint8_t`).
     * \note Saturates at 255; widening would change get_count() and field capacity.
     */
    using count_type = unsigned char;

    /**
     * \brief The nine clap actions in declaration order, excluding arg_action::infer.
     */
    inline constexpr std::array<arg_action, 9> resolved_actions{
        arg_action::set,
        arg_action::append,
        arg_action::set_true,
        arg_action::set_false,
        arg_action::count,
        arg_action::help,
        arg_action::help_short,
        arg_action::help_long,
        arg_action::version
    };

    /** \brief Whether \p act names a real behaviour rather than the infer sentinel. */
    [[nodiscard]] constexpr bool is_resolved(arg_action act) noexcept {
        return act != arg_action::infer;
    }

    /**
     * \brief Kebab-case spelling of \p act for diagnostics (`"set-true"`, not `"SetTrue"`).
     * \return View into a string literal.
     */
    [[nodiscard]] constexpr std::string_view name_of(arg_action act) noexcept {
        switch (act) {
            case arg_action::infer:
                return "infer";
            case arg_action::set:
                return "set";
            case arg_action::append:
                return "append";
            case arg_action::set_true:
                return "set-true";
            case arg_action::set_false:
                return "set-false";
            case arg_action::count:
                return "count";
            case arg_action::help:
                return "help";
            case arg_action::help_short:
                return "help-short";
            case arg_action::help_long:
                return "help-long";
            case arg_action::version:
                return "version";
        }
        return {};
    }

    /**
     * \brief Whether \p act consumes command-line values (clap `takes_values`).
     * \return tri::yes for set/append, tri::no for the rest, tri::infer for infer.
     * \note Returns tri, not bool: `if (takes_values(act))` must not compile and treat
     *       infer as "no". Compare with `== tri::yes`. Env/default_value are separate.
     */
    [[nodiscard]] constexpr tri takes_values(arg_action act) noexcept {
        switch (act) {
            case arg_action::infer:
                return tri::infer;
            case arg_action::set:
            case arg_action::append:
                return tri::yes;
            case arg_action::set_true:
            case arg_action::set_false:
            case arg_action::count:
            case arg_action::help:
            case arg_action::help_short:
            case arg_action::help_long:
            case arg_action::version:
                return tri::no;
        }
        return tri::infer;
    }

    /**
     * \brief Implied value count when the argument does not set num_args.
     * \return single() for set/append, empty() for the rest, infer() for infer.
     */
    [[nodiscard]] constexpr value_range default_num_args(arg_action act) noexcept {
        switch (act) {
            case arg_action::infer:
                return value_range::infer();
            case arg_action::set:
            case arg_action::append:
                return value_range::single();
            case arg_action::set_true:
            case arg_action::set_false:
            case arg_action::count:
            case arg_action::help:
            case arg_action::help_short:
            case arg_action::help_long:
            case arg_action::version:
                return value_range::empty();
        }
        return value_range::infer();
    }

    /**
     * \brief Widest num_args \p act may be configured with (clap max_num_args).
     * \return full() for set/append/infer; optional() for set_true/false; empty() else.
     * \note Checked at consteval via value_range::is_within(), not only in debug.
     */
    [[nodiscard]] constexpr value_range max_num_args(arg_action act) noexcept {
        switch (act) {
            case arg_action::infer:
            case arg_action::set:
            case arg_action::append:
                return value_range::full();
            case arg_action::set_true:
            case arg_action::set_false:
                return value_range::optional();
            case arg_action::count:
            case arg_action::help:
            case arg_action::help_short:
            case arg_action::help_long:
            case arg_action::version:
                return value_range::empty();
        }
        return value_range::full();
    }

    /**
     * \brief Unparsed default when the argument never appears (clap default_value).
     * \return `"false"` / `"true"` / `"0"` for set_true / set_false / count; else nullopt.
     * \note Strings pass through value_parser so a remapped type still converts.
     */
    [[nodiscard]] constexpr std::optional<std::string_view>
    default_value_of(arg_action act) noexcept {
        switch (act) {
            case arg_action::set_true:
                return "false";
            case arg_action::set_false:
                return "true";
            case arg_action::count:
                return "0";
            case arg_action::infer:
            case arg_action::set:
            case arg_action::append:
            case arg_action::help:
            case arg_action::help_short:
            case arg_action::help_long:
            case arg_action::version:
                return std::nullopt;
        }
        return std::nullopt;
    }

    /**
     * \brief Value when the argument appears with no attached value (default_missing_value).
     * \return `"true"` / `"false"` for set_true / set_false; nullopt otherwise (count increments).
     */
    [[nodiscard]] constexpr std::optional<std::string_view>
    default_missing_value_of(arg_action act) noexcept {
        switch (act) {
            case arg_action::set_true:
                return "true";
            case arg_action::set_false:
                return "false";
            case arg_action::infer:
            case arg_action::set:
            case arg_action::append:
            case arg_action::count:
            case arg_action::help:
            case arg_action::help_short:
            case arg_action::help_long:
            case arg_action::version:
                return std::nullopt;
        }
        return std::nullopt;
    }

    /**
     * \brief Whether \p act prints help and stops parsing.
     *
     * \note Not a clap method; clap matches on the variants at each use site. It is
     *       broken out because the parser has to branch on "any of the three help
     *       actions" in three separate places, and `--help` is control flow rather
     *       than an error even though it travels as one.
     */
    [[nodiscard]] constexpr bool is_help(arg_action act) noexcept {
        return act == arg_action::help || act == arg_action::help_short ||
               act == arg_action::help_long;
    }

    /** \brief Whether \p act prints the version and stops parsing. */
    [[nodiscard]] constexpr bool is_version(arg_action act) noexcept {
        return act == arg_action::version;
    }

    /**
     * \brief Whether \p act ends the parse without producing an `arg_matches`.
     *
     * True for the three help actions and for `version`. These leave the parser as an
     * `error` carrying `display_help` / `display_version`, which the top level turns
     * into a write to stdout and an exit status of 0.
     */
    [[nodiscard]] constexpr bool is_terminating(arg_action act) noexcept {
        return is_help(act) || is_version(act);
    }

    namespace detail {
        // Reflection makes this a real check rather than a restatement: it fails the
        // build the moment an enumerator is added to clapp::action without being added
        // to resolved_actions, which is otherwise a silent omission from every
        // table-driven check in the library.
        // Spelled `^^action` rather than `^^arg_action`: reflecting through the alias
        // happens to work here, but trap 3 in CLAUDE.md — a type alias must be
        // `dealias`ed before its template arguments can be read — is close enough that
        // naming the underlying enumeration outright is the cheaper habit.
        static_assert(std::meta::enumerators_of(^^action).size() == resolved_actions.size() + 1,
                      "clapp: resolved_actions must list every clapp::action except the "
                      "infer sentinel.");

        // The sentinel must sort first so that a value-initialized arg_action means
        // "unspecified" rather than an arbitrary real behaviour.
        static_assert(arg_action{} == arg_action::infer);
        static_assert(!is_resolved(arg_action{}));

        // Each table must agree with the next: an action that takes no values may not
        // default to a range that does, and no action may default outside its envelope.
        /** \brief Verify that every action's default range fits its permitted range. */
        consteval bool action_tables_agree() {
            for (const arg_action act: resolved_actions) {
                if (!default_num_args(act).is_within(max_num_args(act))) return false;
                const bool wants = takes_values(act) == tri::yes;
                if (wants != default_num_args(act).takes_values()) return false;
            }
            return true;
        }

        static_assert(action_tables_agree(),
                      "clapp: takes_values(), default_num_args() and max_num_args() have "
                      "drifted apart. All three are ports of one `match` in clap's "
                      "action.rs and must be edited together.");
    } // namespace detail
} // namespace clapp
