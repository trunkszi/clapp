#include <clapp/builder/value_parser.hpp>
#include <clapp/parser/arg_matches.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
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
    using clapp::arg_id;
    using clapp::arg_matches;
    using clapp::count_type;
    using clapp::matched_arg;
    using clapp::matches_error;
    using clapp::matches_error_kind;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::value_group;
    using clapp::value_source;
    using namespace std::string_view_literals;

    /**
     * An enumeration whose possible values clapp::value_parser derives by reflection —
     * the one builtin parser that is not a fundamental or library type.
     */
    enum class mode : std::uint8_t { fast, careful };

    // ---------------------------------------------------------------------------
    // Compile-time facts
    // ---------------------------------------------------------------------------

    static_assert(std::is_copy_constructible_v<arg_matches>);
    static_assert(std::is_move_constructible_v<arg_matches>);
    static_assert(std::is_nothrow_move_constructible_v<arg_matches>);
    static_assert(std::is_copy_assignable_v<arg_matches>);
    static_assert(std::is_move_assignable_v<arg_matches>);

    // clapp::matches_error is a value type and is comparable, so a caller can branch on it
    // rather than on a formatted string. It is also the one thing in this header that is
    // fully constexpr — clapp::any_id is a compile-time identity — so its whole contract
    // including the rendered message is settled here rather than at run time.
    static_assert(std::is_trivially_copyable_v<matches_error>);
    static_assert(matches_error::unknown_argument().kind() == matches_error_kind::unknown_argument);
    static_assert(matches_error::downcast(any_id::of<long>(), any_id::of<int>()).kind() ==
                  matches_error_kind::downcast);
    static_assert(matches_error::downcast(any_id::of<long>(), any_id::of<int>()).expected() ==
                  any_id::of<long>());
    static_assert(matches_error::downcast(any_id::of<long>(), any_id::of<int>()).actual() ==
                  any_id::of<int>());
    static_assert(matches_error::downcast(any_id::of<long>(), any_id::of<int>()) !=
                  matches_error::downcast(any_id::of<int>(), any_id::of<long>()));
    static_assert(matches_error::downcast(any_id::of<int>(), any_id::of<int>()) !=
                  matches_error::unknown_argument());
    static_assert(name_of(matches_error_kind::downcast) == "downcast"sv);
    static_assert(name_of(matches_error_kind::unknown_argument) == "unknown-argument"sv);
    static_assert(name_of(static_cast<matches_error_kind>(200)).empty());

    /**
     * The rendered message names **both** types, whatever this implementation spells them.
     * Built inside a constant expression on purpose: to_string() assembles a std::string
     * with push_back only, and the `ubsan` preset is where the alternative spellings
     * (`operator+`, `std::string{ptr, n}`) stop being constant expressions — CLAUDE.md
     * trap 10. If someone "tidies" that loop, this assertion is what stops it.
     */
    [[nodiscard]] consteval bool downcast_message_names_both_types() {
        const std::string rendered =
                matches_error::downcast(any_id::of<long>(), any_id::of<int>()).to_string();
        const std::string_view text{rendered};
        return text.find(any_id::of<long>().name()) != std::string_view::npos &&
               text.find(any_id::of<int>().name()) != std::string_view::npos;
    }

    static_assert(downcast_message_names_both_types());

    [[nodiscard]] consteval bool unknown_argument_message_points_at_the_id() {
        const std::string rendered = matches_error::unknown_argument().to_string();
        const std::string_view text{rendered};
        return text.find("argument id") != std::string_view::npos;
    }

    static_assert(unknown_argument_message_points_at_the_id());

    // ---------------------------------------------------------------------------
    // Helpers — values are produced the way the parser will produce them
    // ---------------------------------------------------------------------------

    /**
     * Build a clapp::matched_arg by running \p raws through `value_parser<T>`.
     *
     * This is the join the tests care about: the id recorded in the matched_arg, the id
     * baked into the clapp::any_value by `parser_vtable::parse`, and the id `get_one<T>`
     * asks for all have to be the same one.
     */
    template<class T>
    [[nodiscard]] matched_arg parsed(std::initializer_list<std::string_view> raws,
                                     bool ignore_case = false) {
        matched_arg arg{any_id::of<T>()};
        arg.start_occurrence();
        for (const std::string_view raw : raws) {
            std::expected<any_value, clapp::parse_error> value =
                    clapp::parser_for<T>()->parse(os_str{raw}, ignore_case);
            CLAPP_CHECK(value.has_value());
            arg.append_value(std::move(value).value(), os_string{raw});
        }
        return arg;
    }

    /** A matched_arg holding one hand-built value, for the cases with no parser involved. */
    template<class T>
    [[nodiscard]] matched_arg holding(T value, std::string_view raw) {
        matched_arg arg{any_id::of<T>()};
        arg.start_occurrence();
        arg.append_value(any_value(std::in_place_type<T>, std::move(value)), os_string{raw});
        return arg;
    }

    /** Read back every value of \p id as a `T`, materialized. */
    template<class T>
    [[nodiscard]] std::vector<T> many(const arg_matches& matches, std::string_view id) {
        // Bound to a named variable rather than iterated in place: clang-p2996 rejects
        // `for (auto v : *matches.get_many<T>(id))` under -Werror with -Wdangling-gsl.
        // See the file-level warning on <clapp/parser/arg_matches.hpp>; it is a false
        // positive, and this is the spelling that sidesteps it.
        const std::optional<clapp::values_ref<T>> values = matches.get_many<T>(id);
        if (!values.has_value()) return {};
        std::vector<T> out;
        for (const T& value : *values) out.push_back(value);
        return out;
    }

    /** The occurrence structure of \p id, as plain nested vectors. */
    template<class T>
    [[nodiscard]] std::vector<std::vector<T>> grouped(const arg_matches& matches,
                                                      std::string_view id) {
        const std::optional<clapp::occurrences_ref<T>> occurrences = matches.get_occurrences<T>(id);
        if (!occurrences.has_value()) return {};
        std::vector<std::vector<T>> out;
        for (const auto occurrence : *occurrences) {
            std::vector<T> one;
            for (const T& value : occurrence) one.push_back(value);
            out.push_back(std::move(one));
        }
        return out;
    }

    /** The raw bytes of \p id, as plain strings. */
    [[nodiscard]] std::vector<std::string> raw_of(const arg_matches& matches, std::string_view id) {
        const std::optional<std::span<const os_string>> raws = matches.get_raw(id);
        if (!raws.has_value()) return {};
        std::vector<std::string> out;
        for (const os_string& raw : *raws) out.push_back(raw.chars());
        return out;
    }

}  // namespace

