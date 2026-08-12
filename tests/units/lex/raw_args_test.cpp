#include <clapp/lex/raw_args.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::arg_cursor;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::parsed_arg;
    using clapp::raw_args;
    using clapp::seek_from;
    using clapp::seek_origin;

    // ---------------------------------------------------------------------------
    // Helpers
    //
    // `optional::operator->` is not reliably constexpr once _GLIBCXX_ASSERTIONS is
    // on (see CLAUDE.md), so every unwrap checks engagement first and goes through
    // value().
    // ---------------------------------------------------------------------------

    /** Whether \p view yields exactly \p expected, in order. */
    constexpr bool yields(raw_args::remaining_view view, std::initializer_list<os_str> expected) {
        return std::ranges::equal(view, expected);
    }

    /** Whether peek_os() at \p position reports exactly \p expected. */
    constexpr bool peeks(const raw_args& raw, const arg_cursor& position, os_str expected) {
        const std::optional<os_str> item = raw.peek_os(position);
        return item.has_value() && item.value() == expected;
    }

    /** Whether peek_os() at \p position reports "nothing left". */
    constexpr bool peeks_nothing(const raw_args& raw, const arg_cursor& position) {
        return !raw.peek_os(position).has_value();
    }

    /** Whether next_os() at \p position reports exactly \p expected, advancing it. */
    constexpr bool takes(const raw_args& raw, arg_cursor& position, os_str expected) {
        const std::optional<os_str> item = raw.next_os(position);
        return item.has_value() && item.value() == expected;
    }

    /** Where a cursor lands after seeking \p target in a list of \p size arguments. */
    constexpr std::size_t seek_index(std::size_t size, seek_from target, std::size_t from = 0) {
        raw_args raw;
        arg_cursor position{from};
        for (std::size_t i = 0; i < size; ++i) raw.insert(raw.cursor(), {os_str{"x"}});
        raw.seek(position, target);
        return position.index();
    }

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    constexpr bool default_is_empty() {
        const raw_args raw;
        return raw.empty() && raw.size() == 0 && raw.is_end(raw.cursor()) &&
               !raw.peek_os(raw.cursor()).has_value();
    }
    static_assert(default_is_empty());

    constexpr bool braced_list_keeps_order() {
        const raw_args raw{"bin", "--flag", "value"};
        return raw.size() == 3 && raw.items()[0].view() == os_str{"bin"} &&
               raw.items()[1].view() == os_str{"--flag"} &&
               raw.items()[2].view() == os_str{"value"};
    }
    static_assert(braced_list_keeps_order());

    constexpr bool span_constructor_copies() {
        const os_str source[] = {os_str{"bin"}, os_str{"a"}};
        const raw_args raw{std::span<const os_str>{source}};
        return raw.size() == 2 && raw.items()[1].view() == os_str{"a"};
    }
    static_assert(span_constructor_copies());

    constexpr bool from_range_accepts_any_range() {
        const std::vector<std::string> source{"bin", "--flag", "value"};
        const raw_args raw{std::from_range, source};

        // ... and a lazy pipeline, not just a container.
        const raw_args filtered{std::from_range,
                                source | std::views::filter([](std::string_view item) {
                                    return !item.starts_with('-');
                                })};
        return raw.size() == 3 && filtered.size() == 2 &&
               filtered.items()[1].view() == os_str{"value"};
    }
    static_assert(from_range_accepts_any_range());

    constexpr bool argv_constructor_reads_main_parameters() {
        const char* const argv[] = {"bin", "-v", "file.txt"};
        const raw_args raw(3, argv);
        return raw.size() == 3 && raw.items()[2].view() == os_str{"file.txt"};
    }
    static_assert(argv_constructor_reads_main_parameters());

    constexpr bool argv_constructor_survives_degenerate_input() {
        const char* const argv[] = {"bin", nullptr, "tail"};
        const raw_args holes(3, argv);
        const raw_args none(0, argv);
        const raw_args null_vector(3, nullptr);
        const raw_args negative(-1, argv);
        return holes.size() == 3 && holes.items()[1].view().empty() && none.empty() &&
               null_vector.empty() && negative.empty();
    }
    static_assert(argv_constructor_survives_degenerate_input());

    constexpr bool argv_constructor_keeps_bytes_that_are_not_utf8() {
        // Hex escapes swallow every following hex digit, hence the string-literal split.
        const char* const argv[] = {"bin",
                                    "--path=\xFF"
                                    "b"};
        const raw_args raw(2, argv);
        return raw.size() == 2 &&
               raw.items()[1].view() == os_str{"--path=\xFF"
                                               "b"} &&
               !raw.items()[1].is_utf8();
    }
    static_assert(argv_constructor_keeps_bytes_that_are_not_utf8());

    constexpr bool equality_is_byte_wise() {
        return raw_args{"a", "b"} == raw_args{"a", "b"} && !(raw_args{"a", "b"} == raw_args{"a"}) &&
               !(raw_args{"a"} == raw_args{"A"}) && raw_args{} == raw_args{};
    }
    static_assert(equality_is_byte_wise());

    // ---------------------------------------------------------------------------
    // Reading: peek, next, is_end
    // ---------------------------------------------------------------------------

    constexpr bool empty_list_reads_as_exhausted() {
        const raw_args raw;
        arg_cursor position = raw.cursor();
        return raw.is_end(position) && !raw.next(position).has_value() &&
               !raw.peek(position).has_value() && yields(raw.peek_remaining(raw.cursor()), {});
    }
    static_assert(empty_list_reads_as_exhausted());

    constexpr bool single_element_list_walks_once() {
        const raw_args raw{"only"};
        arg_cursor position = raw.cursor();
        if (raw.is_end(position)) return false;
        if (!takes(raw, position, os_str{"only"})) return false;
        if (!raw.is_end(position)) return false;
        return !raw.next_os(position).has_value();
    }
    static_assert(single_element_list_walks_once());

    constexpr bool peek_does_not_advance() {
        const raw_args raw{"bin", "a"};
        const arg_cursor start = raw.cursor();
        arg_cursor position    = start;
        return peeks(raw, position, os_str{"bin"}) && peeks(raw, position, os_str{"bin"}) &&
               peeks(raw, position, os_str{"bin"}) && position == start;
    }
    static_assert(peek_does_not_advance());

    constexpr bool peek_and_next_agree_at_every_step() {
        const raw_args raw{"bin", "--flag", "", "-", "--"};
        arg_cursor position = raw.cursor();
        for (std::size_t i = 0; i < raw.size() + 2; ++i) {
            const std::optional<os_str> seen  = raw.peek_os(position);
            const std::optional<os_str> taken = raw.next_os(position);
            if (seen.has_value() != taken.has_value()) return false;
            if (seen.has_value() && seen.value() != taken.value()) return false;
        }
        return true;
    }
    static_assert(peek_and_next_agree_at_every_step());

    constexpr bool is_end_flips_exactly_at_the_last_argument() {
        const raw_args raw{"a", "b", "c"};
        return !raw.is_end(arg_cursor{0}) && !raw.is_end(arg_cursor{1}) &&
               !raw.is_end(arg_cursor{2}) && raw.is_end(arg_cursor{3}) && raw.is_end(arg_cursor{4});
    }
    static_assert(is_end_flips_exactly_at_the_last_argument());

    constexpr bool next_classifies_while_next_os_does_not() {
        const raw_args raw{"--flag", "-vv", "--", "-"};
        arg_cursor position = raw.cursor();

        const std::optional<parsed_arg> first = raw.next(position);
        if (!first.has_value() || !first.value().is_long()) return false;
        const std::optional<parsed_arg> second = raw.next(position);
        if (!second.has_value() || !second.value().is_short()) return false;
        const std::optional<parsed_arg> third = raw.next(position);
        if (!third.has_value() || !third.value().is_escape()) return false;
        const std::optional<parsed_arg> fourth = raw.next(position);
        return fourth.has_value() && fourth.value().is_stdio();
    }
    static_assert(next_classifies_while_next_os_does_not());

    // ---------------------------------------------------------------------------
    // Cursors
    // ---------------------------------------------------------------------------

    constexpr bool two_cursors_advance_independently() {
        const raw_args raw{"a", "b", "c"};
        arg_cursor fast = raw.cursor();
        arg_cursor slow = raw.cursor();
        raw.next_os(fast);
        raw.next_os(fast);
        return peeks(raw, fast, os_str{"c"}) && peeks(raw, slow, os_str{"a"}) && slow < fast;
    }
    static_assert(two_cursors_advance_independently());

    constexpr bool a_copied_cursor_rewinds() {
        const raw_args raw{"bin", "sub", "--flag"};
        arg_cursor position = raw.cursor();
        raw.next_os(position);

        const arg_cursor saved = position;  // clap: cursor.clone()
        raw.next_os(position);
        raw.next_os(position);
        if (!raw.is_end(position)) return false;

        position = saved;  // rewind, then replay
        return peeks(raw, position, os_str{"sub"});
    }
    static_assert(a_copied_cursor_rewinds());

    constexpr bool cursors_order_by_position() {
        return arg_cursor{} == arg_cursor{0} && arg_cursor{1} > arg_cursor{0} &&
               (arg_cursor{2} <=> arg_cursor{5}) == std::strong_ordering::less;
    }
    static_assert(cursors_order_by_position());

    constexpr bool next_overshoots_the_end_like_clap_lex() {
        // Measured against clap_lex: two next_os() calls past the end leave the cursor
        // two past size(), so the first seek(current(-1)) only gets back to the end and
        // the second reaches the last argument. Surprising, and deliberately kept.
        const raw_args raw{"a", "b"};
        arg_cursor position = raw.cursor();
        raw.next_os(position);
        raw.next_os(position);
        raw.next_os(position);
        raw.next_os(position);
        if (position.index() != 4) return false;
        if (!raw.is_end(position)) return false;

        raw.seek(position, seek_from::current(-1));
        if (!peeks_nothing(raw, position)) return false;
        raw.seek(position, seek_from::current(-1));
        return peeks(raw, position, os_str{"b"});
    }
    static_assert(next_overshoots_the_end_like_clap_lex());

    // ---------------------------------------------------------------------------
    // seek
    // ---------------------------------------------------------------------------

    constexpr bool seek_factories_build_what_they_say() {
        return seek_from::start(3) == seek_from{.origin = seek_origin::start, .offset = 3} &&
               seek_from::end(-1) == seek_from{.origin = seek_origin::end, .offset = -1} &&
               seek_from::current(2) == seek_from{.origin = seek_origin::current, .offset = 2} &&
               seek_from{} == seek_from::start(0);
    }
    static_assert(seek_factories_build_what_they_say());

    constexpr bool seek_clamps_into_range() {
        // The table clap_lex produced for a three-argument list.
        return seek_index(3, seek_from::start(0)) == 0 && seek_index(3, seek_from::start(2)) == 2 &&
               seek_index(3, seek_from::start(99)) == 3 && seek_index(3, seek_from::end(0)) == 3 &&
               seek_index(3, seek_from::end(-1)) == 2 && seek_index(3, seek_from::end(-99)) == 0 &&
               seek_index(3, seek_from::end(2)) == 3 &&
               seek_index(3, seek_from::current(-99), 3) == 0 &&
               seek_index(3, seek_from::current(99), 0) == 3 &&
               seek_index(3, seek_from::current(1), 1) == 2 &&
               seek_index(3, seek_from::current(-1), 1) == 0;
    }
    static_assert(seek_clamps_into_range());

    constexpr bool seek_survives_extreme_offsets() {
        constexpr std::ptrdiff_t least = std::numeric_limits<std::ptrdiff_t>::min();
        constexpr std::ptrdiff_t most  = std::numeric_limits<std::ptrdiff_t>::max();
        return seek_index(2, seek_from::current(least), 1) == 0 &&
               seek_index(2, seek_from::end(least)) == 0 &&
               seek_index(2, seek_from::end(most)) == 2 &&
               seek_index(2, seek_from::start(std::numeric_limits<std::size_t>::max())) == 2 &&
               seek_index(0, seek_from::end(-1)) == 0;
    }
    static_assert(seek_survives_extreme_offsets());

    constexpr bool seek_to_start_replays_the_whole_list() {
        const raw_args raw{"bin", "a", "b"};
        arg_cursor position = raw.cursor();
        if (!yields(raw.remaining(position), {os_str{"bin"}, os_str{"a"}, os_str{"b"}}))
            return false;
        if (!raw.is_end(position)) return false;
        raw.seek(position, seek_from::start(0));
        return peeks(raw, position, os_str{"bin"}) &&
               yields(raw.peek_remaining(position), {os_str{"bin"}, os_str{"a"}, os_str{"b"}});
    }
    static_assert(seek_to_start_replays_the_whole_list());

    // ---------------------------------------------------------------------------
    // remaining
    // ---------------------------------------------------------------------------

    constexpr bool remaining_consumes_and_peek_remaining_does_not() {
        const raw_args raw{"bin", "a", "b"};
        arg_cursor position = raw.cursor();
        raw.next_os(position);

        if (!yields(raw.peek_remaining(position), {os_str{"a"}, os_str{"b"}})) return false;
        if (position.index() != 1) return false;

        if (!yields(raw.remaining(position), {os_str{"a"}, os_str{"b"}})) return false;
        return raw.is_end(position) && position.index() == raw.size() &&
               yields(raw.remaining(position), {});
    }
    static_assert(remaining_consumes_and_peek_remaining_does_not());

    constexpr bool remaining_after_the_escape_is_the_verbatim_tail() {
        // The `--` contract: everything after it is a value, flags included.
        const raw_args raw{"bin", "--", "--not-a-flag", "-x", "-"};
        arg_cursor position = raw.cursor();
        while (const std::optional<parsed_arg> arg = raw.next(position))
            if (arg.value().is_escape()) break;
        return yields(raw.remaining(position), {os_str{"--not-a-flag"}, os_str{"-x"}, os_str{"-"}});
    }
    static_assert(remaining_after_the_escape_is_the_verbatim_tail());

    constexpr bool remaining_clamps_where_clap_lex_panics() {
        // clap_lex slices `self.items[cursor..]`, which panics once the cursor has
        // overshot; clapp returns an empty range instead. The one deliberate
        // divergence, and it can only be reached through the over-run above.
        const raw_args raw{"a"};
        arg_cursor position = raw.cursor();
        raw.next_os(position);
        raw.next_os(position);
        raw.next_os(position);
        return yields(raw.remaining(position), {}) && position.index() == raw.size();
    }
    static_assert(remaining_clamps_where_clap_lex_panics());

    constexpr bool remaining_is_a_real_range() {
        const raw_args raw{"bin", "a", "bb", "ccc"};
        arg_cursor position = raw.cursor();
        raw.next_os(position);

        const raw_args::remaining_view rest = raw.peek_remaining(position);
        std::size_t total                   = 0;
        for (const os_str value : rest) total += value.size();
        return std::ranges::size(rest) == 3 && total == 6 &&
               std::ranges::count_if(rest, [](os_str value) { return value.size() > 1; }) == 2;
    }
    static_assert(remaining_is_a_real_range());

    // ---------------------------------------------------------------------------
    // insert
    // ---------------------------------------------------------------------------

    constexpr bool insert_splices_before_the_cursor() {
        // Ported verbatim from clap_lex/tests/testsuite/lexer.rs::insert.
        raw_args raw{"bin", "a", "b", "c"};
        arg_cursor position = raw.cursor();
        if (!takes(raw, position, os_str{"bin"})) return false;
        if (!takes(raw, position, os_str{"a"})) return false;

        raw.insert(position, {"1", "2", "3"});

        if (!takes(raw, position, os_str{"1"})) return false;
        if (!takes(raw, position, os_str{"2"})) return false;
        if (!takes(raw, position, os_str{"3"})) return false;
        if (!takes(raw, position, os_str{"b"})) return false;
        if (!takes(raw, position, os_str{"c"})) return false;

        arg_cursor replay = raw.cursor();
        return yields(raw.remaining(replay),
                      {os_str{"bin"},
                       os_str{"a"},
                       os_str{"1"},
                       os_str{"2"},
                       os_str{"3"},
                       os_str{"b"},
                       os_str{"c"}});
    }
    static_assert(insert_splices_before_the_cursor());

    constexpr bool insert_takes_a_span_too() {
        raw_args raw{"bin", "tail"};
        const os_str injected[] = {os_str{"x"}, os_str{"y"}};
        raw.insert(arg_cursor{1}, std::span<const os_str>{injected});
        arg_cursor position = raw.cursor();
        return yields(raw.remaining(position),
                      {os_str{"bin"}, os_str{"x"}, os_str{"y"}, os_str{"tail"}});
    }
    static_assert(insert_takes_a_span_too());

    constexpr bool insert_shifts_what_later_cursors_see() {
        // Cursors are indices, not iterators: they keep their number and therefore
        // change meaning. Measured against clap_lex, which does the same.
        raw_args raw{"a", "b", "c"};
        arg_cursor early = arg_cursor{1};
        arg_cursor late  = arg_cursor{2};
        raw.insert(early, {"X", "Y"});
        return peeks(raw, early, os_str{"X"}) && peeks(raw, late, os_str{"Y"}) &&
               peeks(raw, arg_cursor{0}, os_str{"a"});
    }
    static_assert(insert_shifts_what_later_cursors_see());

    constexpr bool insert_at_or_past_the_end_appends() {
        // clap_lex's Vec::splice panics for a cursor past the end; clapp clamps.
        raw_args raw{"a"};
        raw.insert(arg_cursor{1}, {"x"});
        raw.insert(arg_cursor{999}, {"y"});
        arg_cursor position = raw.cursor();
        return yields(raw.remaining(position), {os_str{"a"}, os_str{"x"}, os_str{"y"}});
    }
    static_assert(insert_at_or_past_the_end_appends());

    constexpr bool insert_of_nothing_changes_nothing() {
        raw_args raw{"a", "b"};
        raw.insert(arg_cursor{1}, {});
        raw.insert(arg_cursor{1}, std::span<const os_str>{});
        return raw == raw_args{"a", "b"};
    }
    static_assert(insert_of_nothing_changes_nothing());

    constexpr bool insert_supports_the_multicall_rewrite() {
        // clap's AppSettings::Multicall: read argv[0], then push the applet name back
        // in so the subcommand parser sees it as an ordinary argument.
        raw_args raw{"/usr/bin/busybox", "--verbose"};
        arg_cursor position = raw.cursor();
        raw.next_os(position);
        raw.insert(position, {"busybox"});
        return peeks(raw, position, os_str{"busybox"}) &&
               yields(raw.peek_remaining(position), {os_str{"busybox"}, os_str{"--verbose"}});
    }
    static_assert(insert_supports_the_multicall_rewrite());

    // ---------------------------------------------------------------------------
    // Helpers that the platform entry point relies on
    // ---------------------------------------------------------------------------

    constexpr bool nul_separated_blobs_split_without_a_trailing_hole() {
        using clapp::detail::split_nul_separated;
        const std::vector<os_string> normal = split_nul_separated(std::string_view{"bin\0-v\0", 7});
        const std::vector<os_string> unterminated =
                split_nul_separated(std::string_view{"bin\0-v", 6});
        const std::vector<os_string> with_empty =
                split_nul_separated(std::string_view{"a\0\0b\0", 5});
        return normal.size() == 2 && normal[0].view() == os_str{"bin"} &&
               normal[1].view() == os_str{"-v"} && unterminated.size() == 2 &&
               unterminated[1].view() == os_str{"-v"} && with_empty.size() == 3 &&
               with_empty[1].view().empty() && split_nul_separated("").empty();
    }
    static_assert(nul_separated_blobs_split_without_a_trailing_hole());

    constexpr bool saturating_arithmetic_never_wraps() {
        using clapp::detail::saturating_increment;
        using clapp::detail::saturating_offset;
        constexpr std::size_t most_size     = std::numeric_limits<std::size_t>::max();
        constexpr std::ptrdiff_t least_diff = std::numeric_limits<std::ptrdiff_t>::min();
        return saturating_increment(0) == 1 && saturating_increment(most_size) == most_size &&
               saturating_offset(5, -2) == 3 && saturating_offset(5, 2) == 7 &&
               saturating_offset(1, least_diff) == 0 &&
               saturating_offset(most_size, 1) == most_size && saturating_offset(0, -1) == 0;
    }
    static_assert(saturating_arithmetic_never_wraps());

    // ---------------------------------------------------------------------------
    // The parse loop as a whole
    // ---------------------------------------------------------------------------

    /**
     * Ported from clap_lex/tests/testsuite/lexer.rs::zero_copy_parsing: nothing in
     * here copies an argument, and the collected values still point into `raw`.
     */
    struct parsed_command_line {
        bool ok = false;
        os_str bin_name{};
        bool verbose = false;
        std::vector<os_str> remainder{};
    };

    constexpr parsed_command_line parse(const raw_args& raw) {
        parsed_command_line out;
        arg_cursor position                 = raw.cursor();
        const std::optional<parsed_arg> bin = raw.next(position);
        if (!bin.has_value()) return out;
        out.bin_name = bin.value().to_value_os();

        const std::optional<parsed_arg> first = raw.next(position);
        if (!first.has_value()) {
            out.ok = true;
            return out;
        }

        const std::optional<parsed_arg::long_flag> flag = first.value().to_long();
        if (flag.has_value()) {
            if (!flag.value().first.has_value()) return out;
            if (flag.value().first.value() != "verbose") return out;
            if (flag.value().second.has_value()) return out;
            out.verbose = true;
        } else {
            out.remainder.push_back(first.value().to_value_os());
        }
        for (const os_str value : raw.remaining(position)) out.remainder.push_back(value);
        out.ok = true;
        return out;
    }

    constexpr bool zero_copy_parsing() {
        const raw_args verbose{"bin", "--verbose", "a", "b", "c"};
        const parsed_command_line first = parse(verbose);
        if (!first.ok || !first.verbose || first.bin_name != os_str{"bin"}) return false;
        if (!std::ranges::equal(
                    first.remainder,
                    std::initializer_list<os_str>{os_str{"a"}, os_str{"b"}, os_str{"c"}}))
            return false;

        const raw_args plain             = raw_args{"bin", "a", "b", "c"};
        const parsed_command_line second = parse(plain);
        if (!second.ok || second.verbose) return false;
        if (!std::ranges::equal(
                    second.remainder,
                    std::initializer_list<os_str>{os_str{"a"}, os_str{"b"}, os_str{"c"}}))
            return false;

        // The collected views point into `plain`, not into copies of its arguments.
        return second.remainder[0].data() == plain.items()[1].data();
    }
    static_assert(zero_copy_parsing());

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
//
// These report the compile-time conclusions above and cover what a constant
// evaluation cannot reach: the process environment, and views that must survive
// past the end of one evaluation.
// ---------------------------------------------------------------------------

