#include <clapp/util/any_value.hpp>
#include <clapp/util/flat_map.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    using clapp::any_id;
    using clapp::any_storable;
    using clapp::any_value;

    /** A type whose name is distinctive enough to look for in a diagnostic. */
    struct probe_type {
        int a = 0;
        std::string b;

        bool operator==(const probe_type&) const = default;
    };

    /**
     * Counts its own live instances, so the tests can prove that any_value really
     * destroys what it owns rather than leaking it.
     */
    struct counted {
        static inline int live = 0;

        int value = 0;

        explicit counted(int v) : value(v) { ++live; }
        counted(const counted& other) : value(other.value) { ++live; }
        counted(counted&& other) noexcept : value(other.value) { ++live; }
        counted& operator=(const counted&)     = default;
        counted& operator=(counted&&) noexcept = default;
        ~counted() { --live; }
    };

    /** Move-only, therefore not storable: clap requires `Clone` for the same reason. */
    struct move_only {
        std::unique_ptr<int> p;
    };

    /**
     * Same identifier, four different scopes — the shape that broke `any_id` when its
     * identity came from `std::meta::display_string_of`, which clang-p2996 emits
     * *unqualified*. `mylib::path` is not contrived: `std::filesystem::path` is a
     * built-in `value_parser` type, so a user type spelled `path` collides with a
     * shipped one.
     */
    struct dup {
        int a = 0;
    };

}  // namespace

namespace ns_a {
    struct dup {
        int a = 0;
    };
    namespace inner {
        struct dup {
            int a = 0;
        };
    }  // namespace inner
}  // namespace ns_a

namespace ns_b {
    struct dup {
        int a = 0;
    };
}  // namespace ns_b

namespace mylib {
    struct path {
        int a = 0;
    };
}  // namespace mylib

namespace {

    // ---------------------------------------------------------------------------
    // any_id — fully constexpr
    // ---------------------------------------------------------------------------

    static_assert(any_id::of<int>() == any_id::of<int>());
    static_assert(any_id::of<int>() != any_id::of<long>());
    static_assert(any_id::of<int>() != any_id::of<unsigned int>());
    static_assert(any_id::of<probe_type>() == any_id::of<probe_type>());
    static_assert(any_id::of<probe_type>() != any_id::of<counted>());
    static_assert(any_id::of<std::string>() != any_id::of<std::string_view>());

    // Cv-qualification and references produce distinct ids. any_value refuses such
    // types outright (see any_storable below) precisely so that this distinction can
    // never show up as a silent lookup miss.
    static_assert(any_id::of<int>() != any_id::of<const int>());
    static_assert(any_id::of<int>() != any_id::of<int&>());
    static_assert(any_id::of<int>() != any_id::of<int*>());
    // `const int*` is a pointer to const; `int* const` is a const pointer. A namer that
    // writes both `const` west collapses them.
    static_assert(any_id::of<const int*>() != any_id::of<int* const>());

    // The property the whole design rests on, and the one the suite used to miss: two
    // types whose *unqualified* identifiers are the same must still get different ids.
    //
    // Every distinctness assertion above compares types that already differ in their last
    // identifier component, so all of them passed on clang-p2996 even while
    // display_string_of — which the original any_id used — was emitting unqualified names
    // there. Measured on clang-p2996 0.0.0-p2996.5cc3eb319: `std::filesystem::path` and a
    // user's `mylib::path` both read back as "path", `any_value` holding one answered
    // `holds<>()` true for the other, and `try_get<>()` handed out a pointer to the wrong
    // type with no diagnostic. These four assertions are the regression gate; they fail on
    // clang the moment identity goes back to display_string_of.
    static_assert(any_id::of<ns_a::dup>() != any_id::of<ns_b::dup>());
    static_assert(any_id::of<ns_a::inner::dup>() != any_id::of<ns_b::dup>());
    static_assert(any_id::of<mylib::path>() != any_id::of<std::filesystem::path>());
    static_assert(any_id::of<dup>() != any_id::of<ns_a::dup>());  // unnamed vs named
    // Qualification has to reach *inside* a specialization too: `std::vector<ns_a::dup>`
    // and `std::vector<ns_b::dup>` differ only in a template argument's namespace.
    static_assert(any_id::of<std::vector<ns_a::dup>>() != any_id::of<std::vector<ns_b::dup>>());
    // A name that qualifies must still name the type: the last component is the
    // identifier, at a `::` boundary. The prefix itself is implementation-defined — GCC
    // 16.1.0 writes `std::filesystem::__cxx11::path` where clang-p2996 writes
    // `std::__1::__fs::filesystem::path` — so only the suffix is asserted.
    static_assert(any_id::of<ns_a::dup>().name().ends_with("::dup"));
    static_assert(any_id::of<std::filesystem::path>().name().ends_with("::path"));

