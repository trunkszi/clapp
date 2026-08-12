#include <clapp/builder/action.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/detail/std_meta.hpp>
#include <clapp/meta/annotations.hpp>

#include "support/check.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::tri;
    using clapp::value_range;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // One action type, not two
    //
    // The single most important property in this file: the annotation payload the DSL
    // writes and the value the builder reads are the same type. Were they two, a
    // mismatch in enumerator order would be a silent misparse rather than a compile
    // error, and no test could catch it after the fact.
    // ---------------------------------------------------------------------------

    static_assert(std::is_same_v<clapp::arg_action, clapp::action>);
    static_assert(std::is_same_v<std::underlying_type_t<arg_action>, unsigned char>);

    // The sentinel is enumerator zero, so a value-initialized action means "unspecified".
    static_assert(arg_action{} == arg_action::infer);
    static_assert(!clapp::is_resolved(arg_action{}));
    static_assert(clapp::is_resolved(arg_action::set));

    // resolved_actions holds clap's nine, and clapp::action holds exactly those plus one.
    static_assert(clapp::resolved_actions.size() == 9);
    // Spelled `^^clapp::action`, not `^^arg_action`: the local `using clapp::arg_action`
    // above is a using-*declaration*, and GCC 16 rejects reflecting one with
    // `'^^' cannot be applied to a using-declaration`. The alias-declaration inside the
    // header reflects fine — the two spellings of `using` behave differently here.
    static_assert(std::meta::enumerators_of(^^clapp::action).size() == 10);

    // clap's CountType is u8, and widening it here would change what get_count() returns.
    static_assert(std::is_same_v<clapp::count_type, std::uint8_t>);

    // ---------------------------------------------------------------------------
    // name_of
    // ---------------------------------------------------------------------------

    static_assert(clapp::name_of(arg_action::infer) == "infer"sv);
    static_assert(clapp::name_of(arg_action::set) == "set"sv);
    static_assert(clapp::name_of(arg_action::append) == "append"sv);
    static_assert(clapp::name_of(arg_action::set_true) == "set-true"sv);
    static_assert(clapp::name_of(arg_action::set_false) == "set-false"sv);
    static_assert(clapp::name_of(arg_action::count) == "count"sv);
    static_assert(clapp::name_of(arg_action::help) == "help"sv);
    static_assert(clapp::name_of(arg_action::help_short) == "help-short"sv);
    static_assert(clapp::name_of(arg_action::help_long) == "help-long"sv);
    static_assert(clapp::name_of(arg_action::version) == "version"sv);

    // Kebab-case, not clap's Rust spelling — the divergence is deliberate and matches
    // clapp::naming::kebab, which is the default rename rule everywhere else.
    static_assert(clapp::name_of(arg_action::set_true) != "SetTrue"sv);

    // No two actions share a name, or a diagnostic could not name the one it means.
    consteval bool action_names_are_distinct() {
        for (const arg_action a : clapp::resolved_actions) {
            for (const arg_action b : clapp::resolved_actions) {
                if (a != b && clapp::name_of(a) == clapp::name_of(b)) return false;
            }
            if (clapp::name_of(a) == clapp::name_of(arg_action::infer)) return false;
            if (clapp::name_of(a).empty()) return false;
        }
        return true;
    }
    static_assert(action_names_are_distinct());

    // ---------------------------------------------------------------------------
    // takes_values — clap's ArgAction::takes_values, arm for arm
    // ---------------------------------------------------------------------------

    static_assert(clapp::takes_values(arg_action::set) == tri::yes);
    static_assert(clapp::takes_values(arg_action::append) == tri::yes);
    static_assert(clapp::takes_values(arg_action::set_true) == tri::no);
    static_assert(clapp::takes_values(arg_action::set_false) == tri::no);
    static_assert(clapp::takes_values(arg_action::count) == tri::no);
    static_assert(clapp::takes_values(arg_action::help) == tri::no);
    static_assert(clapp::takes_values(arg_action::help_short) == tri::no);
    static_assert(clapp::takes_values(arg_action::help_long) == tri::no);
    static_assert(clapp::takes_values(arg_action::version) == tri::no);

    // The sentinel has no truthful boolean answer, and tri does not convert to bool, so
    // `if (takes_values(act))` cannot compile and cannot silently read infer as "no".
    static_assert(clapp::takes_values(arg_action::infer) == tri::infer);
    static_assert(!std::is_convertible_v<tri, bool>);

    // ---------------------------------------------------------------------------
    // default_num_args — clap's ArgAction::default_num_args
    // ---------------------------------------------------------------------------

    static_assert(clapp::default_num_args(arg_action::set) == value_range::single());
    static_assert(clapp::default_num_args(arg_action::append) == value_range::single());
    static_assert(clapp::default_num_args(arg_action::set_true) == value_range::empty());
    static_assert(clapp::default_num_args(arg_action::set_false) == value_range::empty());
    static_assert(clapp::default_num_args(arg_action::count) == value_range::empty());
    static_assert(clapp::default_num_args(arg_action::help) == value_range::empty());
    static_assert(clapp::default_num_args(arg_action::help_short) == value_range::empty());
    static_assert(clapp::default_num_args(arg_action::help_long) == value_range::empty());
    static_assert(clapp::default_num_args(arg_action::version) == value_range::empty());

    // An unresolved action resolves nothing: the sentinel propagates rather than
    // collapsing to a default that would then look deliberate.
    static_assert(clapp::default_num_args(arg_action::infer).is_infer());

    // ---------------------------------------------------------------------------
    // max_num_args — clap's debug-only envelope, unconditional here
    // ---------------------------------------------------------------------------

    static_assert(clapp::max_num_args(arg_action::set) == value_range::full());
    static_assert(clapp::max_num_args(arg_action::append) == value_range::full());
    static_assert(clapp::max_num_args(arg_action::set_true) == value_range::optional());
    static_assert(clapp::max_num_args(arg_action::set_false) == value_range::optional());
    static_assert(clapp::max_num_args(arg_action::count) == value_range::empty());
    static_assert(clapp::max_num_args(arg_action::help) == value_range::empty());
    static_assert(clapp::max_num_args(arg_action::help_short) == value_range::empty());
    static_assert(clapp::max_num_args(arg_action::help_long) == value_range::empty());
    static_assert(clapp::max_num_args(arg_action::version) == value_range::empty());
    static_assert(clapp::max_num_args(arg_action::infer) == value_range::full());

    // The check this table exists for: a flag may be spelled `--flag=true`, but never
    // given an arity of three.
    static_assert(value_range::optional().is_within(clapp::max_num_args(arg_action::set_true)));
    static_assert(!value_range::exactly(3).is_within(clapp::max_num_args(arg_action::set_true)));
    static_assert(!value_range::single().is_within(clapp::max_num_args(arg_action::count)));

    // clap's tests/builder/action.rs spends SEVEN `#[should_panic]` cases on one rule:
    // `.action(X).num_args(1..)` is a build error for each of the seven actions that store no
    // command-line value. In clapp that rejection is a compile error inside
    // clapp::command_builder::freeze(), and a compile-fail snippet can only witness the FIRST
    // rejection it reaches — so
    // tests/units/builder/compile_fail/action_num_args_conflict_test.cpp pins the diagnostic
    // for `set_true` and the remaining six are pinned here, at the envelope the check reads.
    // Written out one action at a time rather than looped, so a failure names which one.
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::set_true)));
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::set_false)));
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::count)));
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::help)));
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::help_short)));
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::help_long)));
    static_assert(!value_range::at_least(1).is_within(clapp::max_num_args(arg_action::version)));
    // The control: the two actions that DO take values accept the same range, so the seven
    // assertions above are about the actions and not about `at_least(1)` being unacceptable.
    static_assert(value_range::at_least(1).is_within(clapp::max_num_args(arg_action::set)));
    static_assert(value_range::at_least(1).is_within(clapp::max_num_args(arg_action::append)));

    // ---------------------------------------------------------------------------
    // default_value / default_missing_value
    //
    // The pair that makes `matches.get_flag("f")` answer false for an absent flag and
    // true for a present one, without either case being special-cased in the parser.
    // ---------------------------------------------------------------------------

    static_assert(clapp::default_value_of(arg_action::set_true) == "false"sv);
    static_assert(clapp::default_value_of(arg_action::set_false) == "true"sv);
    static_assert(clapp::default_value_of(arg_action::count) == "0"sv);
    static_assert(clapp::default_value_of(arg_action::set) == std::nullopt);
    static_assert(clapp::default_value_of(arg_action::append) == std::nullopt);
    static_assert(clapp::default_value_of(arg_action::help) == std::nullopt);
    static_assert(clapp::default_value_of(arg_action::help_short) == std::nullopt);
    static_assert(clapp::default_value_of(arg_action::help_long) == std::nullopt);
    static_assert(clapp::default_value_of(arg_action::version) == std::nullopt);
    static_assert(clapp::default_value_of(arg_action::infer) == std::nullopt);

    static_assert(clapp::default_missing_value_of(arg_action::set_true) == "true"sv);
    static_assert(clapp::default_missing_value_of(arg_action::set_false) == "false"sv);
    static_assert(clapp::default_missing_value_of(arg_action::set) == std::nullopt);
    static_assert(clapp::default_missing_value_of(arg_action::append) == std::nullopt);
    static_assert(clapp::default_missing_value_of(arg_action::help) == std::nullopt);
    static_assert(clapp::default_missing_value_of(arg_action::help_short) == std::nullopt);
    static_assert(clapp::default_missing_value_of(arg_action::help_long) == std::nullopt);
    static_assert(clapp::default_missing_value_of(arg_action::version) == std::nullopt);
    static_assert(clapp::default_missing_value_of(arg_action::infer) == std::nullopt);

    // `count` has a default but no default-missing: an occurrence increments rather
    // than assigns, so there is nothing for a bare `--flag` to be worth.
    static_assert(clapp::default_missing_value_of(arg_action::count) == std::nullopt);

    // The two are inverses for the boolean actions — that inversion is the mechanism,
    // not a coincidence.
    static_assert(clapp::default_value_of(arg_action::set_true) ==
                  clapp::default_missing_value_of(arg_action::set_false));
    static_assert(clapp::default_value_of(arg_action::set_false) ==
                  clapp::default_missing_value_of(arg_action::set_true));

    // ---------------------------------------------------------------------------
    // Control-flow predicates
    // ---------------------------------------------------------------------------

    static_assert(clapp::is_help(arg_action::help));
    static_assert(clapp::is_help(arg_action::help_short));
    static_assert(clapp::is_help(arg_action::help_long));
    static_assert(!clapp::is_help(arg_action::version));
    static_assert(!clapp::is_help(arg_action::set));
    static_assert(!clapp::is_help(arg_action::infer));

    static_assert(clapp::is_version(arg_action::version));
    static_assert(!clapp::is_version(arg_action::help));

    static_assert(clapp::is_terminating(arg_action::help));
    static_assert(clapp::is_terminating(arg_action::help_long));
    static_assert(clapp::is_terminating(arg_action::version));
    static_assert(!clapp::is_terminating(arg_action::set));
    static_assert(!clapp::is_terminating(arg_action::count));
    static_assert(!clapp::is_terminating(arg_action::infer));

    // A terminating action never consumes values, and never has a default: it produces
    // an error carrying display_help / display_version instead of a match.
    consteval bool terminating_actions_are_inert() {
        for (const arg_action act : clapp::resolved_actions) {
            if (!clapp::is_terminating(act)) continue;
            if (clapp::takes_values(act) != tri::no) return false;
            if (clapp::default_num_args(act) != value_range::empty()) return false;
            if (clapp::default_value_of(act).has_value()) return false;
            if (clapp::default_missing_value_of(act).has_value()) return false;
        }
        return true;
    }
    static_assert(terminating_actions_are_inert());

    // ---------------------------------------------------------------------------
    // Cross-table consistency
    //
    // The header asserts this too; it is repeated here with the failure spelled out,
    // because a disagreement between these three is the one bug in this file that no
    // single-table assertion can see.
    // ---------------------------------------------------------------------------

    consteval bool every_default_fits_its_envelope() {
        for (const arg_action act : clapp::resolved_actions) {
            if (!clapp::default_num_args(act).is_within(clapp::max_num_args(act))) return false;
        }
        return true;
    }
    static_assert(every_default_fits_its_envelope());

    consteval bool takes_values_matches_default_range() {
        for (const arg_action act : clapp::resolved_actions) {
            const bool from_action = clapp::takes_values(act) == tri::yes;
            const bool from_range  = clapp::default_num_args(act).takes_values();
            if (from_action != from_range) return false;
        }
        return true;
    }
    static_assert(takes_values_matches_default_range());

    // An action that takes no command-line values must still be able to hold one from a
    // default, or `get_flag` on an absent flag would have nothing to report.
    consteval bool valueless_actions_carry_a_default() {
        for (const arg_action act : clapp::resolved_actions) {
            if (clapp::takes_values(act) != tri::no) continue;
            if (clapp::is_terminating(act)) continue;  // help/version produce no match at all
            if (!clapp::default_value_of(act).has_value()) return false;
        }
        return true;
    }
    static_assert(valueless_actions_carry_a_default());

    // resolved_actions must contain no duplicates and no sentinel.
    consteval bool resolved_actions_are_a_clean_set() {
        for (std::size_t i = 0; i < clapp::resolved_actions.size(); ++i) {
            if (!clapp::is_resolved(clapp::resolved_actions[i])) return false;
            for (std::size_t j = i + 1; j < clapp::resolved_actions.size(); ++j) {
                if (clapp::resolved_actions[i] == clapp::resolved_actions[j]) return false;
            }
        }
        return true;
    }
    static_assert(resolved_actions_are_a_clean_set());

}  // namespace