CLAPP_TEST("a cursor is a value: two of them walk the same list independently") {
    const raw_args raw{"bin", "a", "b"};
    arg_cursor fast = raw.cursor();
    arg_cursor slow = raw.cursor();
    raw.next_os(fast);
    raw.next_os(fast);

    CLAPP_CHECK(raw.peek_os(fast).value() == os_str{"b"});
    CLAPP_CHECK(raw.peek_os(slow).value() == os_str{"bin"});
    CLAPP_CHECK(slow < fast);
}

CLAPP_TEST("peek looks, next takes") {
    const raw_args raw{"one", "two"};
    arg_cursor position = raw.cursor();

    CLAPP_CHECK(raw.peek_os(position).value() == os_str{"one"});
    CLAPP_CHECK(raw.peek_os(position).value() == os_str{"one"});
    CLAPP_CHECK(position == raw.cursor());
    CLAPP_CHECK(raw.next_os(position).value() == os_str{"one"});
    CLAPP_CHECK(raw.next_os(position).value() == os_str{"two"});
    CLAPP_CHECK(!raw.next_os(position).has_value());
    CLAPP_CHECK(raw.is_end(position));
}

CLAPP_TEST("seek reaches the start and the end, and clamps in between") {
    const raw_args raw{"a", "b", "c"};
    arg_cursor position = raw.cursor();

    raw.seek(position, seek_from::end(0));
    CLAPP_CHECK(raw.is_end(position));
    raw.seek(position, seek_from::end(-1));
    CLAPP_CHECK(raw.peek_os(position).value() == os_str{"c"});
    raw.seek(position, seek_from::start(0));
    CLAPP_CHECK(raw.peek_os(position).value() == os_str{"a"});
    raw.seek(position, seek_from::current(99));
    CLAPP_CHECK(position.index() == raw.size());
    raw.seek(position, seek_from::current(-99));
    CLAPP_CHECK(position.index() == 0);
}

