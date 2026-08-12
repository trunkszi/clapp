#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/util/any_value.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/**
 * Two enumerations with the same unqualified identifier, in different namespaces.
 * They exist only so the type-name assertions below have a collision to fail on;
 * nothing else in this file uses them.
 */
namespace vp_a {
    enum class mode { fast, slow };
}
namespace vp_b {
    enum class mode { fast, slow };
}

namespace {

    using clapp::os_str;
    using clapp::os_string;
    using clapp::parse_error;
    using clapp::parse_error_kind;
    using clapp::parser_vtable;
    using clapp::possible_value;
    using namespace std::string_view_literals;

    // -----------------------------------------------------------------------
    // Shorthands. Every one of these is usable in a constant expression, which is
    // the point: a regression fails the build rather than a test run.
    // -----------------------------------------------------------------------

    /** Whether \p text parses to exactly \p wanted. */
    template<class T>
    [[nodiscard]] constexpr bool yields(std::string_view text, T wanted, bool ignore_case = false) {
        const std::expected<T, parse_error> result =
                clapp::parse_value<T>(os_str{text}, ignore_case);
        return result.has_value() && result.value() == wanted;
    }

    /** Whether \p text is rejected with exactly \p kind. */
    template<class T>
    [[nodiscard]] constexpr bool
    rejects(std::string_view text, parse_error_kind kind, bool ignore_case = false) {
        const std::expected<T, parse_error> result =
                clapp::parse_value<T>(os_str{text}, ignore_case);
        return !result.has_value() && result.error().kind == kind;
    }

    /** The error \p text produces. Precondition: it really does fail. */
    template<class T>
    [[nodiscard]] constexpr parse_error failure(std::string_view text, bool ignore_case = false) {
        const std::expected<T, parse_error> result =
                clapp::parse_value<T>(os_str{text}, ignore_case);
        return result.has_value() ? parse_error{} : result.error();
    }

    /**
     * Whether \p got names a class or enum whose own identifier is \p identifier.
     *
     * Type names here come from `std::meta::display_string_of`, which
     * [meta.reflection.strings] leaves **implementation-defined**: for a type in an
     * unnamed namespace GCC 16.1.0 writes `{anonymous}::color` and clang-p2996
     * 0.0.0-p2996.5cc3eb319 writes plain `color`. Both are conforming, so pinning one
     * spelling pins a compiler, not a behavior. What every implementation must agree on
     * is that the name ends in the type's own identifier, so that is what is checked —
     * and at a `::` boundary, so `not_a_color` cannot pass for `color`. Callers pair
     * this with a distinctness check; together those are what clapp actually relies on.
     */
    [[nodiscard]] constexpr bool names_type(std::string_view got, std::string_view identifier) {
        if (!got.ends_with(identifier)) return false;
        if (got.size() == identifier.size()) return true;
        return got[got.size() - identifier.size() - 1U] == ':';
    }

    // -----------------------------------------------------------------------
    // The customization point itself
    // -----------------------------------------------------------------------

    struct not_a_value {};

    static_assert(!clapp::parsable<not_a_value>,
                  "an unspecialized type must simply not be parsable, not a hard error");
    static_assert(clapp::parsable<int>);
    static_assert(clapp::parsable<bool>);
    static_assert(clapp::parsable<char>);
    static_assert(clapp::parsable<double>);
    static_assert(clapp::parsable<std::string>);
    static_assert(clapp::parsable<os_string>);
    static_assert(clapp::parsable<std::filesystem::path>);

    // Every <cstdint> fixed-width alias resolves to one of the ten standard integer
    // types, so listing those covers the whole family.
    static_assert(clapp::parsable<std::int8_t>);
    static_assert(clapp::parsable<std::int16_t>);
    static_assert(clapp::parsable<std::int32_t>);
    static_assert(clapp::parsable<std::int64_t>);
    static_assert(clapp::parsable<std::uint8_t>);
    static_assert(clapp::parsable<std::uint16_t>);
    static_assert(clapp::parsable<std::uint32_t>);
    static_assert(clapp::parsable<std::uint64_t>);
    static_assert(clapp::parsable<std::size_t>);
    static_assert(clapp::parsable<std::ptrdiff_t>);
    static_assert(clapp::parsable<std::int_fast16_t>);
    static_assert(clapp::parsable<std::uintmax_t>);

    // Only enumerations fold case.
    static_assert(!clapp::case_insensitively_parsable<int>);
    static_assert(!clapp::case_insensitively_parsable<bool>);
    static_assert(!clapp::case_insensitively_parsable<std::string>);

    // Every builtin declares possible_values(); only bool and the enumerations return
    // anything from it. "Does this type enumerate" is the emptiness question, not the
    // concept.
    static_assert(clapp::enumerable_parser<bool>);
    static_assert(clapp::enumerable_parser<int>);
    static_assert(!clapp::enumerable_parser<not_a_value>);
    static_assert(clapp::possible_values_of<int>().empty());
    static_assert(clapp::possible_values_of<double>().empty());
    static_assert(clapp::possible_values_of<std::string>().empty());
    static_assert(clapp::possible_values_of<os_string>().empty());
    static_assert(clapp::possible_values_of<char>().empty());
    static_assert(clapp::possible_values_of<bool>().size() == 12);

    static_assert(clapp::erasable_parsable<int>);
    static_assert(clapp::erasable_parsable<std::string>);
    static_assert(!clapp::erasable_parsable<not_a_value>);

    // -----------------------------------------------------------------------
    // parse_error
    // -----------------------------------------------------------------------

    static_assert(std::is_aggregate_v<parse_error>);
    static_assert(std::is_trivially_copyable_v<parse_error>);

    static_assert(parse_error{}.message() == "value is not one of the accepted values");
    static_assert(parse_error{.kind = parse_error_kind::out_of_range}.message() ==
                  "value is out of range for its type");
    static_assert(parse_error{.kind = parse_error_kind::empty_value, .reason = "custom"}
                          .message() == "custom");
    static_assert(clapp::describe(parse_error_kind::invalid_utf8) == "value is not valid UTF-8");
    static_assert(clapp::describe(parse_error_kind::empty_value) == "value is empty");
    static_assert(clapp::describe(parse_error_kind::invalid_digit) == "value is not a number");
    static_assert(!parse_error{}.has_possible_values());
    static_assert(parse_error{} == parse_error{});
    static_assert(!(parse_error{} == parse_error{.kind = parse_error_kind::empty_value}));

