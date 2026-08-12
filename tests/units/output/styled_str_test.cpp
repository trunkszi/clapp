#include <clapp/output/styled_str.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::style_class;
    using clapp::styled_span;
    using clapp::styled_str;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    consteval bool default_is_empty() {
        const styled_str message;
        return message.empty() && message.span_count() == 0 && message.size() == 0 &&
               message.to_string().empty();
    }

    static_assert(default_is_empty());

    consteval bool plain_constructor_makes_one_run() {
        const styled_str message{"hello"};
        return message.span_count() == 1 && message.spans()[0].class_ == style_class::plain &&
               message.spans()[0].text == "hello" && message.size() == 5;
    }

    static_assert(plain_constructor_makes_one_run());

    consteval bool class_constructor_keeps_the_class() {
        const styled_str message{style_class::invalid, "--nope"};
        return message.span_count() == 1 && message.spans()[0].class_ == style_class::invalid &&
               message.to_string() == "--nope";
    }

    static_assert(class_constructor_keeps_the_class());

    // An empty text never produces a span, whichever constructor is used — otherwise an
    // empty run would make two equal messages compare unequal.
    consteval bool empty_text_never_makes_a_span() {
        return styled_str{""}.empty() && styled_str{style_class::error, ""}.empty();
    }

    static_assert(empty_text_never_makes_a_span());

    // ---------------------------------------------------------------------------
    // The merging invariant
    // ---------------------------------------------------------------------------

    consteval bool adjacent_same_class_runs_merge() {
        styled_str piecewise;
        piecewise.push(style_class::literal, "--he")
                .push(style_class::literal, "lp")
                .push(style_class::literal, "");
        const styled_str whole{style_class::literal, "--help"};
        return piecewise == whole && piecewise.span_count() == 1;
    }

    static_assert(adjacent_same_class_runs_merge());

    // ...and runs of *different* classes do not, even when the text would concatenate to
    // the same bytes. This is the pair a plausible bug would confuse: a merge keyed on
    // text rather than class, or on nothing at all, passes the assertion above and fails
    // this one.
    consteval bool different_class_runs_stay_separate() {
        styled_str two;
        two.push(style_class::literal, "--help").push(style_class::invalid, "--nope");

        styled_str one;
        one.push(style_class::literal, "--help--nope");

        return two.span_count() == 2 && one.span_count() == 1 && two != one &&
               two.to_string() == one.to_string();
    }

    static_assert(different_class_runs_stay_separate());

    // A class that reappears after an interruption starts a new run: merging is adjacency,
    // not grouping.
    consteval bool interrupted_class_starts_a_new_run() {
        styled_str message;
        message.push(style_class::valid, "a").push_plain("-").push(style_class::valid, "b");
        return message.span_count() == 3 && message.text_of(style_class::valid) == "ab" &&
               message.to_string() == "a-b";
    }

    static_assert(interrupted_class_starts_a_new_run());

    // ---------------------------------------------------------------------------
    // append()
    // ---------------------------------------------------------------------------

    consteval bool append_preserves_classes_and_merges_at_the_seam() {
        styled_str head;
        head.push(style_class::error, "error:").push_plain(" ");

        styled_str tail;
        tail.push_plain("nope").push(style_class::invalid, "!");

        head.append(tail);
        return head.to_string() == "error: nope!" && head.span_count() == 3 &&
               head.spans()[0].class_ == style_class::error &&
               head.spans()[1].text == " nope"  // the two plain runs merged
               && head.spans()[2].class_ == style_class::invalid;
    }

    static_assert(append_preserves_classes_and_merges_at_the_seam());

    // Self-append must not read a reallocated buffer. The fragment list is copied first;
    // this is the assertion that keeps that copy from being "simplified" away.
    consteval bool self_append_doubles_the_text() {
        styled_str message;
        message.push(style_class::valid, "ab").push_plain("cd");
        message.append(message);
        return message.to_string() == "abcdabcd";
    }

    static_assert(self_append_doubles_the_text());

    consteval bool appending_nothing_changes_nothing() {
        styled_str message{style_class::header, "Options:"};
        const styled_str before = message;
        message.append(styled_str{});
        return message == before;
    }

    static_assert(appending_nothing_changes_nothing());

    // ---------------------------------------------------------------------------
    // Queries
    // ---------------------------------------------------------------------------

    consteval bool size_counts_bytes_not_runs() {
        styled_str message;
        message.push(style_class::valid, "ab").push(style_class::invalid, "cde");
        return message.span_count() == 2 && message.size() == 5;
    }

    static_assert(size_counts_bytes_not_runs());

    consteval bool text_of_collects_one_class() {
        styled_str message;
        message.push_plain("invalid value '")
                .push(style_class::invalid, "gren")
                .push_plain("' for '")
                .push(style_class::literal, "--color")
                .push_plain("'");
        return message.text_of(style_class::invalid) == "gren" &&
               message.text_of(style_class::literal) == "--color" &&
               message.text_of(style_class::header).empty();
    }

    static_assert(text_of_collects_one_class());

    // contains() flattens first, so a needle that straddles a class boundary is still
    // found. Error messages quote the offending token, and the quotes are plain while the
    // token is not — every "the message says X" assertion depends on this.
    consteval bool contains_spans_run_boundaries() {
        styled_str message;
        message.push_plain("value '").push(style_class::invalid, "gren").push_plain("' is wrong");
        return message.contains("'gren'") && message.contains("") && !message.contains("green");
    }

    static_assert(contains_spans_run_boundaries());

    consteval bool clear_empties() {
        styled_str message{style_class::error, "error:"};
        message.clear();
        return message.empty() && message == styled_str{};
    }

    static_assert(clear_empties());

    // ---------------------------------------------------------------------------
    // push_decimal
    // ---------------------------------------------------------------------------

    consteval bool push_decimal_spells_numbers() {
        styled_str message;
        message.push_decimal(style_class::valid, 0)
                .push_plain("/")
                .push_decimal(style_class::valid, 42)
                .push_plain("/")
                .push_decimal(style_class::invalid, -7);
        return message.to_string() == "0/42/-7" && message.text_of(style_class::invalid) == "-7";
    }

    static_assert(push_decimal_spells_numbers());

    consteval bool push_decimal_handles_the_extreme() {
        // Negating PTRDIFF_MIN in the signed domain is UB; the implementation negates in
        // the unsigned domain instead. A constant expression is the one place that would
        // be diagnosed rather than silently wrong.
        styled_str message;
        message.push_decimal(style_class::plain, -9223372036854775807 - 1);
        return message.to_string().size() == 20 && message.to_string()[0] == '-';
    }

    static_assert(push_decimal_handles_the_extreme());

    // ---------------------------------------------------------------------------
    // styled_span
    // ---------------------------------------------------------------------------

    consteval bool spans_compare_by_class_and_text() {
        // Not `const`: GCC 16.1.0 rejects initializing a *const* std::string from a string
        // literal inside a constant expression — basic_string::_M_init_local_buf() writes
        // through the object being initialized, which it reports as "modifying a const
        // object". Nothing to do with clapp; the locals are simply non-const.
        styled_span a{.class_ = style_class::valid, .text = "x"};
        styled_span b{.class_ = style_class::valid, .text = "x"};
        styled_span c{.class_ = style_class::invalid, .text = "x"};
        return a == b && a != c;
    }

    static_assert(spans_compare_by_class_and_text());

    // A value-initialized span is plain, matching style_class's enumerator zero.
    static_assert(styled_span{}.class_ == style_class::plain);

    // ---------------------------------------------------------------------------
    // Runtime mirror
    // ---------------------------------------------------------------------------
    //
    // The cases below report compile-time conclusions and cover the one thing that cannot
    // cross the consteval boundary: a styled_str that outlives the expression that built
    // it, i.e. the shape every renderer actually returns.

    CLAPP_TEST("styled_str: merging invariant holds at run time too") {
        styled_str piecewise;
        piecewise.push(style_class::literal, "--he").push(style_class::literal, "lp");
        CLAPP_CHECK(piecewise == styled_str(style_class::literal, "--help"));
        CLAPP_CHECK(piecewise.span_count() == 1);
        CLAPP_CHECK(adjacent_same_class_runs_merge());
        CLAPP_CHECK(different_class_runs_stay_separate());
    }

    CLAPP_TEST("styled_str: survives being returned from a function") {
        const styled_str message = [] {
            styled_str out;
            out.push(style_class::error, "error:")
                    .push_plain(" unexpected argument ")
                    .push(style_class::invalid, "--verbose");
            return out;
        }();

        CLAPP_CHECK(message.to_string() == "error: unexpected argument --verbose");
        CLAPP_CHECK(message.text_of(style_class::invalid) == "--verbose");
        CLAPP_CHECK(message.span_count() == 3);
        CLAPP_CHECK(message.size() == 36);
    }

    CLAPP_TEST("styled_str: copies and moves are independent") {
        styled_str original{style_class::valid, "keep"};
        styled_str copy = original;
        copy.push_plain("-more");

        CLAPP_CHECK(original.to_string() == "keep");
        CLAPP_CHECK(copy.to_string() == "keep-more");

        const styled_str moved = std::move(copy);
        CLAPP_CHECK(moved.to_string() == "keep-more");
    }

    CLAPP_TEST("styled_str: spans() is a real view of the storage") {
        styled_str message;
        message.push(style_class::header, "Usage:").push_plain(" prog");

        const std::span<const styled_span> view = message.spans();
        CLAPP_CHECK(view.size() == 2);
        CLAPP_CHECK(view[0].class_ == style_class::header);
        CLAPP_CHECK(view[0].text == "Usage:");
        CLAPP_CHECK(view[1].text == " prog");
    }

}  // namespace