CLAPP_TEST("arg_action: aliases clapp::action rather than duplicating it") {
    CLAPP_CHECK((std::is_same_v<clapp::arg_action, clapp::action>));
    CLAPP_CHECK(arg_action{} == arg_action::infer);
    CLAPP_CHECK(!clapp::is_resolved(arg_action{}));
}

CLAPP_TEST("arg_action: only set and append consume command-line values") {
    // std::vector is a transient allocation and cannot be a constexpr variable, so
    // the table walk gets its runtime witness here.
    std::vector<arg_action> consuming;
    for (const arg_action act : clapp::resolved_actions) {
        if (clapp::takes_values(act) == clapp::tri::yes) consuming.push_back(act);
    }
    CLAPP_CHECK(consuming.size() == 2);
    CLAPP_CHECK(consuming[0] == arg_action::set);
    CLAPP_CHECK(consuming[1] == arg_action::append);
}

CLAPP_TEST("arg_action: every resolved action has a distinct kebab-cased name") {
    std::vector<std::string_view> names;
    for (const arg_action act : clapp::resolved_actions) names.push_back(clapp::name_of(act));
    CLAPP_CHECK(names.size() == 9);
    CLAPP_CHECK(names.front() == "set");
    CLAPP_CHECK(names.back() == "version");
}

CLAPP_TEST("arg_action: boolean actions invert default and default-missing") {
    CLAPP_CHECK(clapp::default_value_of(arg_action::set_true) == "false"sv);
    CLAPP_CHECK(clapp::default_missing_value_of(arg_action::set_true) == "true"sv);
    CLAPP_CHECK(clapp::default_value_of(arg_action::set_false) == "true"sv);
    CLAPP_CHECK(clapp::default_missing_value_of(arg_action::set_false) == "false"sv);
}