    /**
     * Whether \p text is rejected by \p T with parse_error_kind::empty_value.
     *
     * clap raises `Error::empty_value` from exactly two value parsers —
     * `PathBufValueParser` and `NonEmptyStringValueParser` — and clapp has a builtin for
     * only the first. Every *other* builtin handed "" reports an ordinary conversion
     * failure, because a value WAS supplied. Reading the kind as "the input was empty"
     * instead of "this type has no empty member" made `--port ""` say
     * `a value is required for '--port <port>' but none was supplied`, with clap's other
     * kind and clap's other sentence, on an ordinary `--port "$MAYBE_UNSET"`.
     *
     * Asserted as a closed list rather than one case at a time so that a NEW builtin
     * reaching for the kind has to come here and justify it. `std::filesystem::path` is
     * not on it because none of its constructors are `constexpr`; its case is the
     * runtime one further down, which asserts the *positive* side — this trap has both
     * halves, exactly as CLAUDE.md trap 10 does.
     */
    template<class T>
    [[nodiscard]] constexpr bool claims_empty_is_absent(std::string_view text) {
        const std::expected<T, parse_error> result = clapp::parse_value<T>(os_str{text});
        return !result.has_value() && result.error().kind == parse_error_kind::empty_value;
    }

    static_assert(!claims_empty_is_absent<int>(""sv));
    static_assert(!claims_empty_is_absent<unsigned>(""sv));
    static_assert(!claims_empty_is_absent<std::int8_t>(""sv));
    static_assert(!claims_empty_is_absent<std::uint64_t>(""sv));
    static_assert(!claims_empty_is_absent<bool>(""sv));
    static_assert(!claims_empty_is_absent<char>(""sv));
    static_assert(!claims_empty_is_absent<vp_a::mode>(""sv));
    // The two builtins that accept "" outright cannot claim it either.
    static_assert(!claims_empty_is_absent<std::string>(""sv));
    static_assert(!claims_empty_is_absent<os_string>(""sv));

    // -----------------------------------------------------------------------
    // Integers — the plain cases
    // -----------------------------------------------------------------------

    static_assert(yields<int>("42"sv, 42));
    static_assert(yields<int>("-42"sv, -42));
    static_assert(yields<int>("0"sv, 0));
    static_assert(yields<int>("-0"sv, 0));

