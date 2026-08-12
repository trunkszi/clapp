#include <clapp/lex/parsed_arg.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace {

    using clapp::os_str;
    using clapp::os_string;
    using clapp::parsed_arg;
    using clapp::short_flags;

    // ---------------------------------------------------------------------------
    // Helpers
    //
    // `optional::operator->` is not reliably constexpr once _GLIBCXX_ASSERTIONS is
    // on (see CLAUDE.md), so every unwrap checks engagement first and goes through
    // value() / error().
    // ---------------------------------------------------------------------------

    /** Everything to_long() reported, flattened so a `static_assert` can read it. */
    struct long_view {
        bool present      = false; /**< to_long() returned a value at all */
        bool name_is_text = false; /**< the name decoded as UTF-8 */
        std::string_view name{};   /**< meaningful only when #name_is_text */
        os_str raw_name{};         /**< the name's bytes, decoded or not */
        bool has_value = false;    /**< an `=` was present */
        os_str value{};            /**< meaningful only when #has_value */
    };

    constexpr long_view inspect_long(os_str raw) {
        long_view out;
        const std::optional<parsed_arg::long_flag> parsed = parsed_arg{raw}.to_long();
        if (!parsed.has_value()) return out;

        out.present = true;
        if (parsed.value().first.has_value()) {
            out.name_is_text = true;
            out.name         = parsed.value().first.value();
            out.raw_name     = os_str{out.name};
        } else {
            out.raw_name = parsed.value().first.error();
        }
        if (parsed.value().second.has_value()) {
            out.has_value = true;
            out.value     = parsed.value().second.value();
        }
        return out;
    }

    /**
     * Whether \p step is an engaged, non-error flag carrying exactly \p expected.
     *
     * This spells out what `step == std::optional{short_flags::flag_result{expected}}`
     * would have said, because that comparison does not compile against the libc++ the
     * clang-p2996 fork bundles: checking the constraints of
     * `operator==(const optional<T>&, const U&)` for `U = optional<expected<...>>` recurses
     * through `expected`'s heterogeneous `operator==` and clang reports
     * `satisfaction of constraint 'requires { { *__x == __v } -> __core_convertible_to<bool>; }'
     * depends on itself`. Reduced to a five-line program with no clapp in it; libstdc++ and
     * Apple clang 21's (newer) libc++ both accept it, so the fork's libc++ is simply stale.
     * The check is the same one, component by component: engaged, holding a value, equal.
     */
    constexpr bool flag_is(const std::optional<short_flags::flag_result>& step, char32_t expected) {
        return step.has_value() && step.value().has_value() && step.value().value() == expected;
    }

    /** The cluster to_short() handed back, or `std::nullopt` if it declined. */
    constexpr std::optional<os_str> short_cluster(os_str raw) {
        const std::optional<short_flags> flags = parsed_arg{raw}.to_short();
        if (!flags.has_value()) return std::nullopt;
        return flags.value().cluster();
    }

    /** Whether to_value() decoded, and to what. */
    constexpr bool value_is_text(os_str raw, std::string_view expected) {
        const std::expected<std::string_view, os_str> value = parsed_arg{raw}.to_value();
        return value.has_value() && value.value() == expected;
    }

    /** Whether to_value() refused to decode, handing back the original bytes. */
    constexpr bool value_is_raw(os_str raw) {
        const std::expected<std::string_view, os_str> value = parsed_arg{raw}.to_value();
        return !value.has_value() && value.error() == raw;
    }

    // ---------------------------------------------------------------------------
    // Sample arguments
    //
    // Non-UTF-8 bytes are written as hex escapes so the tests do not depend on this
    // file's encoding. A hex escape swallows every following hex digit, so a
    // trailing a..f has to live in a separately concatenated literal.
    // ---------------------------------------------------------------------------

    constexpr os_str arg_empty{""};
    constexpr os_str arg_stdio{"-"};
    constexpr os_str arg_escape{"--"};
    constexpr os_str arg_triple_dash{"---"};
    constexpr os_str arg_long{"--long"};
    constexpr os_str arg_long_empty_value{"--long="};
    constexpr os_str arg_long_value{"--long=hello"};
    constexpr os_str arg_anonymous_long{"--=value"};
    constexpr os_str arg_short{"-short"};
    constexpr os_str arg_value{"value"};
    constexpr os_str arg_bad_long{"--\xFF=v"};           // name is not UTF-8
    constexpr os_str arg_accented_long{"--\xC3\xA9=v"};  // name is é, valid UTF-8
    constexpr os_str arg_bad_value{"--path=\xFF\xFE"};

    // ---------------------------------------------------------------------------
    // The shape predicates
    //
    // The whole table in one place, because the interesting property is that these
    // are mutually exclusive in exactly the way clap says and not in the obvious
    // way: `-` is not short, `--` is not long, and `---` is.
    // ---------------------------------------------------------------------------

    //                                   empty  stdio  escape  long   short
    static_assert(parsed_arg{arg_empty}.is_empty());
    static_assert(!parsed_arg{arg_empty}.is_stdio());
    static_assert(!parsed_arg{arg_empty}.is_escape());
    static_assert(!parsed_arg{arg_empty}.is_long());
    static_assert(!parsed_arg{arg_empty}.is_short());

    static_assert(!parsed_arg{arg_stdio}.is_empty());
    static_assert(parsed_arg{arg_stdio}.is_stdio());
    static_assert(!parsed_arg{arg_stdio}.is_escape());
    static_assert(!parsed_arg{arg_stdio}.is_long());
    static_assert(!parsed_arg{arg_stdio}.is_short());  // `-` alone has no flags

    static_assert(!parsed_arg{arg_escape}.is_stdio());
    static_assert(parsed_arg{arg_escape}.is_escape());
    static_assert(!parsed_arg{arg_escape}.is_long());  // `--` is the escape, not an option
    static_assert(!parsed_arg{arg_escape}.is_short());

    static_assert(!parsed_arg{arg_triple_dash}.is_escape());
    static_assert(parsed_arg{arg_triple_dash}.is_long());  // `---` is the long option `-`
    static_assert(!parsed_arg{arg_triple_dash}.is_short());

    static_assert(parsed_arg{arg_long}.is_long());
    static_assert(!parsed_arg{arg_long}.is_short());

    static_assert(parsed_arg{arg_short}.is_short());
    static_assert(!parsed_arg{arg_short}.is_long());

    static_assert(!parsed_arg{arg_value}.is_long());
    static_assert(!parsed_arg{arg_value}.is_short());
    static_assert(!parsed_arg{arg_value}.is_stdio());
    static_assert(!parsed_arg{arg_value}.is_escape());

    // A default-constructed parsed_arg is the empty argument.
    static_assert(parsed_arg{}.is_empty());
    static_assert(parsed_arg{} == parsed_arg{arg_empty});

    // The predicates agree with the conversions by construction.
    static_assert(parsed_arg{arg_long}.is_long() == parsed_arg{arg_long}.to_long().has_value());
    static_assert(parsed_arg{arg_escape}.is_long() == parsed_arg{arg_escape}.to_long().has_value());
    static_assert(parsed_arg{arg_short}.is_short() == parsed_arg{arg_short}.to_short().has_value());
    static_assert(parsed_arg{arg_stdio}.is_short() == parsed_arg{arg_stdio}.to_short().has_value());

    // ---------------------------------------------------------------------------
    // to_long
    // ---------------------------------------------------------------------------

    // No value at all, versus an empty value. These must stay distinguishable:
    // `--color` and `--color=` mean different things to every real CLI.
    static_assert(inspect_long(arg_long).present);
    static_assert(inspect_long(arg_long).name == "long");
    static_assert(!inspect_long(arg_long).has_value);

    static_assert(inspect_long(arg_long_empty_value).name == "long");
    static_assert(inspect_long(arg_long_empty_value).has_value);
    static_assert(inspect_long(arg_long_empty_value).value == os_str{""});

    static_assert(inspect_long(arg_long_value).name == "long");
    static_assert(inspect_long(arg_long_value).value == os_str{"hello"});

    // Splits at the *first* `=`, so the value may itself contain one.
    static_assert(inspect_long(os_str{"--k=a=b"}).name == "k");
    static_assert(inspect_long(os_str{"--k=a=b"}).value == os_str{"a=b"});

    // `---` is the long option whose name is `-`; `--=v` is the one whose name is
    // empty. Both are nonsense no command defines, and reporting them as long
    // options is what turns them into "unexpected argument" rather than a silent
    // reinterpretation as a positional.
    static_assert(inspect_long(arg_triple_dash).present);
    static_assert(inspect_long(arg_triple_dash).name == "-");
    static_assert(!inspect_long(arg_triple_dash).has_value);

    static_assert(inspect_long(arg_anonymous_long).present);
    static_assert(inspect_long(arg_anonymous_long).name.empty());
    static_assert(inspect_long(arg_anonymous_long).name_is_text);
    static_assert(inspect_long(arg_anonymous_long).value == os_str{"value"});

    // Everything that is not a long option.
    static_assert(!inspect_long(arg_escape).present);
    static_assert(!inspect_long(arg_stdio).present);
    static_assert(!inspect_long(arg_short).present);
    static_assert(!inspect_long(arg_value).present);
    static_assert(!inspect_long(arg_empty).present);

    // A name that is not UTF-8 comes back on the unexpected side, with its bytes
    // intact, while the value is handed over untouched — a value has no obligation
    // to be text.
    static_assert(inspect_long(arg_bad_long).present);
    static_assert(!inspect_long(arg_bad_long).name_is_text);
    static_assert(inspect_long(arg_bad_long).raw_name == os_str{"\xFF"});
    static_assert(inspect_long(arg_bad_long).value == os_str{"v"});

    // Non-ASCII is not the same as non-UTF-8: `--é=v` has a perfectly good name.
    static_assert(inspect_long(arg_accented_long).name_is_text);
    static_assert(inspect_long(arg_accented_long).name == "\xC3\xA9");

    // An undecodable *value* never reaches the expected/unexpected split at all.
    static_assert(inspect_long(arg_bad_value).name == "path");
    static_assert(inspect_long(arg_bad_value).value == os_str{"\xFF\xFE"});

    // ---------------------------------------------------------------------------
    // to_short
    // ---------------------------------------------------------------------------

    static_assert(short_cluster(arg_short) == std::optional{os_str{"short"}});
    static_assert(short_cluster(os_str{"-abc"}) == std::optional{os_str{"abc"}});
    static_assert(short_cluster(os_str{"-1"}) == std::optional{os_str{"1"}});
    static_assert(short_cluster(os_str{"-="}) == std::optional{os_str{"="}});

    static_assert(!short_cluster(arg_stdio).has_value());        // `-`
    static_assert(!short_cluster(arg_escape).has_value());       // `--`
    static_assert(!short_cluster(arg_long).has_value());         // `--long`
    static_assert(!short_cluster(arg_triple_dash).has_value());  // `---`
    static_assert(!short_cluster(arg_value).has_value());
    static_assert(!short_cluster(arg_empty).has_value());

    // The cursor starts at the beginning: `-abc` iterates to a, b, c.
    static_assert([] {
        std::optional<short_flags> flags = parsed_arg{os_str{"-abc"}}.to_short();
        if (!flags.has_value()) return false;
        short_flags cursor = flags.value();
        return flag_is(cursor.next_flag(), U'a') && flag_is(cursor.next_flag(), U'b') &&
               flag_is(cursor.next_flag(), U'c') && !cursor.next_flag().has_value();
    }());

    // ---------------------------------------------------------------------------
    // is_negative_number
    //
    // The rows come straight from clap_lex's parsed.rs.
    // ---------------------------------------------------------------------------

    static_assert(parsed_arg{os_str{"-10.0"}}.is_negative_number());
    static_assert(parsed_arg{os_str{"-1"}}.is_negative_number());
    static_assert(parsed_arg{os_str{"-100"}}.is_negative_number());
    static_assert(parsed_arg{os_str{"-3.5"}}.is_negative_number());
    static_assert(parsed_arg{os_str{"-1e10"}}.is_negative_number());
    static_assert(parsed_arg{os_str{"-1.3e10"}}.is_negative_number());
    static_assert(parsed_arg{os_str{"-1E10"}}.is_negative_number());

    static_assert(!parsed_arg{os_str{"10.0"}}.is_negative_number());  // positive: no leading `-`
    static_assert(!parsed_arg{os_str{"--10.0"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-.."}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-2.."}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-e"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-1e"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-1e10.2"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-.2"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-E"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-1E"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"-1E10.2"}}.is_negative_number());

    // Bytes that are not text cannot be a number, whatever they start with.
    static_assert(!parsed_arg{os_str{"-1\xFF"}}.is_negative_number());
    static_assert(!parsed_arg{os_str{"\xFF"}}.is_negative_number());
    static_assert(!parsed_arg{arg_empty}.is_negative_number());

    // Documented quirk, inherited on purpose: `-` counts as a negative number
    // because the part after the `-` is empty and clap treats "" as a number.
    // The parser must therefore test is_stdio() first.
    static_assert(parsed_arg{arg_stdio}.is_negative_number());
    static_assert(parsed_arg{arg_stdio}.is_stdio());

    // `-1` is a negative number *and* a short cluster. Only the command definition
    // breaks the tie; the lexer refuses to guess.
    static_assert(parsed_arg{os_str{"-1"}}.is_short());
    static_assert(parsed_arg{os_str{"-1"}}.is_negative_number());

    // ---------------------------------------------------------------------------
    // to_value / to_value_os
    //
    // Both are deliberately unfiltered: they hand back flags and escapes as-is.
    // ---------------------------------------------------------------------------

    static_assert(parsed_arg{arg_value}.to_value_os() == arg_value);
    static_assert(parsed_arg{arg_long}.to_value_os() == arg_long);      // yes, `--long`
    static_assert(parsed_arg{arg_escape}.to_value_os() == arg_escape);  // and `--`
    static_assert(parsed_arg{arg_empty}.to_value_os() == os_str{""});

    static_assert(value_is_text(arg_value, "value"));
    static_assert(value_is_text(arg_long, "--long"));
    static_assert(value_is_text(os_str{"\xC3\xA9"}, "\xC3\xA9"));
    static_assert(value_is_raw(os_str{"\xFF\xFE"}));
    static_assert(value_is_raw(arg_bad_value));

    // An encoded surrogate is well-formed WTF-8 but not strict UTF-8, so it stays
    // on the raw side rather than leaking into a std::string_view.
    static_assert(os_str{"\xED\xA0\x80"}.is_wtf8());
    static_assert(value_is_raw(os_str{"\xED\xA0\x80"}));

    // ---------------------------------------------------------------------------
    // display
    //
    // The rendering clap_builder puts inside "unexpected argument '{}'". One U+FFFD
    // per maximal subpart, so the count matches what clap prints for the same bytes.
    // ---------------------------------------------------------------------------

    static_assert([] {
        return parsed_arg{arg_bad_value}.display() ==
               std::string{"--path=\xEF\xBF\xBD\xEF\xBF\xBD"};
    }());
    static_assert([] {
        return parsed_arg{arg_long_value}.display() == std::string{"--long=hello"};
    }());
    static_assert(
            [] {
                return parsed_arg{os_str{"--path=\xE0\x80\x80"}}.display() ==
                       std::string{"--path=\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"};
            }(),
            "an overlong sequence is three errors, not one");
    static_assert(
            [] {
                return clapp::validate_utf8(parsed_arg{os_str{"\xED\xA0\x80"}}.display())
                        .has_value();
            }(),
            "whatever goes in, what comes out is printable UTF-8");

    // ---------------------------------------------------------------------------
    // Comparison
    // ---------------------------------------------------------------------------

    static_assert(parsed_arg{arg_long} == parsed_arg{os_str{"--long"}});
    static_assert(parsed_arg{arg_long} != parsed_arg{arg_short});
    static_assert(parsed_arg{os_str{"a"}} < parsed_arg{os_str{"b"}});
    // Unsigned byte ordering, as on os_str: "\x80" sorts after "\x7F".
    static_assert(parsed_arg{os_str{"\x7F"}} < parsed_arg{os_str{"\x80"}});

    // ---------------------------------------------------------------------------
    // A worked example: `--verbose -c auto -- -not-a-flag`
    //
    // Exercises the six predicates in the order a parser would ask them.
    // ---------------------------------------------------------------------------

    static_assert(inspect_long(os_str{"--verbose"}).name == "verbose");
    static_assert(!inspect_long(os_str{"--verbose"}).has_value);
    static_assert(short_cluster(os_str{"-c"}) == std::optional{os_str{"c"}});
    static_assert(parsed_arg{os_str{"--"}}.is_escape());
    static_assert(parsed_arg{os_str{"-not-a-flag"}}.is_short());
    static_assert(parsed_arg{os_str{"-not-a-flag"}}.to_value_os() == os_str{"-not-a-flag"});

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
//
// These report the compile-time conclusions above and cover the results that
// need heap storage outliving one constant evaluation.
// ---------------------------------------------------------------------------

