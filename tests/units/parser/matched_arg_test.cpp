#include <clapp/parser/matched_arg.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    using clapp::any_id;
    using clapp::any_value;
    using clapp::matched_arg;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::value_group;
    using clapp::value_source;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Compile-time facts
    // ---------------------------------------------------------------------------

    // clapp::value_group is a plain boundary record and stays usable in constant
    // evaluation, even though the class that holds them does not.
    static_assert(value_group{.first = 2, .count = 3}.last() == 5);
    static_assert(value_group{.first = 2, .count = 0}.empty());
    static_assert(!value_group{.first = 0, .count = 1}.empty());
    static_assert(value_group{.first = 1, .count = 2} == value_group{.first = 1, .count = 2});
    static_assert(!(value_group{.first = 1, .count = 2} == value_group{.first = 1, .count = 3}));

    // The reason every other assertion in this file is a runtime one. any_value's
    // destructor is not constexpr (type erasure needs a static_cast from void*), so a
    // matched_arg is a runtime-only object by construction rather than by choice.
    static_assert(!std::is_trivially_destructible_v<matched_arg>);
    static_assert(std::is_copy_constructible_v<matched_arg>);
    static_assert(std::is_move_constructible_v<matched_arg>);
    static_assert(std::is_nothrow_move_constructible_v<matched_arg>);

    // The three public view aliases. What matters about each of them is decidable, and
    // each property guards a regression that would otherwise be invisible:
    //
    //   * `const T&` rather than `T` — returning by value still compiles and still yields
    //     correct values, while copying every std::string on every traversal;
    //   * random access and sizedness — clap's ValuesRef is ExactSizeIterator and
    //     DoubleEndedIterator, and losing either would silently remove `.size()` and
    //     reverse iteration from clapp::arg_matches::get_many()'s result;
    //   * the element type of an occurrence range is the value range itself, so
    //     get_occurrences() really is a range of ranges rather than a flattened one.
    static_assert(std::ranges::random_access_range<clapp::values_ref<std::string>>);
    static_assert(std::ranges::sized_range<clapp::values_ref<std::string>>);
    static_assert(std::same_as<std::ranges::range_reference_t<clapp::values_ref<std::string>>,
                               const std::string&>);
    static_assert(std::same_as<std::ranges::range_value_t<clapp::occurrences_ref<std::string>>,
                               clapp::values_ref<std::string>>);
    static_assert(std::same_as<std::ranges::range_value_t<clapp::raw_occurrences_ref>,
                               std::span<const os_string>>);

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    /** Append one `int` value with its raw spelling, the shape the parser produces. */
    void push_int(matched_arg& arg, int value, std::string_view raw) {
        arg.append_value(any_value(std::in_place_type<int>, value), os_string{raw});
    }

    /** Append one `std::string` value whose raw spelling is itself. */
    void push_text(matched_arg& arg, std::string_view value) {
        arg.append_value(any_value(std::in_place_type<std::string>, std::string{value}),
                         os_string{value});
    }

    /** Read every stored value back as a `T`, materialized so the assertions read plainly. */
    template<class T>
    [[nodiscard]] std::vector<T> collect(const matched_arg& arg) {
        std::vector<T> out;
        for (const any_value& value : arg.values()) out.push_back(value.get<T>());
        return out;
    }

    /** The raw bytes, as ordinary strings. */
    [[nodiscard]] std::vector<std::string> collect_raw(const matched_arg& arg) {
        std::vector<std::string> out;
        for (const os_string& raw : arg.raw_values()) out.push_back(raw.chars());
        return out;
    }

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: a fresh argument knows its type and holds nothing") {
    const matched_arg arg{any_id::of<int>()};
    CLAPP_CHECK(arg.has_type_id());
    CLAPP_CHECK(arg.type_id() == any_id::of<int>());
    CLAPP_CHECK(arg.empty());
    CLAPP_CHECK(arg.value_count() == 0);
    CLAPP_CHECK(arg.occurrence_count() == 0);
    CLAPP_CHECK(arg.value_count_in_last_occurrence() == 0);
    CLAPP_CHECK(!arg.has_source());
    CLAPP_CHECK(!arg.source().has_value());
    CLAPP_CHECK(arg.indices().empty());
    CLAPP_CHECK(!arg.ignore_case());
}