// ---------------------------------------------------------------------------
// Every builtin value parser, end to end
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: values come back as the type the value parser produced") {
    arg_matches matches;
    matches.insert_arg(arg_id{"count"}, parsed<int>({"-42"}));
    matches.insert_arg(arg_id{"port"}, parsed<unsigned>({"8080"}));
    matches.insert_arg(arg_id{"wide"}, parsed<std::int64_t>({"9007199254740993"}));
    matches.insert_arg(arg_id{"ratio"}, parsed<double>({"0.5"}));
    matches.insert_arg(arg_id{"scale"}, parsed<float>({"2.5"}));
    matches.insert_arg(arg_id{"yes"}, parsed<bool>({"true"}));
    matches.insert_arg(arg_id{"sep"}, parsed<char>({","}));
    matches.insert_arg(arg_id{"name"}, parsed<std::string>({"clapp"}));
    matches.insert_arg(arg_id{"opaque"}, parsed<os_string>({"raw-bytes"}));
    matches.insert_arg(arg_id{"file"}, parsed<std::filesystem::path>({"a/b.txt"}));
    matches.insert_arg(arg_id{"mode"}, parsed<mode>({"careful"}));

    CLAPP_CHECK(**matches.get_one<int>("count") == -42);
    CLAPP_CHECK(**matches.get_one<unsigned>("port") == 8080U);
    CLAPP_CHECK(**matches.get_one<std::int64_t>("wide") == 9007199254740993LL);
    CLAPP_CHECK(**matches.get_one<double>("ratio") == 0.5);
    CLAPP_CHECK(**matches.get_one<float>("scale") == 2.5F);
    CLAPP_CHECK(**matches.get_one<bool>("yes"));
    CLAPP_CHECK(**matches.get_one<char>("sep") == ',');
    CLAPP_CHECK(**matches.get_one<std::string>("name") == "clapp");
    CLAPP_CHECK((*matches.get_one<os_string>("opaque"))->view() == os_str{"raw-bytes"});
    CLAPP_CHECK(**matches.get_one<std::filesystem::path>("file") ==
                std::filesystem::path{"a/b.txt"});
    CLAPP_CHECK(**matches.get_one<mode>("mode") == mode::careful);

    // The raw bytes survive alongside the parsed value, for every one of them.
    CLAPP_CHECK(raw_of(matches, "count") == std::vector<std::string>{"-42"});
    CLAPP_CHECK(raw_of(matches, "mode") == std::vector<std::string>{"careful"});
    CLAPP_CHECK(matches.arg_count() == 11);
}

CLAPP_TEST("arg_matches: near-miss types are distinct, not merely spelled differently") {
    // The distinctness that matters is between types a plausible bug would confuse —
    // int vs long on a 64-bit platform where both are integers of some width, and
    // char vs the unsigned char that clapp::count_type actually is. Comparing the
    // names to literals is exactly what CLAUDE.md trap 11 forbids, so this compares
    // ids to ids.
    arg_matches matches;
    matches.insert_arg(arg_id{"n"}, parsed<int>({"1"}));

    CLAPP_CHECK(matches.try_get_one<int>("n").has_value());
    CLAPP_CHECK(!matches.try_get_one<long>("n").has_value());
    CLAPP_CHECK(!matches.try_get_one<unsigned>("n").has_value());
    CLAPP_CHECK(!matches.try_get_one<std::int64_t>("n").has_value());

    matches.insert_arg(arg_id{"v"}, holding<count_type>(count_type{3}, "3"));
    CLAPP_CHECK(matches.try_get_one<count_type>("v").has_value());
    CLAPP_CHECK(!matches.try_get_one<char>("v").has_value());
    CLAPP_CHECK(!matches.try_get_one<signed char>("v").has_value());
    CLAPP_CHECK(any_id::of<count_type>() != any_id::of<char>());
}

