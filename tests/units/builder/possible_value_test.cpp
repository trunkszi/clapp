#include <clapp/builder/possible_value.hpp>
#include <clapp/util/id.hpp>

#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

    using clapp::arg_id;
    using clapp::possible_value;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Layout — the structural-type contract
    // ---------------------------------------------------------------------------

    static_assert(std::is_aggregate_v<possible_value>);
    static_assert(std::is_trivially_copyable_v<possible_value>);

    // A structural type may have no private non-static data members, which is why the
    // storage is public and why none of the obvious standard-library members could be
    // used. The header carries a probe of its own; this one restates it from outside, so
    // the property is pinned by the test suite and not only by the header that claims it.
    template<possible_value>
    struct structural_probe {};
    using possible_value_is_structural = structural_probe<possible_value{}>;

    // The three members that were ruled out, and why. Each was measured on GCC 16.1.0.
    static_assert(!std::is_same_v<decltype(possible_value::help_text), std::string_view>);
    static_assert(std::is_same_v<decltype(possible_value::name), arg_id>);
    static_assert(std::is_same_v<decltype(possible_value::alias_data), const arg_id*>);

    // ---------------------------------------------------------------------------
    // Defaults
    // ---------------------------------------------------------------------------

    static_assert(possible_value{}.get_name().empty());
    static_assert(possible_value{}.get_help() == std::nullopt);
    static_assert(possible_value{}.get_aliases().empty());
    static_assert(!possible_value{}.is_hide_set());
    static_assert(!possible_value{}.should_show_help());
    static_assert(!possible_value{}.needs_quoting());

    // ---------------------------------------------------------------------------
    // Construction — aggregate, wither chain, and the consteval factory
    // ---------------------------------------------------------------------------

    constexpr possible_value fast{.name = arg_id{"fast"}};
    static_assert(fast.get_name() == "fast"sv);
    static_assert(fast.get_help() == std::nullopt);
    static_assert(!fast.is_hide_set());

    constexpr possible_value slow =
            possible_value{.name = arg_id{"slow"}}.with_help("slower than fast");
    static_assert(slow.get_name() == "slow"sv);
    static_assert(slow.get_help() == "slower than fast"sv);
    static_assert(slow.should_show_help());

    constexpr possible_value secret = possible_value{.name = arg_id{"secret speed"}}.with_hide();
    static_assert(secret.is_hide_set());
    static_assert(secret.get_visible_name() == std::nullopt);
    static_assert(!secret.should_show_help());
    static_assert(secret.needs_quoting());

    // A hidden value with help still does not get a help line: hiding wins.
    static_assert(
            !possible_value{.name = arg_id{"x"}}.with_help("h").with_hide().should_show_help());

    // with_hide(false) is the way back.
    static_assert(!secret.with_hide(false).is_hide_set());
    static_assert(secret.with_hide(false).get_visible_name() == "secret speed"sv);

    // An empty help string is "no help", not "help that is empty" — otherwise every value
    // built by reflection from an enumerator with no `[[= clapp::value{}]]` annotation
    // would claim a blank help line.
    static_assert(slow.with_help("").get_help() == std::nullopt);
    static_assert(!slow.with_help("").should_show_help());

    // The consteval factory lifts both strings into static storage.
    constexpr possible_value lifted = clapp::make_possible_value("always", "colour every time");
    static_assert(lifted.get_name() == "always"sv);
    static_assert(lifted.get_help() == "colour every time"sv);
    static_assert(clapp::make_possible_value("never").get_help() == std::nullopt);

    // ---------------------------------------------------------------------------
    // Aliases
    // ---------------------------------------------------------------------------

    constexpr auto slow_aliases =
            clapp::make_static_aliases(std::array{"not-fast"sv, "snake-like"sv});
    constexpr possible_value aliased =
            clapp::make_possible_value("slow").with_aliases(slow_aliases);

    static_assert(aliased.get_aliases().size() == 2);
    static_assert(aliased.get_aliases()[0] == "not-fast"sv);
    static_assert(aliased.get_aliases()[1] == "snake-like"sv);

    // ---------------------------------------------------------------------------
    // matches — clap's doctest, line for line
    // ---------------------------------------------------------------------------

    constexpr possible_value clap_example = clapp::make_possible_value("fast").with_aliases(
            clapp::make_static_aliases(std::array{"not-slow"sv}));

    static_assert(clap_example.matches("fast", false));
    static_assert(clap_example.matches("not-slow", false));
    static_assert(clap_example.matches("FAST", true));
    static_assert(!clap_example.matches("FAST", false));

    // Aliases obey ignore_case too, and a near miss is still a miss.
    static_assert(clap_example.matches("NOT-SLOW", true));
    static_assert(!clap_example.matches("not_slow", false));
    static_assert(!clap_example.matches("not_slow", true));
    static_assert(!clap_example.matches("fas", false));
    static_assert(!clap_example.matches("fastt", false));
    static_assert(!clap_example.matches("", false));

    // A hidden value still matches: hiding removes it from help, not from the grammar.
    static_assert(secret.matches("secret speed", false));

    // Case folding is ASCII-only, matching clap's eq_ignore_case. A non-ASCII byte is
    // compared as-is, which keeps this consistent with the way the lexer compares bytes.
    constexpr possible_value accented = clapp::make_possible_value("caf\xc3\xa9");
    static_assert(accented.matches("caf\xc3\xa9", true));
    static_assert(!accented.matches("CAF\xc3\x89", true));

    // ---------------------------------------------------------------------------
    // needs_quoting — the input to the renderer's quoting decision
    // ---------------------------------------------------------------------------

    static_assert(!clapp::make_possible_value("fast").needs_quoting());
    static_assert(clapp::make_possible_value("secret speed").needs_quoting());
    static_assert(clapp::make_possible_value("two\tcolumns").needs_quoting());
    static_assert(!clapp::make_possible_value("kebab-case-name").needs_quoting());

    // ---------------------------------------------------------------------------
    // Equality is by content, not by address
    //
    // The defaulted operator== would compare help_text and alias_data as pointers, so
    // two values with identical help built from different string literals would compare
    // unequal. That is the bug this assertion exists to prevent.
    // ---------------------------------------------------------------------------

    constexpr possible_value help_from_literal =
            possible_value{.name = arg_id{"x"}}.with_help("same words");
    constexpr possible_value help_from_static = clapp::make_possible_value("x", "same words");

    static_assert(help_from_literal.help_text != help_from_static.help_text);  // different storage
    static_assert(help_from_literal == help_from_static);                      // same content

    static_assert(fast != slow);
    static_assert(clap_example != clapp::make_possible_value("fast"));  // aliases differ
    static_assert(secret != secret.with_hide(false));                   // hide differs

    // ---------------------------------------------------------------------------
    // Reaching static storage
    //
    // The property command_of<T>() depends on: a table of possible values built during
    // constant evaluation and promoted into .rodata.
    // ---------------------------------------------------------------------------

    consteval std::array<possible_value, 3> build_color_choices() {
        return {clapp::make_possible_value("auto", "colour when the stream is a terminal"),
                clapp::make_possible_value("always"),
                clapp::make_possible_value("never")};
    }

    constexpr auto color_choices = std::define_static_array(build_color_choices());

    static_assert(color_choices.size() == 3);
    static_assert(color_choices[0].get_name() == "auto"sv);
    static_assert(color_choices[0].should_show_help());
    static_assert(color_choices[1].get_name() == "always"sv);
    static_assert(!color_choices[1].should_show_help());
    static_assert(color_choices[2].matches("NEVER", true));

    // A possible_value is also usable as a non-type template parameter, the other half of
    // the structural-type contract.
    template<possible_value V>
    struct tagged {
        static constexpr std::string_view name = V.get_name();
    };
    static_assert(tagged<clapp::make_possible_value("tagged")>::name == "tagged"sv);

}  // namespace

