#include <clapp/lex/short_flags.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::os_str;
    using clapp::short_flags;
    using clapp::split_attached_equals;

    // ---------------------------------------------------------------------------
    // Sample clusters
    //
    // A `short_flags` holds the argument *without* its leading `-`, so these are
    // spelled the way the cursor sees them. Non-UTF-8 bytes are written as hex
    // escapes; a hex escape swallows every following hex digit, so a trailing
    // a..f must live in a separately concatenated literal.
    // ---------------------------------------------------------------------------

    constexpr os_str cluster_short{"short"};        // -short
    constexpr os_str cluster_number{"1.0"};         // -1.0
    constexpr os_str cluster_attached{"abcv"};      // -abcv
    constexpr os_str cluster_equals{"abc=v"};       // -abc=v
    constexpr os_str cluster_accented{"\xC3\xA9"};  // -é, one flag U+00E9
    constexpr os_str cluster_bad_tail{"a\xFF"
                                      "b"};        // -a<0xFF>b
    constexpr os_str cluster_bad_only{"\xFF"};     // -<0xFF>
    constexpr os_str cluster_bad_number{"1\xFF"};  // -1<0xFF>

    // ---------------------------------------------------------------------------
    // Helpers
    //
    // `optional::operator->` is not reliably constexpr once _GLIBCXX_ASSERTIONS is
    // on (see CLAUDE.md), so every unwrap below checks engagement first and goes
    // through value() / error().
    // ---------------------------------------------------------------------------

    /** Everything one full iteration of a cluster produced. */
    struct flag_trace {
        std::array<char32_t, 12> flags{};
        std::size_t count = 0;
        bool saw_invalid  = false;
        os_str invalid{};
    };

    constexpr flag_trace trace_flags(os_str cluster) {
        flag_trace out;
        short_flags cursor{cluster};
        while (true) {
            const std::optional<short_flags::flag_result> step = cursor.next_flag();
            if (!step.has_value()) break;
            if (step.value().has_value()) {
                out.flags[out.count] = step.value().value();
                ++out.count;
            } else {
                out.saw_invalid = true;
                out.invalid     = step.value().error();
            }
        }
        return out;
    }

    /** Whether \p cluster iterates to exactly \p expected, with no invalid tail. */
    constexpr bool flags_are(os_str cluster, std::u32string_view expected) {
        const flag_trace trace = trace_flags(cluster);
        if (trace.saw_invalid) return false;
        // `ranges::equal` compares the lengths itself, so no separate count guard.
        // `views::take` counts in `ptrdiff_t`, hence the cast: -Wsign-conversion is on.
        return std::ranges::equal(
                std::views::take(trace.flags, static_cast<std::ptrdiff_t>(trace.count)), expected);
    }

    /** The value left after skipping \p skip flags, or `std::nullopt` if exhausted. */
    constexpr std::optional<os_str> value_after(os_str cluster, std::size_t skip) {
        short_flags cursor{cluster};
        for (std::size_t i = 0; i < skip; ++i) (void)cursor.next_flag();
        return cursor.next_value_os();
    }

    /** A cursor advanced past \p skip flags, for querying the state afterwards. */
    constexpr short_flags after(os_str cluster, std::size_t skip) {
        short_flags cursor{cluster};
        for (std::size_t i = 0; i < skip; ++i) (void)cursor.next_flag();
        return cursor;
    }

    /** `advance_by(n)` folded to "how many were consumed", with `npos` for success. */
    constexpr std::size_t advanced_by(os_str cluster, std::size_t n) {
        short_flags cursor{cluster};
        const std::expected<void, std::size_t> result = cursor.advance_by(n);
        return result.has_value() ? os_str::npos : result.error();
    }

    // ---------------------------------------------------------------------------
    // clapp::detail::is_number — the rule that keeps `-1` from being the flag `1`
    //
    // Ported from clap_lex's private `is_number`, including the cases its own
    // testsuite pins down in `parsed.rs::is_not_number`.
    // ---------------------------------------------------------------------------

    static_assert(clapp::detail::is_number("1"));
    static_assert(clapp::detail::is_number("100"));
    static_assert(clapp::detail::is_number("10.0"));
    static_assert(clapp::detail::is_number("3.5"));
    static_assert(clapp::detail::is_number("1."));  // clap allows the trailing dot
    static_assert(clapp::detail::is_number("1e10"));
    static_assert(clapp::detail::is_number("1E10"));
    static_assert(clapp::detail::is_number("1.3e10"));
    static_assert(clapp::detail::is_number("0e0"));

    static_assert(!clapp::detail::is_number("-1"));  // the sign is not part of it
    static_assert(!clapp::detail::is_number(".."));
    static_assert(!clapp::detail::is_number("2.."));
    static_assert(!clapp::detail::is_number(".2"));  // a dot may not lead
    static_assert(!clapp::detail::is_number("e"));
    static_assert(!clapp::detail::is_number("E"));
    static_assert(!clapp::detail::is_number("1e"));  // exponent marker, no exponent
    static_assert(!clapp::detail::is_number("1E"));
    static_assert(!clapp::detail::is_number("1e10.2"));  // no dot after the exponent
    static_assert(!clapp::detail::is_number("1E10.2"));
    static_assert(!clapp::detail::is_number("1e1e1"));  // at most one exponent
    static_assert(!clapp::detail::is_number("1 0"));
    static_assert(!clapp::detail::is_number("0x10"));
    static_assert(!clapp::detail::is_number("\xFF"));

    // The empty string is a number. Documented quirk, inherited from clap so that
    // `parsed_arg{"-"}.is_negative_number()` keeps reporting true.
    static_assert(clapp::detail::is_number(""));

    // ---------------------------------------------------------------------------
    // Iteration
    // ---------------------------------------------------------------------------

    static_assert(flags_are(cluster_short, U"short"));
    static_assert(flags_are(os_str{"a"}, U"a"));
    static_assert(flags_are(cluster_attached, U"abcv"));
    static_assert(flags_are(cluster_equals, U"abc=v"));  // `=` is just another byte here

    // A multi-byte character is one flag, not one flag per byte. This is why
    // short_flags::flag_type is char32_t rather than char.
    static_assert(cluster_accented.size() == 2);
    static_assert(flags_are(cluster_accented, U"\u00E9"));
    static_assert(trace_flags(cluster_accented).count == 1);
    static_assert(flags_are(os_str{"a\xE4\xB8\xAD"
                                   "b"},
                            U"a\u4E2Db"));
    static_assert(flags_are(os_str{"\xF0\x9F\x92\xA9"}, U"\U0001F4A9"));

    // Exhaustion is sticky.
    static_assert([] {
        short_flags cursor{os_str{"a"}};
        return cursor.next_flag().has_value() && !cursor.next_flag().has_value() &&
               !cursor.next_flag().has_value();
    }());

    // ---------------------------------------------------------------------------
    // Invalid UTF-8 is never silently dropped
    //
    // The cluster is split once into a valid prefix and an invalid suffix; flags
    // come out of the prefix and the suffix is surfaced exactly once, whole.
    // ---------------------------------------------------------------------------

    static_assert(trace_flags(cluster_bad_tail).count == 1);
    static_assert(trace_flags(cluster_bad_tail).flags[0] == U'a');
    static_assert(trace_flags(cluster_bad_tail).saw_invalid);
    static_assert(trace_flags(cluster_bad_tail).invalid == os_str{"\xFF"
                                                                  "b"});

    static_assert(trace_flags(cluster_bad_only).count == 0);
    static_assert(trace_flags(cluster_bad_only).saw_invalid);
    static_assert(trace_flags(cluster_bad_only).invalid == cluster_bad_only);

    // The suffix is reported once and then gone; the cursor does not loop.
    static_assert([] {
        short_flags cursor{cluster_bad_only};
        return cursor.next_flag().has_value() && !cursor.next_flag().has_value();
    }());

    // An encoded surrogate is not strict UTF-8, so it lands on the invalid side
    // even though os_str stores it happily as WTF-8.
    static_assert(trace_flags(os_str{"a\xED\xA0\x80"}).saw_invalid);
    static_assert(trace_flags(os_str{"a\xED\xA0\x80"}).count == 1);

    // ---------------------------------------------------------------------------
    // next_value_os — "everything left is a value"
    // ---------------------------------------------------------------------------

    // Untouched cursor: the whole cluster is the value. This is `-ovalue`.
    static_assert(value_after(cluster_short, 0) == std::optional{os_str{"short"}});
    // After one flag: the rest. This is `-o` taking `value` out of `-ovalue`.
    static_assert(value_after(cluster_short, 1) == std::optional{os_str{"hort"}});
    static_assert(value_after(cluster_short, 4) == std::optional{os_str{"t"}});
    // Fully consumed: nothing left, not an empty value.
    static_assert(!value_after(cluster_short, 5).has_value());
    static_assert(!value_after(cluster_short, 500).has_value());

    // `-abcv`, where `c` takes a value, versus `-abc=v`. Both attach `v`, but only
    // the second one said so explicitly, and the lexer keeps the `=` so the parser
    // can tell them apart.
    static_assert(value_after(cluster_attached, 3) == std::optional{os_str{"v"}});
    static_assert(value_after(cluster_equals, 3) == std::optional{os_str{"=v"}});

    static_assert(split_attached_equals(os_str{"=v"}) == std::pair{os_str{"v"}, true});
    static_assert(split_attached_equals(os_str{"v"}) == std::pair{os_str{"v"}, false});
    static_assert(split_attached_equals(os_str{"="}) == std::pair{os_str{}, true});
    static_assert(split_attached_equals(os_str{}) == std::pair{os_str{}, false});
    static_assert(split_attached_equals(os_str{"=a=b"}) == std::pair{os_str{"a=b"}, true});

    // A value may be undecodable — it might be a filename. Unlike next_flag(), this
    // never reports an error, and the invalid bytes stay attached to the remainder.
    static_assert(value_after(cluster_bad_tail, 0) == std::optional{cluster_bad_tail});
    static_assert(value_after(cluster_bad_tail, 1) == std::optional{os_str{"\xFF"
                                                                           "b"}});
    static_assert(value_after(cluster_bad_only, 0) == std::optional{cluster_bad_only});
    static_assert(!value_after(cluster_bad_only, 1).has_value());

    // Taking the value exhausts the cursor, invalid suffix included.
    static_assert([] {
        short_flags cursor{cluster_bad_tail};
        return cursor.next_value_os().has_value() && cursor.is_empty() &&
               !cursor.next_value_os().has_value() && !cursor.next_flag().has_value();
    }());

    // ---------------------------------------------------------------------------
    // advance_by
    // ---------------------------------------------------------------------------

    static_assert(advanced_by(cluster_short, 0) == os_str::npos);
    static_assert(advanced_by(cluster_short, 2) == os_str::npos);
    static_assert(advanced_by(cluster_short, 5) == os_str::npos);
    static_assert(advanced_by(cluster_short, 6) == 5);
    static_assert(advanced_by(cluster_short, 2000) == 5);

    static_assert([] {
        short_flags cursor{cluster_short};
        return cursor.advance_by(2).has_value() && cursor.rest() == os_str{"ort"};
    }());

    // Hitting undecodable bytes stops the walk and reports how far it got. The
    // suffix is consumed on the way out, matching clap_lex's plain iterator loop.
    static_assert(advanced_by(cluster_bad_tail, 2) == 1);
    static_assert([] {
        short_flags cursor{cluster_bad_tail};
        return !cursor.advance_by(4).has_value() && cursor.is_empty();
    }());

    // ---------------------------------------------------------------------------
    // is_empty
    // ---------------------------------------------------------------------------

    static_assert(!short_flags{cluster_short}.is_empty());
    static_assert(!after(cluster_short, 1).is_empty());
    static_assert(after(cluster_short, 5).is_empty());
    static_assert(after(cluster_short, 50).is_empty());
    static_assert(short_flags{}.is_empty());

    // A pending invalid suffix is not emptiness: it still has to be reported.
    static_assert(!after(cluster_bad_tail, 1).is_empty());
    static_assert(after(cluster_bad_tail, 2).is_empty());

    // ---------------------------------------------------------------------------
    // is_negative_number
    // ---------------------------------------------------------------------------

    static_assert(short_flags{cluster_number}.is_negative_number());  // -1.0
    static_assert(short_flags{os_str{"1"}}.is_negative_number());     // -1
    static_assert(short_flags{os_str{"1e5"}}.is_negative_number());   // -1e5
    static_assert(!short_flags{os_str{"hello"}}.is_negative_number());
    static_assert(!short_flags{os_str{"1e"}}.is_negative_number());

    // Bytes that are not text cannot be digits, whatever the valid prefix says.
    static_assert(!short_flags{cluster_bad_number}.is_negative_number());

    // Reports on the *unread* remainder, which is why clap says to call it first:
    // after one flag, `-1.5` is asking about `.5`, and a leading dot is not a number.
    static_assert(short_flags{os_str{"1.5"}}.is_negative_number());
    static_assert(!after(os_str{"1.5"}, 1).is_negative_number());

    // ---------------------------------------------------------------------------
    // cluster() / rest()
    // ---------------------------------------------------------------------------

    static_assert(short_flags{cluster_short}.cluster() == cluster_short);
    static_assert(after(cluster_short, 2).cluster() == cluster_short);  // never shrinks
    static_assert(short_flags{cluster_short}.rest() == cluster_short);
    static_assert(after(cluster_short, 2).rest() == os_str{"ort"});
    static_assert(after(cluster_short, 5).rest() == os_str{});
    static_assert(after(cluster_bad_tail, 1).rest() == os_str{"\xFF"
                                                              "b"});
    static_assert(after(cluster_bad_tail, 2).rest() == os_str{});

    // ---------------------------------------------------------------------------
    // A copy is an independent cursor
    //
    // clap's parser relies on this: it clones the iterator to see whether a value is
    // attached and rolls back by throwing the clone away.
    // ---------------------------------------------------------------------------

    static_assert([] {
        short_flags cursor{cluster_attached};
        (void)cursor.next_flag();
        short_flags probe                    = cursor;
        const std::optional<os_str> attached = probe.next_value_os();
        return attached == std::optional{os_str{"bcv"}} && probe.is_empty() && !cursor.is_empty() &&
               cursor.rest() == os_str{"bcv"};
    }());

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
//
// These report the compile-time conclusions above and additionally exercise the
// results that need heap-allocated storage to outlive one evaluation.
// ---------------------------------------------------------------------------

