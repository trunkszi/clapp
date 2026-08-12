#include <clapp/util/flat_set.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using clapp::flat_set;

    using set_i = flat_set<int>;
    using set_s = flat_set<std::string, std::less<>>;

    // ---------------------------------------------------------------------------
    // Container/range concept conformance
    // ---------------------------------------------------------------------------

    static_assert(std::ranges::range<set_i>);
    static_assert(std::ranges::range<const set_i>);
    static_assert(std::ranges::sized_range<set_i>);
    static_assert(std::ranges::random_access_range<set_i>);
    static_assert(std::ranges::common_range<set_i>);
    static_assert(std::same_as<std::ranges::range_value_t<set_i>, int>);
    static_assert(std::same_as<std::ranges::range_reference_t<set_i>, const int&>);
    static_assert(std::same_as<set_i::iterator, set_i::const_iterator>);
    static_assert(std::same_as<set_i::key_type, int>);
    static_assert(std::same_as<set_i::value_type, int>);
    static_assert(std::default_initializable<set_i>);
    static_assert(std::copyable<set_i>);
    static_assert(std::movable<set_i>);
    static_assert(std::equality_comparable<set_i>);

    // Negative capability checks go through named concepts: GCC 16.1.0 turns an
    // ill-formed call inside a bare `static_assert(!requires{...})` into a hard
    // error rather than an unsatisfied requirement (see flat_map_test.cpp).
    template<class S, class K>
    concept findable = requires(S s, K k) { s.find(k); };
    template<class S, class K>
    concept containable = requires(S s, K k) { s.contains(k); };
    template<class S, class K>
    concept key_erasable = requires(S s, K k) { s.erase(k); };

    static_assert(findable<set_s, std::string_view>);
    static_assert(!findable<set_i, std::string_view>);
    static_assert(containable<set_s, std::string_view>);
    static_assert(!containable<set_i, std::string_view>);
    static_assert(key_erasable<set_s, std::string_view>);
    static_assert(!key_erasable<set_i, std::string_view>);

    // into_vector() is rvalue-qualified, so it must not bind to an lvalue.
    template<class S>
    concept lvalue_drainable = requires(S s) { s.into_vector(); };
    template<class S>
    concept rvalue_drainable = requires(S s) { std::move(s).into_vector(); };
    static_assert(!lvalue_drainable<set_i>);
    static_assert(rvalue_drainable<set_i>);

    // ---------------------------------------------------------------------------
    // Reports
    // ---------------------------------------------------------------------------

    /** Whether \p s yields exactly \p expected, in that order. */
    constexpr bool elements_are(const set_i& s, std::span<const int> expected) {
        return std::ranges::equal(s, expected);
    }

    // --- construction ----------------------------------------------------------

    struct construction_report {
        bool default_is_empty     = false;
        bool default_begin_is_end = false;
        std::size_t init_size     = 0;
        bool init_sorted          = false;
        bool init_deduplicates    = false;
        std::size_t range_size    = 0;
        bool range_sorted         = false;
        bool comparator_stored    = false;
    };

    constexpr construction_report probe_construction() {
        construction_report r;

        const set_i empty;
        r.default_is_empty     = empty.empty() && empty.size() == 0;
        r.default_begin_is_end = empty.begin() == empty.end();

        const set_i init{3, 1, 2, 1, 3};
        r.init_size         = init.size();
        r.init_sorted       = std::ranges::is_sorted(init);
        r.init_deduplicates = init.contains(1) && init.contains(2) && init.contains(3);

        const std::array<int, 5> source{9, 5, 7, 5, 9};
        const set_i from_range(std::from_range, source);
        r.range_size   = from_range.size();
        r.range_sorted = std::ranges::is_sorted(from_range);

        const flat_set<int, std::greater<int>> descending{1, 2, 3};
        r.comparator_stored = std::ranges::is_sorted(descending, std::greater<int>{});

        return r;
    }

    constexpr construction_report construction = probe_construction();

    static_assert(construction.default_is_empty);
    static_assert(construction.default_begin_is_end);
    static_assert(construction.init_size == 3);
    static_assert(construction.init_sorted);
    static_assert(construction.init_deduplicates);
    static_assert(construction.range_size == 3);
    static_assert(construction.range_sorted);
    static_assert(construction.comparator_stored);

    // --- ordering --------------------------------------------------------------

    struct ordering_report {
        std::array<int, 8> elements{};
        std::size_t count     = 0;
        bool reverse_descends = false;
        bool data_matches     = false;
    };

    constexpr ordering_report probe_ordering() {
        ordering_report r;
        set_i s;
        // Deliberately hostile insertion order: back, front, middle, front again.
        s.insert(50);
        s.insert(10);
        s.insert(30);
        s.insert(5);

        // Raw loop: writing into a fixed array while counting, so `ranges::copy`
        // would still need the bound check spelled out separately.
        for (const int value : s) {
            if (r.count >= r.elements.size()) break;
            r.elements[r.count] = value;
            ++r.count;
        }

        r.reverse_descends = std::ranges::equal(std::ranges::subrange(s.rbegin(), s.rend()),
                                                std::array<int, 4>{50, 30, 10, 5});
        r.data_matches     = s.data() != nullptr && s.data()[0] == 5;
        return r;
    }

    constexpr ordering_report ordering = probe_ordering();

    static_assert(ordering.count == 4);
    static_assert(ordering.elements[0] == 5);
    static_assert(ordering.elements[1] == 10);
    static_assert(ordering.elements[2] == 30);
    static_assert(ordering.elements[3] == 50);
    static_assert(ordering.reverse_descends);
    static_assert(ordering.data_matches);

    // --- insertion -------------------------------------------------------------

    struct insert_report {
        bool reports_insert              = false;
        bool reports_duplicate           = false;
        bool iterator_points_at          = false;
        bool duplicate_iterator          = false;
        std::size_t size_after_duplicate = 0;
        bool insert_front                = false;
        bool insert_middle               = false;
        bool insert_back                 = false;
        bool still_sorted                = false;
    };

    constexpr insert_report probe_insert() {
        insert_report r;
        set_i s;

        const std::pair<set_i::iterator, bool> fresh = s.insert(42);
        r.reports_insert                             = fresh.second;
        r.iterator_points_at                         = fresh.first != s.end() && *fresh.first == 42;

        const std::pair<set_i::iterator, bool> again = s.insert(42);
        r.reports_duplicate                          = !again.second;
        r.duplicate_iterator                         = again.first != s.end() && *again.first == 42;
        r.size_after_duplicate                       = s.size();

        set_i front{20, 40};
        front.insert(10);
        r.insert_front = elements_are(front, std::array<int, 3>{10, 20, 40});

        set_i middle{20, 40};
        middle.insert(30);
        r.insert_middle = elements_are(middle, std::array<int, 3>{20, 30, 40});

        set_i back{20, 40};
        back.insert(50);
        r.insert_back = elements_are(back, std::array<int, 3>{20, 40, 50});

        r.still_sorted = std::ranges::is_sorted(front) && std::ranges::is_sorted(middle) &&
                         std::ranges::is_sorted(back);
        return r;
    }

    constexpr insert_report insertion = probe_insert();

    static_assert(insertion.reports_insert);
    static_assert(insertion.iterator_points_at);
    static_assert(insertion.reports_duplicate);
    static_assert(insertion.duplicate_iterator);
    static_assert(insertion.size_after_duplicate == 1);
    static_assert(insertion.insert_front);
    static_assert(insertion.insert_middle);
    static_assert(insertion.insert_back);
    static_assert(insertion.still_sorted);

    // --- lookup ----------------------------------------------------------------

    struct lookup_report {
        bool found_present        = false;
        bool missing_is_end       = false;
        bool contains_present     = false;
        bool contains_absent      = false;
        bool below_range_missing  = false;
        bool above_range_missing  = false;
        bool between_keys_missing = false;
        bool empty_finds_nothing  = false;
    };

    constexpr lookup_report probe_lookup() {
        lookup_report r;
        const set_i s{10, 20, 30};

        const set_i::const_iterator hit = s.find(20);
        r.found_present                 = hit != s.end() && *hit == 20;
        r.missing_is_end                = s.find(21) == s.end();

        r.contains_present = s.contains(30);
        r.contains_absent  = !s.contains(31);

        r.below_range_missing  = s.find(1) == s.end();
        r.above_range_missing  = s.find(99) == s.end();
        r.between_keys_missing = s.find(25) == s.end();

        const set_i nothing;
        r.empty_finds_nothing = nothing.find(0) == nothing.end() && !nothing.contains(0);
        return r;
    }

    constexpr lookup_report lookup = probe_lookup();

    static_assert(lookup.found_present);
    static_assert(lookup.missing_is_end);
    static_assert(lookup.contains_present);
    static_assert(lookup.contains_absent);
    static_assert(lookup.below_range_missing);
    static_assert(lookup.above_range_missing);
    static_assert(lookup.between_keys_missing);
    static_assert(lookup.empty_finds_nothing);

    // --- removal ---------------------------------------------------------------

    struct erase_report {
        bool erase_reports_hit       = false;
        bool erase_reports_miss      = false;
        bool erase_shrinks           = false;
        bool erase_keeps_order       = false;
        bool iterator_next           = false;
        std::size_t erase_if_removed = 0;
        bool erase_if_kept_rest      = false;
        std::size_t erase_if_none    = 0;
        bool clear_empties           = false;
        bool capacity_survives       = false;
    };

    constexpr erase_report probe_erase() {
        erase_report r;
        set_i s{1, 2, 3, 4};

        r.erase_reports_hit  = s.erase(2);
        r.erase_reports_miss = !s.erase(2);
        r.erase_shrinks      = s.size() == 3;
        r.erase_keeps_order  = elements_are(s, std::array<int, 3>{1, 3, 4});

        const set_i::iterator after = s.erase(s.find(1));
        r.iterator_next             = after != s.end() && *after == 3;

        set_i many{1, 2, 3, 4, 5};
        r.erase_if_removed   = erase_if(many, [](int v) { return v % 2 == 0; });
        r.erase_if_kept_rest = elements_are(many, std::array<int, 3>{1, 3, 5});
        r.erase_if_none      = erase_if(many, [](int v) { return v > 100; });

        const std::size_t before = many.capacity();
        many.clear();
        r.clear_empties     = many.empty() && many.size() == 0;
        r.capacity_survives = many.capacity() == before;
        return r;
    }

    constexpr erase_report erasure = probe_erase();

    static_assert(erasure.erase_reports_hit);
    static_assert(erasure.erase_reports_miss);
    static_assert(erasure.erase_shrinks);
    static_assert(erasure.erase_keeps_order);
    static_assert(erasure.iterator_next);
    static_assert(erasure.erase_if_removed == 2);
    static_assert(erasure.erase_if_kept_rest);
    static_assert(erasure.erase_if_none == 0);
    static_assert(erasure.clear_empties);
    static_assert(erasure.capacity_survives);

    // --- bulk, equality, swap, drain -------------------------------------------

    struct bulk_report {
        std::size_t extended_size                = 0;
        bool extended_sorted                     = false;
        bool equal_same                          = false;
        bool equal_regardless_of_insertion_order = false;
        bool unequal_member                      = false;
        bool unequal_size                        = false;
        bool swap_exchanges                      = false;
        bool drain_sorted                        = false;
        bool drain_empties                       = false;
        bool reserve_grows                       = false;
    };

    constexpr bulk_report probe_bulk() {
        bulk_report r;

        set_i s{1, 2};
        const std::array<int, 4> more{2, 3, 4, 3};
        s.insert_range(more);
        r.extended_size   = s.size();
        r.extended_sorted = std::ranges::is_sorted(s);

        const set_i a{1, 2, 3};
        const set_i b{3, 2, 1};
        r.equal_same                          = a == b;
        r.equal_regardless_of_insertion_order = b == a;
        r.unequal_member                      = !(a == set_i{1, 2, 4});
        r.unequal_size                        = !(a == set_i{1, 2});

        set_i left{1};
        set_i right{2, 3};
        swap(left, right);
        r.swap_exchanges =
                left.size() == 2 && right.size() == 1 && left.contains(2) && right.contains(1);

        set_i drained{7, 3, 5};
        const std::vector<int> out = std::move(drained).into_vector();
        r.drain_sorted             = std::ranges::equal(out, std::array<int, 3>{3, 5, 7});
        r.drain_empties            = drained.empty();

        set_i sized;
        sized.reserve(32);
        r.reserve_grows = sized.capacity() >= 32 && sized.empty();
        return r;
    }

    constexpr bulk_report bulk = probe_bulk();

    static_assert(bulk.extended_size == 4);
    static_assert(bulk.extended_sorted);
    static_assert(bulk.equal_same);
    static_assert(bulk.equal_regardless_of_insertion_order);
    static_assert(bulk.unequal_member);
    static_assert(bulk.unequal_size);
    static_assert(bulk.swap_exchanges);
    static_assert(bulk.drain_sorted);
    static_assert(bulk.drain_empties);
    static_assert(bulk.reserve_grows);

    // --- string elements and heterogeneous lookup ------------------------------

    struct string_report {
        bool find_by_view     = false;
        bool miss_by_view     = false;
        bool contains_by_view = false;
        bool erase_by_view    = false;
        bool sorted_lexically = false;
        bool dedup_by_value   = false;
    };

    constexpr string_report probe_strings() {
        string_report r;
        set_s s;
        s.insert(std::string("verbose"));
        s.insert(std::string("color"));
        s.insert(std::string("quiet"));
        s.insert(std::string("color"));

        r.dedup_by_value = s.size() == 3;

        const std::string_view color{"color"};
        r.find_by_view     = s.find(color) != s.end();
        r.miss_by_view     = s.find(std::string_view{"colour"}) == s.end();
        r.contains_by_view = s.contains(std::string_view{"quiet"});

        const std::array<std::string_view, 3> expected{"color", "quiet", "verbose"};
        r.sorted_lexically = std::ranges::equal(s, expected);

        r.erase_by_view = s.erase(std::string_view{"quiet"}) && s.size() == 2;
        return r;
    }

    constexpr string_report strings = probe_strings();

    static_assert(strings.dedup_by_value);
    static_assert(strings.find_by_view);
    static_assert(strings.miss_by_view);
    static_assert(strings.contains_by_view);
    static_assert(strings.sorted_lexically);
    static_assert(strings.erase_by_view);

    // --- ranges pipelines ------------------------------------------------------

    struct pipeline_report {
        int sum                    = 0;
        std::size_t filtered_count = 0;
        int first_even             = 0;
        bool reversible            = false;
    };

    constexpr pipeline_report probe_pipelines() {
        pipeline_report r;
        const set_i s{5, 1, 4, 2, 3};

        for (const int value : s) r.sum += value;
        r.filtered_count =
                static_cast<std::size_t>(std::ranges::count_if(s, [](int v) { return v > 2; }));

        const auto even = std::ranges::find_if(s, [](int v) { return v % 2 == 0; });
        r.first_even    = even == s.end() ? -1 : *even;

        r.reversible =
                std::ranges::equal(s | std::views::reverse, std::array<int, 5>{5, 4, 3, 2, 1});
        return r;
    }

    constexpr pipeline_report pipelines = probe_pipelines();

    static_assert(pipelines.sum == 15);
    static_assert(pipelines.filtered_count == 3);
    static_assert(pipelines.first_even == 2);
    static_assert(pipelines.reversible);

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
// ---------------------------------------------------------------------------