CLAPP_TEST("matched_arg: a group declares no type of its own") {
    const matched_arg group = matched_arg::for_group();
    CLAPP_CHECK(!group.has_type_id());
    CLAPP_CHECK(!group.type_id().has_value());
    // clap's `new_group` and a default-constructed one are the same state.
    CLAPP_CHECK(group == matched_arg{});
    // An explicitly empty id means the same thing, which is what lets the parser pass
    // a value parser's id through unconditionally.
    CLAPP_CHECK(matched_arg{any_id{}} == group);
}

// ---------------------------------------------------------------------------
// Occurrences, values and raw values
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: occurrences slice one flat store, values and raw in step") {
    matched_arg arg{any_id::of<std::string>()};
    arg.start_occurrence();
    push_text(arg, "a");
    push_text(arg, "b");
    arg.start_occurrence();
    push_text(arg, "c");
    push_text(arg, "d");

    // Flat: the order the values arrived in, across occurrence boundaries.
    CLAPP_CHECK(collect<std::string>(arg) == std::vector<std::string>{"a", "b", "c", "d"});
    CLAPP_CHECK(collect_raw(arg) == std::vector<std::string>{"a", "b", "c", "d"});
    CLAPP_CHECK(arg.value_count() == 4);

    // Grouped: two occurrences of two values each, over the same storage.
    CLAPP_CHECK(arg.occurrence_count() == 2);
    const std::span<const value_group> groups = arg.occurrences();
    CLAPP_CHECK(groups[0] == (value_group{.first = 0, .count = 2}));
    CLAPP_CHECK(groups[1] == (value_group{.first = 2, .count = 2}));
    CLAPP_CHECK(arg.value_count_in_last_occurrence() == 2);

    // The boundaries index BOTH stores; that is the invariant append_value() exists to
    // hold. Slicing raw_values() with a group taken from values() must be meaningful.
    const std::span<const os_string> raw = arg.raw_values();
    CLAPP_CHECK(raw.subspan(groups[1].first, groups[1].count)[0].view() == os_str{"c"});
}

CLAPP_TEST("matched_arg: an occurrence with no values survives — that is a flag") {
    matched_arg flag{any_id::of<bool>()};
    flag.start_occurrence();
    flag.start_occurrence();
    flag.start_occurrence();

    // Three sightings, nothing stored. clap's `new_val_group` pushes an empty vector
    // for exactly this; collapsing empty groups away would lose the occurrence count,
    // which is the only thing a `count` action has.
    CLAPP_CHECK(flag.occurrence_count() == 3);
    CLAPP_CHECK(flag.value_count() == 0);
    CLAPP_CHECK(flag.empty());
    CLAPP_CHECK(flag.occurrences()[2] == (value_group{.first = 0, .count = 0}));
}

CLAPP_TEST("matched_arg: append_value opens an occurrence when none is open") {
    // clap panics here ("We assume there is always a group created before"). clapp
    // takes the recoverable reading, which agrees with clap on every well-formed
    // sequence and does not abort a process over an off-by-one in bookkeeping.
    matched_arg arg{any_id::of<int>()};
    push_int(arg, 7, "7");

    CLAPP_CHECK(arg.occurrence_count() == 1);
    CLAPP_CHECK(arg.occurrences()[0] == (value_group{.first = 0, .count = 1}));
    CLAPP_CHECK(collect<int>(arg) == std::vector<int>{7});
}

// ---------------------------------------------------------------------------
// Source merging
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: set_source keeps the stronger source, in either order") {
    matched_arg from_argv{any_id::of<int>()};
    from_argv.set_source(value_source::command_line);
    from_argv.set_source(value_source::default_value);
    // The trap: a plain assignment here would report `default_value` and the help
    // renderer would then present a user-supplied value as a default.
    CLAPP_CHECK(from_argv.source() == std::optional{value_source::command_line});

    matched_arg from_default{any_id::of<int>()};
    from_default.set_source(value_source::default_value);
    from_default.set_source(value_source::command_line);
    CLAPP_CHECK(from_default.source() == std::optional{value_source::command_line});

    matched_arg from_env{any_id::of<int>()};
    from_env.set_source(value_source::default_value);
    from_env.set_source(value_source::env_variable);
    CLAPP_CHECK(from_env.source() == std::optional{value_source::env_variable});
}

