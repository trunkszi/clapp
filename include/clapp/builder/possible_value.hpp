/**
 * \file
 * \brief clapp::possible_value — one accepted value with help, aliases, and hide flag.
 */

#pragma once

#include <clapp/detail/std_meta.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace clapp {

    /**
     * \brief One value an argument is allowed to take.
     *
     * Aggregate with with_* copy-builders for consteval chains.
     *
     * \warning Strings are borrowed, never copied. A name/help from consteval rename()
     *          dangles once the call returns (often only diagnosed when the value
     *          escapes). Use make_possible_value() to lift via define_static_string.
     * \warning Literal-backed names (`arg_id{"fast"}`) compare fine but cannot go into
     *          define_static_array (reflect_constant failed on GCC 16). Use
     *          make_possible_value() so the name is promoted to a static variable.
     */
    struct possible_value {
        /**
         * \name Structural storage
         * Public so the type stays structural for define_static_array.
         * \{
         */
        arg_id name{}; /**< Spelling the user types; byte-compared in matches(). */
        /** Help prose start; only when #help_length != 0. Never pointer-compared. */
        const char* help_text = nullptr;
        std::size_t help_length = 0; /**< Help bytes; 0 = absent. */
        const arg_id* alias_data = nullptr; /**< Alias array start, or null. */
        std::size_t alias_count = 0; /**< Number of aliases. */
        bool hide = false; /**< Omit from help and completions. */
        /** \} */

        /** \brief The spelling. clap's `get_name`. */
        [[nodiscard]] constexpr std::string_view get_name() const noexcept { return name.name(); }

        /**
         * \brief Help text if any. clap's `get_help`.
         * \return Borrowed view, or nullopt when absent.
         * \note #help_length is the sentinel (not a null pointer) so ubsan consteval folds.
         */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help() const noexcept {
            if (help_length == 0) return std::nullopt;
            return std::string_view{help_text, help_length};
        }

        /**
         * \brief The hidden aliases this value also answers to. clap's alias list.
         *
         * \return A view over the borrowed alias array; empty when there are none.
         * \note Aliases are always hidden — they exist so a renamed value keeps
         *       accepting its old spelling, and listing them in help would suggest they
         *       are equally canonical.
         */
        [[nodiscard]] constexpr std::span<const arg_id> get_aliases() const noexcept {
            return {alias_data, alias_count};
        }

        /** \brief Whether #hide is set. clap's `is_hide_set`. */
        [[nodiscard]] constexpr bool is_hide_set() const noexcept { return hide; }

        /**
         * \brief Whether this value should get its own line in the long help.
         *
         * clap's `should_show_help`: visible **and** carrying help text. A visible value
         * with no help appears only in the inline `[possible values: ...]` list.
         */
        [[nodiscard]] constexpr bool should_show_help() const noexcept {
            return !hide && help_length != 0;
        }

        /**
         * \brief The spelling, unless this value is hidden.
         *
         * \return The name, or `std::nullopt` when #hide is set.
         * \note clap's `get_visible_quoted_name` also wraps the result in quotes when it
         *       contains whitespace. That step is rendering and belongs to
         *       `clapp::output`; needs_quoting() reports the condition without
         *       performing it, so nothing here has to allocate.
         */
        [[nodiscard]] constexpr std::optional<std::string_view> get_visible_name() const noexcept {
            if (hide) return std::nullopt;
            return get_name();
        }

        /**
         * \brief Whether the spelling contains whitespace and must be quoted in help.
         *
         * A possible value may legitimately contain a space — clap's own test suite uses
         * `"secret speed"` — and rendering it bare would read as two separate values.
         */
        [[nodiscard]] constexpr bool needs_quoting() const noexcept {
            return std::ranges::any_of(get_name(), [](char c) noexcept {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            });
        }

        /**
         * \brief Whether \p value matches this name or any alias (clap matches).
         * \param value User text.
         * \param ignore_case ASCII case fold only; Unicode case folding is not performed.
         * \return True when \p value names this value.
         */
        [[nodiscard]] constexpr bool matches(std::string_view value,
                                             bool ignore_case) const noexcept {
            const auto same = [value, ignore_case](std::string_view candidate) noexcept {
                return ignore_case ? detail::equals_ignore_ascii_case(candidate, value)
                                   : candidate == value;
            };
            if (same(get_name())) return true;
            return std::ranges::any_of(get_aliases(), same, &arg_id::name);
        }

        /**
         * \brief A copy with a different spelling.
         * \warning \p spelling is borrowed; see the warning on possible_value.
         */
        [[nodiscard]] constexpr possible_value with_name(arg_id spelling) const noexcept {
            possible_value copy = *this;
            copy.name           = spelling;
            return copy;
        }

        /**
         * \brief A copy with help \p text (empty → absent). clap's `help`.
         * \warning \p text is borrowed; see possible_value.
         */
        [[nodiscard]] constexpr possible_value with_help(std::string_view text) const noexcept {
            possible_value copy = *this;
            copy.help_text      = text.empty() ? nullptr : text.data();
            copy.help_length    = text.size();
            return copy;
        }

        /** \brief A copy with #hide set to \p yes. clap's `hide`. */
        [[nodiscard]] constexpr possible_value with_hide(bool yes = true) const noexcept {
            possible_value copy = *this;
            copy.hide           = yes;
            return copy;
        }

        /**
         * \brief A copy that also answers to \p names. clap's `aliases`.
         * \param names Alias array with static storage duration.
         * \warning Array is referenced, not copied; use make_static_aliases() for locals.
         */
        [[nodiscard]] constexpr possible_value
        with_aliases(std::span<const arg_id> names) const noexcept {
            possible_value copy = *this;
            copy.alias_data     = names.data();
            copy.alias_count    = names.size();
            return copy;
        }

        /**
         * \brief Equality by content (not pointer identity of help/aliases).
         */
        [[nodiscard]] constexpr bool operator==(const possible_value& other) const noexcept {
            return name == other.name && get_help() == other.get_help() && hide == other.hide &&
                   std::ranges::equal(get_aliases(), other.get_aliases());
        }
    };

    /**
     * \brief Build a possible_value with name/help lifted into static storage.
     * \param name Spelling; may be transient (copied).
     * \param help Help text, or empty for none.
     * \return Value safe to return from consteval and pass to define_static_array.
     */
    [[nodiscard]] consteval possible_value make_possible_value(std::string_view name,
                                                               std::string_view help = {}) {
        possible_value value{.name = make_static_id(name)};
        if (!help.empty()) value = value.with_help(std::define_static_string(help));
        return value;
    }

    /**
     * \brief Lift aliases into static storage for possible_value::with_aliases().
     * \tparam N Alias count.
     * \param names Spellings; may be transient.
     * \return Span over static storage.
     */
    template<std::size_t N>
    [[nodiscard]] consteval std::span<const arg_id>
    make_static_aliases(const std::array<std::string_view, N>& names) {
        // A raw loop rather than `names | views::transform(make_static_id)`: the result
        // has to be a std::array to reach define_static_array, ranges::to has no
        // fixed-extent target, and an intermediate std::vector would be a transient
        // allocation that define_static_array then has to copy back out anyway.
        std::array<arg_id, N> ids{};
        for (std::size_t i = 0; i < N; ++i) ids[i] = make_static_id(names[i]);
        return std::define_static_array(ids);
    }

    namespace detail {

        /**
         * Compile-time contract: possible_value must remain a structural type, or
         * `command_of<T>()` loses its only route into static storage. Catching it here
         * names this file; the same mistake found by `define_static_array` names
         * neither the type nor the offending member.
         */
        template<possible_value>
        struct possible_value_structural_probe {};

        /** \brief Proof that clapp::possible_value is a structural type. */
        using possible_value_is_structural = possible_value_structural_probe<possible_value{}>;

        static_assert(possible_value{}.get_help() == std::nullopt,
                      "clapp: a default-constructed possible_value must carry no help, so "
                      "that should_show_help() cannot promote an empty string into a help "
                      "line.");

    }  // namespace detail

}  // namespace clapp