// ---------------------------------------------------------------------------
// Wrong type
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: a wrong T reports both types, and never a value") {
    arg_matches matches;
    matches.insert_arg(arg_id{"port"}, parsed<int>({"8080"}));

    const std::expected<std::optional<const long*>, matches_error> wrong =
            matches.try_get_one<long>("port");
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(wrong.error().kind() == matches_error_kind::downcast);
    CLAPP_CHECK(wrong.error().expected() == any_id::of<long>());
    CLAPP_CHECK(wrong.error().actual() == any_id::of<int>());
    CLAPP_CHECK(wrong.error() == matches_error::downcast(any_id::of<long>(), any_id::of<int>()));

    // The message names both types, using whatever spelling this implementation gives
    // them. Asserting the spelling itself would pin the test to one compiler.
    const std::string message = wrong.error().to_string();
    CLAPP_CHECK(message.find(any_id::of<long>().name()) != std::string::npos);
    CLAPP_CHECK(message.find(any_id::of<int>().name()) != std::string::npos);

    // Every typed accessor refuses in the same way; none of them half-succeeds.
    CLAPP_CHECK(!matches.try_get_many<long>("port").has_value());
    CLAPP_CHECK(!matches.try_get_occurrences<long>("port").has_value());
    CLAPP_CHECK(matches.try_get_many<long>("port").error().kind() == matches_error_kind::downcast);

    // ... but the raw bytes are untyped and stay readable, which is the escape hatch
    // for a caller that got the type wrong and needs to find out what is there.
    CLAPP_CHECK(matches.try_get_raw("port").has_value());
    CLAPP_CHECK(raw_of(matches, "port") == std::vector<std::string>{"8080"});
}

CLAPP_TEST("arg_matches: get_one aborts exactly where try_get_one reports") {
    // The abort itself is not exercised — that needs fork(), which clapp's
    // zero-dependency harness does not have, and which tests/units/util/any_value_test.cpp
    // declined for the same reason. What IS checked is that the two halves share one
    // decision, so the documented "get_one aborts on a wrong T" cannot drift away from
    // the condition tested above.
    arg_matches matches;
    matches.insert_arg(arg_id{"port"}, parsed<int>({"8080"}));

    // Agreement on the right type: try_ succeeds, so get_one does not abort, and both
    // produce the same value.
    CLAPP_CHECK(matches.try_get_one<int>("port").has_value());
    CLAPP_CHECK(**matches.get_one<int>("port") == 8080);

    // Agreement on an absent id: try_ succeeds with an empty optional, so get_one is
    // safe to call and answers std::nullopt rather than aborting.
    CLAPP_CHECK(matches.try_get_one<int>("nope").has_value());
    CLAPP_CHECK(!matches.try_get_one<int>("nope").value().has_value());
    CLAPP_CHECK(!matches.get_one<int>("nope").has_value());

    // Disagreement: try_ reports, and this is the one case where get_one would abort.
    CLAPP_CHECK(!matches.try_get_one<long>("port").has_value());
}

CLAPP_TEST("arg_matches: an empty group accepts any T, a populated one does not") {
    // clap's infer_type_id, reached through the container: a group that collected
    // nothing has no type to disagree with.
    arg_matches matches;
    matches.insert_arg(arg_id{"empty-group"}, matched_arg::for_group());
    CLAPP_CHECK(matches.try_get_one<int>("empty-group").has_value());
    CLAPP_CHECK(matches.try_get_one<std::string>("empty-group").has_value());
    CLAPP_CHECK(!matches.get_one<int>("empty-group").has_value());

    matched_arg group = matched_arg::for_group();
    group.start_occurrence();
    group.append_value(any_value(std::in_place_type<int>, 1), os_string{"1"});
    matches.insert_arg(arg_id{"int-group"}, std::move(group));
    CLAPP_CHECK(matches.try_get_one<int>("int-group").has_value());
    CLAPP_CHECK(!matches.try_get_one<std::string>("int-group").has_value());
    CLAPP_CHECK(**matches.get_one<int>("int-group") == 1);

    // A group whose members disagree, read through the container. This is the case
    // that separates "infer the type from the first value" — which passes every
    // assertion above — from clap's actual rule, "find the first value that is NOT the
    // requested type". The group deliberately LEADS with an int, so a first-element
    // implementation would hand back a std::string reinterpreted as an int.
    matched_arg mixed = matched_arg::for_group();
    mixed.start_occurrence();
    mixed.append_value(any_value(std::in_place_type<int>, 1), os_string{"1"});
    mixed.append_value(any_value(std::in_place_type<std::string>, std::string{"two"}),
                       os_string{"two"});
    matches.insert_arg(arg_id{"mixed-group"}, std::move(mixed));

    CLAPP_CHECK(!matches.try_get_one<int>("mixed-group").has_value());
    CLAPP_CHECK(matches.try_get_one<int>("mixed-group").error().actual() ==
                any_id::of<std::string>());
    CLAPP_CHECK(!matches.try_get_one<std::string>("mixed-group").has_value());
    CLAPP_CHECK(matches.try_get_one<std::string>("mixed-group").error().actual() ==
                any_id::of<int>());
    CLAPP_CHECK(!matches.try_get_many<int>("mixed-group").has_value());
    // The untyped view of the same group still works, which is how a caller finds out
    // what is actually in there.
    CLAPP_CHECK(raw_of(matches, "mixed-group") == std::vector<std::string>{"1", "two"});
}