CLAPP_TEST("matched_arg: is_explicit treats 'no source yet' as explicit") {
    // clap's `check_explicit` bails out only when a source is recorded AND it is a
    // default. An argument the parser has not attributed yet must not be filtered out.
    const matched_arg unattributed{any_id::of<int>()};
    CLAPP_CHECK(!unattributed.has_source());
    CLAPP_CHECK(unattributed.is_explicit());

    matched_arg defaulted{any_id::of<int>()};
    defaulted.set_source(value_source::default_value);
    CLAPP_CHECK(!defaulted.is_explicit());

    matched_arg from_env{any_id::of<int>()};
    from_env.set_source(value_source::env_variable);
    CLAPP_CHECK(from_env.is_explicit());
}

// ---------------------------------------------------------------------------
// Type inference
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: a declared type wins over whatever is stored") {
    matched_arg arg{any_id::of<int>()};
    arg.start_occurrence();
    push_int(arg, 1, "1");
    CLAPP_CHECK(arg.infer_type_id(any_id::of<int>()) == any_id::of<int>());
    // Asking for the wrong type reports the declared one, not the requested one.
    CLAPP_CHECK(arg.infer_type_id(any_id::of<long>()) == any_id::of<int>());
}

CLAPP_TEST("matched_arg: an empty group accepts whatever type is asked for") {
    const matched_arg group = matched_arg::for_group();
    CLAPP_CHECK(group.infer_type_id(any_id::of<int>()) == any_id::of<int>());
    CLAPP_CHECK(group.infer_type_id(any_id::of<std::string>()) == any_id::of<std::string>());
}

CLAPP_TEST("matched_arg: a group reports the first value that is NOT the asked type") {
    // The subtle half of clap's `infer_type_id`: it searches for a MISMATCH, not for
    // the first element. Implementing it as "type of vals[0]" passes when the first
    // member disagrees and fails silently when only a later one does — so the group
    // below deliberately leads with a matching value.
    matched_arg group = matched_arg::for_group();
    group.start_occurrence();
    push_int(group, 1, "1");
    group.append_value(any_value(std::in_place_type<std::string>, std::string{"two"}),
                       os_string{"two"});

    CLAPP_CHECK(group.infer_type_id(any_id::of<int>()) == any_id::of<std::string>());
    CLAPP_CHECK(group.infer_type_id(any_id::of<std::string>()) == any_id::of<int>());
    // Homogeneous read of a homogeneous prefix must still not pass.
    CLAPP_CHECK(group.infer_type_id(any_id::of<int>()) != any_id::of<int>());
}

// ---------------------------------------------------------------------------
// Indices
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: indices are kept in order and independently of values") {
    matched_arg flag{any_id::of<bool>()};
    flag.start_occurrence();
    flag.push_index(1);
    flag.start_occurrence();
    flag.push_index(4);

    // A flag stores no value at all, and still has to report where it was seen.
    CLAPP_CHECK(flag.value_count() == 0);
    CLAPP_CHECK(flag.indices().size() == 2);
    CLAPP_CHECK(flag.indices()[0] == 1);
    CLAPP_CHECK(flag.indices()[1] == 4);
    CLAPP_CHECK(flag.index_at(0) == std::optional<std::size_t>{1});
    CLAPP_CHECK(flag.index_at(1) == std::optional<std::size_t>{4});
    CLAPP_CHECK(!flag.index_at(2).has_value());

    const matched_arg none{any_id::of<int>()};
    CLAPP_CHECK(!none.index_at(0).has_value());
}

// ---------------------------------------------------------------------------
// Raw-value matching — the `ArgPredicate::Equals` half of clap's check_explicit
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: has_raw_value compares bytes, and folds case only when told") {
    matched_arg arg{any_id::of<std::string>()};
    arg.start_occurrence();
    push_text(arg, "Always");

    CLAPP_CHECK(arg.has_raw_value(os_str{"Always"}));
    CLAPP_CHECK(!arg.has_raw_value(os_str{"always"}));
    CLAPP_CHECK(!arg.has_raw_value(os_str{"never"}));

    arg.set_ignore_case(true);
    CLAPP_CHECK(arg.ignore_case());
    CLAPP_CHECK(arg.has_raw_value(os_str{"always"}));
    CLAPP_CHECK(arg.has_raw_value(os_str{"ALWAYS"}));
    CLAPP_CHECK(!arg.has_raw_value(os_str{"never"}));
}

CLAPP_TEST("matched_arg: has_raw_value works on bytes that are not valid UTF-8") {
    // The reason the comparison is on os_str and not on std::string_view: a value may
    // be any byte sequence, and clap compares OsStr here for the same reason.
    const std::string invalid{"\xe9!", 2};
    matched_arg arg{any_id::of<os_string>()};
    arg.start_occurrence();
    arg.append_value(any_value(std::in_place_type<os_string>, os_string{invalid}),
                     os_string{invalid});

    CLAPP_CHECK(!arg.raw_values()[0].is_utf8());
    CLAPP_CHECK(arg.has_raw_value(os_str{std::string_view{invalid}}));
    CLAPP_CHECK(!arg.has_raw_value(os_str{"!"}));
}