    // The empty id.
    static_assert(!any_id{}.has_value());
    static_assert(any_id{} == any_id{});
    static_assert(any_id{}.name() == "<none>");
    static_assert(any_id::of<int>().has_value());
    static_assert(any_id{} != any_id::of<int>());

    // Names come from reflection and are available in every build, unlike clap's
    // AnyValueId::type_name which exists only under debug_assertions.
    static_assert(any_id::of<int>().name() == "int");
    static_assert(any_id::of<double>().name() == "double");
    // `ends_with` rather than `contains`: GCC 16.1.0 routes `contains` through
    // `string_view::find`, whose internal null check on the haystack pointer is a
    // pointer comparison, and `-fsanitize=undefined` makes any constexpr pointer
    // comparison non-constant on that compiler (measured; four-line repro in the
    // any_id docs). `ends_with` lowers to `__builtin_memcmp` and is unaffected.
    static_assert(any_id::of<probe_type>().name().ends_with("probe_type"));
    static_assert(any_id::of<std::uint8_t>().name() == "unsigned char");

    // Ordering is a strict total order and, for distinct names, decides without ever
    // reaching the non-constexpr pointer tie-break.
    static_assert((any_id::of<int>() <=> any_id::of<int>()) == std::strong_ordering::equal);
    static_assert((any_id::of<double>() <=> any_id::of<int>()) == std::strong_ordering::less);
    static_assert((any_id::of<int>() <=> any_id::of<double>()) == std::strong_ordering::greater);
    static_assert((any_id{} <=> any_id{}) == std::strong_ordering::equal);
    static_assert(std::totally_ordered<any_id>);
    static_assert(std::equality_comparable<any_id>);
    static_assert(std::copyable<any_id>);
    static_assert(std::is_trivially_copyable_v<any_id>);

    // ---------------------------------------------------------------------------
    // any_storable — what may be stored
    // ---------------------------------------------------------------------------

    static_assert(any_storable<int>);
    static_assert(any_storable<double>);
    static_assert(any_storable<std::string>);
    static_assert(any_storable<std::vector<int>>);
    static_assert(any_storable<probe_type>);
    static_assert(any_storable<int*>);

    static_assert(!any_storable<void>);
    static_assert(!any_storable<const int>);
    static_assert(!any_storable<volatile int>);
    static_assert(!any_storable<int&>);
    static_assert(!any_storable<int&&>);
    static_assert(!any_storable<int[4]>);
    static_assert(!any_storable<int()>);
    static_assert(!any_storable<move_only>);

    // ---------------------------------------------------------------------------
    // any_value — type-level contract
    //
    // Negative checks go through named concepts: GCC 16.1.0 turns an ill-formed call
    // inside a bare `static_assert(!requires{...})` into a hard error rather than an
    // unsatisfied requirement (measured; see flat_map_test.cpp for the same note).
    // ---------------------------------------------------------------------------

    template<class T>
    concept gettable = requires(any_value v) { v.try_get<T>(); };
    template<class T>
    concept takeable = requires(any_value v) { v.take<T>(); };
    template<class T>
    concept emplaceable = requires(any_value v) { v.template emplace<T>(); };

    static_assert(gettable<int>);
    static_assert(gettable<std::string>);
    static_assert(!gettable<const int>);  // would otherwise return a silent nullptr
    static_assert(!gettable<int&>);
    static_assert(!gettable<move_only>);
    static_assert(takeable<std::string>);
    static_assert(!takeable<const std::string>);
    static_assert(emplaceable<std::string>);
    static_assert(!emplaceable<probe_type&>);

    static_assert(std::default_initializable<any_value>);
    static_assert(std::copyable<any_value>);
    static_assert(std::movable<any_value>);
    static_assert(std::is_nothrow_move_constructible_v<any_value>);
    static_assert(std::is_nothrow_move_assignable_v<any_value>);
    static_assert(std::destructible<any_value>);