// ---------------------------------------------------------------------------
// Absent ids
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: an absent id answers nullopt everywhere, and errors nowhere") {
    arg_matches matches;
    matches.insert_arg(arg_id{"present"}, parsed<int>({"1"}));

    CLAPP_CHECK(!matches.contains_id("absent"));
    CLAPP_CHECK(!matches.get_one<int>("absent").has_value());
    CLAPP_CHECK(!matches.get_many<int>("absent").has_value());
    CLAPP_CHECK(!matches.get_occurrences<int>("absent").has_value());
    CLAPP_CHECK(!matches.get_raw("absent").has_value());
    CLAPP_CHECK(!matches.get_raw_occurrences("absent").has_value());
    CLAPP_CHECK(!matches.value_source("absent").has_value());
    CLAPP_CHECK(!matches.index_of("absent").has_value());
    CLAPP_CHECK(!matches.indices_of("absent").has_value());
    CLAPP_CHECK(matches.find_arg("absent") == nullptr);

    // "Absent" is not an error, and the type check does not even run: asking for a
    // type nothing there could have is still Ok.
    CLAPP_CHECK(matches.try_get_one<std::string>("absent").has_value());
    CLAPP_CHECK(!matches.try_get_one<std::string>("absent").value().has_value());

    const arg_matches nothing;
    CLAPP_CHECK(nothing.empty());
    CLAPP_CHECK(nothing.arg_count() == 0);
    CLAPP_CHECK(!nothing.args_present());
    CLAPP_CHECK(!nothing.has_subcommand());
    CLAPP_CHECK(!nothing.subcommand().has_value());
    CLAPP_CHECK(!nothing.subcommand_name().has_value());
}

CLAPP_TEST("arg_matches: present-with-only-a-default is present, and says where from") {
    // The distinction clap's own documentation warns about twice: contains_id() and
    // get_one() both answer yes for a value nobody typed.
    arg_matches matches;
    matched_arg defaulted = parsed<int>({"1"});
    defaulted.set_source(value_source::default_value);
    matches.insert_arg(arg_id{"jobs"}, std::move(defaulted));

    matched_arg supplied = parsed<int>({"4"});
    supplied.set_source(value_source::command_line);
    matches.insert_arg(arg_id{"threads"}, std::move(supplied));

    CLAPP_CHECK(matches.contains_id("jobs"));
    CLAPP_CHECK(**matches.get_one<int>("jobs") == 1);
    CLAPP_CHECK(matches.value_source("jobs") == std::optional{value_source::default_value});
    CLAPP_CHECK(matches.value_source("threads") == std::optional{value_source::command_line});

    // args_present() is the aggregate form of the same question.
    CLAPP_CHECK(matches.args_present());
    arg_matches only_defaults;
    matched_arg only_default = parsed<int>({"1"});
    only_default.set_source(value_source::default_value);
    only_defaults.insert_arg(arg_id{"jobs"}, std::move(only_default));
    CLAPP_CHECK(only_defaults.contains_id("jobs"));
    CLAPP_CHECK(!only_defaults.args_present());
}

// ---------------------------------------------------------------------------
// Ordering, occurrences and indices
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: multiple values keep the order they were supplied in") {
    arg_matches matches;
    matches.insert_arg(arg_id{"ports"}, parsed<int>({"22", "80", "2020"}));

    // clap's `get_many` example, value for value.
    CLAPP_CHECK(many<int>(matches, "ports") == std::vector<int>{22, 80, 2020});
    CLAPP_CHECK(raw_of(matches, "ports") == std::vector<std::string>{"22", "80", "2020"});
    CLAPP_CHECK(matches.get_many<int>("ports")->size() == 3);

    // Order is not merely "the set is right": reversing it must fail.
    CLAPP_CHECK(!(many<int>(matches, "ports") == std::vector<int>{2020, 80, 22}));
}

CLAPP_TEST("arg_matches: occurrences group what get_many flattens") {
    // clap's get_occurrences example: `-x a b -x c d`.
    matched_arg arg{any_id::of<std::string>()};
    arg.start_occurrence();
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"a"}), os_string{"a"});
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"b"}), os_string{"b"});
    arg.start_occurrence();
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"c"}), os_string{"c"});
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"d"}), os_string{"d"});

    arg_matches matches;
    matches.insert_arg(arg_id{"x"}, std::move(arg));

    CLAPP_CHECK(many<std::string>(matches, "x") == std::vector<std::string>{"a", "b", "c", "d"});
    const std::vector<std::vector<std::string>> groups = grouped<std::string>(matches, "x");
    CLAPP_CHECK(groups.size() == 2);
    CLAPP_CHECK(groups[0] == std::vector<std::string>{"a", "b"});
    CLAPP_CHECK(groups[1] == std::vector<std::string>{"c", "d"});

    // The raw side groups identically, over its own storage.
    const std::optional<clapp::raw_occurrences_ref> raws = matches.get_raw_occurrences("x");
    CLAPP_CHECK(raws.has_value());
    std::vector<std::size_t> sizes;
    for (const std::span<const os_string> occurrence : *raws) sizes.push_back(occurrence.size());
    CLAPP_CHECK(sizes == std::vector<std::size_t>{2, 2});
}

CLAPP_TEST("arg_matches: indices are preserved, in order, per id") {
    // clap's indices_of example: `myapp -o val1 -f -o val2 -f`, where the option
    // records its VALUES' positions and the flag records the switch's.
    arg_matches matches;
    matched_arg option = parsed<std::string>({"val1", "val2"});
    option.push_index(2);
    option.push_index(5);
    matches.insert_arg(arg_id{"option"}, std::move(option));

    matched_arg flag{any_id::of<count_type>()};
    flag.start_occurrence();
    flag.append_value(any_value(std::in_place_type<count_type>, count_type{2}), os_string{"2"});
    flag.push_index(6);
    matches.insert_arg(arg_id{"flag"}, std::move(flag));

    CLAPP_CHECK(matches.index_of("option") == std::optional<std::size_t>{2});
    CLAPP_CHECK(matches.indices_of("option")->size() == 2);
    CLAPP_CHECK((*matches.indices_of("option"))[0] == 2);
    CLAPP_CHECK((*matches.indices_of("option"))[1] == 5);
    CLAPP_CHECK(matches.index_of("flag") == std::optional<std::size_t>{6});
    CLAPP_CHECK(matches.indices_of("flag")->size() == 1);

    // An id that recorded no index at all is present but has none — distinct from
    // being absent, which is the previous test.
    matches.insert_arg(arg_id{"defaulted"}, parsed<int>({"1"}));
    CLAPP_CHECK(matches.contains_id("defaulted"));
    CLAPP_CHECK(!matches.index_of("defaulted").has_value());
    CLAPP_CHECK(matches.indices_of("defaulted")->empty());
}