// ---------------------------------------------------------------------------
// Copying and equality
// ---------------------------------------------------------------------------

CLAPP_TEST("matched_arg: release_values empties everything derived from the values") {
    // The invariant release_values() has to keep: values() and raw_values() are
    // parallel and occurrences() indexes both, so the values cannot leave on their own.
    matched_arg arg{any_id::of<std::string>()};
    arg.start_occurrence();
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"a"}), os_string{"a"});
    arg.push_index(2);
    arg.set_source(value_source::command_line);

    const std::vector<any_value> taken = arg.release_values();
    CLAPP_CHECK(taken.size() == 1);
    CLAPP_CHECK(taken[0].get<std::string>() == "a");
    CLAPP_CHECK(arg.empty());
    CLAPP_CHECK(arg.raw_values().empty());
    CLAPP_CHECK(arg.occurrences().empty());
    CLAPP_CHECK(arg.indices().empty());
    CLAPP_CHECK(!arg.has_source());
    // Only the declared type survives, so the emptied entry still knows what it holds.
    CLAPP_CHECK(arg.type_id() == any_id::of<std::string>());
}

CLAPP_TEST("matched_arg: copying is deep") {
    matched_arg original{any_id::of<std::string>()};
    original.start_occurrence();
    push_text(original, "one");
    original.set_source(value_source::command_line);
    original.push_index(3);

    matched_arg copy = original;
    CLAPP_CHECK(copy == original);
    CLAPP_CHECK(collect<std::string>(copy) == std::vector<std::string>{"one"});
    // Distinct storage: mutating the copy must not be visible through the original.
    push_text(copy, "two");
    CLAPP_CHECK(copy.value_count() == 2);
    CLAPP_CHECK(original.value_count() == 1);
    CLAPP_CHECK(!(copy == original));
}

CLAPP_TEST("matched_arg: equality covers everything comparable, and says so") {
    matched_arg lhs{any_id::of<std::string>()};
    lhs.start_occurrence();
    push_text(lhs, "x");
    matched_arg rhs = lhs;
    CLAPP_CHECK(lhs == rhs);

    // Each field on its own is enough to break equality.
    matched_arg other_source = lhs;
    other_source.set_source(value_source::command_line);
    CLAPP_CHECK(!(other_source == lhs));

    matched_arg other_index = lhs;
    other_index.push_index(1);
    CLAPP_CHECK(!(other_index == lhs));

    matched_arg other_case = lhs;
    other_case.set_ignore_case(true);
    CLAPP_CHECK(!(other_case == lhs));

    matched_arg other_groups = lhs;
    other_groups.start_occurrence();
    CLAPP_CHECK(!(other_groups == lhs));

    matched_arg other_raw{any_id::of<std::string>()};
    other_raw.start_occurrence();
    push_text(other_raw, "y");
    CLAPP_CHECK(!(other_raw == lhs));

    matched_arg other_type = matched_arg::for_group();
    other_type.start_occurrence();
    push_text(other_type, "x");
    CLAPP_CHECK(!(other_type == lhs));
}

CLAPP_TEST("matched_arg: equality ignores parsed values, and raw values cover for it") {
    // clapp::any_value has no operator==, so clap and clapp both compare raw_vals
    // instead and destructure `vals: _`. The consequence is worth stating out loud:
    // two matched_args that agree on every raw byte compare equal even when their
    // stored values differ, which can only happen if two different value parsers were
    // fed identical bytes.
    matched_arg as_text{any_id{}};
    as_text.start_occurrence();
    as_text.append_value(any_value(std::in_place_type<std::string>, std::string{"1"}),
                         os_string{"1"});

    matched_arg as_number{any_id{}};
    as_number.start_occurrence();
    as_number.append_value(any_value(std::in_place_type<int>, 1), os_string{"1"});

    CLAPP_CHECK(as_text == as_number);
    // ... and the difference is still visible where it matters.
    CLAPP_CHECK(as_text.values()[0].type() != as_number.values()[0].type());
    CLAPP_CHECK(as_text.infer_type_id(any_id::of<int>()) == any_id::of<std::string>());
}
