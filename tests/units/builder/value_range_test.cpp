#include <clapp/builder/value_range.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>

namespace {

    using clapp::value_range;

    // ---------------------------------------------------------------------------
    // Layout
    // ---------------------------------------------------------------------------

    // Two words, trivially copyable: cheap enough to pass by value everywhere, which is
    // what every signature in the builder assumes.
    static_assert(sizeof(value_range) == 2 * sizeof(std::size_t));
    static_assert(std::is_trivially_copyable_v<value_range>);
    static_assert(std::is_aggregate_v<value_range>);

    // ---------------------------------------------------------------------------
    // The infer sentinel
    // ---------------------------------------------------------------------------

    static_assert(value_range{}.is_infer());
    static_assert(value_range::infer().is_infer());
    static_assert(value_range::infer() == value_range{});
    static_assert(value_range::infer().is_valid());

    // The sentinel must not collide with any real range, including the degenerate ones.
    static_assert(!value_range::empty().is_infer());
    static_assert(!value_range::single().is_infer());
    static_assert(!value_range::optional().is_infer());
    static_assert(!value_range::full().is_infer());
    static_assert(!value_range::exactly(0).is_infer());
    static_assert(!value_range::at_least(0).is_infer());
    static_assert(!value_range::at_most(0).is_infer());

    // Nor with the ranges that touch `unbounded` from either side.
    static_assert(!value_range::at_least(value_range::unbounded).is_infer());
    static_assert(!value_range::exactly(value_range::unbounded).is_infer());
    static_assert(!value_range::at_most(value_range::unbounded).is_infer());

    // resolve_or() is the only sanctioned way to discharge it.
    static_assert(value_range::infer().resolve_or(value_range::single()) == value_range::single());
    static_assert(value_range::empty().resolve_or(value_range::single()) == value_range::empty());

    // ---------------------------------------------------------------------------
    // Factories — bounds
    // ---------------------------------------------------------------------------

    static_assert(value_range::empty().min_values() == 0);
    static_assert(value_range::empty().max_values() == 0);
    static_assert(value_range::single().min_values() == 1);
    static_assert(value_range::single().max_values() == 1);
    static_assert(value_range::optional().min_values() == 0);
    static_assert(value_range::optional().max_values() == 1);
    static_assert(value_range::full().min_values() == 0);
    static_assert(value_range::full().max_values() == value_range::unbounded);
    static_assert(value_range::exactly(5).min_values() == 5);
    static_assert(value_range::exactly(5).max_values() == 5);
    static_assert(value_range::at_least(5).min_values() == 5);
    static_assert(value_range::at_least(5).max_values() == value_range::unbounded);
    static_assert(value_range::at_most(10).min_values() == 0);
    static_assert(value_range::at_most(10).max_values() == 10);
    static_assert(value_range::between(5, 10).min_values() == 5);
    static_assert(value_range::between(5, 10).max_values() == 10);

    // Degenerate but legal spellings collapse onto the named ranges.
    static_assert(value_range::exactly(0) == value_range::empty());
    static_assert(value_range::exactly(1) == value_range::single());
    static_assert(value_range::at_most(0) == value_range::empty());
    static_assert(value_range::at_most(1) == value_range::optional());
    static_assert(value_range::at_least(0) == value_range::full());
    static_assert(value_range::between(0, 0) == value_range::empty());
    static_assert(value_range::between(1, 1) == value_range::single());
    static_assert(value_range::between(0, 1) == value_range::optional());
    static_assert(value_range::between(0, value_range::unbounded) == value_range::full());

    // A reversed range yields the sentinel rather than a range that accepts nothing.
    // The compile-time half of this branch throws, so it can only be reached at runtime;
    // see the CLAPP_TEST below.
    static_assert(value_range::between(5, 10).is_valid());
    static_assert(!value_range{.start_inclusive = 5, .end_inclusive = 3}.is_valid());

    // ---------------------------------------------------------------------------
    // clap range syntax → clapp factories
    //
    // The table on clapp::value_range, executed. The exclusive forms are the ones worth
    // pinning: clap reaches them through `end.saturating_sub(1)`, which is exactly where
    // its `5..5` panic comes from.
    // ---------------------------------------------------------------------------

    static_assert(value_range::exactly(5) == value_range{5, 5});                        // 5
    static_assert(value_range::between(5, 10) == value_range{5, 10});                   // 5..=10
    static_assert(value_range::between(5, 9) == value_range{5, 9});                     // 5..10
    static_assert(value_range::at_least(5) == value_range{5, value_range::unbounded});  // 5..
    static_assert(value_range::at_most(10) == value_range{0, 10});                      // ..=10
    static_assert(value_range::at_most(9) == value_range{0, 9});                        // ..10
    static_assert(value_range::full() == value_range{0, value_range::unbounded});       // ..

    // ---------------------------------------------------------------------------
    // Queries — clap's `mod test`, case for case
    // ---------------------------------------------------------------------------

    // from_fixed: 5
    static_assert(value_range::exactly(5).is_fixed());
    static_assert(value_range::exactly(5).is_multiple());
    static_assert(value_range::exactly(5).num_values() == std::optional<std::size_t>{5});
    static_assert(value_range::exactly(5).takes_values());

    // from_fixed_empty: 0
    static_assert(value_range::exactly(0).is_fixed());
    static_assert(!value_range::exactly(0).is_multiple());
    static_assert(value_range::exactly(0).num_values() == std::optional<std::size_t>{0});
    static_assert(!value_range::exactly(0).takes_values());