CLAPP_TEST("flat_set deduplicates and orders regardless of insertion order") {
    set_i s;
    for (const int value : {7, 3, 9, 3, 1, 7}) s.insert(value);

    const std::vector<int> elements = s | std::ranges::to<std::vector>();
    CLAPP_CHECK(elements == std::vector<int>({1, 3, 7, 9}));
}

CLAPP_TEST("flat_set of std::string looks up by std::string_view") {
    set_s s;
    s.insert("output");
    s.insert("input");

    const std::string probe = "input";
    CLAPP_CHECK(s.contains(std::string_view{probe}));
    CLAPP_CHECK(s.find(std::string_view{"missing"}) == s.end());
    CLAPP_CHECK(s.size() == 2);
}

CLAPP_TEST("flat_set collected from a pipeline keeps only distinct values") {
    const std::vector<int> noisy{4, 4, 2, 8, 2, 6};
    flat_set<int> s(std::from_range, noisy | std::views::filter([](int v) { return v != 8; }));

    const std::vector<int> elements = std::move(s).into_vector();
    CLAPP_CHECK(elements == std::vector<int>({2, 4, 6}));
}

CLAPP_TEST("flat_set of owning values copies deeply") {
    flat_set<std::string> original;
    original.insert("alpha");
    original.insert("beta");

    flat_set<std::string> copy = original;
    CLAPP_CHECK(copy.erase("alpha"));
    CLAPP_CHECK(original.contains("alpha"));
    CLAPP_CHECK(original.size() == 2);
    CLAPP_CHECK(copy.size() == 1);
}

CLAPP_TEST("flat_set compile-time suite ran") {
    CLAPP_CHECK(construction.init_size == 3);
    CLAPP_CHECK(erasure.erase_if_removed == 2);
    CLAPP_CHECK(bulk.extended_size == 4);
}
