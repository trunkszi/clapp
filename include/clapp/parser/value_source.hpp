/**
 * \file
 * \brief clapp::value_source — where a matched value came from.
 */

#pragma once


#include <array>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <string_view>

namespace clapp {

    /**
     * \brief Where the value stored for an argument came from. clap's `ValueSource`.
     *
     * `default_value` < `env_variable` < `command_line`.
     *
     * \warning **Enumerator values are the precedence order.** clapp::strongest() and
     *          clap's `MatchedArg::set_source` keep the larger source. Renumbering or
     *          inserting in the middle changes which value survives when an argument is
     *          set from two places — a wrong parse, not a compile error. The
     *          `static_assert`s below guard this.
     */
    enum class value_source : std::uint8_t {
        /** From `Arg::default_value` / `default_value_if`. clap: `DefaultValue`. */
        default_value = 0,
        /** From the env var named by `Arg::env`. clap: `EnvVariable`. */
        env_variable = 1,
        /** Typed on the command line. clap: `CommandLine`. */
        command_line = 2,
    };

    /** \brief How many sources there are. */
    inline constexpr std::size_t value_source_count = 3;

    /** \brief Every source, weakest first (enumerator order). */
    inline constexpr std::array<value_source, value_source_count> all_value_sources{
            value_source::default_value,
            value_source::env_variable,
            value_source::command_line,
    };

    /**
     * \brief Stable kebab-case name of \p source.
     * \param source The source to name.
     * \return Empty view for a value outside the enumeration.
     */
    [[nodiscard]] constexpr std::string_view name_of(value_source source) noexcept {
        switch (source) {
        case value_source::default_value:
            return "default-value";
        case value_source::env_variable:
            return "env-variable";
        case value_source::command_line:
            return "command-line";
        }
        return {};
    }

    /**
     * \brief One-line description of \p source for diagnostics.
     * \param source The source to describe.
     * \return Empty view for a value outside the enumeration.
     */
    [[nodiscard]] constexpr std::string_view describe(value_source source) noexcept {
        switch (source) {
        case value_source::default_value:
            return "the argument's default value; the user supplied nothing";
        case value_source::env_variable:
            return "the environment variable named by `env`";
        case value_source::command_line:
            return "typed on the command line";
        }
        return {};
    }

    /**
     * \brief Whether \p source counts as user-supplied. clap's `is_explicit`.
     * \param source The source to test.
     * \return `false` only for value_source::default_value.
     *
     * \warning **Not "typed on the command line".** An env value is explicit too;
     *          `required_if_eq`, `conflicts_with`, and ArgGroup occupancy ask "did
     *          anything other than a default put this here?". Reading it as "came from
     *          argv" makes env-supplied arguments silently drop out of those checks.
     */
    [[nodiscard]] constexpr bool is_explicit(value_source source) noexcept {
        return source != value_source::default_value;
    }

    /**
     * \brief The stronger of two sources (later in the enumeration wins).
     *
     * Port of clap's `MatchedArg::set_source` merge via `existing.max(source)`.
     *
     * \param a First source.
     * \param b Second source.
     * \return Whichever of \p a and \p b sits later in the enumeration.
     */
    [[nodiscard]] constexpr value_source strongest(value_source a, value_source b) noexcept {
        return a < b ? b : a;
    }

    namespace detail {

        /**
         * Compile-time contract: the enumeration *is* the precedence order.
         * Three comparisons so a failure names which pair went wrong.
         */
        static_assert(value_source::default_value < value_source::env_variable);
        static_assert(value_source::env_variable < value_source::command_line);
        static_assert(strongest(value_source::default_value, value_source::command_line) ==
                      value_source::command_line);
        static_assert(strongest(value_source::command_line, value_source::env_variable) ==
                      value_source::command_line);

        /**
         * Compile-time contract: every source is named and described, names distinct.
         * A forgotten `switch` case falls through to empty — this makes it loud.
         */
        [[nodiscard]] consteval bool value_source_tables_are_total() {
            for (std::size_t i = 0; i < all_value_sources.size(); ++i) {
                if (name_of(all_value_sources[i]).empty()) return false;
                if (describe(all_value_sources[i]).empty()) return false;
                for (std::size_t j = i + 1; j < all_value_sources.size(); ++j)
                    if (name_of(all_value_sources[i]) == name_of(all_value_sources[j]))
                        return false;
            }
            return true;
        }

        static_assert(value_source_tables_are_total());

    }  // namespace detail

}  // namespace clapp