CLAPP_TEST("the escape, the stdio placeholder and the triple dash are three things") {
    CLAPP_CHECK(parsed_arg{"--"}.is_escape());
    CLAPP_CHECK(!parsed_arg{"--"}.is_long());
    CLAPP_CHECK(parsed_arg{"-"}.is_stdio());
    CLAPP_CHECK(!parsed_arg{"-"}.is_short());
    CLAPP_CHECK(parsed_arg{"---"}.is_long());
    CLAPP_CHECK(!parsed_arg{"---"}.is_escape());
}

CLAPP_TEST("to_long keeps `--key` and `--key=` apart") {
    const std::optional<parsed_arg::long_flag> without = parsed_arg{"--key"}.to_long();
    CLAPP_CHECK(without.has_value());
    CLAPP_CHECK(without.value().first.has_value());
    CLAPP_CHECK(without.value().first.value() == "key");
    CLAPP_CHECK(!without.value().second.has_value());

    const std::optional<parsed_arg::long_flag> with = parsed_arg{"--key="}.to_long();
    CLAPP_CHECK(with.has_value());
    CLAPP_CHECK(with.value().second.has_value());
    CLAPP_CHECK(with.value().second.value().empty());
}

CLAPP_TEST("a long name that is not UTF-8 comes back as raw bytes") {
    const std::optional<parsed_arg::long_flag> parsed = parsed_arg{"--\xFF=v"}.to_long();
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(!parsed.value().first.has_value());
    CLAPP_CHECK(parsed.value().first.error() == os_str{"\xFF"});
    CLAPP_CHECK(parsed.value().second.value() == os_str{"v"});
}