// ---------------------------------------------------------------------------
// Flags and counts
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: get_flag reads what set_true and set_false store") {
    arg_matches matches;
    // clapp::default_value_of(arg_action::set_true) is "false", parsed by
    // value_parser<bool> — so the stored type is bool whichever way it went.
    matches.insert_arg(arg_id{"verbose"}, parsed<bool>({"true"}));
    matches.insert_arg(arg_id{"quiet"}, parsed<bool>({"false"}));

    CLAPP_CHECK(matches.get_flag("verbose"));
    CLAPP_CHECK(!matches.get_flag("quiet"));
    CLAPP_CHECK(matches.contains_id("verbose"));

    // The same value through the general accessor, which is the escape hatch when
    // aborting on an absent id is unacceptable.
    CLAPP_CHECK(**matches.get_one<bool>("verbose"));
    CLAPP_CHECK(matches.try_get_one<bool>("quiet").has_value());
    CLAPP_CHECK(!**matches.try_get_one<bool>("quiet").value());
}

CLAPP_TEST("arg_matches: get_count reads what the count action accumulates") {
    arg_matches matches;
    // clapp::count_type is std::uint8_t, matching clap's u8: `-vvv` is 3, and 256
    // occurrences would saturate rather than widen. The stored type has to be that
    // type exactly, which is why get_count does not return `unsigned`.
    matches.insert_arg(arg_id{"verbose"}, holding<count_type>(count_type{3}, "3"));
    matches.insert_arg(arg_id{"silent"}, holding<count_type>(count_type{0}, "0"));

    CLAPP_CHECK(matches.get_count("verbose") == 3);
    CLAPP_CHECK(matches.get_count("silent") == 0);
    static_assert(std::is_same_v<decltype(matches.get_count("verbose")), count_type>);
    static_assert(std::is_same_v<count_type, std::uint8_t>);

    // A `count` flag read as a flag is a wrong-type access, not a coincidence that
    // happens to work: std::uint8_t is not bool.
    CLAPP_CHECK(!matches.try_get_one<bool>("verbose").has_value());
    CLAPP_CHECK(matches.try_get_one<bool>("verbose").error().kind() ==
                matches_error_kind::downcast);
}

// ---------------------------------------------------------------------------
// Moving values out
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: remove_* hands ownership over and drops the id") {
    arg_matches matches;
    matches.insert_arg(arg_id{"name"}, parsed<std::string>({"clapp"}));
    matches.insert_arg(arg_id{"files"}, parsed<std::string>({"a.txt", "b.txt", "c.txt"}));

    // The reason this family exists: the caller ends up owning the string, where
    // get_one() would hand back a pointer that dies with the arg_matches. This is
    // what `from_matches<T>` will be built on.
    const std::optional<std::string> name = matches.remove_one<std::string>("name");
    CLAPP_CHECK(name == std::optional<std::string>{"clapp"});
    CLAPP_CHECK(!matches.contains_id("name"));
    CLAPP_CHECK(!matches.remove_one<std::string>("name").has_value());

    const std::optional<std::vector<std::string>> files = matches.remove_many<std::string>("files");
    CLAPP_CHECK(files.has_value());
    CLAPP_CHECK(*files == std::vector<std::string>{"a.txt", "b.txt", "c.txt"});
    CLAPP_CHECK(!matches.contains_id("files"));
    CLAPP_CHECK(matches.empty());

    // Absent is still not an error.
    CLAPP_CHECK(matches.try_remove_one<std::string>("nope").has_value());
    CLAPP_CHECK(!matches.try_remove_one<std::string>("nope").value().has_value());
    CLAPP_CHECK(!matches.remove_many<std::string>("nope").has_value());
}

CLAPP_TEST("arg_matches: remove_occurrences keeps the grouping get_occurrences shows") {
    matched_arg arg{any_id::of<std::string>()};
    arg.start_occurrence();
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"a"}), os_string{"a"});
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"b"}), os_string{"b"});
    arg.start_occurrence();
    arg.append_value(any_value(std::in_place_type<std::string>, std::string{"c"}), os_string{"c"});

    arg_matches matches;
    matches.insert_arg(arg_id{"x"}, std::move(arg));

    const std::optional<std::vector<std::vector<std::string>>> groups =
            matches.remove_occurrences<std::string>("x");
    CLAPP_CHECK(groups.has_value());
    CLAPP_CHECK(groups->size() == 2);
    CLAPP_CHECK((*groups)[0] == std::vector<std::string>{"a", "b"});
    CLAPP_CHECK((*groups)[1] == std::vector<std::string>{"c"});
    CLAPP_CHECK(!matches.contains_id("x"));
}