CLAPP_TEST("a cluster yields one flag per character, `-short` being five") {
    short_flags cursor{cluster_short};

    std::u32string seen;
    while (const std::optional<short_flags::flag_result> step = cursor.next_flag()) {
        CLAPP_CHECK(step.value().has_value());
        seen.push_back(step.value().value());
    }

    CLAPP_CHECK(seen == U"short");
    CLAPP_CHECK(cursor.is_empty());
}

CLAPP_TEST("`-é` is one flag, not two bytes") {
    short_flags cursor{cluster_accented};

    const std::optional<short_flags::flag_result> first = cursor.next_flag();
    CLAPP_CHECK(first.has_value());
    CLAPP_CHECK(first.value().has_value());
    CLAPP_CHECK(first.value().value() == U'\u00E9');
    CLAPP_CHECK(!cursor.next_flag().has_value());
}

CLAPP_TEST("undecodable bytes come back whole, on the unexpected side") {
    short_flags cursor{cluster_bad_tail};

    const std::optional<short_flags::flag_result> good = cursor.next_flag();
    CLAPP_CHECK(good.has_value() && good.value().has_value());
    CLAPP_CHECK(good.value().value() == U'a');

    const std::optional<short_flags::flag_result> bad = cursor.next_flag();
    CLAPP_CHECK(bad.has_value() && !bad.value().has_value());
    CLAPP_CHECK(bad.value().error() == os_str{"\xFF"
                                              "b"});
    CLAPP_CHECK(bad.value().error().to_string_lossy() == std::string{"\xEF\xBF\xBD"
                                                                     "b"});

    CLAPP_CHECK(!cursor.next_flag().has_value());
    CLAPP_CHECK(cursor.is_empty());
}