CLAPP_TEST("insert splices arguments in and parsing continues through them") {
    raw_args raw{"bin", "a", "b", "c"};
    arg_cursor position = raw.cursor();
    raw.next_os(position);
    raw.next_os(position);
    raw.insert(position, {"1", "2", "3"});

    std::vector<std::string> seen;
    while (const std::optional<os_str> item = raw.next_os(position))
        seen.push_back(item.value().to_string_lossy());

    CLAPP_CHECK(seen == std::vector<std::string>({"1", "2", "3", "b", "c"}));
    CLAPP_CHECK(raw.size() == 7);
}

CLAPP_TEST("remaining after the escape hands back the tail verbatim") {
    const raw_args raw{"bin", "--", "--looks-like-a-flag", "-x"};
    arg_cursor position = raw.cursor();
    while (const std::optional<parsed_arg> arg = raw.next(position))
        if (arg.value().is_escape()) break;

    std::vector<std::string> tail;
    for (const os_str value : raw.remaining(position)) tail.push_back(value.to_string_lossy());

    CLAPP_CHECK(tail == std::vector<std::string>({"--looks-like-a-flag", "-x"}));
    CLAPP_CHECK(raw.is_end(position));
}

CLAPP_TEST("an empty command line and a one-element one both terminate cleanly") {
    const raw_args nothing;
    arg_cursor at_nothing = nothing.cursor();
    CLAPP_CHECK(nothing.empty());
    CLAPP_CHECK(nothing.is_end(at_nothing));
    CLAPP_CHECK(!nothing.next(at_nothing).has_value());
    CLAPP_CHECK(std::ranges::empty(nothing.remaining(at_nothing)));

    const raw_args only{"bin"};
    arg_cursor at_only = only.cursor();
    CLAPP_CHECK(!only.is_end(at_only));
    CLAPP_CHECK(only.next(at_only).value().to_value_os() == os_str{"bin"});
    CLAPP_CHECK(only.is_end(at_only));
}