CLAPP_TEST("display() renders undecodable arguments without corrupting a terminal") {
    CLAPP_CHECK(parsed_arg{"--path=\xFF\xFE"}.display() ==
                std::string{"--path=\xEF\xBF\xBD\xEF\xBF\xBD"});
    CLAPP_CHECK(parsed_arg{"--long=hello"}.display() == std::string{"--long=hello"});
    CLAPP_CHECK(parsed_arg{"--path=\xE0\x80\x80"}.display() ==
                std::string{"--path=\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"});
    CLAPP_CHECK(clapp::validate_utf8(parsed_arg{"\xED\xA0\x80"}.display()).has_value());
}

CLAPP_TEST("std::format goes through display(), so `{}` is always printable") {
    CLAPP_CHECK(std::format("{}", parsed_arg{"--long"}) == "--long");
    CLAPP_CHECK(std::format("[{:>8}]", parsed_arg{"-v"}) == "[      -v]");
    CLAPP_CHECK(std::format("{}", parsed_arg{"\xFF"}) == std::string{"\xEF\xBF\xBD"});
}

CLAPP_TEST("std::hash agrees with byte-wise equality") {
    CLAPP_CHECK(std::hash<parsed_arg>{}(parsed_arg{"--long"}) ==
                std::hash<os_str>{}(os_str{"--long"}));
    CLAPP_CHECK(std::hash<parsed_arg>{}(parsed_arg{"--long"}) !=
                std::hash<parsed_arg>{}(parsed_arg{"--lonh"}));
}

CLAPP_TEST("a parsed_arg views storage owned elsewhere, os_string included") {
    const os_string owned{"--output=file.txt"};
    const parsed_arg viewed{owned};

    CLAPP_CHECK(viewed.is_long());
    const std::optional<parsed_arg::long_flag> parsed = viewed.to_long();
    CLAPP_CHECK(parsed.has_value());
    CLAPP_CHECK(parsed.value().first.value() == "output");
    CLAPP_CHECK(parsed.value().second.value() == os_str{"file.txt"});
}