    // from_range: 5..10
    static_assert(!value_range::between(5, 9).is_fixed());
    static_assert(value_range::between(5, 9).is_multiple());
    static_assert(value_range::between(5, 9).num_values() == std::nullopt);
    static_assert(value_range::between(5, 9).takes_values());

    // from_range_full: ..
    static_assert(!value_range::full().is_fixed());
    static_assert(value_range::full().is_multiple());
    static_assert(value_range::full().num_values() == std::nullopt);
    static_assert(value_range::full().takes_values());
    static_assert(value_range::full().is_unbounded());

    // from_range_from: 5..
    static_assert(value_range::at_least(5).is_unbounded());
    static_assert(value_range::at_least(5).is_multiple());

    // A variable arity counts as "multiple" even when it admits a single value: that is
    // what makes `0..=1` render as `<V>...` and keeps the parser looking for more.
    static_assert(value_range::optional().is_multiple());
    static_assert(!value_range::single().is_multiple());
    static_assert(!value_range::empty().is_multiple());

    // ---------------------------------------------------------------------------
    // accepts_more / contains — the arity checks the parser runs
    // ---------------------------------------------------------------------------

    static_assert(!value_range::empty().accepts_more(0));
    static_assert(value_range::single().accepts_more(0));
    static_assert(!value_range::single().accepts_more(1));
    static_assert(value_range::full().accepts_more(1'000'000));
    static_assert(value_range::between(2, 3).accepts_more(2));
    static_assert(!value_range::between(2, 3).accepts_more(3));

    static_assert(value_range::between(2, 3).contains(2));
    static_assert(value_range::between(2, 3).contains(3));
    static_assert(!value_range::between(2, 3).contains(1));
    static_assert(!value_range::between(2, 3).contains(4));
    static_assert(value_range::empty().contains(0));
    static_assert(!value_range::empty().contains(1));

    // ---------------------------------------------------------------------------
    // is_within — the envelope check that catches `.action(set_true).num_args(3)`
    // ---------------------------------------------------------------------------

    static_assert(value_range::single().is_within(value_range::full()));
    static_assert(value_range::empty().is_within(value_range::optional()));
    static_assert(value_range::optional().is_within(value_range::optional()));
    static_assert(!value_range::single().is_within(value_range::empty()));
    static_assert(!value_range::exactly(3).is_within(value_range::optional()));
    static_assert(!value_range::between(0, 5).is_within(value_range::between(1, 5)));

    // An unresolved range has claimed nothing, so it fits inside anything.
    static_assert(value_range::infer().is_within(value_range::empty()));

    // ---------------------------------------------------------------------------
    // The infer sentinel answers the queries as if it were a flag
    //
    // Pinned deliberately, not aspirationally: this is the silent-failure mode the
    // \warning on clapp::value_range describes, and a change to it must be a conscious
    // one that updates the documentation too.
    // ---------------------------------------------------------------------------

    static_assert(!value_range::infer().takes_values());
    static_assert(!value_range::infer().is_fixed());
    static_assert(value_range::infer().num_values() == std::nullopt);
    static_assert(!value_range::infer().accepts_more(0));
    static_assert(value_range::infer().is_multiple());

    // ---------------------------------------------------------------------------
    // Equality
    // ---------------------------------------------------------------------------

    static_assert(value_range::single() == value_range::single());
    static_assert(value_range::single() != value_range::empty());
    static_assert(value_range::infer() != value_range::empty());
    static_assert(value_range::infer() != value_range::full());

    // A value_range survives constant evaluation into a variable, as required by the
    // frozen command tree.
    constexpr value_range promoted = value_range::at_least(1);
    static_assert(promoted.min_values() == 1);
    static_assert(promoted.is_unbounded());

}  // namespace

CLAPP_TEST("value_range: infer is distinguishable from every real range") {
    CLAPP_CHECK(value_range{}.is_infer());
    CLAPP_CHECK(!value_range::single().is_infer());
    CLAPP_CHECK(value_range::infer().resolve_or(value_range::single()) == value_range::single());
}

CLAPP_TEST("value_range: between() yields infer at runtime when reversed") {
    // The compile-time half of this branch throws, so it can only be observed here.
    // Non-const locals keep the call out of a constant-expression context; even if the
    // optimizer folds it, `if consteval` is false during folding, so the answer is the
    // same one a genuine runtime call gives.
    std::size_t min = 5;
    std::size_t max = 3;
    CLAPP_CHECK(value_range::between(min, max).is_infer());
    CLAPP_CHECK(value_range::between(max, min) == value_range{3, 5});
}

CLAPP_TEST("value_range: hand-built reversed bounds are reported as invalid") {
    // Aggregate initialization can produce what the factories refuse to.
    const value_range reversed{.start_inclusive = 5, .end_inclusive = 3};
    CLAPP_CHECK(!reversed.is_valid());
    CLAPP_CHECK(!reversed.is_infer());
}

CLAPP_TEST("value_range: usable in a runtime container") {
    // std::vector is a transient allocation and cannot be a constexpr variable, so the
    // trivially-copyable claim gets its one runtime witness here.
    const std::vector<value_range> ranges{value_range::empty(),
                                          value_range::single(),
                                          value_range::at_least(1),
                                          value_range::infer()};
    CLAPP_CHECK(ranges.size() == 4);
    CLAPP_CHECK(!ranges[0].takes_values());
    CLAPP_CHECK(ranges[1].takes_values());
    CLAPP_CHECK(ranges[2].is_unbounded());
    CLAPP_CHECK(ranges[3].is_infer());
}