    // Rust's FromStr accepts a leading '+', std::from_chars does not; clapp follows
    // Rust, because that is what clap accepts.
    static_assert(yields<int>("+42"sv, 42));
    static_assert(yields<unsigned>("+7"sv, 7u));
    static_assert(rejects<int>("+"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("+-1"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("++1"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("-+1"sv, parse_error_kind::invalid_digit));

    // Leading zeros are digits, not an octal prefix.
    static_assert(yields<int>("007"sv, 7));
    static_assert(yields<int>("0000000000000000000000009"sv, 9));
    static_assert(yields<int>("-007"sv, -7));

    // No base prefixes: "0" parses and "x2A" is trailing junk.
    static_assert(rejects<int>("0x2A"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("0b1"sv, parse_error_kind::invalid_digit));

    // Whitespace is never skipped, on either end.
    static_assert(rejects<int>(" 42"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("42 "sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("\t42"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("4 2"sv, parse_error_kind::invalid_digit));

    // An empty value is NOT parse_error_kind::empty_value. That kind is a *policy*
    // ("this parser treats empty as absent", clap's `Error::empty_value`), and only
    // value_parser<std::filesystem::path> holds it; a number handed "" simply failed to
    // convert, which is Rust's `IntErrorKind::Empty` and clap's `ValueValidation`.
    // Measured on clap 4.x: `--port ""` renders
    //   invalid value '' for '--port <port>': cannot parse integer from empty string
    // where reading this as empty_value renders clap's *other* sentence,
    //   a value is required for '--port <port>' but none was supplied
    // — which is a lie about an ordinary `--port "$MAYBE_UNSET"`. The end-to-end
    // rendering is pinned in tests/units/parser/parse_test.cpp; this is the source.
    static_assert(rejects<int>(""sv, parse_error_kind::invalid_digit));
    static_assert(failure<int>(""sv).reason == "cannot parse integer from empty string");
    static_assert(rejects<int>("abc"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("42abc"sv, parse_error_kind::invalid_digit));
    static_assert(rejects<int>("-"sv, parse_error_kind::invalid_digit));

    // An embedded NUL is trailing junk, not a terminator: os_str keeps the bytes and
    // from_chars stops at the NUL, so the value is not fully consumed.
    static_assert(rejects<int>(std::string_view{"4\0 2", 4}, parse_error_kind::invalid_digit));
    static_assert(rejects<int>(std::string_view{"42\0", 3}, parse_error_kind::invalid_digit));

    // Non-UTF-8 bytes are reported as an encoding failure, with the offset kept.
    static_assert(rejects<int>("\xFF"sv, parse_error_kind::invalid_utf8));
    static_assert(rejects<int>("1\xFF"sv, parse_error_kind::invalid_utf8));
    static_assert(failure<int>("1\xFF"sv).encoding.valid_up_to == 1);
    static_assert(failure<int>("1\xFF"sv).encoding.kind == clapp::encoding_error::invalid_lead);

    // The diagnostic names the type and the accepted interval.
    static_assert(failure<int>("abc"sv).type_name == "int");
    static_assert(failure<int>("abc"sv).reason == "invalid digit found in string");
    static_assert(failure<std::uint16_t>("99999"sv).domain == "0..=65535");
    static_assert(failure<std::int8_t>("999"sv).domain == "-128..=127");
    static_assert(failure<std::uint64_t>("x"sv).domain == "0..=18446744073709551615");
    static_assert(failure<std::int64_t>("x"sv).domain ==
                  "-9223372036854775808..=9223372036854775807");
    static_assert(failure<int>("abc"sv).possible.empty());

    // A range failure carries NO sentence of its own, and that is load-bearing:
    // clapp::detail::value_reason() prefers a written reason over the interval, so
    // `std::from_chars`'s "number too large to fit in target type" here would outrank
    // the bounds and put back the message this pair exists to replace. The interval is
    // the only thing that tells the user what would have fit.
    static_assert(failure<std::uint16_t>("99999"sv).reason.empty());
    static_assert(failure<std::int8_t>("128"sv).reason.empty());
    static_assert(failure<std::uint16_t>("99999"sv).message() ==
                  "value is out of range for its type");
    // The failures that are NOT about range still say what they are.
    static_assert(!failure<std::uint16_t>("abc"sv).reason.empty());
    static_assert(!failure<std::uint16_t>(""sv).reason.empty());

    // -----------------------------------------------------------------------
    // Integers — every fixed-width boundary, and one step past each
    // -----------------------------------------------------------------------

    static_assert(yields<std::int8_t>("-128"sv, std::int8_t{-128}));
    static_assert(yields<std::int8_t>("127"sv, std::int8_t{127}));
    static_assert(rejects<std::int8_t>("-129"sv, parse_error_kind::out_of_range));
    static_assert(rejects<std::int8_t>("128"sv, parse_error_kind::out_of_range));

    static_assert(yields<std::uint8_t>("0"sv, std::uint8_t{0}));
    static_assert(yields<std::uint8_t>("255"sv, std::uint8_t{255}));
    static_assert(rejects<std::uint8_t>("256"sv, parse_error_kind::out_of_range));
    static_assert(rejects<std::uint8_t>("-1"sv, parse_error_kind::invalid_digit));

    static_assert(yields<std::int16_t>("-32768"sv, std::int16_t{-32768}));
    static_assert(yields<std::int16_t>("32767"sv, std::int16_t{32767}));
    static_assert(rejects<std::int16_t>("-32769"sv, parse_error_kind::out_of_range));
    static_assert(rejects<std::int16_t>("32768"sv, parse_error_kind::out_of_range));

    static_assert(yields<std::uint16_t>("0"sv, std::uint16_t{0}));
    static_assert(yields<std::uint16_t>("65535"sv, std::uint16_t{65535}));
    static_assert(rejects<std::uint16_t>("65536"sv, parse_error_kind::out_of_range));
    // The headline case: --port 99999 must not wrap to 34463.
    static_assert(rejects<std::uint16_t>("99999"sv, parse_error_kind::out_of_range));
    static_assert(!yields<std::uint16_t>("99999"sv, std::uint16_t{34463}));
    static_assert(rejects<std::uint16_t>("-1"sv, parse_error_kind::invalid_digit));

    static_assert(yields<std::int32_t>("-2147483648"sv, std::numeric_limits<std::int32_t>::min()));
    static_assert(yields<std::int32_t>("2147483647"sv, std::numeric_limits<std::int32_t>::max()));
    static_assert(rejects<std::int32_t>("-2147483649"sv, parse_error_kind::out_of_range));
    static_assert(rejects<std::int32_t>("2147483648"sv, parse_error_kind::out_of_range));

    static_assert(yields<std::uint32_t>("0"sv, std::uint32_t{0}));
    static_assert(yields<std::uint32_t>("4294967295"sv, std::numeric_limits<std::uint32_t>::max()));
    static_assert(rejects<std::uint32_t>("4294967296"sv, parse_error_kind::out_of_range));

    static_assert(yields<std::int64_t>("-9223372036854775808"sv,
                                       std::numeric_limits<std::int64_t>::min()));
    static_assert(yields<std::int64_t>("9223372036854775807"sv,
                                       std::numeric_limits<std::int64_t>::max()));
    static_assert(rejects<std::int64_t>("-9223372036854775809"sv, parse_error_kind::out_of_range));
    static_assert(rejects<std::int64_t>("9223372036854775808"sv, parse_error_kind::out_of_range));

    static_assert(yields<std::uint64_t>("0"sv, std::uint64_t{0}));
    static_assert(yields<std::uint64_t>("18446744073709551615"sv,
                                        std::numeric_limits<std::uint64_t>::max()));
    static_assert(rejects<std::uint64_t>("18446744073709551616"sv, parse_error_kind::out_of_range));
    // clap parses into i64 first and would call this one an invalid digit; clapp asks
    // from_chars for the target type directly, so the full u64 range is reachable
    // without opting into a second parser. Recorded as a deliberate divergence.
    static_assert(yields<std::uint64_t>("9223372036854775808"sv,
                                        std::uint64_t{9223372036854775808ULL}));

    static_assert(rejects<std::uint32_t>("99999999999999999999999999"sv,
                                         parse_error_kind::out_of_range));

    // -----------------------------------------------------------------------
    // bool — clap's twelve spellings, folded
    // -----------------------------------------------------------------------

    static_assert(yields<bool>("y"sv, true));
    static_assert(yields<bool>("yes"sv, true));
    static_assert(yields<bool>("t"sv, true));
    static_assert(yields<bool>("true"sv, true));
    static_assert(yields<bool>("on"sv, true));
    static_assert(yields<bool>("1"sv, true));
    static_assert(yields<bool>("n"sv, false));
    static_assert(yields<bool>("no"sv, false));
    static_assert(yields<bool>("f"sv, false));
    static_assert(yields<bool>("false"sv, false));
    static_assert(yields<bool>("off"sv, false));
    static_assert(yields<bool>("0"sv, false));

    // Case is folded unconditionally — this is clap's BoolishValueParser, not its
    // case-sensitive default bool parser.
    static_assert(yields<bool>("TRUE"sv, true));
    static_assert(yields<bool>("True"sv, true));
    static_assert(yields<bool>("Yes"sv, true));
    static_assert(yields<bool>("oN"sv, true));
    static_assert(yields<bool>("Y"sv, true));
    static_assert(yields<bool>("FALSE"sv, false));
    static_assert(yields<bool>("oFF"sv, false));
    static_assert(yields<bool>("No"sv, false));
    static_assert(yields<bool>("N"sv, false));

    static_assert(rejects<bool>("maybe"sv, parse_error_kind::invalid_value));
    static_assert(rejects<bool>("2"sv, parse_error_kind::invalid_value));
    static_assert(rejects<bool>("00"sv, parse_error_kind::invalid_value));
    static_assert(rejects<bool>("truthy"sv, parse_error_kind::invalid_value));
    // clap's BoolValueParser ends in a single `Error::invalid_value` call for every
    // rejection, empty included, and the accepted-value list is what carries it there.
    // The rendering is unchanged either way — an invalid_value whose bad value is empty
    // renders as `a value is required … but none was supplied [possible values: true,
    // false]`, which is exactly clap's — but the kind now says why.
    static_assert(rejects<bool>(""sv, parse_error_kind::invalid_value));
    static_assert(rejects<bool>("\xFF"sv, parse_error_kind::invalid_utf8));
    static_assert(rejects<bool>(std::string_view{"true\0", 5}, parse_error_kind::invalid_value));

    // The error carries the whole list, so "did you mean" has something to work with,
    // and the encoding failure still carries the list too.
    static_assert(failure<bool>("maybe"sv).possible.size() == 12);
    static_assert(failure<bool>("\xFF"sv).possible.size() == 12);
    static_assert(failure<bool>("maybe"sv).reason == "value was not a boolean");
    static_assert(failure<bool>("maybe"sv).type_name == "bool");

    // Only "true" and "false" are visible; the other ten are matched but hidden, as in
    // clap's BoolishValueParser::possible_values.
    static_assert(clapp::possible_values_of<bool>()[3].get_name() == "true");
    static_assert(clapp::possible_values_of<bool>()[9].get_name() == "false");
    static_assert(!clapp::possible_values_of<bool>()[3].is_hide_set());
    static_assert(!clapp::possible_values_of<bool>()[9].is_hide_set());
    static_assert(clapp::possible_values_of<bool>()[0].is_hide_set());
    static_assert(clapp::possible_values_of<bool>()[11].is_hide_set());
    static_assert(std::ranges::equal(failure<bool>("maybe"sv).visible_values(),
                                     std::array{"true"sv, "false"sv}));

    // -----------------------------------------------------------------------
    // char — one byte, byte-oriented on purpose
    // -----------------------------------------------------------------------

    static_assert(yields<char>("a"sv, 'a'));
    static_assert(yields<char>(","sv, ','));
    static_assert(yields<char>(" "sv, ' '));
    static_assert(yields<char>("\xFF"sv, '\xFF'));
    static_assert(yields<char>(std::string_view{"\0", 1}, '\0'));
    // Empty is an ordinary conversion failure here too, with Rust's own `ParseCharError`
    // wording, which clap forwards verbatim: `invalid value '' for '--sep <sep>': cannot
    // parse char from empty string` (measured, clap 4.x). No accepted-value list, so it
    // routes to clapp::error_kind::value_validation.
    static_assert(rejects<char>(""sv, parse_error_kind::invalid_value));
    static_assert(failure<char>(""sv).reason == "cannot parse char from empty string");
    static_assert(failure<char>(""sv).possible.empty());
    static_assert(rejects<char>("ab"sv, parse_error_kind::invalid_value));
    // A two-byte UTF-8 sequence is two bytes, and a char cannot hold it.
    static_assert(rejects<char>("é"sv, parse_error_kind::invalid_value));
    static_assert(failure<char>("ab"sv).reason == "expected exactly one byte");

    // -----------------------------------------------------------------------
    // std::string — valid UTF-8 required, emptiness allowed
    // -----------------------------------------------------------------------

    static_assert(yields<std::string>("hello"sv, std::string("hello")));
    static_assert(yields<std::string>(""sv, std::string()));
    static_assert(yields<std::string>("é中"sv, std::string("é中")));
    static_assert(yields<std::string>(std::string_view{"a\0b", 3}, std::string("a\0b", 3)));

    static_assert(rejects<std::string>("\xFF"sv, parse_error_kind::invalid_utf8));
    static_assert(rejects<std::string>("caf\xe9"sv, parse_error_kind::invalid_utf8));
    // Strict UTF-8, so an encoded unpaired surrogate is rejected as well.
    static_assert(rejects<std::string>("\xED\xA0\x80"sv, parse_error_kind::invalid_utf8));
    static_assert(failure<std::string>("caf\xe9"sv).encoding.valid_up_to == 3);
    static_assert(failure<std::string>("\xED\xA0\x80"sv).encoding.kind ==
                  clapp::encoding_error::surrogate);

    // -----------------------------------------------------------------------
    // clapp::os_string — the escape hatch, and the only parser that cannot fail
    // -----------------------------------------------------------------------

    static_assert(yields<os_string>("hello"sv, os_string("hello")));
    static_assert(yields<os_string>(""sv, os_string()));
    static_assert(yields<os_string>("\xFF\xFE"sv, os_string("\xFF\xFE"sv)));
    static_assert(yields<os_string>(std::string_view{"a\0b", 3},
                                    os_string(std::string_view{"a\0b", 3})));
    static_assert(clapp::parse_value<os_string>(os_str{"\xFF"sv}).has_value());

    // -----------------------------------------------------------------------
    // Enumerations — no opt-in, names derived by reflection
    // -----------------------------------------------------------------------

    /** Plain enumeration; `auto_` exercises the keyword-avoidance underscore. */
    enum class color { auto_, always, never };

    /**
     * Every naming rule at once: a keyword clash, a multi-word identifier, a
     * `[[= clapp::value{.name}]]` override, and a hidden variant.
     */
    enum class shell {
        auto_,
        bash,
        zsh,
        power_shell,
        fish[[= clapp::value{.name = "fishy", .help = "the friendly interactive shell"}]],
        legacy_csh[[= clapp::value{.hide = true}]],
    };

    /** Unscoped enumerations work identically; nothing here depends on scoping. */
    enum verbosity { quiet_level, normal_level, loud_level };

    /** Explicit enumerator values must survive the reflection round trip. */
    enum class signal_number : int { hangup = 1, interrupt = 2, kill_ = 9 };

    static_assert(clapp::parsable<color>);
    static_assert(clapp::parsable<shell>);
    static_assert(clapp::parsable<verbosity>);
    static_assert(clapp::parsable<signal_number>);
    static_assert(clapp::case_insensitively_parsable<color>);
    static_assert(clapp::enumerable_parser<color>);

    // Derived spellings. `auto_` -> "auto" is the trailing-underscore rule;
    // `power_shell` -> "power-shell" is the kebab rule clap_derive uses.
    static_assert(clapp::possible_values_of<color>().size() == 3);
    static_assert(clapp::possible_values_of<color>()[0].get_name() == "auto");
    static_assert(clapp::possible_values_of<color>()[1].get_name() == "always");
    static_assert(clapp::possible_values_of<color>()[2].get_name() == "never");

    static_assert(clapp::possible_values_of<shell>().size() == 6);
    static_assert(clapp::possible_values_of<shell>()[0].get_name() == "auto");
    static_assert(clapp::possible_values_of<shell>()[3].get_name() == "power-shell");
    static_assert(clapp::possible_values_of<shell>()[4].get_name() == "fishy");
    static_assert(clapp::possible_values_of<shell>()[4].get_help() ==
                  std::optional<std::string_view>{"the friendly interactive shell"});
    static_assert(clapp::possible_values_of<shell>()[5].get_name() == "legacy-csh");
    static_assert(clapp::possible_values_of<shell>()[5].is_hide_set());
    static_assert(!clapp::possible_values_of<shell>()[4].is_hide_set());

    static_assert(clapp::possible_values_of<verbosity>()[0].get_name() == "quiet-level");
    static_assert(clapp::possible_values_of<verbosity>()[2].get_name() == "loud-level");
    static_assert(clapp::possible_values_of<signal_number>()[2].get_name() == "kill");

    // Matching.
    static_assert(yields<color>("auto"sv, color::auto_));
    static_assert(yields<color>("always"sv, color::always));
    static_assert(yields<color>("never"sv, color::never));
    static_assert(yields<verbosity>("normal-level"sv, normal_level));
    static_assert(yields<signal_number>("kill"sv, signal_number::kill_));
    static_assert(yields<signal_number>("hangup"sv, signal_number::hangup));

    // The override replaces the derived name rather than adding to it.
    static_assert(yields<shell>("fishy"sv, shell::fish));
    static_assert(rejects<shell>("fish"sv, parse_error_kind::invalid_value));

    // A hidden value is still selectable — that is what lets a renamed value keep
    // answering to its old spelling.
    static_assert(yields<shell>("legacy-csh"sv, shell::legacy_csh));

    // ignore_case is off by default and honoured when asked for.
    static_assert(rejects<color>("AUTO"sv, parse_error_kind::invalid_value));
    static_assert(yields<color>("AUTO"sv, color::auto_, /*ignore_case=*/true));
    static_assert(yields<color>("AlWaYs"sv, color::always, /*ignore_case=*/true));
    static_assert(yields<shell>("POWER-SHELL"sv, shell::power_shell, /*ignore_case=*/true));
    // Case folding is ASCII-only and must not turn a non-match into a match.
    static_assert(rejects<color>("aut"sv, parse_error_kind::invalid_value, /*ignore_case=*/true));

    // Both spellings of the entry point agree.
    static_assert(clapp::value_parser<color>::parse(os_str{"auto"sv}).value() == color::auto_);
    static_assert(clapp::value_parser<color>::parse(os_str{"AUTO"sv}, true).value() ==
                  color::auto_);

    // Rejections carry the list, including the hidden entries, so the renderer can
    // both suggest and filter.
    static_assert(rejects<color>(""sv, parse_error_kind::invalid_value));
    static_assert(rejects<color>("magenta"sv, parse_error_kind::invalid_value));
    static_assert(rejects<color>("\xFF"sv, parse_error_kind::invalid_utf8));
    static_assert(failure<color>("magenta"sv).possible.size() == 3);
    static_assert(failure<color>("\xFF"sv).possible.size() == 3);
    static_assert(failure<shell>("nope"sv).possible.size() == 6);
    static_assert(std::ranges::equal(
            failure<shell>("nope"sv).visible_values(),
            std::array{"auto"sv, "bash"sv, "zsh"sv, "power-shell"sv, "fishy"sv}));
    // The rejection names the enum it could not parse — and names it distinctly, which
    // is the property any_id's identity rests on. See names_type() for why the exact
    // spelling (the enclosing scope is implementation-defined) is not asserted.
    static_assert(names_type(failure<color>("magenta"sv).type_name, "color"));
    static_assert(failure<color>("magenta"sv).type_name != failure<shell>("nope"sv).type_name);
    // `color` vs `shell` differs already in the last identifier component, so it would
    // pass even under an unqualified namer. The case that actually bites is two types
    // whose identifiers *match* — parse_error::operator== compares type_name, so a
    // collision makes two errors about different types compare equal, and the erased
    // parser tables become indistinguishable to arg_spec::operator==. Measured on
    // clang-p2996 0.0.0-p2996.5cc3eb319, display_string_of returned "Mode" for both of
    // these; the qualified namer in any_value.hpp is what separates them.
    static_assert(failure<vp_a::mode>("x"sv).type_name != failure<vp_b::mode>("x"sv).type_name);
    static_assert(clapp::parser_for<vp_a::mode>()->type_name() !=
                  clapp::parser_for<vp_b::mode>()->type_name());
    static_assert(names_type(clapp::parser_for<vp_a::mode>()->type_name(), "mode"));

    // The one enum rule that is a compile error rather than a runtime one: two
    // enumerators deriving the same spelling. It cannot be asserted here without
    // failing the build on purpose — clapp::detail::enum_values_of() throws during
    // constant evaluation, and the message names the fix.

    // -----------------------------------------------------------------------
    // parser_vtable — structural, constant-initializable, and in .rodata
    // -----------------------------------------------------------------------

    static_assert(std::is_aggregate_v<parser_vtable>);
    static_assert(std::is_trivially_copyable_v<parser_vtable>);

    // By value and through a pointer: both must be usable as non-type template
    // arguments, which is the operational definition of "structural" and the
    // precondition for reaching static storage.
    template<parser_vtable>
    struct by_value_probe {};
    template<const parser_vtable*>
    struct by_pointer_probe {};

    inline constexpr parser_vtable int_table_copy = *clapp::parser_for<int>();
    using value_probe                             = by_value_probe<int_table_copy>;
    using pointer_probe                           = by_pointer_probe<clapp::parser_for<color>()>;

    // The property ADR-0004 is actually claiming: a table pointer survives
    // std::define_static_array, so an array of them can live in a command_spec.
    inline constexpr std::span<const parser_vtable* const> promoted_tables =
            std::define_static_array(
                    std::array<const parser_vtable*, 3>{clapp::parser_for<int>(),
                                                        clapp::parser_for<color>(),
                                                        clapp::parser_for<std::string>()});
    static_assert(promoted_tables.size() == 3);
    static_assert(promoted_tables[1]->possible_values().size() == 3);

    // And a static constexpr instance really is a constant, callable at compile time
    // for everything except the erased parse().
    static constexpr const parser_vtable* port_table = clapp::parser_for<std::uint16_t>();
    // `std::uint16_t` resolves to `unsigned short`, which the two implementations spell
    // differently — GCC 16.1.0 orders the words `short unsigned int`, clang-p2996
    // 0.0.0-p2996.5cc3eb319 writes `unsigned short`. Both name the same type and both
    // are conforming (display_string_of is implementation-defined), so the accepted
    // spellings are listed rather than reduced: a third, unrelated string still fails.
    // names_type() does not apply — the two spellings share no common suffix.
    static_assert(port_table->type_name() == "short unsigned int" ||
                  port_table->type_name() == "unsigned short");
    static_assert(port_table->type_name() != clapp::parser_for<std::int16_t>()->type_name());
    static_assert(port_table->possible_values().empty());

    static constexpr const parser_vtable* color_table = clapp::parser_for<color>();
    static_assert(color_table->possible_values().size() == 3);
    static_assert(color_table->possible_values()[0].get_name() == "auto");

    static_assert(clapp::parser_for<double>()->type_name() == "double");
    static_assert(clapp::parser_for<std::filesystem::path>()->possible_values().empty());

    // Table *identity* — stable across calls, distinct across types — is checked at
    // runtime, not here. GCC 16.1.0 refuses to compare two pointers during constant
    // evaluation under -fsanitize=undefined, so a static_assert on it would break the
    // ubsan preset. See the warning in clapp::detail beside the structural probes.

    // -----------------------------------------------------------------------
    // Fixtures for the runtime half
    // -----------------------------------------------------------------------

    template<class T>
    [[nodiscard]] bool run_yields(std::string_view text, T wanted) {
        const std::expected<T, parse_error> result = clapp::parse_value<T>(os_str{text});
        return result.has_value() && result.value() == wanted;
    }

    template<class T>
    [[nodiscard]] bool run_rejects(std::string_view text, parse_error_kind kind) {
        const std::expected<T, parse_error> result = clapp::parse_value<T>(os_str{text});
        return !result.has_value() && result.error().kind == kind;
    }

}  // namespace

// ---------------------------------------------------------------------------
// Floating point — runtime only: std::from_chars is not constexpr for it
// ---------------------------------------------------------------------------

CLAPP_TEST("value_parser<double>: decimal and scientific spellings") {
    CLAPP_CHECK(run_yields<double>("1.5"sv, 1.5));
    CLAPP_CHECK(run_yields<double>("-2.25"sv, -2.25));
    CLAPP_CHECK(run_yields<double>("0"sv, 0.0));
    CLAPP_CHECK(run_yields<double>("1e3"sv, 1000.0));
    CLAPP_CHECK(run_yields<double>("1E3"sv, 1000.0));
    CLAPP_CHECK(run_yields<double>("1e-3"sv, 0.001));
    CLAPP_CHECK(run_yields<double>(".5"sv, 0.5));
    CLAPP_CHECK(run_yields<double>("1."sv, 1.0));
    CLAPP_CHECK(run_yields<float>("1.5"sv, 1.5F));
    CLAPP_CHECK(run_yields<long double>("1.5"sv, 1.5L));
}

CLAPP_TEST("value_parser<double>: a leading + is accepted, as in Rust's FromStr") {
    CLAPP_CHECK(run_yields<double>("+3.5"sv, 3.5));
    CLAPP_CHECK(run_yields<double>("+1e2"sv, 100.0));
    CLAPP_CHECK(run_rejects<double>("+"sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>("++1"sv, parse_error_kind::invalid_digit));
}

CLAPP_TEST("value_parser<double>: infinities and NaN are accepted, as clap accepts them") {
    const std::expected<double, parse_error> plus_inf = clapp::parse_value<double>(os_str{"inf"sv});
    CLAPP_CHECK(plus_inf.has_value() && std::isinf(plus_inf.value()) && plus_inf.value() > 0);

    const std::expected<double, parse_error> upper = clapp::parse_value<double>(os_str{"INF"sv});
    CLAPP_CHECK(upper.has_value() && std::isinf(upper.value()));

    const std::expected<double, parse_error> spelled =
            clapp::parse_value<double>(os_str{"Infinity"sv});
    CLAPP_CHECK(spelled.has_value() && std::isinf(spelled.value()));

    const std::expected<double, parse_error> minus = clapp::parse_value<double>(os_str{"-inf"sv});
    CLAPP_CHECK(minus.has_value() && std::isinf(minus.value()) && minus.value() < 0);

    const std::expected<double, parse_error> quiet = clapp::parse_value<double>(os_str{"NaN"sv});
    CLAPP_CHECK(quiet.has_value() && std::isnan(quiet.value()));
}

CLAPP_TEST("value_parser<double>: the C-only nan(payload) spelling is rejected") {
    // std::from_chars honours it; Rust's FromStr does not, and clapp follows Rust so
    // that "--ratio nan(0x1f)" is a typo rather than a silently accepted NaN.
    CLAPP_CHECK(run_rejects<double>("nan(1)"sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>("nan()"sv, parse_error_kind::invalid_digit));
}

CLAPP_TEST("value_parser<double>: hex float literals are not a spelling clapp accepts") {
    CLAPP_CHECK(run_rejects<double>("0x1p3"sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>("0x10"sv, parse_error_kind::invalid_digit));
}

CLAPP_TEST("value_parser<double>: junk, whitespace and empty input are all errors") {
    // Empty is invalid_digit, not empty_value, for the reason spelled at the integer
    // cases above: clap reports `--ratio ""` as ValueValidation / `cannot parse float
    // from empty string`, never as "none was supplied".
    CLAPP_CHECK(run_rejects<double>(""sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(clapp::parse_value<double>(os_str{""sv}).error().reason ==
                "cannot parse float from empty string");
    CLAPP_CHECK(run_rejects<double>("abc"sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>("1.5x"sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>(" 1.5"sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>("1.5 "sv, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>(std::string_view{"1.5\0", 4}, parse_error_kind::invalid_digit));
    CLAPP_CHECK(run_rejects<double>("\xFF"sv, parse_error_kind::invalid_utf8));
}

CLAPP_TEST("value_parser<double>: overflow is an error, not a silent infinity") {
    // Rust saturates 1e400 to inf; clapp reports it. Deliberate divergence — a CLI
    // that turns a mistyped --timeout into an infinite one is worse than one that
    // says the number does not fit.
    CLAPP_CHECK(run_rejects<double>("1e400"sv, parse_error_kind::out_of_range));
    CLAPP_CHECK(run_rejects<float>("1e40"sv, parse_error_kind::out_of_range));
    const parse_error error = clapp::parse_value<double>(os_str{"1e400"sv}).error();
    CLAPP_CHECK(error.type_name == "double");
    CLAPP_CHECK(error.message() == "number is outside the representable range");
}

// ---------------------------------------------------------------------------
// std::filesystem::path — runtime only: its constructors are not constexpr
// ---------------------------------------------------------------------------

CLAPP_TEST("value_parser<path>: bytes make the round trip, empty is rejected") {
    CLAPP_CHECK(run_yields<std::filesystem::path>("/tmp/report.txt"sv,
                                                  std::filesystem::path("/tmp/report.txt")));
    CLAPP_CHECK(run_yields<std::filesystem::path>("relative/dir"sv,
                                                  std::filesystem::path("relative/dir")));
    CLAPP_CHECK(run_rejects<std::filesystem::path>(""sv, parse_error_kind::empty_value));
    CLAPP_CHECK(clapp::parse_value<std::filesystem::path>(os_str{""sv}).error().reason ==
                "path cannot be empty");
}

CLAPP_TEST("value_parser<path>: a path is not required to be valid UTF-8") {
    // A POSIX filename is a byte string; rejecting one for being Latin-1 would make
    // clapp less capable than cp.
    const std::expected<std::filesystem::path, parse_error> parsed =
            clapp::parse_value<std::filesystem::path>(os_str{"caf\xe9.txt"sv});
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(parsed.value().native() == clapp::os_str{"caf\xe9.txt"sv}.to_native());
}

// ---------------------------------------------------------------------------
// The erased path — runtime only: type erasure needs a static_cast from void*
// ---------------------------------------------------------------------------

CLAPP_TEST("parser_vtable: one table per type, at one stable address") {
    // The half of the structural contract that cannot be a static_assert: GCC 16.1.0
    // rejects every pointer comparison in a constant expression under
    // -fsanitize=undefined, so asserting this at compile time would break the ubsan
    // preset for every translation unit that includes the header.
    CLAPP_CHECK(clapp::parser_for<int>() == clapp::parser_for<int>());
    CLAPP_CHECK(clapp::parser_for<int>() != clapp::parser_for<unsigned>());
    CLAPP_CHECK(clapp::parser_for<int>() != clapp::parser_for<long>());
    CLAPP_CHECK(promoted_tables[1] == clapp::parser_for<color>());
    CLAPP_CHECK(promoted_tables[0] == clapp::parser_for<int>());
}

CLAPP_TEST("parser_vtable: dispatch produces an any_value of the right type") {
    const parser_vtable* table                                = clapp::parser_for<int>();
    const std::expected<clapp::any_value, parse_error> parsed = table->parse(os_str{"42"sv}, false);
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(parsed->holds<int>());
    CLAPP_CHECK(parsed->get<int>() == 42);
    CLAPP_CHECK(parsed->type() == clapp::any_id::of<int>());
    CLAPP_CHECK(parsed->try_get<long>() == nullptr);
}

CLAPP_TEST("parser_vtable: a failed parse comes back as the same parse_error") {
    const parser_vtable* table = clapp::parser_for<std::uint16_t>();
    const std::expected<clapp::any_value, parse_error> parsed =
            table->parse(os_str{"99999"sv}, false);
    CLAPP_CHECK(!parsed.has_value());
    CLAPP_CHECK(parsed.error().kind == parse_error_kind::out_of_range);
    CLAPP_CHECK(parsed.error().domain == "0..=65535");
    CLAPP_CHECK(parsed.error().input == os_str{"99999"sv});
}

CLAPP_TEST("parser_vtable: ignore_case reaches the parser that wants it") {
    const parser_vtable* table = clapp::parser_for<color>();
    CLAPP_CHECK(!table->parse(os_str{"AUTO"sv}, false).has_value());

    const std::expected<clapp::any_value, parse_error> folded =
            table->parse(os_str{"AUTO"sv}, true);
    CLAPP_CHECK(folded.has_value());
    CLAPP_CHECK(folded->get<color>() == color::auto_);
}

CLAPP_TEST("parser_vtable: ignore_case is harmlessly ignored by parsers without it") {
    const parser_vtable* table = clapp::parser_for<std::string>();
    const std::expected<clapp::any_value, parse_error> parsed =
            table->parse(os_str{"MiXeD"sv}, true);
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(parsed->get<std::string>() == "MiXeD");
}

CLAPP_TEST("parser_vtable: a table erased into a container still dispatches") {
    // What clapp::arg will actually hold: a pointer with no static knowledge of T.
    const std::vector<const parser_vtable*> tables{
            clapp::parser_for<bool>(), clapp::parser_for<shell>(), clapp::parser_for<os_string>()};
    CLAPP_CHECK(tables[0]->parse(os_str{"oFF"sv}, false)->get<bool>() == false);
    CLAPP_CHECK(tables[1]->parse(os_str{"fishy"sv}, false)->get<shell>() == shell::fish);
    CLAPP_CHECK(tables[2]->parse(os_str{"\xFF"sv}, false)->get<os_string>() == os_string("\xFF"sv));

    CLAPP_CHECK(tables[0]->possible_values().size() == 12);
    CLAPP_CHECK(tables[1]->possible_values().size() == 6);
    CLAPP_CHECK(tables[2]->possible_values().empty());
}

CLAPP_TEST("parser_vtable: the erased float and path parsers work despite not being constexpr") {
    const std::expected<clapp::any_value, parse_error> ratio =
            clapp::parser_for<double>()->parse(os_str{"2.5"sv}, false);
    CLAPP_CHECK(ratio.has_value() && ratio->get<double>() == 2.5);

    const std::expected<clapp::any_value, parse_error> file =
            clapp::parser_for<std::filesystem::path>()->parse(os_str{"a/b"sv}, false);
    CLAPP_CHECK(file.has_value() && file->get<std::filesystem::path>() == "a/b");
}

// ---------------------------------------------------------------------------
// Types that only exist at runtime
// ---------------------------------------------------------------------------

CLAPP_TEST("value_parser<std::string>: non-UTF-8 bytes are reported, not truncated") {
    const std::expected<std::string, parse_error> parsed =
            clapp::parse_value<std::string>(os_str{"caf\xe9 au lait"sv});
    CLAPP_CHECK(!parsed.has_value());
    CLAPP_CHECK(parsed.error().kind == parse_error_kind::invalid_utf8);
    CLAPP_CHECK(parsed.error().encoding.valid_up_to == 3);
    CLAPP_CHECK(parsed.error().message() == "lead byte not followed by a continuation byte");
    // The good prefix is still recoverable from the error, which is the whole reason
    // invalid_encoding carries an offset.
    CLAPP_CHECK(parsed.error().input.chars().substr(0, parsed.error().encoding.valid_up_to) ==
                "caf");
}

CLAPP_TEST("value_parser<os_string>: every byte survives, NUL included") {
    const std::expected<os_string, parse_error> parsed =
            clapp::parse_value<os_string>(os_str{std::string_view{"a\0\xFF", 3}});
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(parsed.value().size() == 3);
    CLAPP_CHECK(parsed.value().chars() == std::string("a\0\xFF", 3));
}

CLAPP_TEST("parse_error: visible_values filters what help may list") {
    const parse_error error = clapp::parse_value<shell>(os_str{"nope"sv}).error();
    std::vector<std::string_view> visible;
    for (std::string_view name : error.visible_values()) visible.push_back(name);
    CLAPP_CHECK(visible.size() == 5);
    CLAPP_CHECK(visible.back() == "fishy");
    CLAPP_CHECK(std::ranges::find(visible, "legacy-csh"sv) == visible.end());

    // ... but the hidden value is still in `possible`, so a suggestion engine can see
    // it and a renamed variant keeps working.
    CLAPP_CHECK(error.possible.size() == 6);
    CLAPP_CHECK(error.possible[5].get_name() == "legacy-csh");
}

CLAPP_TEST("value_parser: a user specialization is picked up by the concept and the table") {
    // The extension story from ADR-0004, exercised end to end.
    CLAPP_CHECK(clapp::parsable<std::int_least32_t>);
    CLAPP_CHECK(clapp::parse_value<std::int_least32_t>(os_str{"-7"sv}).value() == -7);
}

// ---------------------------------------------------------------------------
// The extension point from ADR-0004, exercised end to end
//
// A user type clapp has never heard of becomes parsable by specializing one class
// template — no base class, no registration, no `#[derive]`. The specialization
// composes with the builtin one for std::uint8_t, which is where the range check on
// each channel comes from for free.
// ---------------------------------------------------------------------------

namespace {

    struct rgb {
        std::uint8_t red   = 0;
        std::uint8_t green = 0;
        std::uint8_t blue  = 0;

        [[nodiscard]] constexpr bool operator==(const rgb&) const noexcept = default;
    };

    constexpr std::string_view rgb_type_name = "rgb";

}  // namespace

template<>
struct clapp::value_parser<rgb> {
    [[nodiscard]] static constexpr std::expected<rgb, clapp::parse_error>
    parse(clapp::os_str value) {
        const auto malformed = [value](std::string_view why) {
            return std::unexpected(
                    clapp::parse_error{.kind      = clapp::parse_error_kind::invalid_value,
                                       .input     = value,
                                       .type_name = rgb_type_name,
                                       .reason    = why});
        };

        rgb parsed{};
        std::array<std::uint8_t*, 3> channels{&parsed.red, &parsed.green, &parsed.blue};
        std::size_t seen = 0;

        for (const clapp::os_str piece : value.split(",")) {
            if (seen == channels.size()) return malformed("expected exactly three channels");
            const std::expected<std::uint8_t, clapp::parse_error> channel =
                    clapp::parse_value<std::uint8_t>(piece);
            if (!channel.has_value()) return std::unexpected(channel.error());
            *channels[seen] = channel.value();
            ++seen;
        }
        if (seen != channels.size()) return malformed("expected exactly three channels");
        return parsed;
    }
};

namespace {

    static_assert(clapp::parsable<rgb>);
    static_assert(clapp::erasable_parsable<rgb>);
    // The specialization declares no possible_values(), and the uniform entry point
    // still answers.
    static_assert(!clapp::enumerable_parser<rgb>);
    static_assert(clapp::possible_values_of<rgb>().empty());

    static_assert(yields<rgb>("1,2,3"sv, rgb{.red = 1, .green = 2, .blue = 3}));
    static_assert(yields<rgb>("255,0,0"sv, rgb{.red = 255}));
    static_assert(rejects<rgb>("1,2"sv, parse_error_kind::invalid_value));
    static_assert(rejects<rgb>("1,2,3,4"sv, parse_error_kind::invalid_value));
    // Composed from value_parser<std::uint8_t>, so each channel is range-checked with
    // no extra code — a channel of 256 is out_of_range, not a wrapped 0.
    static_assert(rejects<rgb>("1,2,256"sv, parse_error_kind::out_of_range));
    static_assert(rejects<rgb>("1,2,x"sv, parse_error_kind::invalid_digit));
    static_assert(failure<rgb>("1,2"sv).type_name == rgb_type_name);

    static_assert(clapp::parser_for<rgb>()->possible_values().empty());
    // The erased table reports the *reflected* name of rgb, which is not the
    // `rgb_type_name` the specialization's own parse() puts in its errors. Spelling as
    // per names_type(): `{anonymous}::rgb` on GCC, `rgb` on clang.
    static_assert(names_type(clapp::parser_for<rgb>()->type_name(), "rgb"));
    static_assert(clapp::parser_for<rgb>()->type_name() != clapp::parser_for<int>()->type_name());

}  // namespace

CLAPP_TEST("value_parser: a user specialization dispatches through the erased table") {
    const parser_vtable* table = clapp::parser_for<rgb>();
    CLAPP_CHECK(table != clapp::parser_for<int>());
    const std::expected<clapp::any_value, parse_error> parsed =
            table->parse(os_str{"10,20,30"sv}, false);
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(parsed->holds<rgb>());
    CLAPP_CHECK(parsed->get<rgb>() == (rgb{.red = 10, .green = 20, .blue = 30}));

    const std::expected<clapp::any_value, parse_error> bad =
            table->parse(os_str{"10,20,300"sv}, false);
    CLAPP_CHECK(!bad.has_value());
    CLAPP_CHECK(bad.error().kind == parse_error_kind::out_of_range);
    CLAPP_CHECK(bad.error().domain == "0..=255");
}
