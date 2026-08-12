#include <clapp/error/context.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using clapp::all_context_kinds;
    using clapp::context_kind;
    using clapp::context_kind_count;
    using clapp::context_value;
    using clapp::context_value_kind;
    using clapp::cow_str;
    using clapp::describe;
    using clapp::name_of;
    using clapp::style_class;
    using clapp::styled_str;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // context_kind
    // ---------------------------------------------------------------------------

    static_assert(context_kind_count == 17);
    static_assert(all_context_kinds.size() == context_kind_count);
    static_assert(all_context_kinds.front() == context_kind::invalid_subcommand);
    static_assert(all_context_kinds.back() == context_kind::custom);

    consteval bool every_context_kind_appears_exactly_once() {
        for (std::size_t value = 0; value < context_kind_count; ++value) {
            std::size_t seen = 0;
            for (const context_kind kind : all_context_kinds) {
                if (static_cast<std::size_t>(kind) == value) ++seen;
            }
            if (seen != 1) return false;
        }
        return true;
    }

    static_assert(every_context_kind_appears_exactly_once());

    // clap's labels, verbatim. The two without one carry text that is already a message.
    static_assert(describe(context_kind::invalid_arg) == "Invalid Argument"sv);
    static_assert(describe(context_kind::prior_arg) == "Prior Argument"sv);
    static_assert(describe(context_kind::valid_value) == "Valid Value"sv);
    static_assert(describe(context_kind::actual_num_values) == "Actual Number of Values"sv);
    static_assert(describe(context_kind::min_values) == "Minimum Number of Values"sv);
    static_assert(describe(context_kind::suggested_arg) == "Suggested Argument"sv);
    static_assert(describe(context_kind::usage) == std::nullopt);
    static_assert(describe(context_kind::custom) == std::nullopt);

    static_assert(name_of(context_kind::invalid_arg) == "invalid-arg"sv);
    static_assert(name_of(context_kind::expected_num_values) == "expected-num-values"sv);
    static_assert(name_of(context_kind::usage) == "usage"sv);

    consteval bool context_names_are_nonempty_and_distinct() {
        for (std::size_t i = 0; i < all_context_kinds.size(); ++i) {
            if (name_of(all_context_kinds[i]).empty()) return false;
            for (std::size_t j = i + 1; j < all_context_kinds.size(); ++j) {
                if (name_of(all_context_kinds[i]) == name_of(all_context_kinds[j])) return false;
            }
        }
        return true;
    }

    static_assert(context_names_are_nonempty_and_distinct());

    // ---------------------------------------------------------------------------
    // cow_str
    // ---------------------------------------------------------------------------

    consteval bool default_cow_str_is_empty_and_owning() {
        const cow_str value;
        return value.empty() && value.size() == 0 && value.is_owned() && value.view().empty();
    }

    static_assert(default_cow_str_is_empty_and_owning());

    // The populated side of the flag, in both states. This is the assertion CLAUDE.md
    // trap 10 asks for: without it, a cow_str that always reported `is_owned() == false`
    // would pass every content comparison in the suite.
    consteval bool both_states_are_reachable_and_distinguishable() {
        const cow_str borrowed = cow_str::borrowed("--verbose");
        const cow_str owned    = cow_str::owned("--verbose");
        return !borrowed.is_owned() && owned.is_owned();
    }

    static_assert(both_states_are_reachable_and_distinguishable());

    // ...and the flag is *not* part of identity: the whole point of borrowing is that the
    // rest of the library cannot tell.
    consteval bool the_two_states_compare_equal() {
        return cow_str::borrowed("--verbose") == cow_str::owned("--verbose") &&
               cow_str::borrowed("--verbose") == "--verbose"sv &&
               cow_str::owned("--verbose").view() == "--verbose"sv &&
               cow_str::borrowed("a") != cow_str::borrowed("b");
    }

    static_assert(the_two_states_compare_equal());

    consteval bool a_moved_in_string_owns() {
        std::string text;
        for (const char byte : "computed"sv) text.push_back(byte);
        const cow_str value{std::move(text)};
        return value.is_owned() && value == "computed"sv && value.size() == 8;
    }

    static_assert(a_moved_in_string_owns());

    // An owning copy really copies: mutating the source afterwards must not reach it.
    // (Constant evaluation cannot observe a dangling borrow, which is exactly why the
    // borrowed side is a documented promise rather than a checked one.)
    consteval bool owned_is_independent_of_its_source() {
        std::string source;
        for (const char byte : "abc"sv) source.push_back(byte);
        const cow_str copy = cow_str::owned(source);
        source.push_back('!');
        return copy == "abc"sv && source == "abc!";
    }

    static_assert(owned_is_independent_of_its_source());

    // Empty is a legitimate value, not a sentinel for "absent" — clapp::error::empty_value
    // builds exactly this, and the renderer branches on it.
    consteval bool empty_is_a_value() {
        const cow_str borrowed_empty = cow_str::borrowed("");
        return borrowed_empty.empty() && !borrowed_empty.is_owned() && borrowed_empty == cow_str{};
    }

    static_assert(empty_is_a_value());

    // ---------------------------------------------------------------------------
    // context_value: the discriminant
    // ---------------------------------------------------------------------------

    static_assert(context_value{}.kind() == context_value_kind::none);
    static_assert(context_value::none().kind() == context_value_kind::none);

    consteval bool kind_matches_the_alternative_built() {
        return context_value::boolean(false).kind() == context_value_kind::boolean &&
               context_value::string(cow_str::borrowed("x")).kind() == context_value_kind::string &&
               context_value::strings({}).kind() == context_value_kind::strings &&
               context_value::styled(styled_str{"x"}).kind() == context_value_kind::styled &&
               context_value::styled_list({}).kind() == context_value_kind::styled_list &&
               context_value::number(-1).kind() == context_value_kind::number;
    }

    static_assert(kind_matches_the_alternative_built());

    // ---------------------------------------------------------------------------
    // context_value: the accessors
    // ---------------------------------------------------------------------------

    consteval bool accessors_answer_for_their_own_alternative() {
        return context_value::boolean(true).as_bool() == true &&
               context_value::number(7).as_number() == 7 &&
               context_value::string(cow_str::borrowed("--n")).as_string() == "--n"sv;
    }

    static_assert(accessors_answer_for_their_own_alternative());

    // ...and only for their own. The pair that matters is as_string() against a
    // single-element Strings: clap's InvalidArg is a String for one error kind and a
    // Strings for another, and an accessor that quietly unwrapped the one-element case
    // would make write_dynamic_context() take the wrong branch.
    consteval bool accessors_refuse_the_wrong_alternative() {
        std::vector<cow_str> one;
        one.push_back(cow_str::borrowed("--only"));
        const context_value strings = context_value::strings(std::move(one));

        return strings.as_string() == std::nullopt && strings.as_number() == std::nullopt &&
               strings.as_bool() == std::nullopt && strings.as_strings().size() == 1 &&
               context_value::string(cow_str::borrowed("--only")).as_strings().empty() &&
               context_value::number(1).as_bool() == std::nullopt &&
               context_value::boolean(true).as_number() == std::nullopt &&
               context_value::none().as_string() == std::nullopt;
    }

    static_assert(accessors_refuse_the_wrong_alternative());

    // A `strings` holding nothing and a non-`strings` both yield an empty span, so kind()
    // is the only way to tell them apart. Stated so the ambiguity is a documented
    // property rather than a surprise at a call site.
    consteval bool empty_span_is_ambiguous_and_kind_resolves_it() {
        const context_value empty_list = context_value::strings({});
        const context_value not_a_list = context_value::number(0);
        return empty_list.as_strings().empty() && not_a_list.as_strings().empty() &&
               empty_list.kind() != not_a_list.kind();
    }

    static_assert(empty_span_is_ambiguous_and_kind_resolves_it());

    consteval bool styled_accessors_round_trip() {
        const styled_str usage{style_class::usage, "Usage: prog [OPTIONS]"};
        const context_value value = context_value::styled(usage);

        std::vector<styled_str> tips;
        tips.push_back(styled_str{style_class::valid, "tip one"});
        tips.push_back(styled_str{style_class::valid, "tip two"});
        const context_value list = context_value::styled_list(std::move(tips));

        return value.as_styled() == usage && value.as_styled_list().empty() &&
               list.as_styled_list().size() == 2 && list.as_styled() == std::nullopt &&
               list.as_styled_list()[1].to_string() == "tip two";
    }

    static_assert(styled_accessors_round_trip());

    // ---------------------------------------------------------------------------
    // context_value: to_string(), clap's Display
    // ---------------------------------------------------------------------------

    consteval bool to_string_matches_claps_display() {
        std::vector<cow_str> names;
        names.push_back(cow_str::borrowed("--a"));
        names.push_back(cow_str::borrowed("--b"));

        std::vector<styled_str> tips;
        tips.push_back(styled_str{"first"});
        tips.push_back(styled_str{"second"});

        return context_value::none().to_string().empty() &&
               context_value::boolean(true).to_string() == "true" &&
               context_value::boolean(false).to_string() == "false" &&
               context_value::number(-42).to_string() == "-42" &&
               context_value::string(cow_str::borrowed("--n")).to_string() == "--n" &&
               context_value::strings(std::move(names)).to_string() == "--a, --b" &&
               context_value::styled(styled_str{style_class::valid, "hi"}).to_string() == "hi" &&
               context_value::styled_list(std::move(tips)).to_string() == "first, second";
    }

    static_assert(to_string_matches_claps_display());

    // ---------------------------------------------------------------------------
    // context_value: equality
    // ---------------------------------------------------------------------------

    consteval bool equality_is_by_alternative_and_content() {
        std::vector<cow_str> left;
        left.push_back(cow_str::borrowed("x"));
        std::vector<cow_str> right;
        right.push_back(cow_str::owned("x"));

        // Same content, one borrowed and one owning: equal, as cow_str's own == says.
        const bool lists_agree =
                context_value::strings(std::move(left)) == context_value::strings(std::move(right));

        // Different alternatives that stringify identically must still differ — the pair a
        // "compare to_string()" shortcut would confuse.
        const bool shapes_differ =
                context_value::string(cow_str::borrowed("1")) != context_value::number(1);

        return lists_agree && shapes_differ && context_value::none() == context_value{} &&
               context_value::number(1) != context_value::number(2);
    }

    static_assert(equality_is_by_alternative_and_content());

    // ---------------------------------------------------------------------------
    // Runtime mirror
    // ---------------------------------------------------------------------------

    CLAPP_TEST("context_kind: the enumeration is complete and named") {
        CLAPP_CHECK(every_context_kind_appears_exactly_once());
        CLAPP_CHECK(context_names_are_nonempty_and_distinct());
        CLAPP_CHECK(all_context_kinds.size() == 17);
    }

    CLAPP_TEST("cow_str: borrowing a runtime string_view does not copy it") {
        // The case constant evaluation cannot reach: a borrow whose target is a real
        // object with an address. Content and flag are both checked, and the borrowed view
        // is checked to alias — data() equality is the only direct evidence that nothing
        // was copied.
        const std::string owner = "--verbose";
        const cow_str borrow    = cow_str::borrowed(owner);
        const cow_str copy      = cow_str::owned(owner);

        CLAPP_CHECK(!borrow.is_owned());
        CLAPP_CHECK(copy.is_owned());
        CLAPP_CHECK(borrow == copy);
        CLAPP_CHECK(borrow.view().data() == owner.data());
        CLAPP_CHECK(copy.view().data() != owner.data());
    }

    CLAPP_TEST("cow_str: an owning value survives its source") {
        cow_str kept;
        {
            std::string transient = "temporary";
            kept                  = cow_str::owned(transient);
            transient.clear();
        }
        CLAPP_CHECK(kept.is_owned());
        CLAPP_CHECK(kept == "temporary"sv);
    }

    CLAPP_TEST("context_value: alternatives round-trip at run time") {
        CLAPP_CHECK(kind_matches_the_alternative_built());
        CLAPP_CHECK(accessors_refuse_the_wrong_alternative());
        CLAPP_CHECK(to_string_matches_claps_display());

        std::vector<cow_str> values;
        values.push_back(cow_str::borrowed("red"));
        values.push_back(cow_str::borrowed("green"));
        const context_value listed = context_value::strings(std::move(values));

        CLAPP_CHECK(listed.kind() == context_value_kind::strings);
        const std::span<const clapp::cow_str> view = listed.as_strings();
        CLAPP_CHECK(view.size() == 2);
        CLAPP_CHECK(view[0] == "red"sv);
        CLAPP_CHECK(listed.to_string() == "red, green");
    }

}  // namespace