CLAPP_TEST("possible_value: matches the name and every alias") {
    CLAPP_CHECK(clap_example.matches("fast", false));
    CLAPP_CHECK(clap_example.matches("not-slow", false));
    CLAPP_CHECK(!clap_example.matches("slow", false));
}

CLAPP_TEST("possible_value: ignore_case folds ASCII only") {
    CLAPP_CHECK(clap_example.matches("FAST", true));
    CLAPP_CHECK(!clap_example.matches("FAST", false));
    CLAPP_CHECK(accented.matches("caf\xc3\xa9", true));
    CLAPP_CHECK(!accented.matches("CAF\xc3\x89", true));
}

CLAPP_TEST("possible_value: matches a string the user typed") {
    // std::string is a transient allocation and cannot be a constexpr variable, so
    // matching against an owning, runtime-built string is checked only here.
    std::string typed;
    typed += "not";
    typed += "-slow";
    CLAPP_CHECK(clap_example.matches(typed, false));

    std::string shouted = typed;
    for (char& c : shouted) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    CLAPP_CHECK(!clap_example.matches(shouted, false));
    CLAPP_CHECK(clap_example.matches(shouted, true));
}

CLAPP_TEST("possible_value: a hidden value is still selectable") {
    CLAPP_CHECK(secret.matches("secret speed", false));
    CLAPP_CHECK(secret.get_visible_name() == std::nullopt);
    CLAPP_CHECK(secret.needs_quoting());
}

CLAPP_TEST("possible_value: help lines come only from visible values that have help") {
    const std::vector<possible_value> values{fast, slow, secret};
    std::vector<std::string_view> with_help;
    for (const possible_value& value : values) {
        if (value.should_show_help()) with_help.push_back(value.get_name());
    }
    CLAPP_CHECK(with_help.size() == 1);
    CLAPP_CHECK(with_help.front() == "slow");
}

CLAPP_TEST("possible_value: the visible names are what help lists") {
    const std::vector<possible_value> values{fast, slow, secret};
    std::vector<std::string_view> visible;
    for (const possible_value& value : values) {
        if (const std::optional<std::string_view> name = value.get_visible_name()) {
            visible.push_back(*name);
        }
    }
    CLAPP_CHECK(visible.size() == 2);
    CLAPP_CHECK(visible[0] == "fast");
    CLAPP_CHECK(visible[1] == "slow");
}