CLAPP_TEST("arg_matches: a failed remove puts the entry back, losing nothing") {
    // The half of clap's try_remove_arg_t that is easy to skip. Without the restore, a
    // caller that guessed the type wrong would destroy the very values it was trying
    // to read — and would only find out later, when the id turned up absent.
    arg_matches matches;
    matches.insert_arg(arg_id{"port"}, parsed<int>({"8080"}));

    const std::expected<std::optional<long>, matches_error> wrong =
            matches.try_remove_one<long>("port");
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(wrong.error().kind() == matches_error_kind::downcast);
    CLAPP_CHECK(wrong.error().actual() == any_id::of<int>());

    CLAPP_CHECK(matches.contains_id("port"));
    CLAPP_CHECK(matches.arg_count() == 1);
    CLAPP_CHECK(**matches.get_one<int>("port") == 8080);
    CLAPP_CHECK(raw_of(matches, "port") == std::vector<std::string>{"8080"});
    // ... and the right type still works afterwards.
    CLAPP_CHECK(matches.remove_one<int>("port") == std::optional{8080});
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: subcommands nest, and each level keeps its own arguments") {
    arg_matches leaf;
    leaf.insert_arg(arg_id{"target"}, parsed<std::string>({"aarch64"}));

    arg_matches middle;
    middle.insert_arg(arg_id{"release"}, parsed<bool>({"true"}));
    middle.set_subcommand(std::string{"target"}, std::move(leaf));

    arg_matches root;
    root.insert_arg(arg_id{"verbose"}, parsed<bool>({"true"}));
    root.set_subcommand(std::string{"build"}, std::move(middle));

    CLAPP_CHECK(root.has_subcommand());
    CLAPP_CHECK(root.subcommand_name() == std::optional{"build"sv});

    const std::optional<std::pair<std::string_view, const arg_matches&>> level1 = root.subcommand();
    CLAPP_CHECK(level1.has_value());
    CLAPP_CHECK(level1->first == "build"sv);
    CLAPP_CHECK(level1->second.get_flag("release"));
    // Both levels can hold arguments at the same time; the child does not shadow the
    // parent and the parent does not see the child's ids.
    CLAPP_CHECK(root.get_flag("verbose"));
    CLAPP_CHECK(!level1->second.contains_id("verbose"));
    CLAPP_CHECK(!root.contains_id("release"));

    const std::optional<std::pair<std::string_view, const arg_matches&>> level2 =
            level1->second.subcommand();
    CLAPP_CHECK(level2.has_value());
    CLAPP_CHECK(level2->first == "target"sv);
    CLAPP_CHECK(**level2->second.get_one<std::string>("target") == "aarch64");
    CLAPP_CHECK(!level2->second.has_subcommand());

    // subcommand_matches() answers only for the subcommand that actually ran.
    CLAPP_CHECK(root.subcommand_matches("build") != nullptr);
    CLAPP_CHECK(root.subcommand_matches("clean") == nullptr);
    CLAPP_CHECK(root.subcommand_matches("build")->subcommand_matches("target") != nullptr);
}

CLAPP_TEST("arg_matches: a subcommand can be detached, and the parent forgets it") {
    arg_matches child;
    child.insert_arg(arg_id{"release"}, parsed<bool>({"true"}));
    arg_matches root;
    root.set_subcommand(std::string{"build"}, std::move(child));

    std::optional<std::pair<std::string, arg_matches>> taken = root.remove_subcommand();
    CLAPP_CHECK(taken.has_value());
    CLAPP_CHECK(taken->first == "build");
    CLAPP_CHECK(taken->second.get_flag("release"));

    // The detached child now outlives the parent's ownership of it — the supported way
    // to keep one alive without copying the whole subtree.
    CLAPP_CHECK(!root.has_subcommand());
    CLAPP_CHECK(!root.subcommand().has_value());
    CLAPP_CHECK(root.subcommand_matches("build") == nullptr);
    CLAPP_CHECK(!root.remove_subcommand().has_value());
}

CLAPP_TEST("arg_matches: copying copies the whole subtree, references do not") {
    arg_matches child;
    child.insert_arg(arg_id{"release"}, parsed<bool>({"true"}));
    arg_matches root;
    root.set_subcommand(std::string{"build"}, std::move(child));

    arg_matches copy = root;
    CLAPP_CHECK(copy == root);
    CLAPP_CHECK(copy.has_subcommand());
    // Independent storage: the two children are equal but not the same object, so a
    // change to one is invisible in the other.
    CLAPP_CHECK(copy.subcommand_matches("build") != root.subcommand_matches("build"));

    std::optional<std::pair<std::string, arg_matches>> detached = copy.remove_subcommand();
    CLAPP_CHECK(detached.has_value());
    CLAPP_CHECK(!copy.has_subcommand());
    CLAPP_CHECK(root.has_subcommand());
    CLAPP_CHECK(root.subcommand_matches("build")->get_flag("release"));
}

CLAPP_TEST("arg_matches: an external subcommand keeps its trailing arguments under \"\"") {
    // clap stores the trailing arguments of an unrecognized subcommand in the child
    // matches under the EMPTY id (`Id::EXTERNAL`), and that id is always askable even
    // when id validation is on.
    arg_matches child;
    child.insert_arg(clapp::external_id, parsed<os_string>({"--option", "value", "--flag"}));
    child.set_valid_ids({});

    arg_matches root;
    root.set_subcommand(std::string{"subcmd"}, std::move(child));

    CLAPP_CHECK(root.subcommand_name() == std::optional{"subcmd"sv});
    const arg_matches* external = root.subcommand_matches("subcmd");
    CLAPP_CHECK(external != nullptr);
    CLAPP_CHECK(external->has_id_validation());
    CLAPP_CHECK(external->is_valid_id(""));
    CLAPP_CHECK(external->contains_id(""));
    CLAPP_CHECK(raw_of(*external, "") == std::vector<std::string>{"--option", "value", "--flag"});
}

