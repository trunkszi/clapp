#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/parser/arg_matcher.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/validator.hpp>
#include <clapp/util/graph.hpp>
#include <clapp/util/id.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#    include <stdlib.h>  // setenv / unsetenv are POSIX, not in <cstdlib>
#endif

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_flags;
    using clapp::arg_id;
    using clapp::arg_matches;
    using clapp::arg_setting;
    using clapp::arg_spec;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::digraph;
    using clapp::error;
    using clapp::error_kind;
    using clapp::group_builder;
    using clapp::group_spec;
    using clapp::raw_args;
    using clapp::requires_spec;
    using clapp::style_class;
    using clapp::styled_str;
    using clapp::value_range;

    using clapp::detail::arg_name_no_brackets;
    using clapp::detail::concat_ids;
    using clapp::detail::conflicts;
    using clapp::detail::contains_id;
    using clapp::detail::format_group;
    using clapp::detail::gather_arg_direct_conflicts;
    using clapp::detail::gather_direct_conflicts;
    using clapp::detail::gather_group_direct_conflicts;
    using clapp::detail::id_set;
    using clapp::detail::required_graph;
    using clapp::detail::requires_digraph;
    using clapp::detail::styled_trim_end;
    using clapp::detail::stylized_arg;
    using clapp::detail::unconditional_requirement;
    using clapp::detail::unroll_arg_requires;
    using clapp::detail::unroll_args_in_group;
    using clapp::detail::usage_renderer;

    // The two functions in parse.hpp this file exists to stay in step with.
    using clapp::detail::arg_display;
    using clapp::detail::render_arg_values;

    // ---------------------------------------------------------------------------
    // Compile-time: the one value-name renderer
    // ---------------------------------------------------------------------------
    //
    // Every spec here is built from string *literals*: under `-fsanitize=null` GCC 16.1.0
    // will not fold libstdc++'s `basic_string(const CharT*, size_type)` for a variable source
    // pointer, and clapp::os_string reaches it. CLAUDE.md trap 10.
    //
    // This used to assert that two copies of the function agreed with each other, which is
    // the weakest thing it could have asserted — two copies of a wrong rule agree perfectly.
    // Now that there is one function, the cases pin what it actually renders.

    consteval bool the_value_names_follow_claps_rules() {
        const arg_spec flag{.id       = arg_id{"verbose"},
                            .short_   = 'v',
                            .long_    = arg_id{"verbose"},
                            .num_args = value_range::empty()};
        const arg_spec option{.id = arg_id{"port"}, .long_ = arg_id{"port"}};
        const arg_spec pair{.id       = arg_id{"point"},
                            .long_    = arg_id{"point"},
                            .num_args = value_range::exactly(2)};
        const arg_spec many{.id       = arg_id{"path"},
                            .long_    = arg_id{"path"},
                            .num_args = value_range::at_least(1)};
        const arg_spec positional{.id = arg_id{"src"}, .index = 1};
        const arg_spec collector{.id       = arg_id{"rest"},
                                 .index    = 1,
                                 .act      = arg_action::append,
                                 .num_args = value_range::at_least(0)};

        return
                // A single declared name is repeated to min_values, so `num_args(2)` reads
                // `<point> <point>` rather than `<point>`.
                render_arg_values(pair, true) == "<point> <point>"
                // An unbounded upper end adds the ellipsis, once.
                && render_arg_values(many, true) == "<path>..." &&
                render_arg_values(option, true) == "<port>" &&
                render_arg_values(flag, true) == "<verbose>"
                // `required` is the caller's question, and it changes only the positionals:
                // a named argument's brackets come from its own num_args.
                && render_arg_values(positional, true) == "<src>" &&
                render_arg_values(positional, false) == "[src]" &&
                render_arg_values(option, false) == "<port>"
                // An `append` positional is always variadic, whatever num_args says.
                && render_arg_values(collector, true) == "[rest]..." &&
                render_arg_values(collector, false) == "[rest]...";
    }
    static_assert(the_value_names_follow_claps_rules());

    consteval bool stylized_arg_is_the_parse_loops_display() {
        const arg_spec flag{.id       = arg_id{"verbose"},
                            .short_   = 'v',
                            .long_    = arg_id{"verbose"},
                            .num_args = value_range::empty()};
        const arg_spec counter{.id       = arg_id{"verbose"},
                               .short_   = 'v',
                               .act      = arg_action::count,
                               .num_args = value_range::empty()};
        const arg_spec option{.id = arg_id{"port"}, .long_ = arg_id{"port"}};
        // The id and the long spelling routinely differ; a renderer that reached for the id
        // looks right on every argument whose two names happen to coincide.
        const arg_spec renamed{.id = arg_id{"no_color"}, .long_ = arg_id{"no-color"}};
        const arg_spec short_only{.id = arg_id{"jobs"}, .short_ = 'j'};
        const arg_spec pair{.id       = arg_id{"point"},
                            .long_    = arg_id{"point"},
                            .num_args = value_range::exactly(2)};
        const arg_spec optional_value{.id       = arg_id{"color"},
                                      .long_    = arg_id{"color"},
                                      .num_args = value_range::optional(),
                                      .settings = arg_flags{}.set(arg_setting::require_equals)};
        const arg_spec positional{.id       = arg_id{"src"},
                                  .index    = 1,
                                  .settings = arg_flags{}.set(arg_setting::required)};

        const arg_spec cases[] = {
                flag, counter, option, renamed, short_only, pair, optional_value, positional};
        for (const arg_spec& one : cases) {
            if (stylized_arg(one, std::nullopt).to_string() != arg_display(one)) return false;
        }
        // And the text really is what the twelve diagnostics quote, not merely self-consistent.
        return stylized_arg(option, std::nullopt).to_string() == "--port <port>" &&
               stylized_arg(renamed, std::nullopt).to_string() == "--no-color <no_color>" &&
               stylized_arg(counter, std::nullopt).to_string() == "-v..." &&
               stylized_arg(pair, std::nullopt).to_string() == "--point <point> <point>" &&
               stylized_arg(optional_value, std::nullopt).to_string() == "--color[=<color>]";
    }
    static_assert(stylized_arg_is_the_parse_loops_display());

    // The `required` override is the only thing stylized_arg() adds over arg_display(), and
    // it is exactly what a usage line needs: a line that DEMANDS an argument writes `<>`,
    // one that merely lists it writes `[]`.
    consteval bool the_required_override_is_what_changes_the_brackets() {
        const arg_spec positional{.id = arg_id{"src"}, .index = 1};
        return stylized_arg(positional, std::nullopt).to_string() == "[src]" &&
               stylized_arg(positional, true).to_string() == "<src>" &&
               stylized_arg(positional, false).to_string() == "[src]" &&
               arg_display(positional) == "[src]";
    }
    static_assert(the_required_override_is_what_changes_the_brackets());

    // The literal / placeholder split survives, so a colour front-end can style the two
    // halves differently. Asserting only to_string() would let the classes rot.
    consteval bool stylized_arg_keeps_its_style_classes() {
        const arg_spec option{.id = arg_id{"port"}, .long_ = arg_id{"port"}};
        const styled_str rendered = stylized_arg(option, true);
        return rendered.text_of(style_class::literal) == "--port" &&
               rendered.text_of(style_class::placeholder) == " <port>";
    }
    static_assert(stylized_arg_keeps_its_style_classes());

    consteval bool value_names_render_without_brackets_for_a_group() {
        const arg_id one_name[]  = {arg_id{"FILE"}};
        const arg_id two_names[] = {arg_id{"X"}, arg_id{"Y"}};
        const arg_spec bare{.id = arg_id{"src"}, .index = 1};
        const arg_spec named{.id               = arg_id{"src"},
                             .index            = 1,
                             .value_name_data  = one_name,
                             .value_name_count = 1};
        const arg_spec paired{.id               = arg_id{"pt"},
                              .index            = 1,
                              .value_name_data  = two_names,
                              .value_name_count = 2};
        return arg_name_no_brackets(bare) == "src" && arg_name_no_brackets(named) == "FILE" &&
               arg_name_no_brackets(paired) == "<X> <Y>";
    }
    static_assert(value_names_render_without_brackets_for_a_group());

    // ---------------------------------------------------------------------------
    // Compile-time: the little containers
    // ---------------------------------------------------------------------------

    consteval bool an_id_set_keeps_insertion_order_and_deduplicates() {
        id_set set;
        const bool first  = set.insert(arg_id{"zulu"});
        const bool second = set.insert(arg_id{"alpha"});
        const bool again  = set.insert(arg_id{"zulu"});
        // Sorted would give alpha, zulu — and would silently reorder every "required
        // arguments were not provided" list away from clap's declaration order.
        return first && second && !again && set.size() == 2 && set.ids()[0].name() == "zulu" &&
               set.ids()[1].name() == "alpha" && set.contains("alpha") && !set.contains("bravo");
    }
    static_assert(an_id_set_keeps_insertion_order_and_deduplicates());

    // ---------------------------------------------------------------------------
    // Runtime fixtures for clap's insertion order
    // ---------------------------------------------------------------------------
    //
    // Every id here is chosen so that alphabetical order disagrees with at least one of the
    // command lines the runtime cases below type. `--target` collides with three arguments at
    // once, which is what makes the `cannot be used with:` LIST order visible; `--zebra` and
    // `--alpha` collide in a pair, which is what makes the `Usage:` line's echo of the
    // SURVIVING arguments visible. Two innocent bystanders (`--mid`, `--other`) exist purely
    // to be echoed in the order they were typed.

    consteval command_spec make_conflict_order() {
        command_builder app("ord");
        std::move(app)
                .arg(arg_builder("zebra")
                             .long_("zebra")
                             .action(arg_action::set_true)
                             .conflicts_with("alpha"))
                .arg(arg_builder("alpha").long_("alpha").action(arg_action::set_true))
                .arg(arg_builder("mid").long_("mid").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true))
                .arg(arg_builder("target")
                             .long_("target")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"zulu", "yankee", "mike"}))
                .arg(arg_builder("zulu").long_("zulu").action(arg_action::set_true))
                .arg(arg_builder("yankee").long_("yankee").action(arg_action::set_true))
                .arg(arg_builder("mike").long_("mike").action(arg_action::set_true))
                // A `multiple` group over the two bystanders. It changes no conflict — its
                // members may co-occur and it collides with nothing — but it does put a GROUP
                // row into the matcher, whose saved insertion ordinal must participate
                // in the same diagnostic traversal as argument rows.
                .group(group_builder("pack").args({"mid", "other"}).multiple());
        return app.freeze();
    }
    constexpr command_spec conflict_order = make_conflict_order();

    consteval bool concat_ids_preserves_both_sides() {
        const arg_id left[]              = {arg_id{"a"}, arg_id{"b"}};
        const arg_id right[]             = {arg_id{"b"}, arg_id{"c"}};
        const std::vector<arg_id> joined = concat_ids(left, right);
        // Duplicates survive on purpose: deduplication happens on the RENDERED form, because
        // two different ids can render to the same usage fragment.
        return joined.size() == 4 && joined[0].name() == "a" && joined[1].name() == "b" &&
               joined[2].name() == "b" && joined[3].name() == "c" && contains_id(joined, "c") &&
               !contains_id(joined, "d");
    }
    static_assert(concat_ids_preserves_both_sides());

    consteval bool trimming_touches_only_the_tail() {
        styled_str padded;
        padded.push(style_class::literal, "demo");
        padded.push_plain("  \n ");
        return styled_trim_end(padded).to_string() == "demo" &&
               styled_trim_end(styled_str{"   "}).empty() &&
               styled_trim_end(styled_str{"a b "}).to_string() == "a b";
    }
    static_assert(trimming_touches_only_the_tail());

    // ---------------------------------------------------------------------------
    // Compile-time: conflicts and groups over a frozen tree
    // ---------------------------------------------------------------------------

    consteval command_spec make_group_spec_fixture() {
        command_builder app("fmt");
        std::move(app)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("yaml").long_("yaml").action(arg_action::set_true))
                .arg(arg_builder("loud").long_("loud").action(arg_action::set_true))
                .arg(arg_builder("soft").long_("soft").action(arg_action::set_true))
                .arg(arg_builder("plain")
                             .long_("plain")
                             .action(arg_action::set_true)
                             .conflicts_with("format"))
                .group(group_builder("format").args({"json", "yaml"}).required())
                .group(group_builder("volume").args({"loud", "soft"}).multiple());
        return app.freeze();
    }
    constexpr command_spec group_fixture = make_group_spec_fixture();

    // The fixture really does carry those settings; a builder-side change must fail to
    // compile rather than make the assertions below vacuous.
    static_assert(group_fixture.find_group("format")->is_required_set());
    static_assert(!group_fixture.find_group("format")->is_multiple());
    static_assert(group_fixture.find_group("volume")->is_multiple());

    consteval bool a_group_that_admits_one_member_makes_them_conflict() {
        const std::vector<arg_id> json = gather_direct_conflicts(group_fixture, "json");
        const std::vector<arg_id> loud = gather_direct_conflicts(group_fixture, "loud");
        // `multiple(false)` has no dedicated check anywhere: it IS this line. And
        // `multiple(true)` must contribute nothing, or every group becomes exclusive.
        return json.size() == 1 && json.front().name() == "yaml" && loud.empty();
    }
    static_assert(a_group_that_admits_one_member_makes_them_conflict());

    consteval bool an_argument_inherits_its_groups_conflicts() {
        // `--plain` conflicts with the group `format`, so it conflicts with `--json`. The
        // declaration names the group; the *diagnostic* must name the member.
        const std::vector<arg_id> plain  = gather_direct_conflicts(group_fixture, "plain");
        const std::vector<arg_id> format = gather_direct_conflicts(group_fixture, "format");
        return plain.size() == 1 && plain.front().name() == "format" && format.empty() &&
               contains_id(unroll_args_in_group(group_fixture, "format"), "json") &&
               contains_id(unroll_args_in_group(group_fixture, "format"), "yaml");
    }
    static_assert(an_argument_inherits_its_groups_conflicts());

    consteval bool group_conflicts_reach_every_member() {
        // The other direction of the same rule: `--json` picks up `format`'s conflicts,
        // which is what makes `--json --plain` an error even though `--json` declares
        // nothing at all.
        command_builder app("g");
        std::move(app)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("plain").long_("plain").action(arg_action::set_true))
                .group(group_builder("format").arg("json").conflicts_with("plain"));
        const command_spec spec        = app.freeze();
        const std::vector<arg_id> json = gather_arg_direct_conflicts(spec, *spec.find_arg("json"));
        return json.size() == 1 && json.front().name() == "plain" &&
               gather_group_direct_conflicts(*spec.find_group("format")).size() == 1;
    }
    static_assert(group_conflicts_reach_every_member());

    consteval bool overrides_are_conflicts_too() {
        command_builder app("o");
        std::move(app)
                .arg(arg_builder("color")
                             .long_("color")
                             .action(arg_action::set_true)
                             .overrides_with("no_color"))
                .arg(arg_builder("no_color").long_("no-color").action(arg_action::set_true));
        const command_spec spec         = app.freeze();
        const std::vector<arg_id> color = gather_direct_conflicts(spec, "color");
        return color.size() == 1 && color.front().name() == "no_color";
    }
    static_assert(overrides_are_conflicts_too());

    consteval bool an_unknown_id_conflicts_with_nothing() {
        return gather_direct_conflicts(group_fixture, "nonesuch").empty() &&
               unroll_args_in_group(group_fixture, "nonesuch").empty();
    }
    static_assert(an_unknown_id_conflicts_with_nothing());

    consteval bool only_required_groups_and_arguments_seed_the_graph() {
        const id_set required = required_graph(group_fixture);
        return required.size() == 1 && required.contains("format") && !required.contains("json") &&
               !required.contains("volume");
    }
    static_assert(only_required_groups_and_arguments_seed_the_graph());

    consteval bool a_required_group_carries_its_requires_along() {
        command_builder app("g");
        std::move(app)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("out").long_("out"))
                .group(group_builder("format").arg("json").required().requires_("out"));
        const command_spec spec = app.freeze();
        const id_set required   = required_graph(spec);
        // The group first, then what it requires: clap's `insert` / `insert_child` order,
        // and the order the message lists them in.
        return required.size() == 2 && required.ids()[0].name() == "format" &&
               required.ids()[1].name() == "out";
    }
    static_assert(a_required_group_carries_its_requires_along());

    consteval bool a_group_renders_the_spellings_a_user_can_type() {
        return format_group(group_fixture, "format").to_string() == "<--json|--yaml>" &&
               format_group(group_fixture, "nonesuch").to_string() == "<>";
    }
    static_assert(a_group_renders_the_spellings_a_user_can_type());

    // A group listing one member twice. clapp::group_builder::freeze() rejects that outright
    // ("an argument is listed twice in the same group"), so this shape is reachable only from
    // a hand-written clapp::command_spec — which ADR-0007 keeps as the no-reflection
    // degradation path, and which unroll_args_in_group() therefore still has to survive.
    constexpr arg_id doubled_members[] = {arg_id{"json"}, arg_id{"json"}, arg_id{"yaml"}};
    constexpr group_spec doubled_group{
            .id = arg_id{"format"}, .arg_data = doubled_members, .arg_count = 3};
    constexpr arg_spec doubled_args[] = {
            arg_spec{.id       = arg_id{"json"},
                     .long_    = arg_id{"json"},
                     .act      = arg_action::set_true,
                     .num_args = value_range::empty()},
            arg_spec{.id       = arg_id{"yaml"},
                     .long_    = arg_id{"yaml"},
                     .act      = arg_action::set_true,
                     .num_args = value_range::empty()},
    };
    constexpr command_spec doubled_cmd{.name        = arg_id{"g"},
                                       .arg_data    = doubled_args,
                                       .arg_count   = 2,
                                       .group_data  = &doubled_group,
                                       .group_count = 1};

    consteval bool a_member_listed_twice_is_still_one_member() {
        return unroll_args_in_group(doubled_cmd, "format").size() == 2 &&
               format_group(doubled_cmd, "format").to_string() == "<--json|--yaml>";
    }
    static_assert(a_member_listed_twice_is_still_one_member());

    consteval bool a_group_of_positionals_renders_without_brackets() {
        command_builder app("g");
        std::move(app)
                .arg(arg_builder("src").index(1))
                .arg(arg_builder("url").long_("url"))
                .group(group_builder("source").args({"src", "url"}));
        const command_spec spec = app.freeze();
        // clap prints a positional's value name bare inside the group, and a named
        // argument's full usage form.
        return format_group(spec, "source").to_string() == "<src|--url <url>>";
    }
    static_assert(a_group_of_positionals_renders_without_brackets());

    // ---------------------------------------------------------------------------
    // Compile-time: the requires closure, twice
    // ---------------------------------------------------------------------------

    consteval command_spec make_chain() {
        command_builder app("req");
        std::move(app)
                .arg(arg_builder("a").long_("a").action(arg_action::set_true).requires_("b"))
                .arg(arg_builder("b").long_("b").requires_("c"))
                .arg(arg_builder("c").long_("c"))
                .arg(arg_builder("mode").long_("mode").requires_if("fast", "key"))
                .arg(arg_builder("key").long_("key"));
        return app.freeze();
    }
    constexpr command_spec chain = make_chain();

    consteval bool requires_is_transitive() {
        const std::vector<arg_id> from_a =
                unroll_arg_requires(chain, unconditional_requirement, "a");
        const std::vector<arg_id> from_c =
                unroll_arg_requires(chain, unconditional_requirement, "c");
        // `--a` requires `--b` requires `--c`. Reading only `--a`'s edge list yields {b} and
        // stops demanding `--c` with no diagnostic anywhere.
        return contains_id(from_a, "b") && contains_id(from_a, "c") && !contains_id(from_a, "a") &&
               from_c.empty();
    }
    static_assert(requires_is_transitive());

    consteval bool a_conditional_edge_needs_a_condition() {
        // `--mode` requires `--key` only when it equals `fast`, so the unconditional filter
        // must drop it. Treating every edge as unconditional makes `--mode slow` demand
        // `--key`.
        return unroll_arg_requires(chain, unconditional_requirement, "mode").empty();
    }
    static_assert(a_conditional_edge_needs_a_condition());

    // The same relation through <clapp/util/graph.hpp>: two independent implementations of
    // one closure. The digraph is index-based and non-reflexive; the walk is id-based and
    // worklist-driven. If they ever disagree, one of them is wrong.
    consteval bool the_bit_matrix_closure_agrees_with_the_walk() {
        const digraph<5> closed = requires_digraph<5>(chain).transitive_closure();
        const std::size_t a = 0, b = 1, c = 2, mode = 3;
        return closed.has_edge(a, b) && closed.has_edge(b, c) && closed.has_edge(a, c) &&
               !closed.has_edge(c, a) && !closed.has_edge(a, a) && !closed.has_edge(mode, 4) &&
               closed.edge_count() == 3;
    }
    static_assert(the_bit_matrix_closure_agrees_with_the_walk());

    consteval command_spec make_cycle() {
        command_builder app("cy");
        std::move(app)
                .arg(arg_builder("x").long_("x").requires_("y"))
                .arg(arg_builder("y").long_("y").requires_("x"));
        return app.freeze();
    }
    constexpr command_spec cycle = make_cycle();

    consteval bool a_requires_cycle_terminates_and_reports_itself() {
        const std::vector<arg_id> from_x =
                unroll_arg_requires(cycle, unconditional_requirement, "x");
        // Both libraries agree: a cycle is not an error, and an argument on one requires
        // itself — trivially satisfied, since it is present.
        return contains_id(from_x, "y") && contains_id(from_x, "x") &&
               requires_digraph<2>(cycle).transitive_closure().self_reachable(0) &&
               !requires_digraph<2>(cycle).acyclic();
    }
    static_assert(a_requires_cycle_terminates_and_reports_itself());

    // clapp::command_builder::freeze() rejects a group member that is not an argument, so a
    // group cycle is unreachable on a frozen tree — but unroll_args_in_group() is `constexpr`
    // and a hand-written spec can still reach it. clap has no guard here and would spin;
    // this must terminate, and a compiler that spins gives no diagnostic at all.
    constexpr arg_id cyclic_g_members[]  = {arg_id{"h"}};
    constexpr arg_id cyclic_h_members[]  = {arg_id{"g"}};
    constexpr group_spec cyclic_groups[] = {
            group_spec{.id = arg_id{"g"}, .arg_data = cyclic_g_members, .arg_count = 1},
            group_spec{.id = arg_id{"h"}, .arg_data = cyclic_h_members, .arg_count = 1},
    };
    constexpr command_spec cyclic_group_cmd{
            .name = arg_id{"x"}, .group_data = cyclic_groups, .group_count = 2};

    consteval bool nested_groups_cannot_hang_the_compiler() {
        return unroll_args_in_group(cyclic_group_cmd, "g").empty();
    }
    static_assert(nested_groups_cannot_hang_the_compiler());

    // ---------------------------------------------------------------------------
    // Compile-time: the usage renderer
    // ---------------------------------------------------------------------------

    consteval command_spec make_usage_fixture() {
        command_builder app("demo");
        std::move(app)
                .arg(arg_builder("out").short_('o').long_("out").required())
                .arg(arg_builder("tag").long_("tag"))
                .arg(arg_builder("src").index(1).required());
        return app.freeze();
    }
    constexpr command_spec usage_fixture = make_usage_fixture();

    consteval bool the_usage_line_names_what_is_required() {
        const std::optional<styled_str> line =
                usage_renderer{usage_fixture}.create_usage_with_title({});
        // `--out` is required so it is spelled out; `--tag` is not and hides behind
        // `[OPTIONS]`; the positional comes last, in slot order.
        return line.has_value() && line->to_string() == "Usage: demo [OPTIONS] --out <out> <src>";
    }
    static_assert(the_usage_line_names_what_is_required());

    consteval bool smart_usage_describes_this_command_line_instead() {
        const arg_id used[] = {arg_id{"tag"}};
        const std::optional<styled_str> line =
                usage_renderer{usage_fixture}.create_usage_with_title(used);
        // With a `used` list the line stops being a summary of the command and becomes a
        // summary of what this invocation still needs — and `[OPTIONS]` disappears, because
        // the options that matter are now named outright.
        return line.has_value() && line->to_string() == "Usage: demo --out <out> --tag <tag> <src>";
    }
    static_assert(smart_usage_describes_this_command_line_instead());

    consteval bool a_command_with_only_builtins_needs_no_options_tag() {
        command_builder app("bare");
        std::move(app).version("1.0").arg(arg_builder("src").index(1).required());
        const command_spec spec = app.freeze();
        // `--help` and `--version` are injected, and clap deliberately does not let them
        // conjure an `[OPTIONS]` that would appear on every command in existence.
        return usage_renderer{spec}.create_usage_with_title({})->to_string() == "Usage: bare <src>";
    }
    static_assert(a_command_with_only_builtins_needs_no_options_tag());

    consteval bool a_hand_written_help_flag_is_skipped_by_name() {
        command_builder app("hw");
        std::move(app)
                .disable_help_flag()
                .arg(arg_builder("help").long_("help").action(arg_action::set_true))
                .arg(arg_builder("src").index(1).required());
        // clap skips `--help` and `--version` by SPELLING as well as by action, so a
        // hand-rolled help flag does not conjure an `[OPTIONS]` onto a command that has no
        // other options. Only the spelling test can see this: the action here is set_true.
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: hw <src>";
    }
    static_assert(a_hand_written_help_flag_is_skipped_by_name());

    consteval bool a_hidden_or_required_option_needs_no_options_tag() {
        command_builder app("h");
        std::move(app)
                .arg(arg_builder("secret").long_("secret").action(arg_action::set_true).hide())
                .arg(arg_builder("out").long_("out").required());
        const command_spec spec = app.freeze();
        return usage_renderer{spec}.create_usage_with_title({})->to_string() ==
               "Usage: h --out <out>";
    }
    static_assert(a_hidden_or_required_option_needs_no_options_tag());

    consteval bool a_required_group_shows_its_alternatives() {
        return usage_renderer{group_fixture}.create_usage_with_title({})->to_string() ==
               "Usage: fmt [OPTIONS] <--json|--yaml>";
    }
    static_assert(a_required_group_shows_its_alternatives());

    consteval bool a_member_of_a_required_group_needs_no_options_tag() {
        command_builder app("only");
        std::move(app)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("yaml").long_("yaml").action(arg_action::set_true))
                .group(group_builder("format").args({"json", "yaml"}).required());
        // The `[OPTIONS]` in the assertion above comes from `--loud`, `--soft` and `--plain`,
        // which are in no required group. With only the group's own members left, the tag
        // must disappear: `<--json|--yaml>` already tells the whole story, and a second,
        // vaguer summary of the same two flags is noise.
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: only <--json|--yaml>";
    }
    static_assert(a_member_of_a_required_group_needs_no_options_tag());

    consteval bool subcommands_get_a_placeholder() {
        command_builder optional_sub("git");
        std::move(optional_sub).subcommand(command_builder("add"));
        command_builder required_sub("git");
        std::move(required_sub).subcommand_required().subcommand(command_builder("add"));
        command_builder renamed("tool");
        std::move(renamed).subcommand_value_name("TASK").subcommand(command_builder("build"));

        return usage_renderer{optional_sub.freeze()}.create_usage_with_title({})->to_string() ==
                       "Usage: git [COMMAND]" &&
               usage_renderer{required_sub.freeze()}.create_usage_with_title({})->to_string() ==
                       "Usage: git <COMMAND>" &&
               usage_renderer{renamed.freeze()}.create_usage_with_title({})->to_string() ==
                       "Usage: tool [TASK]";
    }
    static_assert(subcommands_get_a_placeholder());

    consteval bool a_last_positional_is_fenced_behind_the_escape() {
        command_builder app("lp");
        std::move(app)
                .arg(arg_builder("opt").long_("opt"))
                .arg(arg_builder("rest").index(1).num_args(value_range::at_least(0)).last());
        // `last()` means "only reachable after `--`", and the usage line has to say so or
        // nobody will ever find the argument.
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: lp [OPTIONS] [-- [rest]...]";
    }
    static_assert(a_last_positional_is_fenced_behind_the_escape());

    consteval bool an_alternative_line_appears_when_subcommands_negate_the_requirements() {
        command_builder app("ng");
        std::move(app)
                .subcommand_negates_reqs()
                .arg(arg_builder("out").long_("out").required())
                .subcommand(command_builder("run"));
        // Two ways to invoke the command, so two lines. Printing only the first tells the
        // user an argument is required that a subcommand would have excused.
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: ng --out <out>\n       ng <COMMAND>";
    }
    static_assert(an_alternative_line_appears_when_subcommands_negate_the_requirements());

    consteval bool arguments_that_conflict_with_subcommands_get_their_own_line() {
        command_builder app("ac");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("out").long_("out"))
                .subcommand(command_builder("run"));
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: ac [OPTIONS]\n       ac <COMMAND>";
    }
    static_assert(arguments_that_conflict_with_subcommands_get_their_own_line());

    consteval bool override_usage_wins_outright() {
        command_builder app("ov");
        std::move(app).override_usage("ov [MAGIC]").arg(arg_builder("out").long_("out").required());
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: ov [MAGIC]";
    }
    static_assert(override_usage_wins_outright());

    consteval bool bin_name_beats_the_command_name() {
        command_builder app("inner");
        std::move(app).bin_name("outer sub").arg(arg_builder("src").index(1).required());
        return usage_renderer{app.freeze()}.create_usage_with_title({})->to_string() ==
               "Usage: outer sub <src>";
    }
    static_assert(bin_name_beats_the_command_name());

    consteval bool the_usage_heading_carries_its_own_style_class() {
        const styled_str line = *usage_renderer{usage_fixture}.create_usage_with_title({});
        // The heading is what a colour front-end underlines; flattening it into plain text
        // would be invisible in every to_string() assertion above.
        return line.text_of(style_class::usage) == "Usage:" &&
               line.text_of(style_class::literal) == "demo--out";
    }
    static_assert(the_usage_heading_carries_its_own_style_class());

    // ---------------------------------------------------------------------------
    // Runtime fixtures and helpers
    // ---------------------------------------------------------------------------

    using outcome = std::expected<arg_matches, error>;

    // The public seam, nothing else. clapp::detail::parse_engine::get_matches_with() ends
    // with clapp::detail::validate(), so clapp::parse() validates — every case below is
    // therefore a statement about what a real program sees, not about a hand-assembled
    // pipeline that might not match the shipped one.
    outcome run(const command_spec& cmd, const raw_args& raw) { return clapp::parse(cmd, raw); }

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    bool says(const outcome& got, std::string_view fragment) {
        return message_of(got).find(fragment) != std::string::npos;
    }

    consteval command_spec make_reqs() {
        command_builder app("demo");
        std::move(app)
                .arg(arg_builder("out").short_('o').long_("out").required())
                .arg(arg_builder("tag").long_("tag"))
                // Declared but never supplied below. `exclusive` suppresses the whole
                // required check when it is PRESENT; deciding that from the declaration
                // instead of from the matches turns every command with an exclusive flag
                // into a command with no required arguments.
                .arg(arg_builder("solo").long_("solo").action(arg_action::set_true).exclusive())
                .arg(arg_builder("src").index(1).required());
        return app.freeze();
    }
    constexpr command_spec reqs = make_reqs();

    consteval command_spec make_groups() {
        command_builder app("fmt");
        std::move(app)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("yaml").long_("yaml").action(arg_action::set_true))
                .arg(arg_builder("loud").long_("loud").action(arg_action::set_true))
                .arg(arg_builder("soft").long_("soft").action(arg_action::set_true))
                .group(group_builder("format").args({"json", "yaml"}).required())
                .group(group_builder("volume").args({"loud", "soft"}).multiple());
        return app.freeze();
    }
    constexpr command_spec groups = make_groups();

    consteval command_spec make_conflict() {
        command_builder app("conf");
        std::move(app)
                .arg(arg_builder("alpha")
                             .long_("alpha")
                             .action(arg_action::set_true)
                             .conflicts_with("beta"))
                .arg(arg_builder("beta").long_("beta").action(arg_action::set_true))
                .arg(arg_builder("gamma")
                             .long_("gamma")
                             .action(arg_action::set_true)
                             .conflicts_with("pack"))
                .arg(arg_builder("one").long_("one").action(arg_action::set_true))
                .arg(arg_builder("two").long_("two").action(arg_action::set_true))
                // The group makes `--solo` fill TWO ids: `solo` and `mine`. The count that
                // decides whether an exclusive argument has company must count arguments,
                // not entries, or an exclusive argument in any group conflicts with itself.
                .arg(arg_builder("solo")
                             .long_("solo")
                             .action(arg_action::set_true)
                             .exclusive()
                             .group("mine"))
                // `omega` declares the conflict and `delta` does not. The validator walks
                // the supplied arguments in COMMAND-LINE order, so typing `--delta --omega`
                // asks `delta` first — and `delta`'s own list is empty. Without this pair,
                // every conflict in the file happens to be reachable from the declaring side
                // and the symmetric half of gather_conflicts() is untested.
                .arg(arg_builder("delta").long_("delta").action(arg_action::set_true))
                .arg(arg_builder("omega")
                             .long_("omega")
                             .action(arg_action::set_true)
                             .conflicts_with("delta"))
                .group(group_builder("pack").args({"one", "two"}).multiple());
        return app.freeze();
    }
    constexpr command_spec conflict = make_conflict();

    consteval command_spec make_self_override() {
        command_builder app("so");
        std::move(app).arg(arg_builder("set").long_("set").overrides_with("set"));
        return app.freeze();
    }
    constexpr command_spec self_override = make_self_override();

    consteval command_spec make_hidden_conflict() {
        command_builder app("hc");
        std::move(app)
                .arg(arg_builder("alpha")
                             .long_("alpha")
                             .action(arg_action::set_true)
                             .conflicts_with("beta"))
                .arg(arg_builder("beta").long_("beta").action(arg_action::set_true))
                .arg(arg_builder("secret").long_("secret").action(arg_action::set_true).hide());
        return app.freeze();
    }
    constexpr command_spec hidden_conflict = make_hidden_conflict();

    consteval command_spec make_unless_any() {
        command_builder app("uy");
        std::move(app)
                .arg(arg_builder("cfg").long_("cfg").required_unless_present_any({"p", "q"}))
                .arg(arg_builder("p").long_("p").action(arg_action::set_true))
                .arg(arg_builder("q").long_("q").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec unless_any = make_unless_any();

    consteval command_spec make_unless() {
        command_builder app("un");
        std::move(app)
                .arg(arg_builder("cfg").long_("cfg").required_unless_present("stdin"))
                .arg(arg_builder("stdin").long_("stdin").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec unless = make_unless();

    consteval command_spec make_unless_all() {
        command_builder app("ua");
        std::move(app)
                .arg(arg_builder("both").long_("both").required_unless_present_all({"x", "y"}))
                .arg(arg_builder("x").long_("x").action(arg_action::set_true))
                .arg(arg_builder("y").long_("y").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec unless_all = make_unless_all();

    consteval command_spec make_req_if() {
        command_builder app("ri");
        std::move(app)
                .arg(arg_builder("mode").long_("mode"))
                .arg(arg_builder("env").long_("env"))
                .arg(arg_builder("key").long_("key").required_if_eq("mode", "secure"))
                // TWO rules, so `any` and `all` finally disagree — with one rule each they
                // are the same predicate and neither test can see the other's bug.
                .arg(arg_builder("audit").long_("audit").required_if_eq_any(
                        {{"mode", "secure"}, {"env", "prod"}}))
                .arg(arg_builder("token").long_("token").required_if_eq_all(
                        {{"mode", "secure"}, {"env", "prod"}}));
        return app.freeze();
    }
    constexpr command_spec req_if = make_req_if();

    consteval command_spec make_subs() {
        command_builder app("git");
        std::move(app)
                .subcommand_required()
                .arg(arg_builder("verbose").short_('v').long_("verbose").action(
                        arg_action::set_true))
                .subcommand(command_builder("add"))
                .subcommand(command_builder("commit").alias("ci"));
        return app.freeze();
    }
    constexpr command_spec subs = make_subs();

    consteval command_spec make_negates() {
        command_builder app("ng");
        std::move(app)
                .subcommand_negates_reqs()
                .arg(arg_builder("out").long_("out").required())
                .subcommand(command_builder("run"));
        return app.freeze();
    }
    constexpr command_spec negates = make_negates();

    consteval command_spec make_else_help() {
        command_builder app("eh");
        std::move(app)
                .about("does things")
                .arg_required_else_help()
                // A subcommand is an answer to "you gave me nothing", so naming one has to
                // switch the check off.
                .subcommand(command_builder("run"))
                .arg(arg_builder("tag").long_("tag"))
                // The defaulted argument is load-bearing: it guarantees the matcher is NOT
                // empty after `eh` alone, so counting entries instead of counting *explicit*
                // entries silently satisfies arg_required_else_help.
                .arg(arg_builder("level").long_("level").default_value("info"));
        return app.freeze();
    }
    constexpr command_spec else_help = make_else_help();

    consteval command_spec make_excused() {
        command_builder app("ex");
        std::move(app)
                .arg(arg_builder("out").long_("out").required().conflicts_with("dry"))
                .arg(arg_builder("dry").long_("dry").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec excused = make_excused();

    consteval command_spec make_excused_by_group() {
        command_builder app("eg");
        std::move(app)
                // Neither `--out` nor its group declares anything: the conflict is declared
                // by `--dry`, and it names the GROUP. Excusing `--out` therefore needs the
                // group loop in is_missing_required_ok(), not the argument's own list.
                .arg(arg_builder("out").long_("out").required().group("sink"))
                .arg(arg_builder("dry")
                             .long_("dry")
                             .action(arg_action::set_true)
                             .conflicts_with("sink"));
        return app.freeze();
    }
    constexpr command_spec excused_by_group = make_excused_by_group();

    consteval command_spec make_conflict_and_requires() {
        command_builder app("cr");
        std::move(app)
                .arg(arg_builder("alpha")
                             .long_("alpha")
                             .action(arg_action::set_true)
                             .conflicts_with("beta")
                             .requires_("gamma"))
                .arg(arg_builder("beta").long_("beta").action(arg_action::set_true))
                .arg(arg_builder("gamma").long_("gamma"));
        return app.freeze();
    }
    constexpr command_spec conflict_and_requires = make_conflict_and_requires();

    consteval command_spec make_last_conditional() {
        command_builder app("lc");
        std::move(app)
                .arg(arg_builder("mode").long_("mode"))
                .arg(arg_builder("first").index(1))
                .arg(arg_builder("rest").index(2).last().required_if_eq("mode", "x"));
        return app.freeze();
    }
    constexpr command_spec last_conditional = make_last_conditional();

    consteval command_spec make_envreq() {
        command_builder app("er");
        std::move(app).arg(
                arg_builder("home").long_("home").env("CLAPP_VALIDATOR_TEST_HOME").required());
        return app.freeze();
    }
    constexpr command_spec envreq = make_envreq();

    consteval command_spec make_overrides() {
        command_builder app("ov");
        std::move(app)
                .arg(arg_builder("color")
                             .long_("color")
                             .action(arg_action::set_true)
                             .overrides_with("no_color"))
                .arg(arg_builder("no_color")
                             .long_("no-color")
                             .action(arg_action::set_true)
                             .overrides_with("color"));
        return app.freeze();
    }
    constexpr command_spec overrides = make_overrides();

    consteval command_spec make_positional_display() {
        command_builder app("pd");
        std::move(app)
                .arg(arg_builder("mode").long_("mode"))
                .arg(arg_builder("first").index(1))
                .arg(arg_builder("second").index(2).required_if_eq("mode", "x"));
        return app.freeze();
    }
    constexpr command_spec positional_display = make_positional_display();

    consteval command_spec make_positional_display_allowed() {
        command_builder app("pa");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("mode").long_("mode"))
                .arg(arg_builder("first").index(1))
                .arg(arg_builder("second").index(2).required_if_eq("mode", "x"));
        return app.freeze();
    }
    constexpr command_spec positional_display_allowed = make_positional_display_allowed();

    consteval command_spec make_hidden() {
        command_builder app("hd");
        std::move(app)
                .arg(arg_builder("secret").long_("secret").action(arg_action::set_true).hide())
                .arg(arg_builder("out").long_("out").required());
        return app.freeze();
    }
    constexpr command_spec hidden = make_hidden();

    consteval command_spec make_parent() {
        command_builder app("p");
        std::move(app).subcommand(
                command_builder("sub").arg(arg_builder("must").long_("must").required()));
        return app.freeze();
    }
    constexpr command_spec parent = make_parent();

    consteval command_spec make_negates_conflict() {
        command_builder app("nc");
        std::move(app)
                .subcommand_negates_reqs()
                .arg(arg_builder("out").long_("out").required())
                .arg(arg_builder("alpha")
                             .long_("alpha")
                             .action(arg_action::set_true)
                             .conflicts_with("beta"))
                .arg(arg_builder("beta").long_("beta").action(arg_action::set_true))
                .subcommand(command_builder("run"));
        return app.freeze();
    }
    constexpr command_spec negates_conflict = make_negates_conflict();

    // -- the ordering fixtures ---------------------------------------------------
    //
    // `conflict_order` lives up in the fixture section. These four are its runtime-only
    // companions.

    // `--target` collides with the group rather than with `--mike` directly, so the group row
    // is what carries `--mike`'s position into the message. Reading the sorted map — or
    // ordering group rows after every argument, which is what an argv-index-only key would do
    // — puts `--mike` last on both command lines below.
    consteval command_spec make_group_order() {
        command_builder app("og");
        std::move(app)
                .arg(arg_builder("target")
                             .long_("target")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"zulu", "grp"}))
                .arg(arg_builder("zulu").long_("zulu").action(arg_action::set_true))
                .arg(arg_builder("mike").long_("mike").action(arg_action::set_true))
                .group(group_builder("grp").args({"mike"}));
        return app.freeze();
    }
    constexpr command_spec group_order = make_group_order();

    // Three innocent flags and one missing requirement: the `Usage:` line echoes all three
    // back, and `zulu` / `bravo` / `mike` sort differently from every command line below.
    consteval command_spec make_required_order() {
        command_builder app("orq");
        std::move(app)
                .arg(arg_builder("zulu").long_("zulu").action(arg_action::set_true))
                .arg(arg_builder("bravo").long_("bravo").action(arg_action::set_true))
                .arg(arg_builder("mike").long_("mike").action(arg_action::set_true))
                .arg(arg_builder("needed").long_("needed").required());
        return app.freeze();
    }
    constexpr command_spec required_order = make_required_order();

    // `--aa` and `--bb` pull in one requirement each, so the order the requirements are
    // DISCOVERED in — which is validator::gather_requires()'s loop — decides both the order of
    // the `were not provided` list and the order of the `Usage:` fragments.
    consteval command_spec make_requires_order() {
        command_builder app("orr");
        std::move(app)
                .arg(arg_builder("aa")
                             .long_("aa")
                             .action(arg_action::set_true)
                             .requires_all({"r1"}))
                .arg(arg_builder("bb")
                             .long_("bb")
                             .action(arg_action::set_true)
                             .requires_all({"r2"}))
                .arg(arg_builder("r1").long_("r1"))
                .arg(arg_builder("r2").long_("r2"));
        return app.freeze();
    }
    constexpr command_spec requires_order = make_requires_order();

    // `count` re-opens its entry on every occurrence and `append` does not — the two halves of
    // the saved row ordinals in clapp::arg_matches.
    consteval command_spec make_repeat_order() {
        command_builder app("orp");
        std::move(app)
                .arg(arg_builder("zulu").long_("zulu").action(arg_action::count))
                .arg(arg_builder("mike").long_("mike").action(arg_action::count))
                .arg(arg_builder("azulu").long_("azulu").action(arg_action::append))
                .arg(arg_builder("bmike").long_("bmike").action(arg_action::append))
                .arg(arg_builder("needed").long_("needed").required());
        return app.freeze();
    }
    constexpr command_spec repeat_order = make_repeat_order();

    // `aaa_env` sorts FIRST by id and must still be echoed LAST, because clap's `add_env` runs
    // after the whole command line.
    consteval command_spec make_env_order() {
        command_builder app("oe");
        std::move(app)
                .arg(arg_builder("aaa_env").long_("aaa-env").env("CLAPP_VALIDATOR_TEST_ORDER"))
                .arg(arg_builder("zulu").long_("zulu").action(arg_action::set_true))
                .arg(arg_builder("needed").long_("needed").required());
        return app.freeze();
    }
    constexpr command_spec env_order = make_env_order();

}  // namespace

// ---------------------------------------------------------------------------
// required
// ---------------------------------------------------------------------------

CLAPP_TEST("a missing required argument is reported by the name the user would type") {
    const outcome got = run(reqs, raw_args{"demo"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "the following required arguments were not provided:"));
    // Both of them, and by spelling — not by id, which for a positional is invisible.
    CLAPP_CHECK(says(got, "--out <out>"));
    CLAPP_CHECK(says(got, "<src>"));
}

CLAPP_TEST("a missing required argument message carries the usage line") {
    const outcome got = run(reqs, raw_args{"demo"});
    // The half of the message that says what to type INSTEAD. An error without it
    // reports a problem and offers no way out.
    CLAPP_CHECK(says(got, "Usage: demo --out <out> <src>"));
}

CLAPP_TEST("only the arguments still missing are listed") {
    const outcome got = run(reqs, raw_args{"demo", "--out", "x"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "<src>"));
    CLAPP_CHECK(!says(got, "--out <out>\n"));
}

CLAPP_TEST("a complete command line validates") {
    const outcome got = run(reqs, raw_args{"demo", "--out", "x", "s"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("out"));
    CLAPP_CHECK(got->contains_id("src"));
}

CLAPP_TEST("a missing_required_argument error exits 2 on stderr") {
    const outcome got = run(reqs, raw_args{"demo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(got.error().exit_code() == 2);
    CLAPP_CHECK(got.error().use_stderr());
}

// ---------------------------------------------------------------------------
// ArgGroup: required, and multiple
// ---------------------------------------------------------------------------

CLAPP_TEST("a required group with no member present names its alternatives") {
    const outcome got = run(groups, raw_args{"fmt"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    // The whole point of a group: the message offers the choice, rather than demanding
    // one arbitrary member.
    CLAPP_CHECK(says(got, "<--json|--yaml>"));
}

CLAPP_TEST("the injected default of a set_true flag does not occupy its group") {
    const outcome got = run(groups, raw_args{"fmt"});
    // clapp::command_builder gives every `set_true` argument `default_value("false")`, so
    // `--json` HAS an entry after a parse that never saw it. Asking "is it present?"
    // instead of "is it explicit?" makes every flag group permanently satisfied.
    CLAPP_CHECK(!got.has_value());
}

CLAPP_TEST("one member satisfies a required group") {
    const outcome got = run(groups, raw_args{"fmt", "--json"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("a single-valued group rejects two members, naming both") {
    const outcome got = run(groups, raw_args{"fmt", "--json", "--yaml"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "the argument '--json' cannot be used with '--yaml'"));
}

CLAPP_TEST("a multiple group accepts two members") {
    const outcome got = run(groups, raw_args{"fmt", "--json", "--loud", "--soft"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("a group conflict still carries a usage line") {
    const outcome got = run(groups, raw_args{"fmt", "--json", "--yaml"});
    CLAPP_CHECK(says(got, "Usage: fmt <--json|--yaml>"));
}

// ---------------------------------------------------------------------------
// conflicts_with
// ---------------------------------------------------------------------------

CLAPP_TEST("a declared conflict names both arguments") {
    const outcome got = run(conflict, raw_args{"conf", "--alpha", "--beta"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "'--alpha'"));
    CLAPP_CHECK(says(got, "'--beta'"));
    CLAPP_CHECK(says(got, "the argument '--alpha' cannot be used with '--beta'"));
}

CLAPP_TEST("a conflict is symmetric: only one side declared it") {
    // `--beta` says nothing about `--alpha`. Catching this needs the second loop in
    // clapp::detail::conflicts::gather_conflicts(), over the OTHER argument's list.
    const outcome got = run(conflict, raw_args{"conf", "--beta", "--alpha"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "'--alpha'"));
    CLAPP_CHECK(says(got, "'--beta'"));
}

CLAPP_TEST("the symmetric half really is what catches the non-declaring side") {
    // The case above is not enough on its own: `alpha` DECLARES the conflict and is also
    // typed first, so the declaring side's own list answers. `delta` declares nothing, so
    // the sentence below can only be produced by the second loop in gather_conflicts().
    // Deleting that loop still rejects the command line, but blames `--omega`, which is
    // the argument the user typed second and did nothing wrong.
    const outcome got = run(conflict, raw_args{"conf", "--delta", "--omega"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "the argument '--delta' cannot be used with '--omega'"));
}

CLAPP_TEST("the conflict message names the argument that was typed FIRST") {
    // The subject of the sentence is decided by clapp::detail::supplied_in_order(), and
    // it must follow the command line rather than the id ordering of the underlying map.
    // `alpha` sorts before `omega` and before `delta`, so an implementation that walks
    // clapp::arg_matches in key order produces the SAME sentence for both lines below —
    // and the pair is the only thing that notices, because both are still rejected with
    // the same clapp::error_kind and both still name both arguments.
    const outcome forward = run(conflict, raw_args{"conf", "--delta", "--omega"});
    const outcome reverse = run(conflict, raw_args{"conf", "--omega", "--delta"});
    CLAPP_CHECK(kind_of(forward) == error_kind::argument_conflict);
    CLAPP_CHECK(kind_of(reverse) == error_kind::argument_conflict);
    CLAPP_CHECK(says(forward, "the argument '--delta' cannot be used with '--omega'"));
    CLAPP_CHECK(says(reverse, "the argument '--omega' cannot be used with '--delta'"));
    CLAPP_CHECK(message_of(forward) != message_of(reverse));

    // ... and the same for the pair where one side declares nothing in either direction.
    const outcome ab = run(conflict, raw_args{"conf", "--alpha", "--beta"});
    const outcome ba = run(conflict, raw_args{"conf", "--beta", "--alpha"});
    CLAPP_CHECK(says(ab, "the argument '--alpha' cannot be used with '--beta'"));
    CLAPP_CHECK(says(ba, "the argument '--beta' cannot be used with '--alpha'"));
}

CLAPP_TEST("an argument that overrides itself is not in conflict with itself") {
    // `overrides_with(self)` is how clap spells "last one wins", and overrides are folded
    // into the conflict list — so without the self-id guard in gather_conflicts() the
    // second `--set` becomes `cannot be used multiple times`.
    const outcome got = run(self_override, raw_args{"so", "--set", "a", "--set", "b"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_one<std::string>("set").has_value());
}

CLAPP_TEST("a hidden argument stays out of a conflict's usage line too") {
    const outcome got = run(hidden_conflict, raw_args{"hc", "--secret", "--alpha", "--beta"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "the argument '--alpha' cannot be used with '--beta'"));
    // `--secret` was supplied and is not part of the conflict, so it would otherwise be
    // echoed back in the suggestion — advertising an argument the author hid.
    CLAPP_CHECK(!says(got, "--secret"));
}

CLAPP_TEST("a conflict with a group names the group's members, not the group") {
    const outcome got = run(conflict, raw_args{"conf", "--gamma", "--one"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "the argument '--gamma' cannot be used with:"));
    // `pack` is an id, not a spelling. Naming it would be unactionable.
    CLAPP_CHECK(says(got, "--one"));
    CLAPP_CHECK(says(got, "--two"));
    CLAPP_CHECK(!says(got, "pack"));
}

CLAPP_TEST("arguments that merely share a multiple group do not conflict") {
    const outcome got = run(conflict, raw_args{"conf", "--one", "--two"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("a conflict error carries a usage line built from what survived") {
    const outcome got = run(conflict, raw_args{"conf", "--alpha", "--beta"});
    // The conflicting argument is filtered out of the suggestion: telling the user to
    // type `--beta` again would be absurd.
    CLAPP_CHECK(says(got, "Usage: conf --alpha"));
    CLAPP_CHECK(!says(got, "Usage: conf --alpha --beta"));
}

// ---------------------------------------------------------------------------
// Every diagnostic list follows the command line, not the id ordering of the map
// ---------------------------------------------------------------------------
//
// clapp::arg_matches keys its arguments with a SORTED clapp::flat_map. A sorted container must not
// be used anywhere the order is observable. These four lists are exactly that, and they
// were all being read straight off the map — so a rename of an argument's *id* silently
// reordered user-facing prose, and, worse, the two orders of the same command line
// produced BYTE-IDENTICAL diagnostics. Each case below therefore asserts a PAIR: the
// message text alone cannot see the bug, because the alphabetical answer is a legal
// message that merely happens to be the same one for both inputs.
//
// Every expectation here was measured against clap_builder 4.6.5 on the same command line.

CLAPP_TEST("the usage line of a conflict echoes the survivors in the order they were typed") {
    // `mid` sorts before `other`, so the alphabetical answer is `--mid --other --zebra`
    // for BOTH of these — which is what clapp printed until the ordering was reconstructed.
    const outcome forward =
            run(conflict_order, raw_args{"ord", "--other", "--mid", "--zebra", "--alpha"});
    const outcome reverse =
            run(conflict_order, raw_args{"ord", "--mid", "--other", "--zebra", "--alpha"});
    CLAPP_CHECK(kind_of(forward) == error_kind::argument_conflict);
    CLAPP_CHECK(kind_of(reverse) == error_kind::argument_conflict);
    CLAPP_CHECK(says(forward, "Usage: ord --other --mid --zebra"));
    CLAPP_CHECK(says(reverse, "Usage: ord --mid --other --zebra"));
    CLAPP_CHECK(message_of(forward) != message_of(reverse));
}

CLAPP_TEST("the cannot-be-used-with list follows the command line") {
    // Sorted gives `--mike --yankee --zulu` for both. The declaration order of
    // `conflicts_with_all` is a third candidate answer (`zulu`, `yankee`, `mike`) and it
    // matches the forward case by accident — which is why the reverse case is here.
    const outcome forward =
            run(conflict_order, raw_args{"ord", "--target", "--zulu", "--yankee", "--mike"});
    const outcome reverse =
            run(conflict_order, raw_args{"ord", "--target", "--mike", "--yankee", "--zulu"});
    CLAPP_CHECK(says(forward, "cannot be used with:\n  --zulu\n  --yankee\n  --mike"));
    CLAPP_CHECK(says(reverse, "cannot be used with:\n  --mike\n  --yankee\n  --zulu"));
}

CLAPP_TEST("a group in the conflict list is unrolled at ITS member's position") {
    // `--target` names the GROUP, not `--mike`; the group's row is created by whichever
    // member the parse loop reached first, so it inherits that member's position. A key
    // built only from argv indices cannot see this — a group row has no index of its own,
    // so it would sink to the end and print `--zulu --mike` on both lines.
    const outcome forward = run(group_order, raw_args{"og", "--target", "--zulu", "--mike"});
    const outcome reverse = run(group_order, raw_args{"og", "--target", "--mike", "--zulu"});
    CLAPP_CHECK(says(forward, "cannot be used with:\n  --zulu\n  --mike"));
    CLAPP_CHECK(says(reverse, "cannot be used with:\n  --mike\n  --zulu"));
}

CLAPP_TEST("the usage line of a missing requirement echoes what was typed, in order") {
    // Sorted gives `--bravo --mike --zulu` for both.
    const outcome forward = run(required_order, raw_args{"orq", "--zulu", "--bravo", "--mike"});
    const outcome reverse = run(required_order, raw_args{"orq", "--mike", "--bravo", "--zulu"});
    CLAPP_CHECK(kind_of(forward) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(forward, "Usage: orq --needed <needed> --zulu --bravo --mike"));
    CLAPP_CHECK(says(reverse, "Usage: orq --needed <needed> --mike --bravo --zulu"));
    CLAPP_CHECK(message_of(forward) != message_of(reverse));
}

CLAPP_TEST("requirements are listed in the order the arguments that pulled them in were typed") {
    // This one is gather_requires(): the order it discovers targets in becomes the order of
    // clapp::detail::id_set, which is the order of BOTH the bullet list and the usage
    // fragments. Sorted gives `--r1` before `--r2` on both command lines.
    const outcome forward = run(requires_order, raw_args{"orr", "--aa", "--bb"});
    const outcome reverse = run(requires_order, raw_args{"orr", "--bb", "--aa"});
    CLAPP_CHECK(kind_of(forward) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(forward, "not provided:\n  --r1 <r1>\n  --r2 <r2>"));
    CLAPP_CHECK(says(reverse, "not provided:\n  --r2 <r2>\n  --r1 <r1>"));
    CLAPP_CHECK(says(forward, "Usage: orr --r1 <r1> --r2 <r2> --aa --bb"));
    CLAPP_CHECK(says(reverse, "Usage: orr --r2 <r2> --r1 <r1> --bb --aa"));
}

CLAPP_TEST("a repeated count argument moves to the end, a repeated append argument does not") {
    // `count` (like `set`, `set_true` and `set_false`) removes its entry and reopens it,
    // which in clap moves the row to the back of the map and here discards the earlier
    // indices — the two are the same answer, and that is the whole reason the FIRST
    // surviving index is the right key. `append` never removes, so it keeps its place.
    const outcome counted = run(repeat_order, raw_args{"orp", "--zulu", "--mike", "--zulu"});
    CLAPP_CHECK(says(counted, "Usage: orp --needed <needed> --mike... --zulu..."));
    const outcome counted_rev = run(repeat_order, raw_args{"orp", "--mike", "--zulu", "--mike"});
    CLAPP_CHECK(says(counted_rev, "Usage: orp --needed <needed> --zulu... --mike..."));

    const outcome appended =
            run(repeat_order, raw_args{"orp", "--azulu", "1", "--bmike", "2", "--azulu", "3"});
    CLAPP_CHECK(says(appended, "Usage: orp --needed <needed> --azulu <azulu> --bmike <bmike>"));
}

CLAPP_TEST("an argument supplied only by the environment is echoed last") {
#ifndef _WIN32
    // `aaa_env` sorts first by id, and clap prints it last, because `add_env` runs after
    // the whole command line. It has no argv index at all, which is the case the sentinel
    // is assigned after command-line collection — and getting it wrong the other way would
    // put the env-sourced argument at the FRONT of every usage line.
    setenv("CLAPP_VALIDATOR_TEST_ORDER", "1", 1);
    const outcome got = run(env_order, raw_args{"oe", "--zulu"});
    unsetenv("CLAPP_VALIDATOR_TEST_ORDER");
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "Usage: oe --needed <needed> --zulu --aaa-env <aaa_env>"));
#endif
}

CLAPP_TEST("an overridden argument is not also reported as a conflict") {
    // `overrides_with` is folded into the conflict list — an override IS a conflict that
    // the parse loop already resolved by deleting the loser. So this only stays quiet
    // because the loop really did remove it; if that removal ever stops happening the
    // parse silently keeps both values AND this case starts failing.
    const outcome forwards = run(overrides, raw_args{"ov", "--color", "--no-color"});
    CLAPP_CHECK(forwards.has_value());
    const outcome backwards = run(overrides, raw_args{"ov", "--no-color", "--color"});
    CLAPP_CHECK(backwards.has_value());
}

// ---------------------------------------------------------------------------
// exclusive
// ---------------------------------------------------------------------------

CLAPP_TEST("an exclusive argument on its own is fine") {
    const outcome got = run(conflict, raw_args{"conf", "--solo"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("an exclusive argument tolerates no company at all") {
    // No `conflicts_with` anywhere between these two: `exclusive` is a separate check
    // and it fires against an argument it was never told about.
    const outcome got = run(conflict, raw_args{"conf", "--solo", "--alpha"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "the argument '--solo' cannot be used with"));
}

CLAPP_TEST("an exclusive violation is reported before the ordinary conflicts") {
    // `--alpha --beta` is a declared conflict too. Whichever check runs first decides
    // the message, and clap runs `validate_exclusive` first.
    const outcome got = run(conflict, raw_args{"conf", "--solo", "--alpha", "--beta"});
    CLAPP_CHECK(says(got, "'--solo'"));
    CLAPP_CHECK(!says(got, "the argument '--alpha' cannot"));
}

// ---------------------------------------------------------------------------
// requires
// ---------------------------------------------------------------------------

CLAPP_TEST("requires is transitive") {
    const outcome got = run(chain, raw_args{"req", "--a"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    // `--a` requires `--b` requires `--c`. Both, or the closure was never computed.
    CLAPP_CHECK(says(got, "--b <b>"));
    CLAPP_CHECK(says(got, "--c <c>"));
}

CLAPP_TEST("satisfying the whole requires chain validates") {
    const outcome got = run(chain, raw_args{"req", "--a", "--b", "1", "--c", "2"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("an argument nobody required is not demanded") {
    const outcome got = run(chain, raw_args{"req", "--c", "2"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("requires_if fires only on the matching value") {
    const outcome fired = run(chain, raw_args{"req", "--mode", "fast"});
    CLAPP_CHECK(kind_of(fired) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(fired, "--key <key>"));

    const outcome quiet = run(chain, raw_args{"req", "--mode", "slow"});
    CLAPP_CHECK(quiet.has_value());
}

// ---------------------------------------------------------------------------
// required_unless_present
// ---------------------------------------------------------------------------

CLAPP_TEST("required_unless_present demands the argument when the excuse is absent") {
    const outcome got = run(unless, raw_args{"un"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "--cfg <cfg>"));
}

CLAPP_TEST("required_unless_present is excused by the named argument") {
    const outcome got = run(unless, raw_args{"un", "--stdin"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("required_unless_present is also satisfied by supplying the argument") {
    const outcome got = run(unless, raw_args{"un", "--cfg", "x"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("required_unless_present_any is excused by one of several") {
    // Two excuses, so `any` and `all` finally disagree — with a single-element list they
    // are the same predicate and the distinction is untested.
    const outcome one = run(unless_any, raw_args{"uy", "--p"});
    CLAPP_CHECK(one.has_value());
    const outcome other = run(unless_any, raw_args{"uy", "--q"});
    CLAPP_CHECK(other.has_value());
    const outcome neither = run(unless_any, raw_args{"uy"});
    CLAPP_CHECK(kind_of(neither) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(neither, "--cfg <cfg>"));
}

CLAPP_TEST("required_unless_present_all needs every excuse, not any one") {
    const outcome partial = run(unless_all, raw_args{"ua", "--x"});
    CLAPP_CHECK(kind_of(partial) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(partial, "--both <both>"));

    const outcome complete = run(unless_all, raw_args{"ua", "--x", "--y"});
    CLAPP_CHECK(complete.has_value());
}

// ---------------------------------------------------------------------------
// required_if_eq
// ---------------------------------------------------------------------------

CLAPP_TEST("required_if_eq fires on the exact value and not otherwise") {
    const outcome fired = run(req_if, raw_args{"ri", "--mode", "secure"});
    CLAPP_CHECK(kind_of(fired) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(fired, "--key <key>"));

    const outcome quiet = run(req_if, raw_args{"ri", "--mode", "other"});
    CLAPP_CHECK(quiet.has_value());
}

CLAPP_TEST("required_if_eq is satisfied by supplying the argument") {
    const outcome got =
            run(req_if, raw_args{"ri", "--mode", "secure", "--key", "k", "--audit", "a"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("required_if_eq_any fires on one rule out of several") {
    const outcome one = run(req_if, raw_args{"ri", "--env", "prod"});
    CLAPP_CHECK(kind_of(one) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(one, "--audit <audit>"));
    // ... and `--token`, which wants BOTH rules, stays quiet on the same line.
    CLAPP_CHECK(!says(one, "--token"));
}

CLAPP_TEST("required_if_eq_all needs every rule to match") {
    const outcome partial =
            run(req_if, raw_args{"ri", "--mode", "secure", "--key", "k", "--audit", "a"});
    CLAPP_CHECK(partial.has_value());

    const outcome complete =
            run(req_if,
                raw_args{"ri", "--mode", "secure", "--env", "prod", "--key", "k", "--audit", "a"});
    CLAPP_CHECK(kind_of(complete) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(complete, "--token <token>"));
}

// ---------------------------------------------------------------------------
// is_missing_required_ok
// ---------------------------------------------------------------------------

CLAPP_TEST("a required argument that conflicts with what was supplied is excused") {
    // `--out` is required and conflicts with `--dry`. Demanding it after `--dry` would
    // ask the user for something the next check would reject.
    const outcome got = run(excused, raw_args{"ex", "--dry"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("the same required argument is still demanded on its own") {
    const outcome got = run(excused, raw_args{"ex"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "--out <out>"));
}

CLAPP_TEST("supplying both halves of that pair is a conflict, not a requirement") {
    const outcome got = run(excused, raw_args{"ex", "--out", "x", "--dry"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "'--out"));
    CLAPP_CHECK(says(got, "'--dry'"));
}

CLAPP_TEST("a required argument is also excused through its group's conflicts") {
    // Here the conflict is declared against the GROUP, by the other argument — so
    // neither `--out`'s own conflict list nor the symmetric scan over it finds anything,
    // and only the loop over the argument's groups can excuse it.
    const outcome got = run(excused_by_group, raw_args{"eg", "--dry"});
    CLAPP_CHECK(got.has_value());

    const outcome alone = run(excused_by_group, raw_args{"eg"});
    CLAPP_CHECK(kind_of(alone) == error_kind::missing_required_argument);
}

CLAPP_TEST("a conflict is reported before an unmet requirement") {
    // `--alpha --beta` breaks two rules at once: it conflicts, and it leaves
    // `--gamma` — which `--alpha` requires — unsupplied. Both orders reject the command
    // line, so only the message says which check ran first.
    const outcome got = run(conflict_and_requires, raw_args{"cr", "--alpha", "--beta"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "the argument '--alpha' cannot be used with '--beta'"));
    CLAPP_CHECK(!says(got, "required arguments were not provided"));
    // `--gamma` does still appear — in the USAGE line, because clap's
    // build_conflict_err_usage() chains the requirements of everything that survived the
    // conflict. The requirement is shown as advice, not raised as the error.
    CLAPP_CHECK(says(got, "Usage: cr --gamma <gamma> --alpha"));

    // With the conflict removed, the requirement surfaces.
    const outcome then_required = run(conflict_and_requires, raw_args{"cr", "--alpha"});
    CLAPP_CHECK(kind_of(then_required) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(then_required, "--gamma <gamma>"));
}

// ---------------------------------------------------------------------------
// Value sources: a default is not an occurrence, the environment is
// ---------------------------------------------------------------------------

CLAPP_TEST("an environment variable satisfies a required argument") {
#ifndef _WIN32
    setenv("CLAPP_VALIDATOR_TEST_HOME", "/tmp", 1);
    const outcome got = run(envreq, raw_args{"er"});
    unsetenv("CLAPP_VALIDATOR_TEST_HOME");
    // clapp::is_explicit() means "not a default", NOT "typed on the command line".
    // Reading it the other way makes every env-supplied argument fail its own
    // `required()`.
    CLAPP_CHECK(got.has_value());
#endif
}

CLAPP_TEST("without the environment variable the same argument is missing") {
#ifndef _WIN32
    unsetenv("CLAPP_VALIDATOR_TEST_HOME");
    const outcome got = run(envreq, raw_args{"er"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "--home <home>"));
#endif
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommand_required with no subcommand names the command and the choices") {
    const outcome got = run(subs, raw_args{"git"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_subcommand);
    CLAPP_CHECK(says(got, "'git' requires a subcommand but one was not provided"));
    CLAPP_CHECK(says(got, "add"));
    CLAPP_CHECK(says(got, "commit"));
    CLAPP_CHECK(says(got, "Usage: git [OPTIONS] <COMMAND>"));
}

CLAPP_TEST("subcommand_required is satisfied by a subcommand") {
    const outcome got = run(subs, raw_args{"git", "add"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"add"});
}

CLAPP_TEST("subcommand_negates_reqs excuses the required arguments") {
    const outcome with_sub = run(negates, raw_args{"ng", "run"});
    CLAPP_CHECK(with_sub.has_value());

    const outcome without = run(negates, raw_args{"ng"});
    CLAPP_CHECK(kind_of(without) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(without, "--out <out>"));
}

CLAPP_TEST("subcommand_negates_reqs does not excuse a conflict") {
    // Only the *required* half is suppressed. `--out` is required and goes unmentioned
    // because `run` was named, but `--alpha --beta` is still rejected — clap runs
    // validate_conflicts unconditionally and returns before it consults the setting.
    const outcome got = run(negates_conflict, raw_args{"nc", "--alpha", "--beta", "run"});
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(says(got, "'--alpha'"));
    CLAPP_CHECK(says(got, "'--beta'"));

    const outcome quiet = run(negates_conflict, raw_args{"nc", "--alpha", "run"});
    CLAPP_CHECK(quiet.has_value());
}

// ---------------------------------------------------------------------------
// arg_required_else_help
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_required_else_help shows help when nothing was supplied") {
    const outcome got = run(else_help, raw_args{"eh"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help_on_missing_argument_or_subcommand);
    // Help-shaped, but still a failure: stderr and exit 2. Treating the kind as "this is
    // help" and exiting 0 turns a misuse into a success.
    CLAPP_CHECK(got.error().exit_code() == 2);
    CLAPP_CHECK(got.error().use_stderr());
    CLAPP_CHECK(says(got, "does things"));
    CLAPP_CHECK(says(got, "Usage: eh [OPTIONS]"));
}

CLAPP_TEST("arg_required_else_help stays quiet once an argument is supplied") {
    const outcome got = run(else_help, raw_args{"eh", "--tag", "x"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("arg_required_else_help stays quiet once a subcommand is named") {
    const outcome got = run(else_help, raw_args{"eh", "run"});
    CLAPP_CHECK(got.has_value());
}

// ---------------------------------------------------------------------------
// Positional display, and hidden arguments
// ---------------------------------------------------------------------------

CLAPP_TEST("a missing positional drags the earlier ones into the message") {
    const outcome got = run(positional_display, raw_args{"pd", "--mode", "x"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    // Only `<second>` is required — `<first>` is listed purely so the user can see where
    // in the line the missing value goes. Telling someone to supply the second
    // positional without mentioning the first is unusable advice.
    CLAPP_CHECK(says(got, "<first>"));
    CLAPP_CHECK(says(got, "<second>"));
}

CLAPP_TEST("allow_missing_positional drops that courtesy") {
    const outcome got = run(positional_display_allowed, raw_args{"pa", "--mode", "x"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "<second>"));
    CLAPP_CHECK(!says(got, "<first>"));
    // ... but the usage line still shows it, as an optional.
    CLAPP_CHECK(says(got, "[first] <second>"));
}

CLAPP_TEST("a last() positional does not drag the earlier ones in") {
    const outcome got = run(last_conditional, raw_args{"lc", "--mode", "x"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "<rest>"));
    // A `last()` positional sits behind `--` and so imposes no ordering on what precedes
    // it; letting it raise the high-water mark demands a positional nobody asked for.
    CLAPP_CHECK(!says(got, "<first>"));
}

CLAPP_TEST("the condition that never fires demands nothing") {
    const outcome got = run(positional_display, raw_args{"pd", "--mode", "y"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("a hidden argument is kept out of the usage line it helped provoke") {
    const outcome got = run(hidden, raw_args{"hd", "--secret"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "Usage: hd --out <out>"));
    CLAPP_CHECK(!says(got, "--secret"));
}

// ---------------------------------------------------------------------------
// The pieces, exercised directly
// ---------------------------------------------------------------------------

CLAPP_TEST("the conflict table holds a row per explicitly supplied id") {
    const raw_args line{"fmt", "--json"};
    clapp::arg_cursor cursor = line.cursor();
    static_cast<void>(line.next_os(cursor));

    clapp::detail::parse_engine engine{groups};
    clapp::detail::arg_matcher matcher{groups};
    CLAPP_CHECK(engine.get_matches_with(matcher, line, cursor).has_value());

    const conflicts table = conflicts::with_args(groups, matcher);
    // `json` and the group `format` are explicit; `yaml`, `loud` and `soft` are present
    // only through their injected `default_value("false")` and must not appear.
    CLAPP_CHECK(table.get_direct_conflicts("json").has_value());
    CLAPP_CHECK(table.get_direct_conflicts("format").has_value());
    CLAPP_CHECK(!table.get_direct_conflicts("yaml").has_value());

    // ... but a question about an ABSENT id still gets an answer, recomputed on the
    // spot. That is the path validator::is_missing_required_ok() takes.
    CLAPP_CHECK(!table.gather_conflicts(groups, "yaml").empty());
}

CLAPP_TEST("check_explicit reads the source, not merely the presence of a value") {
    const raw_args line{"eh", "--tag", "t"};
    clapp::arg_cursor cursor = line.cursor();
    static_cast<void>(line.next_os(cursor));

    clapp::detail::parse_engine engine{else_help};
    clapp::detail::arg_matcher matcher{else_help};
    CLAPP_CHECK(engine.get_matches_with(matcher, line, cursor).has_value());

    const clapp::matched_arg* defaulted = matcher.get("level");
    const clapp::matched_arg* supplied  = matcher.get("tag");
    CLAPP_CHECK(defaulted != nullptr);
    CLAPP_CHECK(supplied != nullptr);
    // `level` HAS a value — "info", from its default. It is still not "present" for any
    // purpose the validator cares about, and neither the presence nor the equality arm
    // may say otherwise.
    const clapp::arg_predicate is_info{.kind  = clapp::predicate_kind::equals,
                                       .value = arg_id{std::string_view{"info"}}};
    CLAPP_CHECK(!clapp::detail::check_explicit(*defaulted, clapp::arg_predicate::present()));
    CLAPP_CHECK(!clapp::detail::check_explicit(*defaulted, is_info));
    CLAPP_CHECK(clapp::detail::check_explicit(*supplied, clapp::arg_predicate::present()));
}

CLAPP_TEST("the validator's required set grows as requires targets are discovered") {
    const raw_args line{"req", "--a"};
    clapp::arg_cursor cursor = line.cursor();
    static_cast<void>(line.next_os(cursor));

    clapp::detail::parse_engine engine{chain};
    clapp::detail::arg_matcher matcher{chain};
    // get_matches_with() now ends with the validator, so this call already fails for the
    // same reason the fresh `checker` below does. What is under test is the SET the
    // validator accumulates while it walks, so the outcome is discarded rather than
    // asserted; the matcher is fully populated either way.
    static_cast<void>(engine.get_matches_with(matcher, line, cursor));

    clapp::detail::validator checker{chain};
    CLAPP_CHECK(checker.required().empty());  // nothing is required() outright
    CLAPP_CHECK(!checker.validate(matcher).has_value());
    CLAPP_CHECK(checker.required().contains("b"));
    CLAPP_CHECK(checker.required().contains("c"));
}

CLAPP_TEST("get_required_usage_from drops what is already satisfied") {
    const raw_args line{"demo", "--out", "x"};
    clapp::arg_cursor cursor = line.cursor();
    static_cast<void>(line.next_os(cursor));

    clapp::detail::parse_engine engine{reqs};
    clapp::detail::arg_matcher matcher{reqs};
    // `src` is required and absent, so get_matches_with()'s own validator pass fails.
    // Irrelevant here: the question is what the usage renderer does with a matcher in
    // which `--out` IS satisfied, and the matcher is populated regardless.
    static_cast<void>(engine.get_matches_with(matcher, line, cursor));

    const arg_id wanted[] = {arg_id{"src"}};
    const std::vector<styled_str> forms =
            usage_renderer{reqs}.get_required_usage_from(wanted, matcher, true);
    CLAPP_CHECK(forms.size() == 1);
    CLAPP_CHECK(forms.front().to_string() == "<src>");
}

CLAPP_TEST("the validator never writes through the matcher") {
    const raw_args line{"fmt", "--json"};
    clapp::arg_cursor cursor = line.cursor();
    static_cast<void>(line.next_os(cursor));

    clapp::detail::parse_engine engine{groups};
    clapp::detail::arg_matcher matcher{groups};
    CLAPP_CHECK(engine.get_matches_with(matcher, line, cursor).has_value());

    const std::size_t before = matcher.arg_count();
    CLAPP_CHECK(clapp::detail::validate(groups, matcher).has_value());
    CLAPP_CHECK(matcher.arg_count() == before);
    // ... and the matches are still usable afterwards, which is the whole reason the
    // hook sits where it does.
    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(result.get_flag("json"));
}

// ---------------------------------------------------------------------------
// The wiring
// ---------------------------------------------------------------------------

CLAPP_TEST("the validator is per command level, so the hook lives inside the engine") {
    // A subcommand's arguments are validated against the SUBCOMMAND's spec, by an
    // instance built from it. clapp::detail::parse_engine::parse_subcommand() recurses
    // through get_matches_with(), which is why the call sits THERE and not in
    // clapp::parse(): a hook at the top level would leave every subcommand unchecked.
    // Both halves are measured — the child alone, and the child reached through its
    // parent, which is the half a top-level hook would silently pass.
    const command_spec& child = *parent.find_subcommand("sub");
    CLAPP_CHECK(!run(child, raw_args{"sub"}).has_value());
    CLAPP_CHECK(says(run(child, raw_args{"sub"}), "--must <must>"));

    const outcome through_parent = run(parent, raw_args{"p", "sub"});
    CLAPP_CHECK(!through_parent.has_value());
    CLAPP_CHECK(kind_of(through_parent) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(through_parent, "--must <must>"));
}

CLAPP_TEST("clapp::parse() runs the validator") {
    // The seam. `clapp::detail::validator` being finished and `clapp::detail::validator`
    // being REACHED are two different claims, and only the second one is what a program
    // linking clapp gets. This case is the whole of the second claim: nothing but the
    // public entry point appears in it.
    const outcome direct = clapp::parse(reqs, raw_args{"demo"});
    CLAPP_CHECK(!direct.has_value());
    CLAPP_CHECK(kind_of(direct) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(direct, "--out <out>"));
    CLAPP_CHECK(says(direct, "<src>"));
    // ... and it is the LAST thing that runs: `--out` is satisfied by a default here,
    // which only the wave that applies defaults can have put there.
    CLAPP_CHECK(clapp::parse(reqs, raw_args{"demo", "--out", "x", "s"}).has_value());
}
