/**
 * \file
 * \brief clapp::value_range — inclusive value-count bounds and the infer() sentinel.
 */

#pragma once

#include <cstdlib>
#include <cstddef>
#include <limits>
#include <optional>

namespace clapp {

    /**
     * \brief Inclusive value-count range for one occurrence of an argument.
     *
     * Structural (public members) so it can reach `command_spec` / static storage.
     * Prefer factories over aggregate init. Map clap ranges at the call site:
     * `exactly(n)`, `between(lo, hi)` (inclusive), `at_least` / `at_most` / `full()`.
     * Exclusive clap forms (`5..10`) become `between(5, 9)` so the off-by-one stays
     * visible. infer() is the sentinel `[unbounded, 0]` — no real factory produces it.
     *
     * \warning Queries ignore infer and treat raw bounds as a flag: takes_values() is
     *          false, accepts_more(0) is false, etc. Skipping resolve leaves the next
     *          token as a positional. Call `r.resolve_or(default_num_args(act))` (or
     *          guard with is_infer()) before any other query.
     */
    struct value_range {
        /** \brief #end_inclusive meaning "no upper limit"; also #start_inclusive in infer(). */
        static constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();

        /**
         * \name Structural storage
         * Public because the type must be structural; defaults spell infer().
         * \{
         */
        std::size_t start_inclusive = unbounded; /**< Inclusive minimum number of values. */
        std::size_t end_inclusive   = 0;         /**< Inclusive maximum number of values. */
        /** \} */

        /**
         * \brief Unspecified arity; resolve against the action default before use.
         * \return Sentinel `[unbounded, 0]`.
         * \note Differs from clap (`SINGLE`): needed so `command_of` can tell an
         *       explicit `num_args = 1` from "inherit from action".
         */
        [[nodiscard]] static constexpr value_range infer() noexcept { return {}; }

        /** \brief No values at all: a flag. clap's `ValueRange::EMPTY`. */
        [[nodiscard]] static constexpr value_range empty() noexcept {
            return {.start_inclusive = 0, .end_inclusive = 0};
        }

        /** \brief Exactly one value, the common case for options. clap's `ValueRange::SINGLE`. */
        [[nodiscard]] static constexpr value_range single() noexcept {
            return {.start_inclusive = 1, .end_inclusive = 1};
        }

        /**
         * \brief Zero or one value — `--flag` or `--flag=value`. clap's `ValueRange::OPTIONAL`.
         *
         * \note Pairs with `default_missing_value`: the zero-value case has to be given a
         *       value from somewhere, or the argument matches with nothing recorded.
         */
        [[nodiscard]] static constexpr value_range optional() noexcept {
            return {.start_inclusive = 0, .end_inclusive = 1};
        }

        /** \brief Any number of values, including none. clap's `ValueRange::FULL`. */
        [[nodiscard]] static constexpr value_range full() noexcept {
            return {.start_inclusive = 0, .end_inclusive = unbounded};
        }

        /**
         * \brief Precisely \p count values.
         * \param count The fixed arity; `0` yields empty().
         */
        [[nodiscard]] static constexpr value_range exactly(std::size_t count) noexcept {
            return {.start_inclusive = count, .end_inclusive = count};
        }

        /** \brief At least \p min values, with no upper limit. */
        [[nodiscard]] static constexpr value_range at_least(std::size_t min) noexcept {
            return {.start_inclusive = min, .end_inclusive = unbounded};
        }

        /**
         * \brief At most \p max values, including none.
         * \param max Inclusive upper bound; `at_most(0)` is empty().
         */
        [[nodiscard]] static constexpr value_range at_most(std::size_t max) noexcept {
            return {.start_inclusive = 0, .end_inclusive = max};
        }

        /**
         * \brief Between \p min and \p max values, both bounds inclusive.
         * \param min Fewest values accepted.
         * \param max Most values accepted; pass #unbounded for an open top.
         * \return The range, or infer() when \p min exceeds \p max.
         * \note Reversed bounds abort constant evaluation (compile error) and return
         *       infer() at runtime so is_infer() stays exact.
         */
        [[nodiscard]] static constexpr value_range between(std::size_t min, std::size_t max) {
            if (min > max) {
                if consteval {
                    std::abort();
                }
                return infer();
            }
            return {.start_inclusive = min, .end_inclusive = max};
        }

        /** \brief Whether this is the infer() sentinel rather than a real range. */
        [[nodiscard]] constexpr bool is_infer() const noexcept {
            return start_inclusive == unbounded && end_inclusive == 0;
        }