// ---------------------------------------------------------------------------
// Id validation
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: id validation turns a typo into an error, not a nullopt") {
    arg_matches matches;
    matches.insert_arg(arg_id{"verbose"}, parsed<bool>({"true"}));

    // Off by default: an unknown id is simply absent, which is what a caller building
    // matches by hand expects.
    CLAPP_CHECK(!matches.has_id_validation());
    CLAPP_CHECK(matches.is_valid_id("--verbose"));
    CLAPP_CHECK(matches.try_get_one<bool>("--verbose").has_value());
    CLAPP_CHECK(!matches.contains_id("--verbose"));

    // On once the parser has declared the command's ids. The classic mistake — asking
    // by the long flag spelling instead of the id — now reports rather than lying.
    matches.set_valid_ids({arg_id{"verbose"}, arg_id{"quiet"}});
    CLAPP_CHECK(matches.has_id_validation());
    CLAPP_CHECK(matches.is_valid_id("verbose"));
    CLAPP_CHECK(matches.is_valid_id("quiet"));  // declared but never supplied
    CLAPP_CHECK(!matches.is_valid_id("--verbose"));

    const std::expected<std::optional<const bool*>, matches_error> typo =
            matches.try_get_one<bool>("--verbose");
    CLAPP_CHECK(!typo.has_value());
    CLAPP_CHECK(typo.error().kind() == matches_error_kind::unknown_argument);
    CLAPP_CHECK(typo.error().to_string().find("argument id") != std::string::npos);

    // A declared id that collected nothing is still absent, not unknown.
    CLAPP_CHECK(matches.try_contains_id("quiet").has_value());
    CLAPP_CHECK(!matches.try_contains_id("quiet").value());
    CLAPP_CHECK(!matches.try_contains_id("--verbose").has_value());

    // try_clear_id follows the same rule; erase_arg, which the parser uses, does not.
    CLAPP_CHECK(!matches.try_clear_id("--verbose").has_value());
    CLAPP_CHECK(matches.try_clear_id("verbose").value());
    CLAPP_CHECK(!matches.contains_id("verbose"));
    CLAPP_CHECK(!matches.erase_arg("verbose"));
}

CLAPP_TEST("arg_matches: validation is a property of the object, not of NDEBUG") {
    // clap keeps its valid-id list under `#[cfg(debug_assertions)]`, so the same
    // program answers differently in release. clapp makes it explicit instead, and an
    // EMPTY declared list is a real state rather than "validation off" — a command
    // with no arguments has to stay validatable.
    arg_matches matches;
    matches.set_valid_ids({});
    CLAPP_CHECK(matches.has_id_validation());
    CLAPP_CHECK(!matches.is_valid_id("anything"));
    CLAPP_CHECK(!matches.try_contains_id("anything").has_value());
    // The external id is the one exemption, exactly as in clap.
    CLAPP_CHECK(matches.is_valid_id(""));
    CLAPP_CHECK(matches.try_contains_id("").has_value());

    arg_matches subcommands;
    CLAPP_CHECK(!subcommands.has_subcommand_validation());
    CLAPP_CHECK(subcommands.is_valid_subcommand("whatever"));
    subcommands.set_valid_subcommands({std::string{"build"}, std::string{"test"}});
    CLAPP_CHECK(subcommands.has_subcommand_validation());
    CLAPP_CHECK(subcommands.is_valid_subcommand("build"));
    CLAPP_CHECK(!subcommands.is_valid_subcommand("buidl"));
}

// ---------------------------------------------------------------------------
// Container behaviour
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matches: ids, entry and erase behave as a container") {
    arg_matches matches;
    matches.insert_arg(arg_id{"zeta"}, parsed<int>({"1"}));
    matches.insert_arg(arg_id{"alpha"}, parsed<int>({"2"}));

    std::vector<std::string_view> ids;
    for (const arg_id& id : matches.ids()) ids.push_back(id.name());
    // NAME order, not insertion order: clapp::flat_map is sorted and clap's FlatMap is
    // not, which is exactly the difference the flat_map warning says nothing
    // user-visible may depend on.
    CLAPP_CHECK(ids == std::vector<std::string_view>{"alpha"sv, "zeta"sv});
    CLAPP_CHECK(matches.arg_count() == 2);

    // entry() creates on demand, the way the parser accumulates.
    matched_arg& fresh = matches.entry(arg_id{"gamma"});
    CLAPP_CHECK(fresh.empty());
    CLAPP_CHECK(matches.arg_count() == 3);
    fresh.start_occurrence();
    fresh.append_value(any_value(std::in_place_type<int>, 9), os_string{"9"});
    CLAPP_CHECK(**matches.get_one<int>("gamma") == 9);
    // ... and returns the existing entry the second time, rather than resetting it.
    CLAPP_CHECK(matches.entry(arg_id{"gamma"}).value_count() == 1);
    CLAPP_CHECK(matches.arg_count() == 3);

    CLAPP_CHECK(matches.erase_arg("gamma"));
    CLAPP_CHECK(!matches.erase_arg("gamma"));
    CLAPP_CHECK(matches.arg_count() == 2);

    // find_arg reaches the accumulator itself, for callers that need more than the
    // typed accessors offer.
    const matched_arg* alpha = matches.find_arg("alpha");
    CLAPP_CHECK(alpha != nullptr);
    CLAPP_CHECK(alpha->value_count() == 1);
    CLAPP_CHECK(matches.args().size() == 2);
}