CLAPP_TEST("`-abcv` and `-abc=v` both attach `v`, and stay distinguishable") {
    short_flags plain{cluster_attached};
    CLAPP_CHECK(plain.advance_by(3).has_value());
    const std::optional<os_str> plain_value = plain.next_value_os();
    CLAPP_CHECK(plain_value.has_value());
    CLAPP_CHECK(split_attached_equals(plain_value.value()) == std::pair{os_str{"v"}, false});

    short_flags equals{cluster_equals};
    CLAPP_CHECK(equals.advance_by(3).has_value());
    const std::optional<os_str> equals_value = equals.next_value_os();
    CLAPP_CHECK(equals_value.has_value());
    CLAPP_CHECK(equals_value.value() == os_str{"=v"});
    CLAPP_CHECK(split_attached_equals(equals_value.value()) == std::pair{os_str{"v"}, true});
}

CLAPP_TEST("advance_by reports how far it got when it cannot finish") {
    short_flags cursor{cluster_short};
    const std::expected<void, std::size_t> result = cursor.advance_by(2000);

    CLAPP_CHECK(!result.has_value());
    CLAPP_CHECK(result.error() == 5);
    CLAPP_CHECK(cursor.is_empty());
}

CLAPP_TEST("is_negative_number separates `-1.0` from `-hello`") {
    CLAPP_CHECK(short_flags{cluster_number}.is_negative_number());
    CLAPP_CHECK(!short_flags{os_str{"hello"}}.is_negative_number());
    CLAPP_CHECK(!short_flags{cluster_bad_number}.is_negative_number());
}
