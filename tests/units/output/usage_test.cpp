#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/output/usage.hpp>
#include <clapp/util/id.hpp>

#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_id;
    using clapp::arg_predicate;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::group_builder;
    using clapp::os_str;
    using clapp::render_usage;
    using clapp::render_usage_body;
    using clapp::style_class;
    using clapp::styled_str;
    using clapp::value_range;
    using clapp::detail::arg_presence_source;
    using clapp::detail::id_set;
    using clapp::detail::required_graph;
    using clapp::detail::usage_renderer;

    // ---------------------------------------------------------------------------
    // Helpers
    //
    // Every fixture is built and frozen inside the consteval function that asserts on it.
    // A frozen command_spec at namespace scope would be tidier, but there would be forty of
    // them, each costing .rodata in a test binary that only ever reads it at compile time.
    // ---------------------------------------------------------------------------

    /** The whole line, heading included — what a user sees. */
    [[nodiscard]] consteval std::string line_of(const command_spec& spec) {
        const std::optional<styled_str> rendered = render_usage(spec);
        if (!rendered.has_value()) return {};
        return rendered->to_string();
    }

    /** The line clap's error path produces: `used` names what the user actually typed. */
    [[nodiscard]] consteval std::string line_of(const command_spec& spec,
                                                std::span<const arg_id> used) {
        const std::optional<styled_str> rendered = render_usage(spec, used);
        if (!rendered.has_value()) return {};
        return rendered->to_string();
    }

    // ---------------------------------------------------------------------------
    // The matcher seam
    //
    // clapp::detail::arg_presence_source, and a model of it that knows nothing about
    // <clapp/parser/arg_matcher.hpp>. This is the type that proves the concept is a seam:
    // if usage.hpp ever goes back to naming `arg_matcher`, this file stops compiling.
    // ---------------------------------------------------------------------------

    /**
     * A matcher over a fixed list of (id, value) pairs.
     *
     * `value` is compared byte-for-byte by clapp::arg_predicate::matches(), so an id
     * recorded with an empty value satisfies `present()` and no `equal_to()`.
     */
    struct fake_matcher {
        struct entry {
            std::string_view id{};
            std::string_view value{};
        };

        std::span<const entry> supplied{};

        [[nodiscard]] constexpr bool check_explicit(std::string_view id,
                                                    const arg_predicate& when) const {
            for (const entry& one : supplied) {
                if (one.id != id) continue;
                return when.matches(os_str{one.value});
            }
            return false;
        }
    };

    static_assert(arg_presence_source<fake_matcher>);

    /**
     * The negative half. Asserting only that `fake_matcher` models the concept would pass
     * under a concept that is satisfied by everything — which is exactly what an
     * `requires { true; }` typo produces.
     */
    struct not_a_matcher {
        [[nodiscard]] constexpr bool check_explicit(std::string_view) const { return false; }
    };

    static_assert(!arg_presence_source<not_a_matcher>);
    static_assert(!arg_presence_source<int>);

    // A `check_explicit` that returns something convertible to bool but is not bool is
    // rejected too: the renderer branches on the answer, and an accidental pointer return
    // would make every requirement look satisfied.
    struct wrong_return_matcher {
        [[nodiscard]] constexpr const char* check_explicit(std::string_view,
                                                           const arg_predicate&) const {
            return "";
        }
    };

    static_assert(!arg_presence_source<wrong_return_matcher>);

    // ---------------------------------------------------------------------------
    // Positionals — clap tests/builder/positionals.rs
    //
    // These five are the only places in clap's suite that call `Command::render_usage()`
    // directly, so they are the closest thing clap has to a unit test for this file.
    // ---------------------------------------------------------------------------

    consteval bool single_positional_usage_string() {
        command_builder cmd("test");
        std::move(cmd).arg(arg_builder("FILE"));
        return line_of(cmd.freeze()) == "Usage: test [FILE]";
    }

    static_assert(single_positional_usage_string());

    consteval bool single_positional_multiple_usage_string() {
        command_builder cmd("test");
        std::move(cmd).arg(arg_builder("FILE").num_args(value_range::at_least(1)));
        return line_of(cmd.freeze()) == "Usage: test [FILE]...";
    }

    static_assert(single_positional_multiple_usage_string());

    consteval bool multiple_positional_usage_string() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("FILE"))
                .arg(arg_builder("FILES").num_args(value_range::at_least(1)));
        return line_of(cmd.freeze()) == "Usage: test [FILE] [FILES]...";
    }

    static_assert(multiple_positional_usage_string());

    consteval bool multiple_positional_one_required_usage_string() {
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("FILE").required())
                .arg(arg_builder("FILES").num_args(value_range::at_least(1)));
        return line_of(cmd.freeze()) == "Usage: test <FILE> [FILES]...";
    }

    static_assert(multiple_positional_one_required_usage_string());

    consteval bool single_positional_required_usage_string() {
        command_builder cmd("test");
        std::move(cmd).arg(arg_builder("FILE").required());
        return line_of(cmd.freeze()) == "Usage: test <FILE>";
    }

    static_assert(single_positional_required_usage_string());

    // ---------------------------------------------------------------------------
    // Multi-value positionals — clap tests/builder/multiple_values.rs
    //
    // `num_args` decides both how many placeholders appear and whether a `...` follows, and
    // the two rules are not the same rule: `num_args(3)` prints three and no ellipsis,
    // `num_args(3..)` prints three *and* an ellipsis, `num_args(1..=3)` prints one and an
    // ellipsis. An implementation that printed `min` placeholders and appended `...`
    // whenever `max > min` gets the middle one wrong and nothing else.
    // ---------------------------------------------------------------------------

    consteval bool an_exact_count_repeats_the_placeholder() {
        // clap `positional_exact_less`: `Usage: myprog [pos] [pos] [pos]`.
        command_builder cmd("myprog");
        std::move(cmd).arg(arg_builder("pos").num_args(value_range::exactly(3)));
        return line_of(cmd.freeze()) == "Usage: myprog [pos] [pos] [pos]";
    }

    static_assert(an_exact_count_repeats_the_placeholder());

    consteval bool an_open_minimum_repeats_and_then_trails() {
        // clap `positional_min_less`: `Usage: myprog [pos] [pos] [pos]...`.
        command_builder cmd("myprog");
        std::move(cmd).arg(arg_builder("pos").num_args(value_range::at_least(3)));
        return line_of(cmd.freeze()) == "Usage: myprog [pos] [pos] [pos]...";
    }

    static_assert(an_open_minimum_repeats_and_then_trails());

    consteval bool a_bounded_range_shows_one_and_trails() {
        // clap `positional_max_more`: `Usage: myprog [pos]...`.
        command_builder cmd("myprog");
        std::move(cmd).arg(arg_builder("pos").num_args(value_range::between(1, 3)));
        return line_of(cmd.freeze()) == "Usage: myprog [pos]...";
    }

    static_assert(a_bounded_range_shows_one_and_trails());

    consteval bool a_variadic_positional_keeps_its_neighbours() {
        // clap `multiple_value_terminator_positional`:
        // `Usage: lip [OPTIONS] [files]... [other]`.
        // A variadic positional followed by another one is only well-formed because of the
        // `value_terminator`, and clapp::command_builder::freeze() rejects it without one —
        // which is a check clap performs at run time and clapp performs at compile time.
        // The usage line says nothing about the terminator, and that is clap's behaviour too.
        command_builder cmd("lip");
        std::move(cmd)
                .arg(arg_builder("files").num_args(value_range::at_least(0)).value_terminator(";"))
                .arg(arg_builder("other"))
                .arg(arg_builder("stop").short_('X').action(arg_action::set_true));
        return line_of(cmd.freeze()) == "Usage: lip [OPTIONS] [files]... [other]";
    }

    static_assert(a_variadic_positional_keeps_its_neighbours());

    // ---------------------------------------------------------------------------
    // `[OPTIONS]` — clap's `Usage::needs_options_tag`
    //
    // The tag is a summary of everything the line does *not* name. Every rule that removes
    // an option from that summary is a rule that can silently over- or under-report.
    // ---------------------------------------------------------------------------

    consteval bool an_optional_option_hides_behind_the_tag() {
        command_builder cmd("t");
        std::move(cmd).arg(arg_builder("tag").long_("tag"));
        return line_of(cmd.freeze()) == "Usage: t [OPTIONS]";
    }

    static_assert(an_optional_option_hides_behind_the_tag());

    consteval bool the_tag_disappears_when_every_option_is_named() {
        // A command whose only option is required has nothing left to summarise.
        command_builder cmd("t");
        std::move(cmd).arg(arg_builder("out").long_("out").required());
        return line_of(cmd.freeze()) == "Usage: t --out <out>";
    }

    static_assert(the_tag_disappears_when_every_option_is_named());

    consteval bool smart_usage_drops_the_tag_outright() {
        // With a `used` list the line answers "what does *this* command line still need",
        // and clap prints no `[OPTIONS]` at all — even though `--tag` is still optional and
        // still unmentioned. Not an oversight: the line is no longer a summary.
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("out").long_("out").required())
                .arg(arg_builder("tag").long_("tag"))
                .arg(arg_builder("src").required());
        const command_spec spec = cmd.freeze();
        const arg_id used[]     = {arg_id{"tag"}};
        return line_of(spec) == "Usage: demo [OPTIONS] --out <out> <src>" &&
               line_of(spec, used) == "Usage: demo --out <out> --tag <tag> <src>";
    }

    static_assert(smart_usage_drops_the_tag_outright());

    // ---------------------------------------------------------------------------
    // Groups — clap tests/builder/groups.rs
    // ---------------------------------------------------------------------------

    consteval bool req_group_usage_string() {
        // clap `req_group_usage_string`: `Usage: clap-test <base|--delete>`.
        // The positional member renders WITHOUT brackets inside the group, the flag member
        // renders with its spelling. Rendering the ids instead would produce `<base|delete>`,
        // and `delete` is not something a user can type.
        command_builder cmd("clap-test");
        std::move(cmd)
                .arg(arg_builder("base"))
                .arg(arg_builder("delete").short_('d').long_("delete").action(arg_action::set_true))
                .group(group_builder("base_or_delete").args({"base", "delete"}).required());
        return line_of(cmd.freeze()) == "Usage: clap-test <base|--delete>";
    }

    static_assert(req_group_usage_string());

    consteval bool req_group_of_only_options() {
        // clap `req_group_with_conflict_usage_string_only_options`:
        // `Usage: clap-test <--all|--delete>`.
        command_builder cmd("clap-test");
        std::move(cmd)
                .arg(arg_builder("all").short_('a').long_("all").action(arg_action::set_true))
                .arg(arg_builder("delete").short_('d').long_("delete").action(arg_action::set_true))
                .group(group_builder("all_or_delete").args({"all", "delete"}).required());
        return line_of(cmd.freeze()) == "Usage: clap-test <--all|--delete>";
    }

    static_assert(req_group_of_only_options());

    consteval bool group_usage_use_val_name() {
        // clap `group_usage_use_val_name`: `Usage: prog <A>`. The group names its member by
        // the member's `value_name`, not by its id.
        command_builder cmd("prog");
        std::move(cmd)
                .arg(arg_builder("a").value_name("A"))
                .group(group_builder("group").arg("a").required());
        return line_of(cmd.freeze()) == "Usage: prog <A>";
    }

    static_assert(group_usage_use_val_name());

    consteval bool a_group_member_is_not_listed_twice() {
        // The member is rendered inside the group, so the positional loop must skip it.
        // Without the `required_groups_members` check the line reads `prog <A> [A]`.
        command_builder cmd("prog");
        std::move(cmd)
                .arg(arg_builder("a").value_name("A"))
                .group(group_builder("group").arg("a").required());
        const command_spec spec = cmd.freeze();
        const std::string line  = line_of(spec);
        return line == "Usage: prog <A>" && line.find("[A]") == std::string::npos;
    }

    static_assert(a_group_member_is_not_listed_twice());

    // ---------------------------------------------------------------------------
    // Required sets — clap tests/builder/require.rs
    // ---------------------------------------------------------------------------

    consteval bool positional_required_with_requires() {
        // clap `positional_required_with_requires`: `Usage: clap-test <flag> <opt> [bar]`.
        // `flag` is required and requires `opt`, so `opt` is rendered `<opt>` even though it
        // is not itself declared required — and `bar`, which nothing demands, stays `[bar]`.
        command_builder cmd("clap-test");
        std::move(cmd)
                .arg(arg_builder("flag").required().requires_("opt"))
                .arg(arg_builder("opt"))
                .arg(arg_builder("bar"));
        return line_of(cmd.freeze()) == "Usage: clap-test <flag> <opt> [bar]";
    }

    static_assert(positional_required_with_requires());

    consteval bool positional_required_with_requires_if_no_value() {
        // clap `positional_required_with_requires_if_no_value`:
        // `Usage: clap-test <flag> [opt] [bar]`.
        // A `requires_if` edge cannot be judged without the matches, so the help usage drops
        // it and `opt` stays optional. Reading the edge unconditionally would demand an
        // argument the user may never need.
        command_builder cmd("clap-test");
        std::move(cmd)
                .arg(arg_builder("flag").required().requires_if("val", "opt"))
                .arg(arg_builder("opt"))
                .arg(arg_builder("bar"));
        return line_of(cmd.freeze()) == "Usage: clap-test <flag> [opt] [bar]";
    }

    static_assert(positional_required_with_requires_if_no_value());

    consteval bool required_error_doesnt_duplicate() {
        // clap `required_error_doesnt_duplicate`: `Usage: clap-test -b <b> <a>`.
        // Options come before positionals regardless of declaration order.
        command_builder cmd("clap-test");
        std::move(cmd)
                .arg(arg_builder("a").required())
                .arg(arg_builder("b").short_('b').conflicts_with("c"))
                .arg(arg_builder("c").short_('c').conflicts_with("b"));
        const command_spec spec = cmd.freeze();
        const arg_id used[]     = {arg_id{"b"}};
        return line_of(spec, used) == "Usage: clap-test -b <b> <a>";
    }

    static_assert(required_error_doesnt_duplicate());

    consteval bool required_require_with_group_shows_flag() {
        // clap `required_require_with_group_shows_flag`:
        // `Usage: test --require-first <--first|--second>`.
        // `--second` was typed, and it is a member of the required group, so it must NOT be
        // listed on its own next to the group that already contains it.
        command_builder cmd("test");
        std::move(cmd)
                .arg(arg_builder("require-first")
                             .long_("require-first")
                             .action(arg_action::set_true)
                             .requires_("first"))
                .arg(arg_builder("first").long_("first").action(arg_action::set_true))
                .arg(arg_builder("second").long_("second").action(arg_action::set_true))
                .group(group_builder("either_or_both")
                               .args({"first", "second"})
                               .multiple()
                               .required());
        const command_spec spec = cmd.freeze();
        const arg_id used[]     = {arg_id{"require-first"}, arg_id{"second"}};
        return line_of(spec, used) == "Usage: test --require-first <--first|--second>";
    }

    static_assert(required_require_with_group_shows_flag());

    consteval bool requires_group_with_overlapping_group_in_error() {
        // clap `requires_group_with_overlapping_group_in_error`:
        // `Usage: prog --config <--in|--spec>`.
        // `--config` belongs to the non-required group `all`, whose members must not leak
        // into the line; only the required group `input` does.
        command_builder cmd("prog");
        std::move(cmd)
                .arg(arg_builder("in").long_("in").action(arg_action::set_true))
                .arg(arg_builder("spec").long_("spec").action(arg_action::set_true))
                .arg(arg_builder("config").long_("config").action(arg_action::set_true))
                .group(group_builder("all").args({"in", "spec", "config"}).multiple())
                .group(group_builder("input").args({"in", "spec"}).required());
        const command_spec spec = cmd.freeze();
        const arg_id used[]     = {arg_id{"config"}};
        return line_of(spec, used) == "Usage: prog --config <--in|--spec>";
    }

    static_assert(requires_group_with_overlapping_group_in_error());

    consteval bool issue_1158_conflicting_requirements() {
        // clap `issue_1158_conflicting_requirements`:
        // `Usage: example -x <X> -y <Y> -z <Z> <ID>`.
        // Three requirements unrolled from one positional, all rendered in declaration order
        // ahead of the positional that demanded them.
        command_builder cmd("example");
        std::move(cmd)
                .arg(arg_builder("config").short_('c').long_("config").value_name("FILE"))
                .arg(arg_builder("ID").required().requires_all({"x", "y", "z"}))
                .arg(arg_builder("x").short_('x').value_name("X"))
                .arg(arg_builder("y").short_('y').value_name("Y"))
                .arg(arg_builder("z").short_('z').value_name("Z"));
        const command_spec spec = cmd.freeze();
        const arg_id used[]     = {arg_id{"ID"}};
        return line_of(spec, used) == "Usage: example -x <X> -y <Y> -z <Z> <ID>";
    }

    static_assert(issue_1158_conflicting_requirements());

    consteval bool require_equals_puts_the_sign_in_the_line() {
        // clap `require_equals` usage, `tests/builder/require.rs`:
        // `Usage: clap-test --opt=<FILE>`. The `=` is `style_class::literal`, because it is
        // something the user must type — a space there is an error.
        command_builder cmd("clap-test");
        std::move(cmd).arg(
                arg_builder("opt").long_("opt").value_name("FILE").require_equals().required());
        const command_spec spec = cmd.freeze();
        const styled_str line   = *render_usage(spec);
        return line.to_string() == "Usage: clap-test --opt=<FILE>" &&
               line.text_of(style_class::literal) == "clap-test--opt=";
    }

    static_assert(require_equals_puts_the_sign_in_the_line());

    // ---------------------------------------------------------------------------
    // The escape hatch: `last()` positionals
    // ---------------------------------------------------------------------------

    consteval bool an_optional_last_positional_is_fenced() {
        // `last()` means "only reachable after `--`". Printing `[rest]...` alone would send
        // the user looking for an argument they cannot reach.
        command_builder cmd("lp");
        std::move(cmd)
                .arg(arg_builder("opt").long_("opt"))
                .arg(arg_builder("rest").num_args(value_range::at_least(0)).last());
        return line_of(cmd.freeze()) == "Usage: lp [OPTIONS] [-- [rest]...]";
    }

    static_assert(an_optional_last_positional_is_fenced());

    consteval bool a_required_last_positional_loses_the_brackets() {
        // Required, so the `--` is not optional either: clap prints `-- <rest>` with no
        // enclosing brackets. The two shapes come from different branches of `write_args`
        // and only one of them was covered before M5.
        command_builder cmd("esc");
        std::move(cmd).arg(arg_builder("rest").required().last());
        return line_of(cmd.freeze()) == "Usage: esc -- <rest>";
    }

    static_assert(a_required_last_positional_loses_the_brackets());

    // ---------------------------------------------------------------------------
    // Subcommands
    // ---------------------------------------------------------------------------

    consteval bool an_optional_subcommand_is_bracketed() {
        // clap `tests/builder/app_settings.rs`: `Usage: clap-test [COMMAND]`.
        command_builder cmd("clap-test");
        std::move(cmd).subcommand(command_builder("add"));
        return line_of(cmd.freeze()) == "Usage: clap-test [COMMAND]";
    }

    static_assert(an_optional_subcommand_is_bracketed());

    consteval bool a_required_subcommand_is_angled() {
        // clap `tests/builder/app_settings.rs`: `Usage: sc_required <COMMAND>`.
        command_builder cmd("sc_required");
        std::move(cmd).subcommand_required().subcommand(command_builder("add"));
        return line_of(cmd.freeze()) == "Usage: sc_required <COMMAND>";
    }

    static_assert(a_required_subcommand_is_angled());

    consteval bool external_subcommands_alone_still_get_a_placeholder() {
        // No *visible* subcommand exists, but an external one may be typed, so the line has
        // to leave room for it. Testing only `has_visible_subcommands()` drops this.
        command_builder cmd("ext");
        std::move(cmd).allow_external_subcommands();
        return line_of(cmd.freeze()) == "Usage: ext [COMMAND]";
    }

    static_assert(external_subcommands_alone_still_get_a_placeholder());

    consteval bool a_hidden_subcommand_is_not_a_visible_one() {
        // The only subcommand is hidden and external subcommands are off, so there is
        // nothing to announce and no placeholder at all. `disable_help_subcommand()` is
        // load-bearing: having *any* subcommand makes clapp inject a visible `help` one, and
        // that alone would bring the placeholder back — which is a real behaviour, not
        // scaffolding, and is why the assertion says what it says.
        command_builder cmd("hid");
        command_builder child("secret");
        std::move(child).hide();
        std::move(cmd).disable_help_subcommand().subcommand(std::move(child));
        return line_of(cmd.freeze()) == "Usage: hid";
    }

    static_assert(a_hidden_subcommand_is_not_a_visible_one());

    consteval bool the_placeholder_can_be_renamed() {
        command_builder cmd("tool");
        std::move(cmd).subcommand_value_name("TASK").subcommand(command_builder("build"));
        return line_of(cmd.freeze()) == "Usage: tool [TASK]";
    }

    static_assert(the_placeholder_can_be_renamed());

    consteval bool subcommands_that_negate_reqs_get_a_second_line() {
        // Two ways to invoke the command, so two lines. Printing only the first tells the
        // user an argument is required that a subcommand would have excused.
        command_builder cmd("ng");
        std::move(cmd)
                .subcommand_negates_reqs()
                .arg(arg_builder("out").long_("out").required())
                .subcommand(command_builder("run"));
        return line_of(cmd.freeze()) == "Usage: ng --out <out>\n       ng <COMMAND>";
    }

    static_assert(subcommands_that_negate_reqs_get_a_second_line());

    consteval bool args_that_conflict_with_subcommands_get_a_bare_second_line() {
        // `args_conflicts_with_subcommands` short-circuits the second line to just the
        // binary name: no argument is relevant once a subcommand is in play.
        command_builder cmd("ac");
        std::move(cmd)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("out").long_("out"))
                .subcommand(command_builder("run"));
        return line_of(cmd.freeze()) == "Usage: ac [OPTIONS]\n       ac <COMMAND>";
    }

    static_assert(args_that_conflict_with_subcommands_get_a_bare_second_line());

    consteval bool flatten_help_lists_one_line_per_subcommand() {
        // clap `flatten_help_cmd` (tests/builder/help.rs), minus the injected `help`
        // subcommand, which clapp can switch off and clap cannot:
        //   Usage: parent [OPTIONS]
        //          parent test [OPTIONS]
        // Each child line is rendered by a *child* renderer that was handed the path its
        // parent computed; without that the second line reads `test [OPTIONS]`.
        command_builder cmd("parent");
        command_builder child("test");
        std::move(child).arg(arg_builder("child").long_("child"));
        std::move(cmd)
                .flatten_help()
                .disable_help_subcommand()
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(child));
        return line_of(cmd.freeze()) == "Usage: parent [OPTIONS]\n       parent test [OPTIONS]";
    }

    static_assert(flatten_help_lists_one_line_per_subcommand());

    consteval bool a_subcommand_named_by_its_parent_says_so() {
        // A frozen command_spec cannot know its own path, so `render_usage(sub)` says
        // `clone` and the caller has to supply `git clone`. This is the parameter that makes
        // `git clone --help` print a usage line the user can actually type.
        command_builder cmd("clone");
        std::move(cmd).arg(arg_builder("repo").required());
        const command_spec spec               = cmd.freeze();
        const std::optional<styled_str> bare  = render_usage(spec);
        const std::optional<styled_str> named = render_usage(spec, {}, "git clone");
        return bare->to_string() == "Usage: clone <repo>" &&
               named->to_string() == "Usage: git clone <repo>";
    }

    static_assert(a_subcommand_named_by_its_parent_says_so());

    consteval bool a_flag_subcommand_names_all_three_spellings() {
        // clap's `Command::_build_subcommand`: a subcommand reachable as `sub`, `--sub` or
        // `-s` is announced as `{sub|--sub|-s}`, braced. Naming only `sub` hides two thirds
        // of the interface.
        command_builder parent("test");
        command_builder child("sub");
        std::move(child).long_flag("sub").short_flag('s');
        std::move(parent).subcommand(std::move(child));
        const command_spec spec = parent.freeze();
        return usage_renderer{spec}.subcommand_usage_name(spec.get_subcommands().front(), "test") ==
               "test {sub|--sub|-s}";
    }

    static_assert(a_flag_subcommand_names_all_three_spellings());

    consteval bool a_parents_requirements_travel_into_the_child_path() {
        // clap threads the parent's still-outstanding requirements into the child's
        // `usage_name`, so `test --out <out> sub` is what the child's own errors quote.
        command_builder parent("test");
        std::move(parent)
                .arg(arg_builder("out").long_("out").required())
                .subcommand(command_builder("sub"));
        const command_spec spec = parent.freeze();
        return usage_renderer{spec}.subcommand_usage_name(spec.get_subcommands().front(), "test") ==
               "test --out <out> sub";
    }

    static_assert(a_parents_requirements_travel_into_the_child_path());

    consteval bool negating_subcommands_strips_the_requirements_from_the_child_path() {
        // With `subcommand_negates_reqs`, the child does not inherit the parent's demands,
        // so quoting them in the child's usage line would be a lie.
        command_builder parent("test");
        std::move(parent)
                .subcommand_negates_reqs()
                .arg(arg_builder("out").long_("out").required())
                .subcommand(command_builder("sub"));
        const command_spec spec = parent.freeze();
        return usage_renderer{spec}.subcommand_usage_name(spec.get_subcommands().front(), "test") ==
               "test sub";
    }

    static_assert(negating_subcommands_strips_the_requirements_from_the_child_path());

    // ---------------------------------------------------------------------------
    // override_usage, bin_name
    // ---------------------------------------------------------------------------

    consteval bool override_usage_replaces_everything() {
        command_builder cmd("ov");
        std::move(cmd).override_usage("ov [MAGIC]").arg(arg_builder("out").long_("out").required());
        const command_spec spec = cmd.freeze();
        const arg_id used[]     = {arg_id{"out"}};
        // Both entry points, and both the help and the smart path: an override that only
        // covered `--help` would leave error messages describing a different command.
        return line_of(spec) == "Usage: ov [MAGIC]" && line_of(spec, used) == "Usage: ov [MAGIC]" &&
               render_usage_body(spec)->to_string() == "ov [MAGIC]";
    }

    static_assert(override_usage_replaces_everything());

    consteval bool bin_name_beats_the_command_name() {
        command_builder cmd("inner");
        std::move(cmd).bin_name("outer sub").arg(arg_builder("src").required());
        return line_of(cmd.freeze()) == "Usage: outer sub <src>";
    }

    static_assert(bin_name_beats_the_command_name());

    // ---------------------------------------------------------------------------
    // The two entry points, and the style classes
    // ---------------------------------------------------------------------------

    consteval bool the_body_is_the_line_without_the_heading() {
        command_builder cmd("demo");
        std::move(cmd).arg(arg_builder("out").long_("out").required());
        const command_spec spec = cmd.freeze();
        const styled_str titled = *render_usage(spec);
        const styled_str bare   = *render_usage_body(spec);
        return titled.to_string() == "Usage: demo --out <out>" &&
               bare.to_string() == "demo --out <out>" &&
               titled.text_of(style_class::usage) == "Usage:" &&
               bare.text_of(style_class::usage).empty();
    }

    static_assert(the_body_is_the_line_without_the_heading());

    consteval bool the_fragments_carry_the_right_classes() {
        // A to_string() comparison cannot see any of this, and a colour front-end sees
        // nothing else. `demo` and `--out` are literal (type them verbatim); `<out>`,
        // `[OPTIONS]` and `[src]` are placeholders (substitute something). The space between
        // `--out` and `<out>` is a placeholder too, which is why the concatenation below has
        // one in it: clap emits it inside the placeholder style so that `--out <out>` is
        // underlined as one unit rather than as two.
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("out").long_("out").required())
                .arg(arg_builder("tag").long_("tag"))
                .arg(arg_builder("src"));
        const styled_str line = *render_usage(cmd.freeze());
        return line.text_of(style_class::usage) == "Usage:" &&
               line.text_of(style_class::literal) == "demo--out" &&
               line.text_of(style_class::placeholder) == "[OPTIONS] <out>[src]";
    }

    static_assert(the_fragments_carry_the_right_classes());

    consteval bool nothing_to_say_is_nullopt_not_an_empty_string() {
        // Both entry points return `std::optional`, and the empty case is reachable: a
        // command with no name, no arguments and no subcommands renders nothing at all.
        // The caller has to be able to tell "no usage" from "a usage line that happens to be
        // blank", or clapp::error emits a stray `Usage:` under its prose.
        //
        // Built as a raw clapp::command_spec rather than through the builder on purpose:
        // clapp::command_builder::freeze() rejects an empty name outright, which is the
        // right rule for a CLI and leaves this branch reachable only from a hand-written
        // spec.
        constexpr command_spec nameless{.name = arg_id{""}};
        return !render_usage(nameless).has_value() && !render_usage_body(nameless).has_value();
    }

    static_assert(nothing_to_say_is_nullopt_not_an_empty_string());

    // ---------------------------------------------------------------------------
    // get_required_usage_from — the bullet list, and the matcher seam
    //
    // This is what fills `error: the following required arguments were not provided:`; it is
    // NOT create_usage_with_title(), and the two disagree in ways that matter (a satisfied
    // requirement disappears from the bullet list and stays in the usage line).
    // ---------------------------------------------------------------------------

    [[nodiscard]] consteval std::vector<std::string> forms_of(const usage_renderer& renderer,
                                                              const fake_matcher& matcher) {
        std::vector<std::string> out;
        for (const styled_str& one : renderer.get_required_usage_from({}, matcher, true))
            out.push_back(one.to_string());
        return out;
    }

    consteval bool the_bullet_list_names_each_requirement_separately() {
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("out").long_("out").required())
                .arg(arg_builder("src").required());
        const command_spec spec = cmd.freeze();
        const fake_matcher empty{};
        const std::vector<std::string> forms = forms_of(usage_renderer{spec}, empty);
        return forms.size() == 2 && forms[0] == "--out <out>" && forms[1] == "<src>";
    }

    static_assert(the_bullet_list_names_each_requirement_separately());

    consteval bool a_satisfied_requirement_leaves_the_bullet_list() {
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("out").long_("out").required())
                .arg(arg_builder("src").required());
        const command_spec spec              = cmd.freeze();
        const fake_matcher::entry supplied[] = {{.id = "out", .value = "x"}};
        const fake_matcher matcher{.supplied = supplied};
        const std::vector<std::string> forms = forms_of(usage_renderer{spec}, matcher);
        // The usage line still names `--out`; the bullet list must not, or the message reads
        // "you did not provide --out" immediately after the user provided it.
        return forms.size() == 1 && forms[0] == "<src>" &&
               render_usage(spec)->to_string() == "Usage: demo --out <out> <src>";
    }

    static_assert(a_satisfied_requirement_leaves_the_bullet_list());

    consteval bool a_satisfied_group_leaves_the_bullet_list() {
        // clap checks group satisfaction member by member: any member present satisfies the
        // group. The group id itself is never in the matches, so asking about it directly
        // would report every required group as missing.
        command_builder cmd("fmt");
        std::move(cmd)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("yaml").long_("yaml").action(arg_action::set_true))
                .group(group_builder("format").args({"json", "yaml"}).required());
        const command_spec spec = cmd.freeze();
        const fake_matcher nothing{};
        const fake_matcher::entry supplied[] = {{.id = "yaml", .value = ""}};
        const fake_matcher has_yaml{.supplied = supplied};
        const std::vector<std::string> missing   = forms_of(usage_renderer{spec}, nothing);
        const std::vector<std::string> satisfied = forms_of(usage_renderer{spec}, has_yaml);
        return missing.size() == 1 && missing[0] == "<--json|--yaml>" && satisfied.empty();
    }

    static_assert(a_satisfied_group_leaves_the_bullet_list());

    consteval bool a_requires_if_edge_fires_only_on_its_value() {
        // `--mode=fast requires --jobs`. With `--mode slow` the edge is dead; with
        // `--mode fast` it demands `--jobs`. The matcher-less overload is clap's `None` and
        // drops the edge unconditionally — which is a THIRD answer, not a synonym for
        // either of the first two.
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("mode").long_("mode").required().requires_if("fast", "jobs"))
                .arg(arg_builder("jobs").long_("jobs"));
        const command_spec spec = cmd.freeze();

        const fake_matcher::entry slow_entries[] = {{.id = "mode", .value = "slow"}};
        const fake_matcher::entry fast_entries[] = {{.id = "mode", .value = "fast"}};
        const fake_matcher slow{.supplied = slow_entries};
        const fake_matcher fast{.supplied = fast_entries};

        const std::vector<std::string> on_slow = forms_of(usage_renderer{spec}, slow);
        const std::vector<std::string> on_fast = forms_of(usage_renderer{spec}, fast);

        std::vector<std::string> without_matches;
        for (const styled_str& one : usage_renderer{spec}.get_required_usage_from({}, true))
            without_matches.push_back(one.to_string());

        return on_slow.empty()                                          // mode supplied, edge dead
               && on_fast.size() == 1 && on_fast[0] == "--jobs <jobs>"  // edge live
               && without_matches.size() == 1 && without_matches[0] == "--mode <mode>";
    }

    static_assert(a_requires_if_edge_fires_only_on_its_value());

    consteval bool incl_last_decides_whether_a_last_positional_is_listed() {
        // The bullet list under a missing-argument error must not tell the user to supply an
        // argument that is only reachable after `--` unless the caller says it is relevant.
        command_builder cmd("lp");
        std::move(cmd).arg(arg_builder("rest").required().last());
        const command_spec spec = cmd.freeze();
        const usage_renderer renderer{spec};
        return renderer.get_required_usage_from({}, true).size() == 1 &&
               renderer.get_required_usage_from({}, false).empty();
    }

    static_assert(incl_last_decides_whether_a_last_positional_is_listed());

    consteval bool an_explicit_inclusion_is_listed_even_when_present() {
        // `incls` is the caller saying "I already know these are missing" — the presence
        // check still applies to them, but they enter the list without being in the required
        // graph at all.
        command_builder cmd("demo");
        std::move(cmd).arg(arg_builder("out").long_("out")).arg(arg_builder("tag").long_("tag"));
        const command_spec spec = cmd.freeze();
        const usage_renderer renderer{spec};
        const arg_id incls[] = {arg_id{"tag"}};
        const fake_matcher nothing{};
        std::vector<std::string> forms;
        for (const styled_str& one : renderer.get_required_usage_from(incls, nothing, true))
            forms.push_back(one.to_string());
        return forms.size() == 1 && forms[0] == "--tag <tag>";
    }

    static_assert(an_explicit_inclusion_is_listed_even_when_present());

    // ---------------------------------------------------------------------------
    // The required set, and the constructor that overrides it
    // ---------------------------------------------------------------------------

    consteval bool a_caller_supplied_required_set_wins() {
        // The validator's required set has grown past required_graph() by the time it renders
        // a message — `required_unless_present` and friends are decided against the matches.
        // The three-argument constructor is how that grown set gets in.
        command_builder cmd("demo");
        std::move(cmd).arg(arg_builder("out").long_("out")).arg(arg_builder("tag").long_("tag"));
        const command_spec spec = cmd.freeze();

        id_set grown;
        grown.insert(arg_id{"tag"});

        // `[OPTIONS]` stays: `needs_options_tag()` reads the command, not the required set,
        // so `--out` is still unnamed and still summarised. Only `--tag` is promoted.
        return render_usage(spec)->to_string() == "Usage: demo [OPTIONS]" &&
               usage_renderer{spec, grown}.create_usage_with_title({})->to_string() ==
                       "Usage: demo [OPTIONS] --tag <tag>";
    }

    static_assert(a_caller_supplied_required_set_wins());

    consteval bool required_graph_holds_groups_by_id_not_by_member() {
        command_builder cmd("fmt");
        std::move(cmd)
                .arg(arg_builder("json").long_("json").action(arg_action::set_true))
                .arg(arg_builder("yaml").long_("yaml").action(arg_action::set_true))
                .group(group_builder("format").args({"json", "yaml"}).required());
        const id_set required = required_graph(cmd.freeze());
        return required.size() == 1 && required.contains("format") && !required.contains("json");
    }

    static_assert(required_graph_holds_groups_by_id_not_by_member());

    // ---------------------------------------------------------------------------
    // Positionals are expanded, never collapsed
    //
    // clap 4 deprecated `dont_collapse_args_in_usage` into a no-op: the getter returns a
    // literal `true` and the setter returns `self` unchanged
    // (clap_builder/src/builder/command.rs:1740 and :4176). Expanding every positional IS
    // the 4.x behaviour, so clapp has no such switch — and this is the assertion that says
    // the missing switch is a decision rather than an omission.
    // ---------------------------------------------------------------------------

    consteval bool positionals_are_never_collapsed_into_args() {
        command_builder cmd("t");
        std::move(cmd)
                .arg(arg_builder("first").required())
                .arg(arg_builder("second").required())
                .arg(arg_builder("third"));
        const std::string line = line_of(cmd.freeze());
        return line == "Usage: t <first> <second> [third]" &&
               line.find("[ARGS]") == std::string::npos;
    }

    static_assert(positionals_are_never_collapsed_into_args());

    // ---------------------------------------------------------------------------
    // Runtime cases
    //
    // Everything above is decided at compile time, so these report rather than re-derive.
    // They add a name in the ctest log for each group
    // of claims, and a failure mode that names the group rather than a line number in a
    // 700-line translation unit.
    // ---------------------------------------------------------------------------

    CLAPP_TEST("positional usage strings match clap's render_usage() tests") {
        CLAPP_CHECK(single_positional_usage_string());
        CLAPP_CHECK(single_positional_multiple_usage_string());
        CLAPP_CHECK(multiple_positional_usage_string());
        CLAPP_CHECK(multiple_positional_one_required_usage_string());
        CLAPP_CHECK(single_positional_required_usage_string());
    }

    CLAPP_TEST("num_args decides both the placeholder count and the ellipsis") {
        CLAPP_CHECK(an_exact_count_repeats_the_placeholder());
        CLAPP_CHECK(an_open_minimum_repeats_and_then_trails());
        CLAPP_CHECK(a_bounded_range_shows_one_and_trails());
        CLAPP_CHECK(a_variadic_positional_keeps_its_neighbours());
    }

    CLAPP_TEST("[OPTIONS] summarises exactly what the line does not name") {
        CLAPP_CHECK(an_optional_option_hides_behind_the_tag());
        CLAPP_CHECK(the_tag_disappears_when_every_option_is_named());
        CLAPP_CHECK(smart_usage_drops_the_tag_outright());
    }

    CLAPP_TEST("a required group renders its members' spellings") {
        CLAPP_CHECK(req_group_usage_string());
        CLAPP_CHECK(req_group_of_only_options());
        CLAPP_CHECK(group_usage_use_val_name());
        CLAPP_CHECK(a_group_member_is_not_listed_twice());
    }

    CLAPP_TEST("required sets match clap's require.rs expectations") {
        CLAPP_CHECK(positional_required_with_requires());
        CLAPP_CHECK(positional_required_with_requires_if_no_value());
        CLAPP_CHECK(required_error_doesnt_duplicate());
        CLAPP_CHECK(required_require_with_group_shows_flag());
        CLAPP_CHECK(requires_group_with_overlapping_group_in_error());
        CLAPP_CHECK(issue_1158_conflicting_requirements());
        CLAPP_CHECK(require_equals_puts_the_sign_in_the_line());
    }

    CLAPP_TEST("a last() positional is fenced behind the escape") {
        CLAPP_CHECK(an_optional_last_positional_is_fenced());
        CLAPP_CHECK(a_required_last_positional_loses_the_brackets());
    }

    CLAPP_TEST("subcommands get a placeholder, a path and their own lines") {
        CLAPP_CHECK(an_optional_subcommand_is_bracketed());
        CLAPP_CHECK(a_required_subcommand_is_angled());
        CLAPP_CHECK(external_subcommands_alone_still_get_a_placeholder());
        CLAPP_CHECK(a_hidden_subcommand_is_not_a_visible_one());
        CLAPP_CHECK(the_placeholder_can_be_renamed());
        CLAPP_CHECK(subcommands_that_negate_reqs_get_a_second_line());
        CLAPP_CHECK(args_that_conflict_with_subcommands_get_a_bare_second_line());
        CLAPP_CHECK(flatten_help_lists_one_line_per_subcommand());
        CLAPP_CHECK(a_subcommand_named_by_its_parent_says_so());
        CLAPP_CHECK(a_flag_subcommand_names_all_three_spellings());
        CLAPP_CHECK(a_parents_requirements_travel_into_the_child_path());
        CLAPP_CHECK(negating_subcommands_strips_the_requirements_from_the_child_path());
    }

    CLAPP_TEST("override_usage and bin_name replace what the line calls the command") {
        CLAPP_CHECK(override_usage_replaces_everything());
        CLAPP_CHECK(bin_name_beats_the_command_name());
    }

    CLAPP_TEST("the two entry points differ only by the heading") {
        CLAPP_CHECK(the_body_is_the_line_without_the_heading());
        CLAPP_CHECK(the_fragments_carry_the_right_classes());
        CLAPP_CHECK(nothing_to_say_is_nullopt_not_an_empty_string());
    }

    CLAPP_TEST("get_required_usage_from consults the matcher through a concept") {
        CLAPP_CHECK(the_bullet_list_names_each_requirement_separately());
        CLAPP_CHECK(a_satisfied_requirement_leaves_the_bullet_list());
        CLAPP_CHECK(a_satisfied_group_leaves_the_bullet_list());
        CLAPP_CHECK(a_requires_if_edge_fires_only_on_its_value());
        CLAPP_CHECK(incl_last_decides_whether_a_last_positional_is_listed());
        CLAPP_CHECK(an_explicit_inclusion_is_listed_even_when_present());
    }

    CLAPP_TEST("the required set can be supplied by the caller") {
        CLAPP_CHECK(a_caller_supplied_required_set_wins());
        CLAPP_CHECK(required_graph_holds_groups_by_id_not_by_member());
    }

    CLAPP_TEST("positionals are expanded, never collapsed into [ARGS]") {
        CLAPP_CHECK(positionals_are_never_collapsed_into_args());
    }

    // A runtime-only claim: the renderer works on a spec it does not own and does not
    // outlive, and the styled_str it returns owns its bytes. Everything above builds the
    // spec inside the same constant expression, so nothing above can see this.
    CLAPP_TEST("the rendered line outlives the renderer") {
        static constexpr auto make = [] consteval {
            command_builder cmd("demo");
            std::move(cmd).arg(arg_builder("out").long_("out").required());
            return cmd.freeze();
        };
        static constexpr command_spec spec = make();

        std::string captured;
        {
            const usage_renderer renderer{spec};
            const std::optional<styled_str> line = renderer.create_usage_with_title({});
            CLAPP_CHECK(line.has_value());
            captured = line->to_string();
        }
        CLAPP_CHECK(captured == "Usage: demo --out <out>");
    }

}  // namespace