CLAPP_TEST("views handed out by raw_args stay valid for as long as it does") {
    const raw_args raw{"bin",
                       "--output=file.txt",
                       "\xFF"
                       "raw"};
    arg_cursor position                    = raw.cursor();
    const std::optional<parsed_arg> bin    = raw.next(position);
    const std::optional<parsed_arg> option = raw.next(position);
    const std::optional<parsed_arg> odd    = raw.next(position);

    CLAPP_CHECK(bin.value().to_value_os() == os_str{"bin"});
    CLAPP_CHECK(option.value().is_long());
    CLAPP_CHECK(option.value().to_long().value().second.value() == os_str{"file.txt"});
    CLAPP_CHECK(!odd.value().to_value().has_value());
    CLAPP_CHECK(odd.value().to_value_os() == os_str{"\xFF"
                                                    "raw"});

    // Still pointing into raw_args' own storage, not at a copy.
    CLAPP_CHECK(bin.value().to_value_os().data() == raw.items()[0].data());
}

#ifndef _WIN32
CLAPP_TEST("the Linux entry point's file reader pumps a whole /proc-shaped blob") {
    // `/proc/self/cmdline` reports a size of 0 and only yields its bytes to a
    // read() loop, so the loop is the part worth testing. It cannot be tested
    // against the real file anywhere but Linux; it can be tested against a
    // fabricated one anywhere POSIX, which is what upgrades the Linux branch of
    // from_args() from "read carefully" to "both halves exercised".
    const char* const path = "clapp_raw_args_cmdline_fixture";
    const std::string blob{"bin\0--output=file.txt\0\xFF"
                           "raw\0",
                           27};
    {
        std::FILE* const file = std::fopen(path, "wb");
        CLAPP_CHECK(file != nullptr);
        if (file == nullptr) return;
        CLAPP_CHECK(std::fwrite(blob.data(), 1, blob.size(), file) == blob.size());
        CLAPP_CHECK(std::fclose(file) == 0);
    }

    const std::string read_back = clapp::detail::read_all_bytes(path);
    std::remove(path);

    CLAPP_CHECK(read_back == blob);

    const raw_args rebuilt{std::from_range, clapp::detail::split_nul_separated(read_back)};
    CLAPP_CHECK(rebuilt.size() == 3);
    CLAPP_CHECK(rebuilt.items()[0].view() == os_str{"bin"});
    CLAPP_CHECK(rebuilt.items()[1].view() == os_str{"--output=file.txt"});
    CLAPP_CHECK(rebuilt.items()[2].view() == os_str{"\xFF"
                                                    "raw"});

    // A path that does not exist is "no command line", not a crash.
    CLAPP_CHECK(clapp::detail::read_all_bytes("/nonexistent/clapp/cmdline").empty());
}
#endif

CLAPP_TEST("from_args reads the running process, argv[0] first") {
    const raw_args live = raw_args::from_args();
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
    CLAPP_CHECK(live.size() >= 1);
    CLAPP_CHECK(!live.items()[0].empty());
    arg_cursor position = live.cursor();
    CLAPP_CHECK(live.next_os(position).value() == live.items()[0].view());
#else
    // No supported source on this platform: an empty list, not a crash.
    CLAPP_CHECK(live.empty());
#endif
}

CLAPP_TEST("every compile-time case above is pinned by a static_assert") {
    CLAPP_CHECK(insert_splices_before_the_cursor());
    CLAPP_CHECK(seek_clamps_into_range());
    CLAPP_CHECK(next_overshoots_the_end_like_clap_lex());
    CLAPP_CHECK(zero_copy_parsing());
}
