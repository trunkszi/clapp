#include <clapp/util/flat_map.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using clapp::flat_map;

    using map_ii = flat_map<int, int>;
    using map_si = flat_map<std::string, int, std::less<>>;

    // ---------------------------------------------------------------------------
    // Container/range concept conformance
    //
    // These are the cheapest possible regression tests: if flat_map ever stops
    // modelling a range, every `m | views::filter(...)` in the parser breaks, and it
    // breaks with a template error a hundred lines long. Catch it here instead.
    // ---------------------------------------------------------------------------

    static_assert(std::ranges::range<map_ii>);
    static_assert(std::ranges::range<const map_ii>);
    static_assert(std::ranges::sized_range<map_ii>);
    static_assert(std::ranges::random_access_range<map_ii>);
    static_assert(std::ranges::common_range<map_ii>);
    static_assert(std::same_as<std::ranges::range_value_t<map_ii>, std::pair<int, int>>);
    static_assert(std::same_as<std::ranges::range_reference_t<map_ii>, const std::pair<int, int>&>);
    static_assert(std::same_as<map_ii::iterator, map_ii::const_iterator>);
    static_assert(std::same_as<map_ii::key_type, int>);
    static_assert(std::same_as<map_ii::mapped_type, int>);
    static_assert(std::same_as<map_ii::value_type, std::pair<int, int>>);
    static_assert(std::default_initializable<map_ii>);
    static_assert(std::copyable<map_ii>);
    static_assert(std::movable<map_ii>);
    static_assert(std::equality_comparable<map_ii>);

    // The transparent-lookup overloads must exist only when the comparator opts in.
    //
    // Each negative check goes through a named concept rather than a bare
    // `static_assert(!requires{...})`. GCC 16.1.0 turns the inner ill-formed call
    // into a hard `error: no matching function for call` instead of an unsatisfied
    // requirement when it is written inline, so the inline spelling cannot express
    // "this overload must not exist" at all.
    template<class M, class K>
    concept findable = requires(M m, K k) { m.find(k); };
    template<class M, class K>
    concept containable = requires(M m, K k) { m.contains(k); };
    template<class M, class K>
    concept value_findable = requires(M m, K k) { m.find_value(k); };
    template<class M, class K>
    concept key_erasable = requires(M m, K k) { m.erase(k); };
    template<class M, class K>
    concept entry_erasable = requires(M m, K k) { m.erase_entry(k); };

    static_assert(clapp::detail::transparent_compare<std::less<>>);
    static_assert(!clapp::detail::transparent_compare<std::less<int>>);
    static_assert(findable<map_si, std::string_view>);
    static_assert(!findable<map_ii, std::string_view>);
    static_assert(containable<map_si, std::string_view>);
    static_assert(!containable<map_ii, std::string_view>);
    static_assert(value_findable<map_si, std::string_view>);
    static_assert(!value_findable<map_ii, std::string_view>);
    static_assert(key_erasable<map_si, std::string_view>);
    static_assert(!key_erasable<map_ii, std::string_view>);
    static_assert(entry_erasable<map_si, std::string_view>);
    static_assert(!entry_erasable<map_ii, std::string_view>);

    // operator[] needs a value-initializable mapped type; a map of a type without a
    // default constructor must still compile, just without that one member.
    struct no_default {
        int v;
        constexpr explicit no_default(int value) : v(value) {}
        constexpr bool operator==(const no_default&) const = default;
    };
    template<class M>
    concept has_subscript = requires(M m) { m[0]; };
    template<class M>
    concept has_try_emplace = requires(M m) { m.try_emplace(0, 1); };
    static_assert(has_subscript<map_ii>);
    static_assert(!has_subscript<flat_map<int, no_default>>);
    static_assert(has_try_emplace<flat_map<int, no_default>>);

    // ---------------------------------------------------------------------------
    // Reports
    //
    // Each probe returns a literal aggregate so that one constant evaluation can
    // feed many independent `static_assert`s; a failure then names the property that
    // broke rather than "the big test returned false".
    // ---------------------------------------------------------------------------

    /** Keys observed in iteration order, plus their count. */
    struct key_trace {
        std::array<int, 8> keys{};
        std::array<int, 8> values{};
        std::size_t count = 0;
    };

    /** Walk \p m and record what iteration yields. */
    constexpr key_trace trace(const map_ii& m) {
        key_trace out;
        // Raw loop rather than `ranges::copy`: two parallel outputs plus a counter,
        // and the destination is a fixed array that must not be overrun.
        for (const std::pair<int, int>& entry : m) {
            if (out.count >= out.keys.size()) break;
            out.keys[out.count]   = entry.first;
            out.values[out.count] = entry.second;
            ++out.count;
        }
        return out;
    }

    /** Whether \p m yields exactly \p expected_keys, in that order. */
    constexpr bool keys_are(const map_ii& m, std::span<const int> expected_keys) {
        return std::ranges::equal(m.keys(), expected_keys);
    }

    // --- construction ----------------------------------------------------------

    struct construction_report {
        bool default_is_empty     = false;
        bool default_begin_is_end = false;
        std::size_t init_size     = 0;
        bool init_sorted          = false;
        bool init_first_wins      = false;
        std::size_t range_size    = 0;
        bool range_sorted         = false;
        bool comparator_stored    = false;
    };

    constexpr construction_report probe_construction() {
        construction_report r;

        const map_ii empty;
        r.default_is_empty     = empty.empty() && empty.size() == 0;
        r.default_begin_is_end = empty.begin() == empty.end();

        // Deliberately unsorted, with a duplicate key carrying a different value.
        const map_ii init{{3, 30}, {1, 10}, {2, 20}, {1, 99}};
        r.init_size       = init.size();
        r.init_sorted     = std::ranges::is_sorted(init.keys());
        const int* first  = init.find_value(1);
        r.init_first_wins = first != nullptr && *first == 10;

        const std::array<std::pair<int, int>, 3> source{{{9, 90}, {5, 50}, {7, 70}}};
        const map_ii from_range(std::from_range, source);
        r.range_size   = from_range.size();
        r.range_sorted = std::ranges::is_sorted(from_range.keys());

        const flat_map<int, int, std::greater<int>> descending{{1, 10}, {2, 20}, {3, 30}};
        r.comparator_stored = std::ranges::is_sorted(descending.keys(), std::greater<int>{});

        return r;
    }

    constexpr construction_report construction = probe_construction();

    static_assert(construction.default_is_empty);
    static_assert(construction.default_begin_is_end);
    static_assert(construction.init_size == 3);
    static_assert(construction.init_sorted);
    static_assert(construction.init_first_wins);
    static_assert(construction.range_size == 3);
    static_assert(construction.range_sorted);
    static_assert(construction.comparator_stored);

    // --- ordering --------------------------------------------------------------

    struct ordering_report {
        std::array<int, 8> keys{};
        std::size_t count     = 0;
        bool reverse_descends = false;
        bool values_follow    = false;
    };

    constexpr ordering_report probe_ordering() {
        ordering_report r;
        map_ii m;
        // Inserted in an order chosen to be wrong in every way: descending, then a
        // value that belongs in the middle, then one that belongs at the front.
        m.try_emplace(50, 500);
        m.try_emplace(40, 400);
        m.try_emplace(45, 450);
        m.try_emplace(10, 100);

        const key_trace t = trace(m);
        r.keys            = t.keys;
        r.count           = t.count;

        const std::array<int, 4> expected_reverse{50, 45, 40, 10};
        r.reverse_descends =
                std::ranges::equal(std::ranges::subrange(m.rbegin(), m.rend()) |
                                           std::views::transform(&map_ii::value_type::first),
                                   expected_reverse);

        r.values_follow = std::ranges::equal(m.values(), std::array<int, 4>{100, 400, 450, 500});
        return r;
    }

    constexpr ordering_report ordering = probe_ordering();

    static_assert(ordering.count == 4);
    static_assert(ordering.keys[0] == 10);
    static_assert(ordering.keys[1] == 40);
    static_assert(ordering.keys[2] == 45);
    static_assert(ordering.keys[3] == 50);
    static_assert(ordering.reverse_descends);
    static_assert(ordering.values_follow);

    // --- lookup ----------------------------------------------------------------

    struct lookup_report {
        bool found_present         = false;
        bool missing_is_end        = false;
        bool contains_present      = false;
        bool contains_absent       = false;
        bool value_ptr_present     = false;
        bool value_ptr_absent_null = false;
        bool mutable_value_writes  = false;
        bool below_range_missing   = false;
        bool above_range_missing   = false;
        bool between_keys_missing  = false;
    };

    constexpr lookup_report probe_lookup() {
        lookup_report r;
        map_ii m{{10, 1}, {20, 2}, {30, 3}};

        const map_ii::const_iterator hit = m.find(20);
        r.found_present                  = hit != m.end() && hit->second == 2;
        r.missing_is_end                 = m.find(21) == m.end();

        r.contains_present = m.contains(30);
        r.contains_absent  = !m.contains(31);

        const int* present      = m.find_value(10);
        r.value_ptr_present     = present != nullptr && *present == 1;
        r.value_ptr_absent_null = m.find_value(11) == nullptr;

        if (int* writable = m.find_value(20)) {
            *writable              = 222;
            const int* readback    = m.find_value(20);
            r.mutable_value_writes = readback != nullptr && *readback == 222;
        }

        // The three ways a binary search can land outside a match.
        r.below_range_missing  = m.find(1) == m.end();
        r.above_range_missing  = m.find(99) == m.end();
        r.between_keys_missing = m.find(25) == m.end();
        return r;
    }

    constexpr lookup_report lookup = probe_lookup();

    static_assert(lookup.found_present);
    static_assert(lookup.missing_is_end);
    static_assert(lookup.contains_present);
    static_assert(lookup.contains_absent);
    static_assert(lookup.value_ptr_present);
    static_assert(lookup.value_ptr_absent_null);
    static_assert(lookup.mutable_value_writes);
    static_assert(lookup.below_range_missing);
    static_assert(lookup.above_range_missing);
    static_assert(lookup.between_keys_missing);

    // --- insertion -------------------------------------------------------------

    struct insert_report {
        bool emplace_reports_insert = false;
        bool emplace_rejects_dup    = false;
        bool emplace_keeps_old      = false;
        bool emplace_iterator_valid = false;
        bool assign_reports_insert  = false;
        bool assign_reports_update  = false;
        bool assign_overwrites      = false;
        bool subscript_creates_zero = false;
        bool subscript_reuses       = false;
        bool or_insert_creates      = false;
        bool or_insert_reuses       = false;
        int factory_calls           = 0;
        std::size_t final_size      = 0;
        bool still_sorted           = false;
    };

    constexpr insert_report probe_insert() {
        insert_report r;
        map_ii m;

        const std::pair<map_ii::iterator, bool> fresh = m.try_emplace(5, 50);
        r.emplace_reports_insert                      = fresh.second;
        r.emplace_iterator_valid =
                fresh.first != m.end() && fresh.first->first == 5 && fresh.first->second == 50;

        const std::pair<map_ii::iterator, bool> dup = m.try_emplace(5, 999);
        r.emplace_rejects_dup                       = !dup.second;
        const int* kept                             = m.find_value(5);
        r.emplace_keeps_old                         = kept != nullptr && *kept == 50;

        r.assign_reports_insert = m.insert_or_assign(6, 60).second;
        r.assign_reports_update = !m.insert_or_assign(6, 66).second;
        const int* updated      = m.find_value(6);
        r.assign_overwrites     = updated != nullptr && *updated == 66;

        r.subscript_creates_zero = m[7] == 0;
        m[7]                     = 70;
        r.subscript_reuses       = m[7] == 70 && m.size() == 3;

        int& made           = m.or_insert_with(8, [&r] {
            ++r.factory_calls;
            return 80;
        });
        r.or_insert_creates = made == 80;
        made                = 88;
        const int& again    = m.or_insert_with(8, [&r] {
            ++r.factory_calls;
            return 0;
        });
        r.or_insert_reuses  = again == 88;

        r.final_size   = m.size();
        r.still_sorted = std::ranges::is_sorted(m.keys());
        return r;
    }

    constexpr insert_report insertion = probe_insert();

    static_assert(insertion.emplace_reports_insert);
    static_assert(insertion.emplace_iterator_valid);
    static_assert(insertion.emplace_rejects_dup);
    static_assert(insertion.emplace_keeps_old);
    static_assert(insertion.assign_reports_insert);
    static_assert(insertion.assign_reports_update);
    static_assert(insertion.assign_overwrites);
    static_assert(insertion.subscript_creates_zero);
    static_assert(insertion.subscript_reuses);
    static_assert(insertion.or_insert_creates);
    static_assert(insertion.or_insert_reuses);
    static_assert(insertion.factory_calls == 1);  // the second call must not run
    static_assert(insertion.final_size == 4);
    static_assert(insertion.still_sorted);

    // --- insertion at every position -------------------------------------------

    struct position_report {
        bool front  = false;
        bool middle = false;
        bool back   = false;
    };

    constexpr position_report probe_positions() {
        position_report r;

        map_ii front{{20, 1}, {40, 2}};
        front.try_emplace(10, 0);
        r.front = keys_are(front, std::array<int, 3>{10, 20, 40});

        map_ii middle{{20, 1}, {40, 2}};
        middle.try_emplace(30, 0);
        r.middle = keys_are(middle, std::array<int, 3>{20, 30, 40});

        map_ii back{{20, 1}, {40, 2}};
        back.try_emplace(50, 0);
        r.back = keys_are(back, std::array<int, 3>{20, 40, 50});
        return r;
    }

    constexpr position_report positions = probe_positions();

    static_assert(positions.front);
    static_assert(positions.middle);
    static_assert(positions.back);

    // --- removal ---------------------------------------------------------------

    struct erase_report {
        bool erase_returns_value     = false;
        bool erase_absent_nullopt    = false;
        bool entry_returns_pair      = false;
        bool erase_shrinks           = false;
        bool erase_keeps_order       = false;
        bool erase_iterator_next     = false;
        std::size_t erase_if_removed = 0;
        bool erase_if_kept_rest      = false;
        bool clear_empties           = false;
        bool capacity_survives       = false;
    };

    constexpr erase_report probe_erase() {
        erase_report r;
        map_ii m{{1, 10}, {2, 20}, {3, 30}, {4, 40}};

        const std::optional<int> gone = m.erase(2);
        r.erase_returns_value         = gone.has_value() && gone.value() == 20;
        r.erase_absent_nullopt        = !m.erase(2).has_value();
        r.erase_shrinks               = m.size() == 3;
        r.erase_keeps_order           = keys_are(m, std::array<int, 3>{1, 3, 4});

        const std::optional<map_ii::value_type> entry = m.erase_entry(3);
        r.entry_returns_pair =
                entry.has_value() && entry.value().first == 3 && entry.value().second == 30;

        const map_ii::iterator after = m.erase(m.find(1));
        r.erase_iterator_next        = after != m.end() && after->first == 4;

        map_ii many{{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
        r.erase_if_removed =
                erase_if(many, [](const map_ii::value_type& e) { return e.first % 2 == 0; });
        r.erase_if_kept_rest = keys_are(many, std::array<int, 3>{1, 3, 5});

        const std::size_t before = many.capacity();
        many.clear();
        r.clear_empties     = many.empty() && many.size() == 0;
        r.capacity_survives = many.capacity() == before;
        return r;
    }

    constexpr erase_report erasure = probe_erase();

    static_assert(erasure.erase_returns_value);
    static_assert(erasure.erase_absent_nullopt);
    static_assert(erasure.erase_shrinks);
    static_assert(erasure.erase_keeps_order);
    static_assert(erasure.entry_returns_pair);
    static_assert(erasure.erase_iterator_next);
    static_assert(erasure.erase_if_removed == 2);
    static_assert(erasure.erase_if_kept_rest);
    static_assert(erasure.clear_empties);
    static_assert(erasure.capacity_survives);

    // --- bulk insertion, equality, swap ----------------------------------------

    struct bulk_report {
        std::size_t merged_size               = 0;
        bool merged_keeps                     = false;
        bool equal_same                       = false;
        bool equal_ignores_order_of_insertion = false;
        bool unequal_value                    = false;
        bool unequal_size                     = false;
        bool swap_exchanges                   = false;
        bool reserve_grows                    = false;
    };

    constexpr bulk_report probe_bulk() {
        bulk_report r;

        map_ii m{{1, 10}, {2, 20}};
        const std::array<std::pair<int, int>, 3> more{{{2, 999}, {3, 30}, {4, 40}}};
        m.insert_range(more);
        r.merged_size        = m.size();
        const int* untouched = m.find_value(2);
        r.merged_keeps       = untouched != nullptr && *untouched == 20;

        const map_ii a{{1, 10}, {2, 20}};
        const map_ii b{{2, 20}, {1, 10}};
        r.equal_same                       = a == b;
        r.equal_ignores_order_of_insertion = b == a;
        r.unequal_value                    = !(a == map_ii{{1, 10}, {2, 21}});
        r.unequal_size                     = !(a == map_ii{{1, 10}});

        map_ii left{{1, 1}};
        map_ii right{{2, 2}, {3, 3}};
        swap(left, right);
        r.swap_exchanges =
                left.size() == 2 && right.size() == 1 && left.contains(2) && right.contains(1);

        map_ii sized;
        sized.reserve(64);
        r.reserve_grows = sized.capacity() >= 64 && sized.empty();
        return r;
    }

    constexpr bulk_report bulk = probe_bulk();

    static_assert(bulk.merged_size == 4);
    static_assert(bulk.merged_keeps);
    static_assert(bulk.equal_same);
    static_assert(bulk.equal_ignores_order_of_insertion);
    static_assert(bulk.unequal_value);
    static_assert(bulk.unequal_size);
    static_assert(bulk.swap_exchanges);
    static_assert(bulk.reserve_grows);

    // --- string keys and heterogeneous lookup ----------------------------------

    struct string_report {
        bool find_by_view        = false;
        bool miss_by_view        = false;
        bool contains_by_view    = false;
        bool value_by_view       = false;
        bool erase_by_view       = false;
        bool sorted_lexically    = false;
        bool mutable_by_view     = false;
        bool erase_entry_by_view = false;
    };

    constexpr string_report probe_strings() {
        string_report r;
        map_si m;
        m.try_emplace(std::string("verbose"), 1);
        m.try_emplace(std::string("color"), 2);
        m.try_emplace(std::string("quiet"), 3);
        m.try_emplace(std::string("alias"), 4);

        const std::string_view color{"color"};
        r.find_by_view     = m.find(color) != m.end();
        r.miss_by_view     = m.find(std::string_view{"colour"}) == m.end();
        r.contains_by_view = m.contains(std::string_view{"quiet"});

        const int* value = m.find_value(color);
        r.value_by_view  = value != nullptr && *value == 2;

        if (int* writable = m.find_value(std::string_view{"quiet"})) *writable = 33;
        const int* readback = m.find_value(std::string_view{"quiet"});
        r.mutable_by_view   = readback != nullptr && *readback == 33;

        const std::array<std::string_view, 4> expected{"alias", "color", "quiet", "verbose"};
        r.sorted_lexically = std::ranges::equal(m.keys(), expected);

        r.erase_by_view = m.erase(std::string_view{"alias"}).has_value();
        const std::optional<map_si::value_type> entry = m.erase_entry(std::string_view{"color"});
        r.erase_entry_by_view =
                entry.has_value() && entry.value().first == "color" && entry.value().second == 2;
        return r;
    }

    constexpr string_report strings = probe_strings();

    static_assert(strings.find_by_view);
    static_assert(strings.miss_by_view);
    static_assert(strings.contains_by_view);
    static_assert(strings.value_by_view);
    static_assert(strings.mutable_by_view);
    static_assert(strings.sorted_lexically);
    static_assert(strings.erase_by_view);
    static_assert(strings.erase_entry_by_view);

    // --- ranges pipelines ------------------------------------------------------

    struct pipeline_report {
        int sum_of_values          = 0;
        std::size_t filtered_count = 0;
        int first_odd_key          = 0;
        bool keys_view_lazy        = false;
        bool values_mutable        = false;
    };

    constexpr pipeline_report probe_pipelines() {
        pipeline_report r;
        map_ii m{{1, 10}, {2, 20}, {3, 30}, {4, 40}};

        for (const int value : m.values()) r.sum_of_values += value;

        r.filtered_count = static_cast<std::size_t>(std::ranges::count_if(
                m, [](const map_ii::value_type& e) { return e.second > 15; }));

        const auto key_view = m.keys();
        const auto odd      = std::ranges::find_if(key_view, [](int k) { return k % 2 == 1; });
        r.first_odd_key     = odd == std::ranges::end(key_view) ? -1 : *odd;

        // keys() must be a view over this map, not a copy: mutating through values()
        // has to be visible through it.
        r.keys_view_lazy = std::ranges::size(m.keys()) == m.size();
        for (int& value : m.values()) value *= 2;
        r.values_mutable = std::ranges::equal(m.values(), std::array<int, 4>{20, 40, 60, 80});
        return r;
    }

    constexpr pipeline_report pipelines = probe_pipelines();

    static_assert(pipelines.sum_of_values == 100);
    static_assert(pipelines.filtered_count == 3);
    static_assert(pipelines.first_odd_key == 1);
    static_assert(pipelines.keys_view_lazy);
    static_assert(pipelines.values_mutable);

    // --- non-default-constructible mapped type ---------------------------------

    constexpr bool probe_no_default() {
        flat_map<int, no_default> m;
        m.try_emplace(2, 20);
        m.try_emplace(1, 10);
        const no_default* found = m.find_value(1);
        return m.size() == 2 && found != nullptr && found->v == 10 && m.begin()->first == 1;
    }

    static_assert(probe_no_default());

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
//
// These report the compile-time conclusions above and cover the few results that
// need a `std::vector` or `std::string` variable, which a `constexpr` variable
// cannot hold.
// ---------------------------------------------------------------------------

CLAPP_TEST("flat_map keeps entries in key order regardless of insertion order") {
    map_ii m;
    for (const int key : {7, 3, 9, 1, 5}) m.try_emplace(key, key * 10);

    const std::vector<int> keys = m.keys() | std::ranges::to<std::vector>();
    CLAPP_CHECK(keys == std::vector<int>({1, 3, 5, 7, 9}));

    const std::vector<int> values = m.values() | std::ranges::to<std::vector>();
    CLAPP_CHECK(values == std::vector<int>({10, 30, 50, 70, 90}));
}

CLAPP_TEST("flat_map with std::string keys looks up by std::string_view") {
    map_si m;
    m.try_emplace("output", 1);
    m.try_emplace("input", 2);

    const std::string probe = "input";
    CLAPP_CHECK(m.contains(std::string_view{probe}));
    CLAPP_CHECK(m.find_value(std::string_view{probe}) != nullptr);
    CLAPP_CHECK(*m.find_value(std::string_view{probe}) == 2);
    CLAPP_CHECK(m.find(std::string_view{"missing"}) == m.end());
}

CLAPP_TEST("flat_map of owning values copies deeply") {
    flat_map<int, std::string> original;
    original.try_emplace(1, "one");
    original.try_emplace(2, "two");

    flat_map<int, std::string> copy = original;
    if (std::string* value = copy.find_value(1)) *value = "changed";

    CLAPP_CHECK(*original.find_value(1) == "one");
    CLAPP_CHECK(*copy.find_value(1) == "changed");
    CLAPP_CHECK(original.size() == copy.size());
}

CLAPP_TEST("flat_map survives a materializing ranges pipeline") {
    map_ii m{{4, 40}, {1, 10}, {3, 30}, {2, 20}};

    const std::vector<std::pair<int, int>> big =
            m | std::views::filter([](const map_ii::value_type& e) { return e.second >= 20; }) |
            std::ranges::to<std::vector>();

    CLAPP_CHECK(big.size() == 3);
    CLAPP_CHECK(big.front().first == 2);
    CLAPP_CHECK(big.back().first == 4);
}

CLAPP_TEST("flat_map compile-time suite ran") {
    // Every assertion above is a static_assert; reaching this line means the
    // translation unit compiled, which is the actual result being reported.
    CLAPP_CHECK(construction.init_size == 3);
    CLAPP_CHECK(insertion.factory_calls == 1);
    CLAPP_CHECK(erasure.erase_if_removed == 2);
}