CLAPP_TEST("arg_matches: insertion ordinals follow the complete entry lifecycle") {
    arg_matches matches;
    matches.insert_arg(arg_id{"a"}, parsed<int>({"1"}));
    const std::optional<std::size_t> a0 = matches.insertion_ordinal_of("a");
    CLAPP_CHECK(a0.has_value());

    matches.insert_arg(arg_id{"b"}, parsed<int>({"2"}));
    const std::optional<std::size_t> b0 = matches.insertion_ordinal_of("b");
    CLAPP_CHECK(b0.has_value());
    CLAPP_CHECK(*a0 < *b0);

    // Overwriting or asking for the existing entry does not move its row.
    matches.insert_arg(arg_id{"a"}, parsed<int>({"3"}));
    static_cast<void>(matches.entry(arg_id{"a"}));
    CLAPP_CHECK(matches.insertion_ordinal_of("a") == a0);

    // Erasing and reopening does move it, exactly as clap's insertion-ordered map does.
    CLAPP_CHECK(matches.erase_arg("a"));
    CLAPP_CHECK(!matches.insertion_ordinal_of("a").has_value());
    matches.insert_arg(arg_id{"a"}, parsed<int>({"4"}));
    const std::optional<std::size_t> a1 = matches.insertion_ordinal_of("a");
    CLAPP_CHECK(a1.has_value());
    CLAPP_CHECK(*b0 < *a1);

    // A failed typed take is observationally atomic, metadata included.
    const std::expected<std::optional<long>, matches_error> wrong =
            matches.try_remove_one<long>("a");
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(matches.insertion_ordinal_of("a") == a1);
    CLAPP_CHECK(**matches.get_one<int>("a") == 4);

    const arg_matches copy = matches;
    CLAPP_CHECK(copy.insertion_ordinal_of("a") == a1);
    CLAPP_CHECK(copy.insertion_ordinal_of("b") == b0);

    arg_matches assigned;
    assigned = matches;
    CLAPP_CHECK(assigned.insertion_ordinal_of("a") == a1);
    CLAPP_CHECK(assigned.insertion_ordinal_of("b") == b0);

    arg_matches moved = std::move(assigned);
    CLAPP_CHECK(moved.insertion_ordinal_of("a") == a1);
    CLAPP_CHECK(moved.insertion_ordinal_of("b") == b0);

    arg_matches other;
    other.insert_arg(arg_id{"x"}, parsed<int>({"9"}));
    const std::optional<std::size_t> x0 = other.insertion_ordinal_of("x");
    swap(moved, other);
    CLAPP_CHECK(other.insertion_ordinal_of("a") == a1);
    CLAPP_CHECK(other.insertion_ordinal_of("b") == b0);
    CLAPP_CHECK(moved.insertion_ordinal_of("x") == x0);

    CLAPP_CHECK(other.remove_one<int>("a") == std::optional{4});
    CLAPP_CHECK(!other.insertion_ordinal_of("a").has_value());

    // Ordinals are parser metadata, not part of semantic equality.
    arg_matches reversed;
    reversed.insert_arg(arg_id{"discarded"}, parsed<int>({"0"}));
    CLAPP_CHECK(reversed.erase_arg("discarded"));
    reversed.insert_arg(arg_id{"b"}, parsed<int>({"2"}));
    arg_matches forward;
    forward.insert_arg(arg_id{"b"}, parsed<int>({"2"}));
    CLAPP_CHECK(reversed.insertion_ordinal_of("b") != forward.insertion_ordinal_of("b"));
    CLAPP_CHECK(reversed == forward);
}

CLAPP_TEST("arg_matches: equality covers the arguments and the subcommand subtree") {
    arg_matches lhs;
    lhs.insert_arg(arg_id{"n"}, parsed<int>({"1"}));
    arg_matches rhs = lhs;
    CLAPP_CHECK(lhs == rhs);

    rhs.insert_arg(arg_id{"m"}, parsed<int>({"2"}));
    CLAPP_CHECK(!(lhs == rhs));

    arg_matches with_child = lhs;
    arg_matches child;
    child.insert_arg(arg_id{"x"}, parsed<int>({"3"}));
    with_child.set_subcommand(std::string{"build"}, child);
    CLAPP_CHECK(!(with_child == lhs));

    arg_matches same_child = lhs;
    same_child.set_subcommand(std::string{"build"}, child);
    CLAPP_CHECK(with_child == same_child);

    arg_matches other_name = lhs;
    other_name.set_subcommand(std::string{"test"}, child);
    CLAPP_CHECK(!(with_child == other_name));

    // The valid-id list describes the command, not the matches, and is deliberately
    // not part of equality.
    arg_matches validated = lhs;
    validated.set_valid_ids({arg_id{"n"}});
    CLAPP_CHECK(validated == lhs);
}

CLAPP_TEST("arg_matches: move and swap leave both sides consistent") {
    arg_matches source;
    source.insert_arg(arg_id{"n"}, parsed<int>({"1"}));
    arg_matches child;
    child.insert_arg(arg_id{"x"}, parsed<int>({"2"}));
    source.set_subcommand(std::string{"build"}, std::move(child));

    const arg_matches expected = source;
    arg_matches moved          = std::move(source);
    CLAPP_CHECK(moved == expected);
    CLAPP_CHECK(moved.has_subcommand());

    arg_matches other;
    swap(moved, other);
    CLAPP_CHECK(other == expected);
    CLAPP_CHECK(moved.empty());
    CLAPP_CHECK(!moved.has_subcommand());

    moved = other;
    CLAPP_CHECK(moved == other);
    CLAPP_CHECK(moved.subcommand_matches("build") != other.subcommand_matches("build"));
}
