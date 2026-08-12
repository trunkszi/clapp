#include <clapp/builder/arg.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

    using clapp::alias_spec;
    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_condition;
    using clapp::arg_flags;
    using clapp::arg_id;
    using clapp::arg_setting;
    using clapp::arg_spec;
    using clapp::os_str;
    using clapp::predicate_kind;
    using clapp::value_range;

    // ---------------------------------------------------------------------------
    // arg_flags — the seventeen-knob bitset
    // ---------------------------------------------------------------------------

    // One word, trivially copyable, aggregate: seventeen separate bools would cost 24 bytes
    // in every arg_spec in .rodata, which is the entire reason this type exists.
    static_assert(sizeof(arg_flags) == sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<arg_flags>);
    static_assert(std::is_aggregate_v<arg_flags>);
    static_assert(clapp::arg_setting_count == 17);
    static_assert(clapp::all_arg_settings.size() == clapp::arg_setting_count);

    // Bit positions are clap's ArgSettings order and must not drift.
    static_assert(arg_flags::bit_of(arg_setting::required) == 1u << 0);
    static_assert(arg_flags::bit_of(arg_setting::global) == 1u << 1);
    static_assert(arg_flags::bit_of(arg_setting::hidden) == 1u << 2);
    static_assert(arg_flags::bit_of(arg_setting::next_line_help) == 1u << 3);
    static_assert(arg_flags::bit_of(arg_setting::hide_possible_values) == 1u << 4);
    static_assert(arg_flags::bit_of(arg_setting::allow_hyphen_values) == 1u << 5);
    static_assert(arg_flags::bit_of(arg_setting::allow_negative_numbers) == 1u << 6);
    static_assert(arg_flags::bit_of(arg_setting::require_equals) == 1u << 7);
    static_assert(arg_flags::bit_of(arg_setting::last) == 1u << 8);
    static_assert(arg_flags::bit_of(arg_setting::trailing_var_arg) == 1u << 9);
    static_assert(arg_flags::bit_of(arg_setting::hide_default_value) == 1u << 10);
    static_assert(arg_flags::bit_of(arg_setting::ignore_case) == 1u << 11);
    static_assert(arg_flags::bit_of(arg_setting::hide_env) == 1u << 12);
    static_assert(arg_flags::bit_of(arg_setting::hide_env_values) == 1u << 13);
    static_assert(arg_flags::bit_of(arg_setting::hidden_short_help) == 1u << 14);
    static_assert(arg_flags::bit_of(arg_setting::hidden_long_help) == 1u << 15);
    static_assert(arg_flags::bit_of(arg_setting::exclusive) == 1u << 16);

    // Every enumerator maps to a distinct bit, and none of them collide.
    consteval std::uint32_t all_bits_or() {
        std::uint32_t bits = 0;
        for (arg_setting setting : clapp::all_arg_settings) bits |= arg_flags::bit_of(setting);
        return bits;
    }
    static_assert(all_bits_or() == (1u << 17) - 1u);

    consteval std::size_t distinct_bit_count() {
        arg_flags flags{};
        for (arg_setting setting : clapp::all_arg_settings) flags.set(setting);
        return flags.count();
    }
    static_assert(distinct_bit_count() == clapp::arg_setting_count);

    static_assert(arg_flags{}.empty());
    static_assert(arg_flags{}.count() == 0);
    static_assert(!arg_flags{}.is_set(arg_setting::required));
    static_assert(arg_flags{}.with(arg_setting::required, true).is_set(arg_setting::required));
    static_assert(!arg_flags{}
                           .with(arg_setting::required, true)
                           .with(arg_setting::required, false)
                           .is_set(arg_setting::required));
    static_assert(arg_flags{}.with(arg_setting::required, true).count() == 1);
    static_assert((arg_flags{}.with(arg_setting::required, true) |
                   arg_flags{}.with(arg_setting::global, true))
                          .count() == 2);
    static_assert(arg_flags{.bits = 3} == arg_flags{.bits = 3});
    static_assert(arg_flags{.bits = 3} != arg_flags{.bits = 2});

    // Names exist for every knob and are all distinct.
    consteval bool setting_names_distinct() {
        for (std::size_t i = 0; i < clapp::all_arg_settings.size(); ++i) {
            if (clapp::name_of(clapp::all_arg_settings[i]).empty()) return false;
            for (std::size_t j = i + 1; j < clapp::all_arg_settings.size(); ++j) {
                if (clapp::name_of(clapp::all_arg_settings[i]) ==
                    clapp::name_of(clapp::all_arg_settings[j]))
                    return false;
            }
        }
        return true;
    }
    static_assert(setting_names_distinct());
    static_assert(clapp::name_of(arg_setting::required) == "required");
    static_assert(clapp::name_of(arg_setting::hidden) == "hidden");
    static_assert(clapp::name_of(arg_setting::exclusive) == "exclusive");

    // ---------------------------------------------------------------------------
    // arg_predicate
    // ---------------------------------------------------------------------------

    static_assert(clapp::arg_predicate{}.is_present_only());
    static_assert(clapp::arg_predicate::present().kind == predicate_kind::is_present);
    // A presence predicate matches anything: reaching it already means the arg was there.
    static_assert(clapp::arg_predicate::present().matches(os_str{"anything"}));
    static_assert(clapp::arg_predicate::present().matches(os_str{""}));

    consteval clapp::arg_predicate fast_predicate() {
        return clapp::arg_predicate::equal_to("fast");
    }
    static constexpr clapp::arg_predicate fast = fast_predicate();
    static_assert(!fast.is_present_only());
    static_assert(fast.kind == predicate_kind::equals);
    static_assert(fast.value == arg_id{"fast"});
    static_assert(fast.matches(os_str{"fast"}));
    // Byte-exact, deliberately: a default value must not shift with the user's locale.
    static_assert(!fast.matches(os_str{"FAST"}));
    static_assert(!fast.matches(os_str{"fas"}));
    static_assert(fast == fast_predicate());
    static_assert(!(fast == clapp::arg_predicate::present()));

    // The builder-side condition is the same vocabulary with owning storage.
    static_assert(arg_condition{}.is_present_only());
    static_assert(arg_condition::present().is_present_only());
    static_assert(!arg_condition::equal_to("fast").is_present_only());
    static_assert(arg_condition::equal_to("fast") == arg_condition::equal_to("fast"));
    static_assert(!(arg_condition::equal_to("fast") == arg_condition::equal_to("slow")));

    // ---------------------------------------------------------------------------
    // A default-constructed arg_spec is the frozen form of a bare arg_builder
    // ---------------------------------------------------------------------------

    static_assert(std::is_trivially_copyable_v<arg_spec>);
    static_assert(std::is_aggregate_v<arg_spec>);
    static_assert(arg_spec{}.is_positional());
    static_assert(arg_spec{}.get_action() == arg_action::set);
    static_assert(arg_spec{}.get_num_args() == value_range::single());
    static_assert(!arg_spec{}.get_short().has_value());
    static_assert(!arg_spec{}.get_long().has_value());
    static_assert(!arg_spec{}.get_index().has_value());
    static_assert(!arg_spec{}.get_help().has_value());
    static_assert(!arg_spec{}.get_env().has_value());
    static_assert(arg_spec{}.get_display_order() == 999);
    static_assert(arg_spec{}.get_all_aliases().empty());
    static_assert(arg_spec{}.get_conflicts().empty());
    static_assert(arg_spec{}.get_possible_values().empty());  // std::string enumerates nothing
    // The default parser is the same one arg_action::set resolves to, and it is never null:
    // get_possible_values() dereferences it without a guard, because a null guard is a
    // pointer comparison and those do not fold under -fsanitize=null (see arg_id).
    static_assert(arg_spec{}.get_value_parser() == clapp::parser_for<std::string>());
    static_assert(arg_spec{} == arg_spec{});

    // ---------------------------------------------------------------------------
    // The headline claim: a consteval function builds an arg and returns something
    // that lives in .rodata
    // ---------------------------------------------------------------------------

    consteval arg_spec make_verbose() {
        return arg_builder("verbose")
                .short_('v')
                .long_("verbose")
                .alias("loud")
                .visible_alias("noisy")
                .short_alias('V')
                .visible_short_alias('D')
                .action(arg_action::count)
                .help("Increase logging detail")
                .long_help("Repeat to raise the level: -v, -vv, -vvv")
                .help_heading("Diagnostics")
                .display_order(3)
                .conflicts_with_all({"quiet", "silent"})
                .overrides_with("verbose")
                .group("logging")
                .global()
                .hide_short_help()
                .freeze();
    }
    static constexpr arg_spec verbose = make_verbose();

    static_assert(verbose.get_id() == arg_id{"verbose"});
    static_assert(!verbose.get_id().bound());  // slots belong to a command's table, not here
    static_assert(verbose.get_short() == 'v');
    static_assert(verbose.get_long() == "verbose");
    static_assert(!verbose.is_positional());
    static_assert(verbose.get_action() == arg_action::count);
    static_assert(verbose.get_num_args() == value_range::empty());
    static_assert(!verbose.is_takes_value_set());
    static_assert(verbose.get_help() == "Increase logging detail");
    static_assert(verbose.get_long_help() == "Repeat to raise the level: -v, -vv, -vvv");
    static_assert(verbose.get_help_heading() == "Diagnostics");
    static_assert(verbose.get_display_order() == 3);
    static_assert(verbose.is_global_set());
    static_assert(verbose.is_hide_short_help_set());
    static_assert(!verbose.is_hide_long_help_set());
    static_assert(verbose.get_settings().count() == 2);

    // Aliases: hidden ones are parseable, visible ones are also printable.
    static_assert(verbose.get_all_aliases().size() == 2);
    static_assert(verbose.get_all_aliases()[0] ==
                  alias_spec{.name = arg_id{"loud"}, .visible = false});
    static_assert(verbose.get_all_aliases()[1] ==
                  alias_spec{.name = arg_id{"noisy"}, .visible = true});
    static_assert(verbose.matches_long("verbose"));
    static_assert(verbose.matches_long("loud"));
    static_assert(verbose.matches_long("noisy"));
    static_assert(!verbose.matches_long("loud", false));  // hidden from help, still parsed
    static_assert(verbose.matches_long("noisy", false));
    static_assert(!verbose.matches_long("unknown"));
    static_assert(verbose.matches_short('v'));
    static_assert(verbose.matches_short('V'));
    static_assert(verbose.matches_short('D'));
    static_assert(!verbose.matches_short('q'));
    static_assert(!verbose.matches_short('\0'));  // no short option is never a match
    static_assert(!verbose.matches_short('V', false));
    static_assert(verbose.matches_short('D', false));
    static_assert(std::ranges::distance(verbose.get_visible_aliases()) == 1);
    static_assert(std::ranges::distance(verbose.get_aliases()) == 1);
    static_assert(std::ranges::distance(verbose.get_visible_short_aliases()) == 1);

    static_assert(verbose.get_conflicts().size() == 2);
    static_assert(verbose.get_conflicts()[0] == arg_id{"quiet"});
    static_assert(verbose.get_conflicts()[1] == arg_id{"silent"});
    static_assert(verbose.get_overrides().size() == 1);
    static_assert(verbose.get_overrides()[0] == arg_id{"verbose"});  // clap's args_override_self
    static_assert(verbose.get_groups().size() == 1);
    static_assert(verbose.get_groups()[0] == arg_id{"logging"});

    // arg_action::count seeds default_value "0", so matches.get_count() answers 0 rather
    // than "no such value" for a flag nobody passed.
    static_assert(verbose.get_default_values().size() == 1);
    static_assert(verbose.get_default_values()[0] == arg_id{"0"});
    static_assert(verbose.get_default_missing_values().empty());

    // Two freezes of the same description are equal by content.
    static_assert(verbose == make_verbose());

    // ---------------------------------------------------------------------------
    // Action inference — clap's Arg::_build, run during constant evaluation
    // ---------------------------------------------------------------------------

    consteval arg_action inferred_action(value_range range, bool positional) {
        arg_builder a("x");
        std::move(a).num_args(range);
        if (!positional) std::move(a).long_("x");
        return a.get_action();
    }

    // num_args(0) means "a flag", which means set_true.
    static_assert(inferred_action(value_range::empty(), true) == arg_action::set_true);
    static_assert(inferred_action(value_range::empty(), false) == arg_action::set_true);
    // An unbounded positional collects, so it appends.
    static_assert(inferred_action(value_range::at_least(1), true) == arg_action::append);
    static_assert(inferred_action(value_range::full(), true) == arg_action::append);
    // A *bounded* positional is probably a tuple; the author must opt into append.
    static_assert(inferred_action(value_range::exactly(2), true) == arg_action::set);
    static_assert(inferred_action(value_range::single(), true) == arg_action::set);
    // Options never infer append, even when unbounded.
    static_assert(inferred_action(value_range::at_least(1), false) == arg_action::set);
    // Saying nothing at all: a positional defaults to a single value, hence set.
    static_assert(inferred_action(value_range::infer(), true) == arg_action::set);
    static_assert(inferred_action(value_range::infer(), false) == arg_action::set);

    // An explicit action always wins over inference.
    consteval arg_action explicit_action() {
        arg_builder a("x");
        std::move(a).num_args(value_range::empty()).action(arg_action::help);
        return a.get_action();
    }
    static_assert(explicit_action() == arg_action::help);

    // arg_action::infer restores inference rather than recording a sentinel.
    consteval arg_action reset_action() {
        arg_builder a("x");
        std::move(a).action(arg_action::count).action(arg_action::infer);
        return a.get_action();
    }
    static_assert(reset_action() == arg_action::set);

    // ---------------------------------------------------------------------------
    // num_args resolution
    // ---------------------------------------------------------------------------

    consteval value_range inferred_num_args(arg_action act) {
        arg_builder a("x");
        std::move(a).long_("x").action(act);
        return a.get_num_args();
    }
    static_assert(inferred_num_args(arg_action::set) == value_range::single());
    static_assert(inferred_num_args(arg_action::append) == value_range::single());
    static_assert(inferred_num_args(arg_action::set_true) == value_range::empty());
    static_assert(inferred_num_args(arg_action::set_false) == value_range::empty());
    static_assert(inferred_num_args(arg_action::count) == value_range::empty());
    static_assert(inferred_num_args(arg_action::help) == value_range::empty());
    static_assert(inferred_num_args(arg_action::version) == value_range::empty());

    // Two or more value names imply a matching count, but never override an explicit one.
    consteval value_range num_args_from_value_names(bool explicit_count) {
        arg_builder a("coords");
        std::move(a).long_("coords").value_names({"X", "Y", "Z"});
        if (explicit_count) std::move(a).num_args(value_range::exactly(1));
        return a.get_num_args();
    }
    static_assert(num_args_from_value_names(false) == value_range::exactly(3));
    static_assert(num_args_from_value_names(true) == value_range::exactly(1));

    // A single value name does not imply anything.
    consteval value_range num_args_from_one_value_name() {
        arg_builder a("out");
        std::move(a).long_("out").value_name("FILE");
        return a.get_num_args();
    }
    static_assert(num_args_from_one_value_name() == value_range::single());

    // The frozen spec never carries the infer sentinel.
    consteval bool no_infer_after_freeze() {
        return !arg_builder("x").long_("x").freeze().get_num_args().is_infer() &&
               clapp::is_resolved(arg_builder("x").long_("x").freeze().get_action());
    }
    static_assert(no_infer_after_freeze());

    // ---------------------------------------------------------------------------
    // Value parser resolution
    // ---------------------------------------------------------------------------

    consteval const clapp::parser_vtable* resolved_parser(arg_action act) {
        arg_builder a("x");
        std::move(a).long_("x").action(act);
        return a.get_value_parser();
    }
    static_assert(resolved_parser(arg_action::set) == clapp::parser_for<std::string>());
    static_assert(resolved_parser(arg_action::append) == clapp::parser_for<std::string>());
    static_assert(resolved_parser(arg_action::set_true) == clapp::parser_for<bool>());
    static_assert(resolved_parser(arg_action::set_false) == clapp::parser_for<bool>());
    static_assert(resolved_parser(arg_action::count) == clapp::parser_for<clapp::count_type>());

    consteval const clapp::parser_vtable* explicit_parser() {
        arg_builder a("port");
        std::move(a).long_("port").value_parser<std::uint16_t>();
        return a.get_value_parser();
    }
    static_assert(explicit_parser() == clapp::parser_for<std::uint16_t>());

    // A path parser also seeds the completion hint — clap derives the same thing from the
    // parser's type id at read time; clapp records it once, at configuration time.
    consteval clapp::value_hint path_hint() {
        arg_builder a("out");
        std::move(a).long_("out").value_parser<std::filesystem::path>();
        return a.get_value_hint();
    }
    static_assert(path_hint() == clapp::value_hint::any_path);

    consteval clapp::value_hint explicit_hint_wins() {
        arg_builder a("out");
        std::move(a).long_("out").value_hint(clapp::value_hint::dir_path);
        std::move(a).value_parser<std::filesystem::path>();
        return a.get_value_hint();
    }
    static_assert(explicit_hint_wins() == clapp::value_hint::dir_path);

    // An enum parser reaches the frozen spec's possible-value list.
    enum class mode : unsigned char { fast, slow };

    consteval arg_spec make_mode() {
        return arg_builder("mode").long_("mode").value_parser<mode>().ignore_case().freeze();
    }
    static constexpr arg_spec mode_arg = make_mode();
    static_assert(mode_arg.get_possible_values().size() == 2);
    static_assert(mode_arg.get_possible_values()[0].get_name() == "fast");
    static_assert(mode_arg.get_possible_values()[1].get_name() == "slow");
    static_assert(mode_arg.is_ignore_case_set());

    // A flag takes no values, so it advertises none — clap's rule, so help never prints
    // `[possible values: true, false]` under a `--flag`.
    consteval arg_spec make_flag() {
        return arg_builder("flag").long_("flag").action(arg_action::set_true).freeze();
    }
    static constexpr arg_spec flag = make_flag();
    static_assert(flag.get_value_parser() == clapp::parser_for<bool>());
    static_assert(flag.get_possible_values().empty());
    static_assert(flag.get_default_values()[0] == arg_id{"false"});
    static_assert(flag.get_default_missing_values()[0] == arg_id{"true"});

    consteval arg_spec make_negated_flag() {
        return arg_builder("no-color").long_("no-color").action(arg_action::set_false).freeze();
    }
    static constexpr arg_spec negated = make_negated_flag();
    static_assert(negated.get_default_values()[0] == arg_id{"true"});
    static_assert(negated.get_default_missing_values()[0] == arg_id{"false"});

    // An author-supplied default is never replaced by the action's.
    consteval arg_spec make_counted_from_two() {
        return arg_builder("v").short_('v').action(arg_action::count).default_value("2").freeze();
    }
    static_assert(make_counted_from_two().get_default_values()[0] == arg_id{"2"});
    static_assert(make_counted_from_two().get_default_values().size() == 1);

    // ---------------------------------------------------------------------------
    // Positionals
    // ---------------------------------------------------------------------------

    consteval arg_spec make_files() {
        return arg_builder("files")
                .index(1)
                .num_args(value_range::at_least(1))
                .value_name("FILE")
                .help("Files to process")
                .freeze();
    }
    static constexpr arg_spec files = make_files();
    static_assert(files.is_positional());
    static_assert(files.get_index() == 1u);
    static_assert(files.get_action() == arg_action::append);
    static_assert(files.get_num_args() == value_range::at_least(1));
    static_assert(files.is_multiple_values_set());
    static_assert(files.get_min_vals() == 1);
    static_assert(files.get_max_vals() == value_range::unbounded);
    static_assert(files.get_value_names().size() == 1);
    static_assert(files.get_value_names()[0] == arg_id{"FILE"});
    static_assert(!files.get_short().has_value());
    static_assert(!files.get_long().has_value());

    // index(0) clears the slot rather than pinning position zero — there is no position 0.
    consteval bool index_zero_clears() {
        arg_builder a("x");
        std::move(a).index(3).index(0);
        return !a.get_index().has_value();
    }
    static_assert(index_zero_clears());

    // ---------------------------------------------------------------------------
    // raw() — the composite setter
    // ---------------------------------------------------------------------------

    consteval arg_spec make_raw() { return arg_builder("cmd").raw().freeze(); }
    static constexpr arg_spec raw_arg = make_raw();
    static_assert(raw_arg.get_num_args() == value_range::at_least(1));
    static_assert(raw_arg.is_allow_hyphen_values_set());
    static_assert(raw_arg.is_last_set());
    static_assert(raw_arg.get_action() == arg_action::append);  // unbounded positional

    // raw() must not stomp an explicit count.
    consteval value_range raw_keeps_explicit_num_args() {
        arg_builder a("cmd");
        std::move(a).num_args(value_range::exactly(2)).raw();
        return a.get_num_args();
    }
    static_assert(raw_keeps_explicit_num_args() == value_range::exactly(2));

    // raw(false) is a full retraction of the two knobs.
    consteval bool raw_false_retracts() {
        arg_builder a("cmd");
        std::move(a).raw(true).raw(false);
        return !a.is_allow_hyphen_values_set() && !a.is_last_set();
    }
    static_assert(raw_false_retracts());

    // ---------------------------------------------------------------------------
    // Delimiters and terminators
    // ---------------------------------------------------------------------------

    consteval arg_spec make_paths() {
        return arg_builder("paths")
                .long_("paths")
                .num_args(value_range::at_least(1))
                .value_delimiter(',')
                .value_terminator(";")
                .freeze();
    }
    static constexpr arg_spec paths = make_paths();
    static_assert(paths.get_value_delimiter() == ',');
    static_assert(paths.get_value_terminator() == ";");

    // use_value_delimiter is clap's older spelling: it seeds ',' but never overwrites.
    consteval char delimiter_after(bool enable, char preset) {
        arg_builder a("x");
        std::move(a).long_("x").num_args(value_range::at_least(1));
        if (preset != '\0') std::move(a).value_delimiter(preset);
        std::move(a).use_value_delimiter(enable);
        return a.get_value_delimiter().value_or('\0');
    }
    static_assert(delimiter_after(true, '\0') == ',');
    static_assert(delimiter_after(true, '|') == '|');
    static_assert(delimiter_after(false, '|') == '\0');
    static_assert(delimiter_after(false, '\0') == '\0');

    // '\0' is clapp's spelling of clap's IntoResettable::None.
    consteval bool delimiter_reset() {
        arg_builder a("x");
        std::move(a).long_("x").num_args(value_range::at_least(1)).value_delimiter(',');
        std::move(a).value_delimiter('\0');
        return !a.get_value_delimiter().has_value();
    }
    static_assert(delimiter_reset());

    // ---------------------------------------------------------------------------
    // Constraints
    // ---------------------------------------------------------------------------

    consteval arg_spec make_constrained() {
        return arg_builder("out")
                .long_("out")
                .requires_("format")
                .requires_if("json", "schema")
                .requires_all({"writer", "sink"})
                .required_unless_present("dry-run")
                .required_unless_present_any({"list", "help-me"})
                .required_unless_present_all({"a", "b"})
                .required_if_eq("mode", "write")
                .required_if_eq_any({{"mode", "append"}, {"mode", "truncate"}})
                .required_if_eq_all({{"x", "1"}, {"y", "2"}})
                .exclusive()
                .groups({"output", "io"})
                .freeze();
    }
    static constexpr arg_spec constrained = make_constrained();

    static_assert(constrained.get_requires().size() == 4);
    static_assert(constrained.get_requires()[0].when.is_present_only());
    static_assert(constrained.get_requires()[0].target == arg_id{"format"});
    static_assert(!constrained.get_requires()[1].when.is_present_only());
    static_assert(constrained.get_requires()[1].when.value == arg_id{"json"});
    static_assert(constrained.get_requires()[1].target == arg_id{"schema"});
    static_assert(constrained.get_requires()[2].target == arg_id{"writer"});
    static_assert(constrained.get_requires()[3].target == arg_id{"sink"});

    // required_unless_present stacks into the "any" list, exactly like repeating clap's.
    static_assert(constrained.get_required_unless_present_any().size() == 3);
    static_assert(constrained.get_required_unless_present_any()[0] == arg_id{"dry-run"});
    static_assert(constrained.get_required_unless_present_any()[2] == arg_id{"help-me"});
    static_assert(constrained.get_required_unless_present_all().size() == 2);
    static_assert(constrained.get_required_unless_present_all()[1] == arg_id{"b"});

    static_assert(constrained.get_required_if_eq_any().size() == 3);
    static_assert(constrained.get_required_if_eq_any()[0].id == arg_id{"mode"});
    static_assert(constrained.get_required_if_eq_any()[0].value == arg_id{"write"});
    static_assert(constrained.get_required_if_eq_any()[2].value == arg_id{"truncate"});
    static_assert(constrained.get_required_if_eq_all().size() == 2);
    static_assert(constrained.get_required_if_eq_all()[1].id == arg_id{"y"});

    static_assert(constrained.is_exclusive_set());
    static_assert(constrained.get_groups().size() == 2);
    static_assert(constrained.get_groups()[0] == arg_id{"output"});

    // ---------------------------------------------------------------------------
    // Value sources
    // ---------------------------------------------------------------------------

    consteval arg_spec make_threads() {
        return arg_builder("threads")
                .long_("threads")
                .value_parser<unsigned>()
                .default_value("1")
                .default_value_if("mode", "fast", "8")
                .default_value_if("mode", arg_condition::present(), std::nullopt)
                .default_values_if("mode", arg_condition::equal_to("wide"), {"16", "32"})
                .env("APP_THREADS")
                .hide_env_values()
                .freeze();
    }
    static constexpr arg_spec threads = make_threads();

    static_assert(threads.get_default_values().size() == 1);
    static_assert(threads.get_default_values()[0] == arg_id{"1"});
    static_assert(threads.get_env() == "APP_THREADS");
    static_assert(threads.is_hide_env_values_set());
    static_assert(!threads.is_hide_env_set());

    static_assert(threads.get_default_values_ifs().size() == 3);
    static_assert(threads.get_default_values_ifs()[0].id == arg_id{"mode"});
    static_assert(threads.get_default_values_ifs()[0].has_values());
    static_assert(threads.get_default_values_ifs()[0].values().size() == 1);
    static_assert(threads.get_default_values_ifs()[0].values()[0] == arg_id{"8"});
    static_assert(threads.get_default_values_ifs()[0].when.matches(os_str{"fast"}));
    static_assert(!threads.get_default_values_ifs()[0].when.matches(os_str{"slow"}));
    // nullopt is "remove the default", which is NOT the same as "supply zero values".
    static_assert(!threads.get_default_values_ifs()[1].has_values());
    static_assert(threads.get_default_values_ifs()[1].values().empty());
    static_assert(threads.get_default_values_ifs()[1].when.is_present_only());
    static_assert(threads.get_default_values_ifs()[2].has_values());
    static_assert(threads.get_default_values_ifs()[2].values().size() == 2);
    static_assert(threads.get_default_values_ifs()[2].values()[1] == arg_id{"32"});

    // default_value replaces rather than appends, matching clap.
    consteval std::size_t default_value_replaces() {
        arg_builder a("x");
        std::move(a).long_("x").default_value("1").default_value("2");
        return a.get_default_values().size();
    }
    static_assert(default_value_replaces() == 1);

    // "supplies zero values" and "removes the default" are different states, and the frozen
    // form has to keep them apart. It does so with `default_value_spec::values_present`, an
    // explicit bool — NOT with the pointer, which used to be the encoding here (a null
    // pointer with count 0 versus a non-null one, kept non-null by a dummy array that
    // promote_default_conditions() no longer builds). `value_data` says nothing about
    // presence any more; asking it would reintroduce the ubsan failure, because
    // `data != nullptr` is not a constant expression under `-fsanitize=null`.
    consteval arg_spec zero_value_rule() {
        std::vector<std::string_view> none{};
        arg_builder a("x");
        std::move(a).long_("x").default_values_if("m", arg_condition::present(), none);
        return a.freeze();
    }
    static constexpr arg_spec zero_rule = zero_value_rule();
    static_assert(zero_rule.get_default_values_ifs().size() == 1);
    static_assert(zero_rule.get_default_values_ifs()[0].has_values());
    static_assert(zero_rule.get_default_values_ifs()[0].values().empty());

    // number_of_values is clap's older spelling of an exact count.
    consteval value_range exact_count() {
        arg_builder a("x");
        std::move(a).long_("x").number_of_values(3);
        return a.get_num_args();
    }
    static_assert(exact_count() == value_range::exactly(3));

    // An empty terminator view removes the terminator rather than matching "".
    consteval bool terminator_reset() {
        arg_builder a("x");
        std::move(a).long_("x").value_terminator(";").value_terminator("");
        return !a.get_value_terminator().has_value();
    }
    static_assert(terminator_reset());

    // default_value_ifs adds several rules in one call.
    consteval std::size_t rules_from_list() {
        arg_builder a("x");
        std::move(a).long_("x").default_value_ifs(
                {{.id = "m", .when = arg_condition::equal_to("a"), .value = "1"},
                 {.id = "m", .when = arg_condition::equal_to("b"), .value = std::nullopt}});
        return a.get_default_values_ifs().size();
    }
    static_assert(rules_from_list() == 2);

    // ---------------------------------------------------------------------------
    // Help presentation
    // ---------------------------------------------------------------------------

    consteval arg_spec make_hidden() {
        return arg_builder("secret")
                .long_("secret")
                .hide()
                .hide_possible_values()
                .hide_default_value()
                .hide_env()
                .hide_long_help()
                .next_line_help()
                .freeze();
    }
    static constexpr arg_spec secret = make_hidden();
    static_assert(secret.is_hide_set());
    static_assert(secret.is_hide_possible_values_set());
    static_assert(secret.is_hide_default_value_set());
    static_assert(secret.is_hide_env_set());
    static_assert(secret.is_hide_long_help_set());
    static_assert(secret.is_next_line_help_set());
    static_assert(secret.get_settings().count() == 6);

    // An empty help view is "no help", not "help that is the empty string" — it keeps the
    // renderer from reserving a column for nothing.
    consteval bool empty_help_is_none() {
        arg_builder a("x");
        std::move(a).long_("x").help("something").help("");
        return !a.get_help().has_value();
    }
    static_assert(empty_help_is_none());

    // An empty heading, on the other hand, IS a value: "this argument has no section",
    // which is different from "inherit the command's".
    consteval bool empty_heading_is_explicit() {
        arg_builder a("x");
        std::move(a).long_("x").help_heading("");
        return a.get_help_heading().has_value() && a.get_help_heading()->empty();
    }
    static_assert(empty_heading_is_explicit());

    consteval bool unset_heading_is_none() {
        return !arg_builder("x").get_help_heading().has_value();
    }
    static_assert(unset_heading_is_none());

    // And the distinction has to survive freeze(). This is the *only* field in clapp where
    // present-but-empty differs from absent, so it is also the only one a "make the last
    // prose field consistent" cleanup could quietly delete: degrading arg_spec's flag to
    // `help_heading_.has_value() && !help_heading_->empty()` — the length sentinel every
    // other prose field uses — was measured to leave all 21 unit TUs and the umbrella pair
    // green. These three assertions are what stops that.
    consteval arg_spec frozen_empty_heading() {
        arg_builder a("x");
        std::move(a).long_("x").help_heading("");
        return a.freeze();
    }
    static constexpr arg_spec empty_heading = frozen_empty_heading();
    static constexpr arg_spec no_heading    = arg_builder("x").long_("x").freeze();

    static_assert(empty_heading.get_help_heading() == std::optional<std::string_view>{""});
    static_assert(!no_heading.get_help_heading().has_value());
    // Which means the two arguments are not equal, even though every byte of prose matches.
    static_assert(!(empty_heading == no_heading));

    // ---------------------------------------------------------------------------
    // Parsing modifiers
    // ---------------------------------------------------------------------------

    consteval arg_spec make_modifiers() {
        return arg_builder("expr")
                .long_("expr")
                .allow_hyphen_values()
                .allow_negative_numbers()
                .require_equals()
                .trailing_var_arg()
                .freeze();
    }
    static constexpr arg_spec modifiers = make_modifiers();
    static_assert(modifiers.is_allow_hyphen_values_set());
    static_assert(modifiers.is_allow_negative_numbers_set());
    static_assert(modifiers.is_require_equals_set());
    static_assert(modifiers.is_trailing_var_arg_set());

    // Every boolean setter takes an explicit `false` too, and retracts.
    consteval bool every_flag_retracts() {
        arg_builder a("x");
        std::move(a)
                .long_("x")
                .required(true)
                .global(true)
                .hide(true)
                .next_line_help(true)
                .hide_possible_values(true)
                .allow_hyphen_values(true)
                .allow_negative_numbers(true)
                .require_equals(true)
                .last(true)
                .trailing_var_arg(true)
                .hide_default_value(true)
                .ignore_case(true)
                .hide_env(true)
                .hide_env_values(true)
                .hide_short_help(true)
                .hide_long_help(true)
                .exclusive(true);
        if (a.get_settings().count() != clapp::arg_setting_count) return false;
        std::move(a)
                .required(false)
                .global(false)
                .hide(false)
                .next_line_help(false)
                .hide_possible_values(false)
                .allow_hyphen_values(false)
                .allow_negative_numbers(false)
                .require_equals(false)
                .last(false)
                .trailing_var_arg(false)
                .hide_default_value(false)
                .ignore_case(false)
                .hide_env(false)
                .hide_env_values(false)
                .hide_short_help(false)
                .hide_long_help(false)
                .exclusive(false);
        return a.get_settings().empty();
    }
    static_assert(every_flag_retracts());

    // setting()/unset_setting() reach the same word the named setters do.
    consteval bool raw_setting_access() {
        arg_builder a("x");
        std::move(a).setting(arg_setting::required);
        if (!a.is_required_set()) return false;
        std::move(a).unset_setting(arg_setting::required);
        if (a.is_required_set()) return false;
        std::move(a).setting(arg_setting::hidden, true);
        return a.is_hide_set();
    }
    static_assert(raw_setting_access());

    // ---------------------------------------------------------------------------
    // Resetting identity
    // ---------------------------------------------------------------------------

    consteval bool short_reset() {
        arg_builder a("x");
        std::move(a).short_('v').short_('\0');
        return !a.get_short().has_value() && a.is_positional();
    }
    static_assert(short_reset());

    consteval bool long_reset() {
        arg_builder a("x");
        std::move(a).long_("verbose").long_("");
        return !a.get_long().has_value() && a.is_positional();
    }
    static_assert(long_reset());

    consteval bool id_rename() {
        arg_builder a("x");
        std::move(a).id("y");
        return a.get_id() == "y";
    }
    static_assert(id_rename());

    // Incremental construction: the returned reference IS the object, so the chain can be
    // broken across statements. This is the shape command_of<T>() will use.
    consteval arg_spec incremental() {
        arg_builder a("out");
        std::move(a).short_('o');
        std::move(a).long_("output");
        std::move(a).help("Where to write");
        for (std::string_view name : {"dest", "target"}) std::move(a).alias(name);
        return a.freeze();
    }
    static constexpr arg_spec out = incremental();
    static_assert(out.get_short() == 'o');
    static_assert(out.get_long() == "output");
    static_assert(out.get_all_aliases().size() == 2);
    static_assert(out.matches_long("target"));

    // ---------------------------------------------------------------------------
    // Plural setters accept both a braced list and any range
    // ---------------------------------------------------------------------------

    consteval std::size_t aliases_from_vector() {
        std::vector<std::string> names{"a", "b", "c"};
        arg_builder arg_obj("x");
        std::move(arg_obj).long_("x").aliases(names);
        return arg_obj.get_all_aliases().size();
    }
    static_assert(aliases_from_vector() == 3);

    consteval std::size_t short_aliases_from_string() {
        std::string letters{"abc"};
        arg_builder arg_obj("x");
        std::move(arg_obj).short_('x').short_aliases(letters);
        return arg_obj.get_all_short_aliases().size();
    }
    static_assert(short_aliases_from_string() == 3);

    consteval std::size_t conflicts_from_vector() {
        std::vector<std::string_view> names{"a", "b"};
        arg_builder arg_obj("x");
        std::move(arg_obj).long_("x").conflicts_with_all(names);
        return arg_obj.get_conflicts().size();
    }
    static_assert(conflicts_from_vector() == 2);

    // ---------------------------------------------------------------------------
    // arg_spec equality is by content, never by pointer
    // ---------------------------------------------------------------------------

    consteval arg_spec bare(std::string_view id) { return arg_builder(id).long_(id).freeze(); }
    static constexpr arg_spec bare_a  = bare("a");
    static constexpr arg_spec bare_a2 = bare("a");
    static constexpr arg_spec bare_b  = bare("b");
    static_assert(bare_a == bare_a2);
    static_assert(!(bare_a == bare_b));

    // Differing only in a list makes them unequal.
    consteval arg_spec with_alias() { return arg_builder("a").long_("a").alias("aa").freeze(); }
    static_assert(!(bare_a == with_alias()));

    // Differing only in the value parser makes them unequal too. Worth pinning: the parser
    // is the one member that used to be compared as an address, which meant this very
    // assertion could not be written under -fsanitize=null — GCC 16.1.0 refuses to fold
    // `&parser_vtable_for<int> == &parser_vtable_for<std::string>`. It is compared through
    // type_name() now, so the same claim holds and also compiles on the ubsan preset.
    consteval arg_spec typed_as_int() {
        return arg_builder("a").long_("a").value_parser<int>().freeze();
    }
    static_assert(!(bare_a == typed_as_int()));
    static_assert(typed_as_int() == typed_as_int());
    static_assert(typed_as_int().get_value_parser() == clapp::parser_for<int>());

    // value_parser(nullptr) is a distinct overload from value_parser(const parser_vtable*),
    // and it restores the action-derived default rather than storing a null table.
    consteval arg_spec reset_parser() {
        arg_builder a("a");
        std::move(a).long_("a").value_parser(clapp::parser_for<int>()).value_parser(nullptr);
        return a.freeze();
    }
    static_assert(reset_parser() == bare_a);
    static_assert(reset_parser().get_value_parser() == clapp::parser_for<std::string>());

    // ---------------------------------------------------------------------------
    // The forward-compatibility claim M4 rests on
    // ---------------------------------------------------------------------------

    // arg_spec is structural for exactly one reason: `command_of<T>()` has to promote an
    // *array* of them into static storage. Nothing in this header exercises that, so the
    // property would rot silently until M4 tripped over it several layers deep inside
    // std::define_static_array, where the diagnostic names neither the type nor the member.
    // Pinning it here means the mistake fails this file instead.
    struct command_sketch {
        const arg_spec* arg_data = nullptr;
        std::size_t arg_count    = 0;
    };
    template<command_sketch>
    struct sketch_probe {};
    using sketch_is_structural = sketch_probe<command_sketch{}>;

    consteval command_sketch promote_args() {
        std::vector<arg_spec> specs{make_verbose(), make_files(), make_flag()};
        const std::span<const arg_spec> promoted = std::define_static_array(specs);
        return {.arg_data = promoted.data(), .arg_count = promoted.size()};
    }
    static constexpr command_sketch sketch = promote_args();
    static_assert(sketch.arg_count == 3);
    static_assert(sketch.arg_data[0].get_long() == "verbose");
    static_assert(sketch.arg_data[1].is_positional());
    static_assert(sketch.arg_data[2].get_action() == arg_action::set_true);
    static_assert(sketch.arg_data[0] == verbose);

    // ---------------------------------------------------------------------------
    // is_multiple() is not is_multiple_values_set()
    // ---------------------------------------------------------------------------
    //
    // clap's `Arg::is_multiple` is `is_multiple_values_set() || action == Append`. The two
    // differ exactly on an appending argument at num_args(1): `--tag a --tag b` collects two
    // values out of two occurrences that each took one.

    consteval arg_spec make_appending() {
        arg_builder a("tag");
        std::move(a).long_("tag").action(arg_action::append);
        return a.freeze();
    }
    static constexpr arg_spec appending = make_appending();
    static_assert(appending.get_num_args() == value_range::single());
    static_assert(!appending.is_multiple_values_set());
    static_assert(appending.is_multiple());

    consteval bool builder_agrees_with_spec_on_multiple() {
        arg_builder appends("tag");
        std::move(appends).long_("tag").action(arg_action::append);
        arg_builder many("files");
        std::move(many).num_args(value_range::at_least(1));
        arg_builder one("input");
        return appends.is_multiple() && !appends.is_multiple_values_set() && many.is_multiple() &&
               many.is_multiple_values_set() && !one.is_multiple() && !one.is_multiple_values_set();
    }
    static_assert(builder_agrees_with_spec_on_multiple());

    // ---------------------------------------------------------------------------
    // Three combinations clap accepts and clapp used to reject
    // ---------------------------------------------------------------------------
    //
    // Each of these froze into a compile error before the M2 audit. clap has no such
    // assertion in debug_asserts.rs, and the first is pinned by a regression test of its
    // own (tests/builder/default_vals.rs::required_args_with_default_values), so a clapp
    // port of that test could not even be written.

    // 1. required() together with default_value().
    consteval arg_spec make_required_with_default() {
        arg_builder a("out");
        std::move(a).long_("out").required().default_value("stdout");
        return a.freeze();
    }
    static constexpr arg_spec required_with_default = make_required_with_default();
    static_assert(required_with_default.is_required_set());
    static_assert(required_with_default.get_default_values().size() == 1);
    static_assert(required_with_default.get_default_values()[0] == "stdout");

    // 2. value_delimiter() on an argument that takes no values — inert, not illegal.
    consteval arg_spec make_delimited_flag() {
        arg_builder a("v");
        std::move(a).short_('v').action(arg_action::count).value_delimiter(',');
        return a.freeze();
    }
    static constexpr arg_spec delimited_flag = make_delimited_flag();
    static_assert(delimited_flag.get_value_delimiter() == ',');
    static_assert(!delimited_flag.is_takes_value_set());

    // 3. require_equals() on a positional. clap's only rule here is
    //    `is_require_equals_set requires is_takes_value_set`, which a positional satisfies.
    consteval arg_spec make_positional_require_equals() {
        arg_builder a("file");
        std::move(a).require_equals();
        return a.freeze();
    }
    static constexpr arg_spec positional_require_equals = make_positional_require_equals();
    static_assert(positional_require_equals.is_positional());
    static_assert(positional_require_equals.is_require_equals_set());

    // ---------------------------------------------------------------------------
    // get_configured_display_order() distinguishes "unset" from "999"
    // ---------------------------------------------------------------------------

    consteval bool display_order_records_the_explicit_999() {
        arg_builder unset("a");
        arg_builder explicit_999("b");
        std::move(explicit_999).display_order(999);
        return !unset.get_configured_display_order().has_value() &&
               unset.get_display_order() == 999 &&
               explicit_999.get_configured_display_order() == 999u &&
               explicit_999.get_display_order() == 999;
    }
    static_assert(display_order_records_the_explicit_999());

    // ---------------------------------------------------------------------------
    // Compile-time consistency checks — the compile-error half of this suite
    // ---------------------------------------------------------------------------
    //
    // clapp::arg_builder::freeze() is clap's `assert_arg` plus `assert_arg_flags`, moved
    // from a debug-only panic at process start to a compile error. None of these can live
    // in this file: a translation unit that triggers one does not compile. Each was
    // reproduced once, by hand, and reports
    // `error: uncaught exception '(const char*)"clapp::arg_builder::freeze: ..."'`.
    //
    //  1. an empty argument id
    //  2. index() on something that also has a short or long option
    //  3. index() on something whose action takes no value
    //  4. num_args wider than clapp::max_num_args() allows for the action
    //  5. arg_action::count together with a value_parser() that is not clapp::count_type
    //     (clap's `ArgAction::value_type_id`, the only action that pins a type)
    //  6. value_hint() on an argument that takes no values
    //  7. value_hint::command_with_arguments on a single-valued argument
    //  8. more value_name()s than num_args can ever fill
    //  9. require_equals() with a num_args whose minimum is above one
    // 10. hide_possible_values(), allow_hyphen_values(), allow_negative_numbers(),
    //     require_equals(), last(), hide_default_value() or ignore_case() on an argument
    //     that takes no values                            (clap's assert_arg_flags)

    // ---------------------------------------------------------------------------
    // Builder-side properties — constant-evaluated, not run
    // ---------------------------------------------------------------------------
    //
    // clapp::arg_builder holds std::vector and std::string, so it can never be a constexpr
    // *variable*. That is not the same as "cannot be constant evaluated": a builder can be
    // created, mutated, queried and destroyed entirely inside a consteval function, exactly
    // as flat_map.hpp:56-60 describes for flat_map. So these gate the build.

    consteval bool a_fresh_argument_is_a_single_valued_positional() {
        const arg_builder a("input");
        return a.get_id() == "input" && a.is_positional() && a.get_action() == arg_action::set &&
               a.get_num_args() == value_range::single() && a.is_takes_value_set() &&
               !a.is_multiple_values_set() && a.get_display_order() == 999 &&
               !a.get_configured_num_args().has_value();
    }
    static_assert(a_fresh_argument_is_a_single_valued_positional());

    consteval bool chaining_mutates_in_place() {
        arg_builder a("out");
        const arg_builder& returned = std::move(a).short_('o').long_("output");
        return &returned == &a && a.get_short() == 'o' && a.get_long() == "output" &&
               !a.is_positional();
    }
    static_assert(chaining_mutates_in_place());

    consteval bool builder_side_getters_borrow_the_builders_storage() {
        arg_builder a("out");
        std::move(a)
                .long_("output")
                .value_names({"FILE", "MODE"})
                .default_values({"a.txt", "w"})
                .conflicts_with("stdout")
                .group("io");
        return a.get_value_names().size() == 2 && a.get_value_names()[1] == "MODE" &&
               a.get_default_values().size() == 2 && a.get_default_values()[0] == "a.txt" &&
               a.get_conflicts().size() == 1 && a.get_conflicts()[0] == "stdout" &&
               a.get_groups()[0] == "io"
               // Two value names imply num_args(2) even though nobody said so.
               && a.get_num_args() == value_range::exactly(2);
    }
    static_assert(builder_side_getters_borrow_the_builders_storage());

    consteval bool aliases_record_visibility_separately_from_spelling() {
        arg_builder a("verbose");
        std::move(a).long_("verbose").alias("loud").visible_alias("noisy");
        return a.get_all_aliases().size() == 2 && !a.get_all_aliases()[0].visible &&
               a.get_all_aliases()[1].visible && a.get_all_aliases()[1].name == "noisy";
    }
    static_assert(aliases_record_visibility_separately_from_spelling());

    consteval bool requires_rules_keep_their_owning_strings() {
        arg_builder a("out");
        std::move(a).long_("out").requires_if("json", "schema").required_if_eq("mode", "write");
        return a.get_requires().size() == 1 &&
               a.get_requires()[0].when.kind == predicate_kind::equals &&
               a.get_requires()[0].when.value == "json" && a.get_requires()[0].target == "schema" &&
               a.get_required_if_eq_any().size() == 1 &&
               a.get_required_if_eq_any()[0].id == "mode" &&
               a.get_required_if_eq_any()[0].value == "write";
    }
    static_assert(requires_rules_keep_their_owning_strings());

    consteval bool conditional_defaults_distinguish_none_from_empty() {
        arg_builder a("threads");
        std::move(a)
                .long_("threads")
                .default_value_if("mode", "fast", "8")
                .default_value_if("mode", arg_condition::present(), std::nullopt);
        return a.get_default_values_ifs().size() == 2 && a.get_default_values_ifs()[0].has_values &&
               a.get_default_values_ifs()[0].values.size() == 1 &&
               !a.get_default_values_ifs()[1].has_values &&
               a.get_default_values_ifs()[1].values.empty();
    }
    static_assert(conditional_defaults_distinguish_none_from_empty());

    consteval bool the_flag_word_round_trips_through_every_knob() {
        arg_flags flags{};
        for (arg_setting setting : clapp::all_arg_settings) {
            if (flags.is_set(setting)) return false;
            flags.set(setting);
            if (!flags.is_set(setting)) return false;
        }
        if (flags.count() != clapp::arg_setting_count) return false;
        for (arg_setting setting : clapp::all_arg_settings) flags.unset(setting);
        return flags.empty();
    }
    static_assert(the_flag_word_round_trips_through_every_knob());

    consteval bool the_renderer_lists_are_what_the_renderer_wants() {
        const std::vector<std::string_view> longs = verbose.get_long_and_visible_aliases();
        const std::vector<char> shorts            = verbose.get_short_and_visible_aliases();
        return longs.size() == 2 && longs[0] == "verbose" && longs[1] == "noisy" &&
               shorts.size() == 2 && shorts[0] == 'v' &&
               shorts[1] == 'D'
               // A positional has neither, and says so with an empty list, not a sentinel.
               && files.get_long_and_visible_aliases().empty() &&
               files.get_short_and_visible_aliases().empty();
    }
    static_assert(the_renderer_lists_are_what_the_renderer_wants());

    // ---------------------------------------------------------------------------
    // Runtime cases — what genuinely cannot cross the constant-evaluation boundary
    // ---------------------------------------------------------------------------

    CLAPP_TEST("arg_spec: frozen specs are usable in a runtime container") {
        // A vector of arg_spec is what a runtime-assembled command tree would hold; it is
        // also the one witness that arg_spec really is trivially copyable in practice.
        const std::vector<arg_spec> args{verbose, files, flag, threads};
        CLAPP_CHECK(args.size() == 4);
        CLAPP_CHECK(args[0].get_long() == "verbose");
        CLAPP_CHECK(args[1].is_positional());
        CLAPP_CHECK(args[2].get_action() == arg_action::set_true);
        CLAPP_CHECK(args[3].get_env() == "APP_THREADS");
    }

    CLAPP_TEST("arg_spec: the frozen parser table actually parses") {
        // parse() goes through type erasure, which needs a static_cast from void* and is
        // therefore runtime-only — the *pointers* are compile-time constants, the call is
        // not. This is the one place the whole chain gets exercised end to end.
        const auto parsed = threads.get_value_parser()->parse(os_str{"12"}, false);
        CLAPP_CHECK(parsed.has_value());
        CLAPP_CHECK(parsed->holds<unsigned>());
        CLAPP_CHECK(parsed->get<unsigned>() == 12u);

        const auto rejected = threads.get_value_parser()->parse(os_str{"nope"}, false);
        CLAPP_CHECK(!rejected.has_value());
        CLAPP_CHECK(rejected.error().kind == clapp::parse_error_kind::invalid_digit);
    }

    CLAPP_TEST("arg_spec: ignore_case reaches the enum parser through the table") {
        const auto folded =
                mode_arg.get_value_parser()->parse(os_str{"FAST"}, mode_arg.is_ignore_case_set());
        CLAPP_CHECK(folded.has_value());
        CLAPP_CHECK(folded->get<mode>() == mode::fast);

        const auto exact = mode_arg.get_value_parser()->parse(os_str{"FAST"}, false);
        CLAPP_CHECK(!exact.has_value());
    }

}  // namespace