        /**
         * \brief Whether the bounds are self-consistent.
         *
         * True for infer() and for every range a factory can produce. Only hand-written
         * aggregate initialization — `value_range{5, 3}` — can make it false.
         */
        [[nodiscard]] constexpr bool is_valid() const noexcept {
            return is_infer() || start_inclusive <= end_inclusive;
        }

        /**
         * \brief This range, or \p fallback when it is infer(). The intended way to
         *        discharge the sentinel before any other query.
         *
         * \code
         *     const value_range n = spec.num_args.resolve_or(default_num_args(spec.act));
         *     if (n.takes_values()) consume_a_value();
         * \endcode
         */
        [[nodiscard]] constexpr value_range resolve_or(value_range fallback) const noexcept {
            return is_infer() ? fallback : *this;
        }

        /** \brief Fewest values accepted. clap's `min_values`. */
        [[nodiscard]] constexpr std::size_t min_values() const noexcept { return start_inclusive; }

        /** \brief Most values accepted. clap's `max_values`. */
        [[nodiscard]] constexpr std::size_t max_values() const noexcept { return end_inclusive; }

        /**
         * \brief Whether the argument takes values at all, i.e. is not a flag.
         * \warning Answers `false` for infer(); see the table on value_range.
         */
        [[nodiscard]] constexpr bool takes_values() const noexcept { return end_inclusive != 0; }

        /** \brief Whether the upper bound is open. */
        [[nodiscard]] constexpr bool is_unbounded() const noexcept {
            return end_inclusive == unbounded;
        }

        /** \brief Whether the arity is a single number rather than a span. */
        [[nodiscard]] constexpr bool is_fixed() const noexcept {
            return start_inclusive == end_inclusive;
        }

        /**
         * \brief Whether more than one value can attach to one occurrence.
         * \note Variable arity counts as multiple even if max is 1 (`optional()` yes,
         *       `single()` no) — matches clap for help (`<V>...`) and token consumption.
         */
        [[nodiscard]] constexpr bool is_multiple() const noexcept {
            return start_inclusive != end_inclusive || 1 < start_inclusive;
        }

        /**
         * \brief The arity when it is fixed.
         * \return The count for a fixed range, otherwise `std::nullopt`.
         */
        [[nodiscard]] constexpr std::optional<std::size_t> num_values() const noexcept {
            return is_fixed() ? std::optional<std::size_t>{start_inclusive} : std::nullopt;
        }

        /**
         * \brief Whether another value may still be attached after \p current of them.
         * \param current How many values have been collected so far.
         */
        [[nodiscard]] constexpr bool accepts_more(std::size_t current) const noexcept {
            return current < end_inclusive;
        }

        /**
         * \brief Whether \p count satisfies both bounds.
         * \note This is the check that turns into `too_few_values` / `too_many_values`.
         */
        [[nodiscard]] constexpr bool contains(std::size_t count) const noexcept {
            return start_inclusive <= count && count <= end_inclusive;
        }

        /**
         * \brief Whether every count this range admits is also admitted by \p outer.
         * \param outer Permitted envelope, typically `max_num_args(act)`.
         * \note infer() is within every envelope. Used to reject e.g. set_true + num_args(3).
         */
        [[nodiscard]] constexpr bool is_within(value_range outer) const noexcept {
            if (is_infer()) return true;
            return outer.start_inclusive <= start_inclusive && end_inclusive <= outer.end_inclusive;
        }

        /** \brief Equality of bounds. Two infer() sentinels compare equal. */
        [[nodiscard]] constexpr bool operator==(const value_range&) const noexcept = default;
    };

    namespace detail {

        /**
         * Compile-time contract: value_range must stay structural for command_spec
         * static promotion; catch failures here rather than in define_static_array.
         */
        template<value_range>
        struct value_range_structural_probe {};

        /** \brief Proof that clapp::value_range is a structural type. */
        using value_range_is_structural = value_range_structural_probe<value_range{}>;

        static_assert(value_range{}.is_infer(),
                      "clapp: a default-constructed value_range must mean 'unspecified', "
                      "so arg_attr::num_args defaults to inference rather than SINGLE.");

        static_assert(!value_range::single().is_infer() && !value_range::empty().is_infer() &&
                              !value_range::full().is_infer(),
                      "clapp: the infer sentinel must be distinguishable from every real "
                      "range, including the degenerate ones.");

    }  // namespace detail

}  // namespace clapp