    // The deducing constructor must never shadow the copy constructor.
    static_assert(std::constructible_from<any_value, int>);
    static_assert(std::constructible_from<any_value, any_value&>);
    static_assert(std::constructible_from<any_value, const any_value&>);
    static_assert(std::constructible_from<any_value, any_value&&>);
    static_assert(
            std::constructible_from<any_value, std::in_place_type_t<probe_type>, int, std::string>);

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
// ---------------------------------------------------------------------------

CLAPP_TEST("any_value default-constructs empty and reports no type") {
    const any_value v;
    CLAPP_CHECK(!v.has_value());
    CLAPP_CHECK(!v.type().has_value());
    CLAPP_CHECK(v.type() == any_id{});
    CLAPP_CHECK(v.try_get<int>() == nullptr);
    CLAPP_CHECK(!v.holds<int>());
}

CLAPP_TEST("any_value hands back the stored type and refuses every other") {
    const any_value v(42);
    CLAPP_CHECK(v.has_value());
    CLAPP_CHECK(v.holds<int>());
    CLAPP_CHECK(v.type() == any_id::of<int>());
    CLAPP_CHECK(v.type().name() == "int");

    const int* stored = v.try_get<int>();
    CLAPP_CHECK(stored != nullptr);
    CLAPP_CHECK(*stored == 42);
    CLAPP_CHECK(v.get<int>() == 42);

    // The mismatch path: nullptr, no diagnostic, no undefined behavior.
    CLAPP_CHECK(v.try_get<long>() == nullptr);
    CLAPP_CHECK(v.try_get<unsigned>() == nullptr);
    CLAPP_CHECK(v.try_get<std::string>() == nullptr);
    CLAPP_CHECK(!v.holds<double>());
}

CLAPP_TEST("any_value refuses a same-named type from another namespace") {
    // The end-to-end form of the any_id assertions above, through the public API only.
    // On clang-p2996 with the old display_string_of identity this test *passed*
    // holds<>(): the two ids were both "dup", try_get<> handed back a pointer to the
    // wrong object, and get<> followed it without ever reaching its abort.
    const any_value v(ns_a::dup{7});
    CLAPP_CHECK(v.holds<ns_a::dup>());
    CLAPP_CHECK(!v.holds<ns_b::dup>());
    CLAPP_CHECK(!v.holds<ns_a::inner::dup>());
    CLAPP_CHECK(!v.holds<dup>());
    CLAPP_CHECK(v.try_get<ns_b::dup>() == nullptr);
    CLAPP_CHECK(v.try_get<ns_a::inner::dup>() == nullptr);
    CLAPP_CHECK(v.try_get<ns_a::dup>() != nullptr);
    CLAPP_CHECK(v.get<ns_a::dup>().a == 7);

    const any_value p(mylib::path{1});
    CLAPP_CHECK(!p.holds<std::filesystem::path>());
    CLAPP_CHECK(p.try_get<std::filesystem::path>() == nullptr);
}

CLAPP_TEST("any_value stores non-trivial types in place") {
    any_value v(std::in_place_type<probe_type>, 7, std::string("seven"));
    const probe_type* p = v.try_get<probe_type>();
    CLAPP_CHECK(p != nullptr);
    CLAPP_CHECK(p->a == 7);
    CLAPP_CHECK(p->b == "seven");
    CLAPP_CHECK(v.type().name().ends_with("probe_type"));

    v.get<probe_type>().a = 8;
    CLAPP_CHECK(v.try_get<probe_type>()->a == 8);
}

CLAPP_TEST("any_value deduces the stored type from the initializer") {
    const any_value from_string(std::string("hello"));
    CLAPP_CHECK(from_string.holds<std::string>());
    CLAPP_CHECK(*from_string.try_get<std::string>() == "hello");

    const std::vector<int> numbers{1, 2, 3};
    const any_value from_vector(numbers);
    CLAPP_CHECK(from_vector.holds<std::vector<int>>());
    CLAPP_CHECK(from_vector.try_get<std::vector<int>>()->size() == 3);
    CLAPP_CHECK(numbers.size() == 3);  // the source was copied, not moved from
}

CLAPP_TEST("any_value copies deeply and independently") {
    any_value original(std::string("first"));
    any_value copy = original;

    CLAPP_CHECK(copy.holds<std::string>());
    *copy.try_get<std::string>() = "second";

    CLAPP_CHECK(*original.try_get<std::string>() == "first");
    CLAPP_CHECK(*copy.try_get<std::string>() == "second");
    CLAPP_CHECK(original.type() == copy.type());

    any_value assigned;
    assigned = original;
    CLAPP_CHECK(*assigned.try_get<std::string>() == "first");
    CLAPP_CHECK(assigned.try_get<std::string>() != original.try_get<std::string>());
}

CLAPP_TEST("any_value moves without copying and leaves the source empty") {
    any_value source(std::string("payload"));
    const std::string* address = source.try_get<std::string>();

    any_value moved = std::move(source);
    CLAPP_CHECK(moved.try_get<std::string>() == address);  // ownership transferred, not copied
    CLAPP_CHECK(!source.has_value());                      // NOLINT: checking the moved-from state
    CLAPP_CHECK(!source.type().has_value());

    any_value target(1);
    target = std::move(moved);
    CLAPP_CHECK(target.holds<std::string>());
    CLAPP_CHECK(!moved.has_value());
}

CLAPP_TEST("any_value destroys what it owns") {
    CLAPP_CHECK(counted::live == 0);
    {
        any_value v(std::in_place_type<counted>, 5);
        CLAPP_CHECK(counted::live == 1);

        any_value copy = v;
        CLAPP_CHECK(counted::live == 2);

        copy.reset();
        CLAPP_CHECK(counted::live == 1);
        CLAPP_CHECK(!copy.has_value());
    }
    CLAPP_CHECK(counted::live == 0);

    {
        any_value v(std::in_place_type<counted>, 1);
        v.emplace<counted>(2);  // old value destroyed, new one live
        CLAPP_CHECK(counted::live == 1);
        CLAPP_CHECK(v.try_get<counted>()->value == 2);

        v = any_value(std::string("something else"));
        CLAPP_CHECK(counted::live == 0);
    }
    CLAPP_CHECK(counted::live == 0);
}

CLAPP_TEST("any_value take moves the value out on a match and nothing on a miss") {
    any_value v(std::string("taken"));

    const std::optional<int> wrong = v.take<int>();
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(v.has_value());  // a failed take must not disturb the value
    CLAPP_CHECK(*v.try_get<std::string>() == "taken");

    const std::optional<std::string> right = v.take<std::string>();
    CLAPP_CHECK(right.has_value());
    CLAPP_CHECK(right.value() == "taken");
    CLAPP_CHECK(!v.has_value());
    CLAPP_CHECK(!v.take<std::string>().has_value());
}

CLAPP_TEST("any_value emplace replaces the stored type") {
    any_value v(1);
    CLAPP_CHECK(v.holds<int>());

    std::string& fresh = v.emplace<std::string>("replaced");
    CLAPP_CHECK(v.holds<std::string>());
    CLAPP_CHECK(!v.holds<int>());
    CLAPP_CHECK(fresh == "replaced");
    CLAPP_CHECK(v.try_get<int>() == nullptr);
}

CLAPP_TEST("any_value swaps contents") {
    any_value a(1);
    any_value b(std::string("two"));

    swap(a, b);
    CLAPP_CHECK(a.holds<std::string>());
    CLAPP_CHECK(b.holds<int>());
    CLAPP_CHECK(*b.try_get<int>() == 1);

    any_value empty;
    a.swap(empty);
    CLAPP_CHECK(!a.has_value());
    CLAPP_CHECK(empty.holds<std::string>());
}

CLAPP_TEST("any_id keys a flat_map, as clap's Extensions map does") {
    clapp::flat_map<any_id, int> registry;
    registry.try_emplace(any_id::of<int>(), 1);
    registry.try_emplace(any_id::of<std::string>(), 2);
    registry.try_emplace(any_id::of<double>(), 3);
    registry.try_emplace(any_id::of<int>(), 99);  // duplicate key, ignored

    CLAPP_CHECK(registry.size() == 3);
    CLAPP_CHECK(*registry.find_value(any_id::of<int>()) == 1);
    CLAPP_CHECK(*registry.find_value(any_id::of<double>()) == 3);
    CLAPP_CHECK(registry.find_value(any_id::of<long>()) == nullptr);

    // Ordering is by type name, so the keys come back alphabetically.
    const std::vector<std::string_view> names =
            registry.keys() | std::views::transform(&any_id::name) | std::ranges::to<std::vector>();
    CLAPP_CHECK(std::ranges::is_sorted(names));
}

CLAPP_TEST("any_value round-trips a vector of erased values, as arg_matches will") {
    std::vector<any_value> values;
    values.emplace_back(std::in_place_type<std::string>, "alpha");
    values.emplace_back(std::in_place_type<std::string>, "beta");

    const std::vector<std::string_view> texts = values |
                                                std::views::transform([](const any_value& v) {
                                                    return std::string_view(v.get<std::string>());
                                                }) |
                                                std::ranges::to<std::vector>();

    CLAPP_CHECK(texts.size() == 2);
    CLAPP_CHECK(texts[0] == "alpha");
    CLAPP_CHECK(texts[1] == "beta");
    CLAPP_CHECK(values[0].type() == values[1].type());
}
