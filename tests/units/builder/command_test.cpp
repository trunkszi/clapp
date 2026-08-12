#include <clapp/builder/command.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_setting;
    using clapp::arg_spec;
    using clapp::color_choice;
    using clapp::command_builder;
    using clapp::command_flags;
    using clapp::command_setting;
    using clapp::command_spec;
    using clapp::group_builder;
    using clapp::value_range;

    // ---------------------------------------------------------------------------
    // command_flags — the thirty-knob bitset
    // ---------------------------------------------------------------------------

    // One word, trivially copyable, aggregate. Thirty separate bools would cost 32 bytes
    // in every command_spec in .rodata against this type's 4.
    static_assert(sizeof(command_flags) == sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<command_flags>);
    static_assert(std::is_aggregate_v<command_flags>);
    static_assert(clapp::command_setting_count == 30);
    static_assert(clapp::all_command_settings.size() == clapp::command_setting_count);

    // Bit positions are clap's AppSettings order and must not drift: bits 0-29 line up
    // with clap's, and clap's last two (Built / BinNameBuilt) have no clapp counterpart.
    static_assert(command_flags::bit_of(command_setting::ignore_errors) == 1u << 0);
    static_assert(command_flags::bit_of(command_setting::allow_hyphen_values) == 1u << 1);
    static_assert(command_flags::bit_of(command_setting::allow_negative_numbers) == 1u << 2);
    static_assert(command_flags::bit_of(command_setting::all_args_override_self) == 1u << 3);
    static_assert(command_flags::bit_of(command_setting::allow_missing_positional) == 1u << 4);
    static_assert(command_flags::bit_of(command_setting::trailing_var_arg) == 1u << 5);
    static_assert(command_flags::bit_of(command_setting::dont_delimit_trailing_values) == 1u << 6);
    static_assert(command_flags::bit_of(command_setting::infer_long_args) == 1u << 7);
    static_assert(command_flags::bit_of(command_setting::infer_subcommands) == 1u << 8);
    static_assert(command_flags::bit_of(command_setting::subcommand_required) == 1u << 9);
    static_assert(command_flags::bit_of(command_setting::allow_external_subcommands) == 1u << 10);
    static_assert(command_flags::bit_of(command_setting::multicall) == 1u << 11);
    static_assert(command_flags::bit_of(command_setting::subcommands_negate_reqs) == 1u << 12);
    static_assert(command_flags::bit_of(command_setting::args_negate_subcommands) == 1u << 13);
    static_assert(command_flags::bit_of(command_setting::subcommand_precedence_over_arg) ==
                  1u << 14);
    static_assert(command_flags::bit_of(command_setting::flatten_help) == 1u << 15);
    static_assert(command_flags::bit_of(command_setting::arg_required_else_help) == 1u << 16);
    static_assert(command_flags::bit_of(command_setting::next_line_help) == 1u << 17);
    static_assert(command_flags::bit_of(command_setting::disable_colored_help) == 1u << 18);
    static_assert(command_flags::bit_of(command_setting::disable_help_flag) == 1u << 19);
    static_assert(command_flags::bit_of(command_setting::disable_help_subcommand) == 1u << 20);
    static_assert(command_flags::bit_of(command_setting::disable_version_flag) == 1u << 21);
    static_assert(command_flags::bit_of(command_setting::propagate_version) == 1u << 22);
    static_assert(command_flags::bit_of(command_setting::hidden) == 1u << 23);
    static_assert(command_flags::bit_of(command_setting::hide_possible_values) == 1u << 24);
    static_assert(command_flags::bit_of(command_setting::help_expected) == 1u << 25);
    static_assert(command_flags::bit_of(command_setting::no_binary_name) == 1u << 26);
    static_assert(command_flags::bit_of(command_setting::color_auto) == 1u << 27);
    static_assert(command_flags::bit_of(command_setting::color_always) == 1u << 28);
    static_assert(command_flags::bit_of(command_setting::color_never) == 1u << 29);

    // all_command_settings must be in bit order, with no gaps and no repeats.
    consteval bool settings_are_in_bit_order() {
        for (std::size_t i = 0; i < clapp::all_command_settings.size(); ++i) {
            if (static_cast<std::size_t>(clapp::all_command_settings[i]) != i) return false;
        }
        return true;
    }
    static_assert(settings_are_in_bit_order());

    static_assert(command_flags{}.empty());
    static_assert(command_flags{}.count() == 0);
    static_assert(!command_flags{}.is_set(command_setting::multicall));
    static_assert(command_flags{}
                          .with(command_setting::multicall, true)
                          .is_set(command_setting::multicall));
    static_assert(!command_flags{}
                           .with(command_setting::multicall, true)
                           .with(command_setting::multicall, false)
                           .is_set(command_setting::multicall));

    consteval command_flags two_knobs() {
        command_flags flags;
        flags.set(command_setting::multicall);
        flags.set(command_setting::hidden);
        return flags;
    }
    static_assert(two_knobs().count() == 2);
    static_assert(two_knobs().is_set(command_setting::hidden));

    consteval command_flags one_knob(command_setting which) {
        command_flags flags;
        flags.set(which);
        return flags;
    }
    static_assert((one_knob(command_setting::multicall) | one_knob(command_setting::hidden)) ==
                  two_knobs());

    consteval command_flags unset_again() {
        command_flags flags = two_knobs();
        flags.unset(command_setting::hidden);
        return flags;
    }
    static_assert(unset_again() == one_knob(command_setting::multicall));

    consteval command_flags inserted() {
        command_flags flags = one_knob(command_setting::multicall);
        flags.insert(one_knob(command_setting::hidden));
        return flags;
    }
    static_assert(inserted() == two_knobs());

    // name_of() must cover every enumerator; "unknown" would mean one was forgotten.
    consteval bool every_setting_is_named() {
        for (const command_setting setting : clapp::all_command_settings) {
            if (clapp::name_of(setting) == "unknown") return false;
            if (clapp::name_of(setting).empty()) return false;
        }
        return true;
    }
    static_assert(every_setting_is_named());
    static_assert(clapp::name_of(command_setting::multicall) == "multicall");
    static_assert(clapp::name_of(command_setting::args_negate_subcommands) ==
                  "args_negate_subcommands");

    // The overload set must still resolve for the sibling enumerations name_of() covers.
    static_assert(clapp::name_of(arg_setting::required) == "required");
    static_assert(clapp::name_of(color_choice::never) == "never");

    // ---------------------------------------------------------------------------
    // command_spec — the neutral, default-constructed command
    // ---------------------------------------------------------------------------

    static_assert(std::is_trivially_copyable_v<command_spec>);
    static_assert(std::is_aggregate_v<command_spec>);

    static_assert(command_spec{}.get_name().empty());
    static_assert(!command_spec{}.get_bin_name().has_value());
    static_assert(!command_spec{}.get_display_name().has_value());
    static_assert(!command_spec{}.get_short_flag().has_value());
    static_assert(!command_spec{}.get_long_flag().has_value());
    static_assert(!command_spec{}.get_version().has_value());
    static_assert(!command_spec{}.get_about().has_value());
    static_assert(command_spec{}.get_arguments().empty());
    static_assert(command_spec{}.get_groups().empty());
    static_assert(command_spec{}.get_subcommands().empty());
    static_assert(!command_spec{}.has_subcommands());
    static_assert(!command_spec{}.has_positionals());
    static_assert(command_spec{}.get_display_order() == 999);
    static_assert(!command_spec{}.get_term_width().has_value());
    static_assert(!command_spec{}.get_max_term_width().has_value());
    static_assert(command_spec{}.get_color() == color_choice::auto_);
    static_assert(command_spec{}.get_styles() == clapp::styles::styled());
    static_assert(command_spec{}.get_external_subcommand_value_parser() == nullptr);
    static_assert(command_spec{}.find_arg("x") == nullptr);
    static_assert(command_spec{}.find_group("x") == nullptr);
    static_assert(command_spec{}.find_subcommand("x") == nullptr);
    static_assert(!command_spec{}.has_arg("x"));
    static_assert(!command_spec{}.has_group("x"));
    static_assert(!command_spec{}.has_subcommand("x"));
    static_assert(!command_spec{}.id_exists("x"));
    static_assert(command_spec{}.get_settings().empty());
    static_assert(command_spec{} == command_spec{});

    // A command_spec must stay structural, or a subcommand list cannot be promoted with
    // std::define_static_array — which is precisely how the recursion is implemented.
    template<command_spec>
    struct spec_probe {};
    using spec_is_structural = spec_probe<command_spec{}>;

    // ---------------------------------------------------------------------------
    // A plain command: identity, help/version injection, positional numbering
    // ---------------------------------------------------------------------------

    consteval command_spec make_demo() {
        command_builder demo("demo");
        std::move(demo)
                .bin_name("demo-bin")
                .display_name("Demo")
                .version("1.2.3")
                .long_version("1.2.3 (deadbeef)")
                .author("Nobody <nobody@example.com>")
                .about("Show what a command looks like")
                .arg(arg_builder("output").short_('o').long_("output").help("Where to write"))
                .arg(arg_builder("input").help("What to read"))
                .arg(arg_builder("extra").help("And then this"));
        return demo.freeze();
    }
    static constexpr command_spec demo = make_demo();

    static_assert(demo.get_name() == "demo");
    static_assert(demo.get_id() == "demo");
    static_assert(demo.get_bin_name() == "demo-bin");
    static_assert(demo.get_display_name() == "Demo");
    static_assert(demo.get_version() == "1.2.3");
    static_assert(demo.get_long_version() == "1.2.3 (deadbeef)");
    static_assert(demo.get_author() == "Nobody <nobody@example.com>");
    static_assert(demo.get_about() == "Show what a command looks like");
    static_assert(!demo.get_long_about().has_value());
    static_assert(demo.get_display_order() == 999);

    // Three authored arguments plus the injected --help and --version.
    static_assert(demo.get_arguments().size() == 5);
    static_assert(demo.get_arguments()[0].get_id() == "output");
    static_assert(demo.get_arguments()[3].get_id() == "help");
    static_assert(demo.get_arguments()[4].get_id() == "version");
    static_assert(demo.find_arg("help")->get_id() == "help");
    static_assert(demo.find_arg("help")->get_short() == 'h');
    static_assert(demo.find_arg("help")->get_long() == "help");
    static_assert(demo.find_arg("help")->get_action() == arg_action::help);
    static_assert(demo.find_arg("help")->get_help() == "Print help");
    static_assert(demo.find_arg("version")->get_short() == 'V');
    static_assert(demo.find_arg("version")->get_action() == arg_action::version);
    static_assert(demo.find_arg("version")->get_help() == "Print version");
    static_assert(demo.find_arg("nosuch") == nullptr);
    // has_arg() is what a `static_assert` must ask instead of `find_arg(id) != nullptr`:
    // under -fsanitize=null GCC 16.1.0 will not fold that comparison once the argument is
    // really found, because the two operands then have different base objects.
    static_assert(demo.has_arg("output"));
    static_assert(demo.has_arg("help"));
    static_assert(!demo.has_arg("nosuch"));
    static_assert(!demo.has_group("output"));  // an argument is not a group
    static_assert(demo.id_exists("output"));
    static_assert(!demo.id_exists("nosuch"));
    static_assert(demo.contains_short('o'));
    static_assert(demo.contains_short('h'));
    static_assert(!demo.contains_short('z'));

    // Positionals are numbered 1..n in declaration order; named arguments are untouched.
    static_assert(demo.find_arg("input")->get_index() == 1u);
    static_assert(demo.find_arg("extra")->get_index() == 2u);
    static_assert(!demo.find_arg("output")->get_index().has_value());
    static_assert(demo.has_positionals());
    static_assert(std::ranges::distance(demo.get_positionals()) == 2);  // input, extra
    static_assert(std::ranges::distance(demo.get_opts()) == 1);         // output
    static_assert(std::ranges::distance(demo.get_flags()) == 2);        // help, version

    // A command with no subcommands has no `help` subcommand to offer.
    static_assert(!demo.has_subcommands());
    static_assert(demo.is_disable_help_subcommand_set());
    static_assert(!demo.is_disable_help_flag_set());
    static_assert(!demo.is_disable_version_flag_set());

    // long_about() switches the injected --help onto the two-line wording, exactly as clap.
    consteval command_spec make_long_about() {
        command_builder cmd("verbose-help");
        std::move(cmd).long_about("A much longer description of what this does.");
        return cmd.freeze();
    }
    static constexpr command_spec long_about_demo = make_long_about();
    static_assert(long_about_demo.find_arg("help")->get_help() ==
                  "Print help (see more with '--help')");
    static_assert(long_about_demo.find_arg("help")->get_long_help() ==
                  "Print help (see a summary with '-h')");

    // ---------------------------------------------------------------------------
    // Version: absent, disabled, and explicitly suppressed
    // ---------------------------------------------------------------------------

    // No version() at all means no --version argument. clap folds this into its
    // is_disable_version_flag_set() accessor; clapp writes it into the flag word so a
    // frozen tree needs no re-derivation.
    consteval command_spec make_unversioned() { return command_builder("bare").freeze(); }
    static constexpr command_spec bare = make_unversioned();
    static_assert(bare.get_arguments().size() == 1);
    static_assert(bare.find_arg("version") == nullptr);
    static_assert(bare.is_disable_version_flag_set());
    static_assert(!bare.get_version().has_value());

    consteval command_spec make_no_flags() {
        command_builder cmd("quiet");
        std::move(cmd).version("9").disable_help_flag().disable_version_flag();
        return cmd.freeze();
    }
    static constexpr command_spec quiet = make_no_flags();
    static_assert(quiet.get_arguments().empty());
    static_assert(quiet.is_disable_help_flag_set());
    static_assert(quiet.is_disable_version_flag_set());

    // ---------------------------------------------------------------------------
    // Aliases: command names, short flags, long flags
    // ---------------------------------------------------------------------------

    consteval command_spec make_aliased() {
        command_builder cmd("checkout");
        std::move(cmd)
                .visible_alias("co")
                .alias("chk")
                .aliases({"switch-to", "goto"})
                .visible_aliases({"c"})
                .short_flag('C')
                .visible_short_flag_alias('K')
                .short_flag_alias('k')
                .short_flag_aliases({'q'})
                .visible_short_flag_aliases({'Q'})
                .long_flag("checkout")
                .visible_long_flag_alias("check-out")
                .long_flag_alias("chkout")
                .long_flag_aliases({"co-long"})
                .visible_long_flag_aliases({"co-vis"});
        return cmd.freeze();
    }
    static constexpr command_spec aliased = make_aliased();

    static_assert(aliased.get_all_aliases().size() == 5);
    static_assert(std::ranges::distance(aliased.get_visible_aliases()) == 2);
    static_assert(std::ranges::distance(aliased.get_aliases()) == 3);
    static_assert(aliased.aliases_to("checkout"));
    static_assert(aliased.aliases_to("co"));
    static_assert(aliased.aliases_to("chk"));
    static_assert(!aliased.aliases_to("chk", /*include_hidden=*/false));
    static_assert(!aliased.aliases_to("nope"));
    static_assert(aliased.get_short_flag() == 'C');
    static_assert(aliased.short_flag_aliases_to('C'));
    static_assert(aliased.short_flag_aliases_to('k'));
    static_assert(!aliased.short_flag_aliases_to('k', false));
    static_assert(aliased.short_flag_aliases_to('Q', false));
    static_assert(!aliased.short_flag_aliases_to('\0'));
    static_assert(aliased.get_long_flag() == "checkout");
    static_assert(aliased.long_flag_aliases_to("check-out"));
    static_assert(aliased.long_flag_aliases_to("chkout"));
    static_assert(!aliased.long_flag_aliases_to("chkout", false));
    static_assert(aliased.get_all_short_flag_aliases().size() == 4);
    static_assert(std::ranges::distance(aliased.get_visible_short_flag_aliases()) == 2);
    static_assert(aliased.get_all_long_flag_aliases().size() == 4);
    static_assert(std::ranges::distance(aliased.get_visible_long_flag_aliases()) == 2);

    consteval bool name_and_visible_aliases_lead_with_the_name() {
        const std::vector<std::string_view> names = aliased.get_name_and_visible_aliases();
        return names.size() == 3 && names[0] == "checkout" && names[1] == "co" && names[2] == "c";
    }
    static_assert(name_and_visible_aliases_lead_with_the_name());

    // ---------------------------------------------------------------------------
    // A three-level tree: recursion, global arguments, version propagation
    // ---------------------------------------------------------------------------

    consteval command_spec make_tree() {
        command_builder root("app");
        std::move(root)
                .version("1.0")
                .propagate_version()
                .arg(arg_builder("config").short_('c').long_("config").global().help("Config file"))
                .subcommand(command_builder("remote")
                                    .about("Manage remotes")
                                    .subcommand(command_builder("add")
                                                        .about("Add a remote")
                                                        .arg(arg_builder("name").required()))
                                    .subcommand(command_builder("remove").about("Drop a remote")))
                .subcommand(command_builder("status").about("Show status"));
        return root.freeze();
    }
    static constexpr command_spec tree = make_tree();

    // The tree really is a tree, promoted into .rodata level by level.
    static_assert(tree.has_subcommands());
    static_assert(tree.get_subcommands().size() == 3);  // remote, status, injected help
    static_assert(tree.find_subcommand("remote")->get_name() == "remote");
    static_assert(tree.find_subcommand("status")->get_about() == "Show status");
    static_assert(tree.find_subcommand("help")->get_name() == "help");
    static_assert(tree.find_subcommand("remote")->find_subcommand("add")->get_name() == "add");
    static_assert(tree.find_subcommand("remote")->find_subcommand("remove")->get_name() ==
                  "remove");
    static_assert(tree.find_subcommand("remote")->find_subcommand("add")->get_about() ==
                  "Add a remote");
    static_assert(tree.find_subcommand("remote")
                          ->find_subcommand("add")
                          ->find_arg("name")
                          ->is_required_set());
    static_assert(
            tree.find_subcommand("remote")->find_subcommand("add")->find_arg("name")->get_index() ==
            1u);
    static_assert(tree.find_subcommand("nope") == nullptr);
    static_assert(tree.find_subcommand("remote")->find_subcommand("nope") == nullptr);
    static_assert(tree.has_subcommand("remote"));
    static_assert(tree.has_subcommand("help"));
    static_assert(!tree.has_subcommand("nope"));

    // propagate_version() is a *global* setting, exactly as in clap
    // (clap_builder/src/builder/command.rs:1529 calls `global_setting`), so it travels with
    // the version all the way down instead of stopping after one level.
    static_assert(tree.find_subcommand("remote")->get_version() == "1.0");
    static_assert(!tree.find_subcommand("remote")->is_disable_version_flag_set());
    static_assert(tree.find_subcommand("remote")->is_propagate_version_set());
    static_assert(tree.find_subcommand("remote")->find_subcommand("add")->get_version() == "1.0");
    static_assert(
            !tree.find_subcommand("remote")->find_subcommand("add")->is_disable_version_flag_set());
    // The injected `help` subtree is the one exception: clap clears its version and stops
    // the propagation there, so `git help --version` is not a thing.
    static_assert(!tree.find_subcommand("help")->get_version().has_value());
    static_assert(!tree.find_subcommand("help")->is_propagate_version_set());

    // A global argument reaches every subcommand, recursively...
    static_assert(tree.find_subcommand("remote")->find_arg("config")->is_global_set());
    static_assert(tree.find_subcommand("status")->find_arg("config")->get_long() == "config");
    static_assert(tree.find_subcommand("remote")
                          ->find_subcommand("add")
                          ->find_arg("config")
                          ->get_short() == 'c');
    // ... except the auto-generated `help` subtree, which would otherwise offer the
    // parent's options during `app help <TAB>` completion.
    static_assert(tree.find_subcommand("help")->find_arg("config") == nullptr);

    // Argument counts, spelled out so an accidental extra injection is caught.
    static_assert(tree.get_arguments().size() == 3);  // config, help, version
    static_assert(tree.find_subcommand("remote")->get_arguments().size() == 3);  // + version
    static_assert(tree.find_subcommand("status")->get_arguments().size() == 3);  // + version
    static_assert(tree.find_subcommand("remote")->find_subcommand("add")->get_arguments().size() ==
                  4);  // name, config, help, version

    // The injected `help` subcommand: one positional taking any number of values, and no
    // help/version flags of its own.
    static_assert(tree.find_subcommand("help")->get_about() ==
                  "Print this message or the help of the given subcommand(s)");
    static_assert(tree.find_subcommand("help")->get_arguments().size() == 1);
    static_assert(tree.find_subcommand("help")->get_arguments()[0].get_id() == "subcommand");
    static_assert(tree.find_subcommand("help")->get_arguments()[0].get_action() ==
                  arg_action::append);
    static_assert(tree.find_subcommand("help")->get_arguments()[0].get_num_args() ==
                  value_range::full());
    static_assert(tree.find_subcommand("help")->is_disable_help_flag_set());
    static_assert(tree.find_subcommand("help")->is_disable_version_flag_set());

    consteval bool subcommand_names_include_aliases() {
        const std::vector<std::string_view> names = tree.all_subcommand_names();
        return names.size() == 3 && names[0] == "remote" && names[1] == "status" &&
               names[2] == "help";
    }
    static_assert(subcommand_names_include_aliases());

    // ---------------------------------------------------------------------------
    // display_name is derived for every subcommand — clap's `_build_bin_names_internal`
    // ---------------------------------------------------------------------------
    //
    // `<parent display name>-<child name>`, chained the whole way down. It is what
    // `prog verb --version` prints and what `{name}` expands to, so a missing derivation
    // makes `git log --version` answer `log 2.44` instead of `git-log 2.44`.
    //
    // clap derives it in a pass of its own because the *other* half of that pass —
    // `bin_name` — depends on `argv[0]` and cannot be settled before parsing.
    // `display_name` has no such dependency: it is built from the parent's `display_name`
    // falling back to its **`name`**, never from `bin_name` (clap's
    // `display_name_subcommand_default` sets `bin_name("child.exe")` on the child and still
    // expects `parent-child`). So clapp settles it inside
    // clapp::command_builder::propagate_into() and the frozen tree carries it.
    //
    // Measured against clap 4.6.5 on this machine: `03_04_subcommands add --version` prints
    // `clap-add 4.6.5`, which is also what clap's own
    // examples/tutorial_builder/03_04_subcommands.md pins.

    // The root keeps none — clap's `display_name_default`.
    static_assert(!tree.get_display_name().has_value());
    static_assert(tree.find_subcommand("remote")->get_display_name() == "app-remote");
    static_assert(tree.find_subcommand("status")->get_display_name() == "app-status");
    // Chained, not flattened: the grandchild builds on the child's derived name.
    static_assert(tree.find_subcommand("remote")->find_subcommand("add")->get_display_name() ==
                  "app-remote-add");
    static_assert(tree.find_subcommand("remote")->find_subcommand("remove")->get_display_name() ==
                  "app-remote-remove");
    // The injected `help` subcommand is derived too: clap calls `_propagate_subcommand` on
    // it inside `_check_help_and_version`, and `_build_bin_names_internal` then sees it in
    // the subcommand list like any other.
    static_assert(tree.find_subcommand("help")->get_display_name() == "app-help");
    static_assert(tree.find_subcommand("remote")->find_subcommand("help")->get_display_name() ==
                  "app-remote-help");

    // clap's `display_name_subcommand_explicit`: a child that named itself keeps that name,
    // and its own children build on the explicit name rather than on the parent's.
    consteval command_spec make_named_tree() {
        command_builder root("parent");
        std::move(root).subcommand(command_builder("child")
                                           .bin_name("child.exe")
                                           .display_name("child.display")
                                           .subcommand(command_builder("grandchild")));
        return root.freeze();
    }
    static constexpr command_spec named_tree = make_named_tree();
    static_assert(named_tree.find_subcommand("child")->get_display_name() == "child.display");
    static_assert(named_tree.find_subcommand("child")
                          ->find_subcommand("grandchild")
                          ->get_display_name() == "child.display-grandchild");

    // Under multicall() the inherited prefix is **empty**, not the root's name — the same
    // rule clapp::detail::child_base_path() applies to `bin_name`, and for the same reason:
    // `argv[0]` names the applet, so `busybox` is not part of any identity a user can see.
    // clap: `if is_multicall_set { display_name.unwrap_or("") } else { ... }`. Measured
    // against clap — a multicall `busybox` with an applet `ls` reports `ls`, and `ls inner`
    // reports `ls-inner`.
    consteval command_spec make_multicall_tree() {
        command_builder root("busybox");
        std::move(root).multicall().subcommand(
                command_builder("ls").subcommand(command_builder("inner")));
        return root.freeze();
    }
    static constexpr command_spec multicall_tree = make_multicall_tree();
    // Only the multicall level's own name is dropped; below it the hyphenation resumes.
    static_assert(multicall_tree.find_subcommand("ls")->get_display_name() == "ls");
    static_assert(
            multicall_tree.find_subcommand("ls")->find_subcommand("inner")->get_display_name() ==
            "ls-inner");

    // ---------------------------------------------------------------------------
    // Subcommand flags: -C / --config style dispatch
    // ---------------------------------------------------------------------------

    consteval command_spec make_flag_subcommands() {
        command_builder root("pacman");
        std::move(root)
                .subcommand(command_builder("sync").short_flag('S').long_flag("sync"))
                .subcommand(command_builder("query").short_flag('Q').long_flag("query"));
        return root.freeze();
    }
    static constexpr command_spec pacman = make_flag_subcommands();
    static_assert(pacman.find_short_subcommand('S')->get_name() == "sync");
    static_assert(pacman.find_short_subcommand('Z') == nullptr);
    static_assert(pacman.find_long_subcommand("query")->get_name() == "query");
    static_assert(pacman.find_long_subcommand("nope") == nullptr);

    // ---------------------------------------------------------------------------
    // Groups: declared, materialised from arg.group(), and unrolled into conflicts
    // ---------------------------------------------------------------------------

    consteval command_spec make_grouped() {
        command_builder cmd("release");
        std::move(cmd)
                .arg(arg_builder("major").long_("major").group("bump"))
                .arg(arg_builder("minor").long_("minor").group("bump"))
                .arg(arg_builder("patch").long_("patch").group("bump"))
                .arg(arg_builder("dry-run").long_("dry-run").conflicts_with("bump"))
                .group(group_builder("bump").arg("major").required());
        return cmd.freeze();
    }
    static constexpr command_spec release = make_grouped();

    // One group, not four: the declared one absorbed the memberships, and "major" — which
    // was listed both ways — appears exactly once.
    static_assert(release.get_groups().size() == 1);
    static_assert(release.find_group("bump")->get_id() == "bump");
    static_assert(release.find_group("bump")->size() == 3);
    static_assert(release.find_group("bump")->is_required_set());
    static_assert(release.find_group("bump")->contains("major"));
    static_assert(release.find_group("bump")->contains("patch"));
    static_assert(!release.find_group("bump")->contains("dry-run"));
    static_assert(release.has_group("bump"));
    static_assert(!release.has_group("major"));  // a group member is not itself a group
    static_assert(!release.has_arg("bump"));     // and the group id is not an argument
    static_assert(release.id_exists("bump"));

    consteval bool groups_for_arg_reports_membership() {
        const std::vector<std::string_view> owners = release.groups_for_arg("minor");
        return owners.size() == 1 && owners[0] == "bump";
    }
    static_assert(groups_for_arg_reports_membership());
    static_assert(release.groups_for_arg("dry-run").empty());

    // A conflict naming a group unrolls into that group's members.
    consteval bool conflicts_unroll_groups() {
        const std::vector<const arg_spec*> blocked =
                release.get_arg_conflicts_with(*release.find_arg("dry-run"));
        return blocked.size() == 3 && blocked[0]->get_id() == "major" &&
               blocked[2]->get_id() == "patch";
    }
    static_assert(conflicts_unroll_groups());

    // A group nobody declared is created from the memberships alone.
    consteval command_spec make_implicit_group() {
        command_builder cmd("io");
        std::move(cmd)
                .arg(arg_builder("read").long_("read").group("mode"))
                .arg(arg_builder("write").long_("write").group("mode"));
        return cmd.freeze();
    }
    static constexpr command_spec io_command = make_implicit_group();
    static_assert(io_command.get_groups().size() == 1);
    static_assert(io_command.find_group("mode")->size() == 2);
    static_assert(!io_command.find_group("mode")->is_required_set());

    // ---------------------------------------------------------------------------
    // Implied settings: multicall, args_conflicts_with_subcommands, external parser
    // ---------------------------------------------------------------------------

    consteval command_spec make_multicall() {
        command_builder cmd("busybox");
        std::move(cmd)
                .multicall()
                .subcommand(command_builder("ls"))
                .subcommand(command_builder("cat"));
        return cmd.freeze();
    }
    static constexpr command_spec busybox = make_multicall();
    static_assert(busybox.is_multicall_set());
    static_assert(busybox.is_subcommand_required_set());
    static_assert(busybox.is_disable_help_flag_set());
    static_assert(busybox.is_disable_version_flag_set());
    static_assert(busybox.get_arguments().empty());
    static_assert(busybox.get_subcommands().size() == 3);  // ls, cat, injected help
    // The applets keep their own names as display names: multicall hands its children an
    // empty prefix. See the display_name section above for the rule and its measurement.
    static_assert(busybox.find_subcommand("ls")->get_display_name() == "ls");
    static_assert(busybox.find_subcommand("cat")->get_display_name() == "cat");
    static_assert(busybox.find_subcommand("help")->get_display_name() == "help");

    consteval command_spec make_negating() {
        command_builder cmd("neg");
        std::move(cmd)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("x").long_("x"))
                .subcommand(command_builder("go"));
        return cmd.freeze();
    }
    static constexpr command_spec negating = make_negating();
    static_assert(negating.is_args_conflicts_with_subcommands_set());
    static_assert(negating.is_subcommand_negates_reqs_set());  // implied

    consteval command_spec make_external() {
        command_builder cmd("wrapper");
        std::move(cmd).external_subcommand_value_parser<std::string>();
        return cmd.freeze();
    }
    static constexpr command_spec wrapper = make_external();
    static_assert(wrapper.is_allow_external_subcommands_set());  // implied by the parser
    static_assert(wrapper.get_external_subcommand_value_parser() ==
                  clapp::parser_for<std::string>());

    // The predicate, asked of a spec that actually *has* a parser. This is the case the
    // getter cannot serve: `get_external_subcommand_value_parser() != nullptr` compiles
    // clean without sanitizers and fails under `-fsanitize=undefined` with
    // `'((& clapp::parser_vtable_for<...>) != 0)' is not a constant expression`, because
    // the two operands name different base objects. The comparison above survives only
    // because both sides are the *same* object. Keep an assertion on the populated side
    // here, or the ubsan preset stops covering this path.
    static_assert(wrapper.has_external_subcommand_value_parser());
    static_assert(!command_spec{}.has_external_subcommand_value_parser());

    // external_parser_present also carries operator==: without it, two commands differing
    // only in their external parser compare equal, since the operator refuses to compare
    // the two pointers. Forcing the flag to false in promote() was measured to leave every
    // unit test and the umbrella pair green, so these three are its only guard.
    consteval command_spec make_external_none() {
        command_builder cmd("wrapper");
        std::move(cmd).allow_external_subcommands();
        return cmd.freeze();
    }
    consteval command_spec make_external_int() {
        command_builder cmd("wrapper");
        std::move(cmd).external_subcommand_value_parser<int>();
        return cmd.freeze();
    }
    static constexpr command_spec wrapper_plain = make_external_none();
    static constexpr command_spec wrapper_int   = make_external_int();

    static_assert(!wrapper_plain.has_external_subcommand_value_parser());
    static_assert(wrapper_int.has_external_subcommand_value_parser());
    // Identical in every other byte — same name, same settings — and still not equal.
    static_assert(wrapper.is_allow_external_subcommands_set() ==
                  wrapper_plain.is_allow_external_subcommands_set());
    static_assert(!(wrapper == wrapper_plain));
    static_assert(!(wrapper_plain == wrapper));
    // Two parsers, two different types.
    static_assert(!(wrapper == wrapper_int));
    // And a command does equal itself through the parser branch.
    static_assert(wrapper == make_external());

    // ---------------------------------------------------------------------------
    // Command-level knobs that are shorthand for the same knob on every argument
    // ---------------------------------------------------------------------------

    consteval command_spec make_propagating_flags() {
        command_builder cmd("calc");
        std::move(cmd)
                .allow_hyphen_values()
                .allow_negative_numbers()
                .hide_possible_values()
                .arg(arg_builder("value").long_("value"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true));
        return cmd.freeze();
    }
    static constexpr command_spec calc = make_propagating_flags();
    static_assert(calc.find_arg("value")->is_allow_hyphen_values_set());
    static_assert(calc.find_arg("value")->is_allow_negative_numbers_set());
    static_assert(calc.find_arg("value")->is_hide_possible_values_set());
    // A flag takes no value, so none of the three has anything to apply to.
    static_assert(!calc.find_arg("flag")->is_allow_hyphen_values_set());
    static_assert(!calc.find_arg("flag")->is_hide_possible_values_set());
    // The injected --help likewise stays untouched.
    static_assert(!calc.find_arg("help")->is_allow_hyphen_values_set());

    consteval command_spec make_trailing() {
        command_builder cmd("run");
        std::move(cmd)
                .trailing_var_arg()
                .arg(arg_builder("program"))
                .arg(arg_builder("rest").num_args(value_range::at_least(1)));
        return cmd.freeze();
    }
    static constexpr command_spec runner = make_trailing();
    static_assert(runner.find_arg("rest")->is_trailing_var_arg_set());
    static_assert(!runner.find_arg("program")->is_trailing_var_arg_set());
    static_assert(runner.find_arg("rest")->get_index() == 2u);

    // ---------------------------------------------------------------------------
    // Positional shapes that are legal — the control cases for the rejected ones
    // ---------------------------------------------------------------------------

    // A multi-valued positional that is not the last one is fine once the final positional
    // is required: the parser then knows exactly one value belongs to it. Required-ness
    // also has to be monotone from the front, so `sources` is required too.
    consteval command_spec make_guarded_multi() {
        command_builder cmd("copy");
        std::move(cmd)
                .arg(arg_builder("sources").num_args(value_range::at_least(1)).required())
                .arg(arg_builder("dest").required());
        return cmd.freeze();
    }
    static constexpr command_spec copier = make_guarded_multi();
    static_assert(copier.find_arg("sources")->get_index() == 1u);
    static_assert(copier.find_arg("sources")->is_multiple_values_set());
    static_assert(copier.find_arg("dest")->get_index() == 2u);

    // allow_missing_positional() opts out of the monotone-requiredness rule; it exists
    // exactly so `prog [opt] <req>` can be written.
    consteval command_spec make_missing_positional() {
        command_builder cmd("gap");
        std::move(cmd)
                .allow_missing_positional()
                .arg(arg_builder("optional"))
                .arg(arg_builder("required").required());
        return cmd.freeze();
    }
    static constexpr command_spec gap = make_missing_positional();
    static_assert(gap.is_allow_missing_positional_set());
    static_assert(!gap.find_arg("optional")->is_required_set());
    static_assert(gap.find_arg("required")->is_required_set());

    // last() on a positional is the intended use, and a required one is legal as long as
    // subcommand_negates_reqs() says who wins when a subcommand is also present.
    consteval command_spec make_escaped() {
        command_builder cmd("escaped");
        std::move(cmd)
                .subcommand_negates_reqs()
                .arg(arg_builder("first"))
                .arg(arg_builder("after").last().required())
                .subcommand(command_builder("go"));
        return cmd.freeze();
    }
    static constexpr command_spec escaped = make_escaped();
    static_assert(escaped.find_arg("after")->is_last_set());
    static_assert(escaped.find_arg("after")->is_required_set());
    static_assert(escaped.is_subcommand_negates_reqs_set());

    // ---------------------------------------------------------------------------
    // Global settings are inherited; local ones are not
    // ---------------------------------------------------------------------------

    consteval command_spec make_global_settings() {
        command_builder cmd("inherit");
        std::move(cmd)
                .global_setting(command_setting::infer_subcommands)
                .setting(command_setting::infer_long_args)
                .subcommand(command_builder("child").subcommand(command_builder("grandchild")));
        return cmd.freeze();
    }
    static constexpr command_spec inherit = make_global_settings();
    static_assert(inherit.is_infer_subcommands_set());
    static_assert(inherit.is_infer_long_args_set());
    static_assert(inherit.get_global_settings().is_set(command_setting::infer_subcommands));
    static_assert(inherit.find_subcommand("child")->is_infer_subcommands_set());
    static_assert(!inherit.find_subcommand("child")->is_infer_long_args_set());
    // The child inherited the *global* word too, so the setting keeps descending.
    static_assert(inherit.find_subcommand("child")
                          ->find_subcommand("grandchild")
                          ->is_infer_subcommands_set());

    // ---------------------------------------------------------------------------
    // Help layout: headings, display order, widths, colour, palette
    // ---------------------------------------------------------------------------

    consteval command_spec make_layout() {
        command_builder cmd("layout");
        std::move(cmd)
                .next_help_heading("Networking")
                .next_display_order(10)
                .arg(arg_builder("host").long_("host"))
                .arg(arg_builder("port").long_("port"))
                .next_help_heading("")
                .arg(arg_builder("debug").long_("debug"))
                .arg(arg_builder("target"))
                .before_help("before")
                .before_long_help("before long")
                .after_help("after")
                .after_long_help("after long")
                .override_usage("layout [OPTIONS]")
                .override_help("nothing to see")
                .help_template("{about}\n{usage}")
                .subcommand_value_name("TASK")
                .subcommand_help_heading("Tasks")
                .display_order(7)
                .term_width(80)
                .max_term_width(120)
                .color(color_choice::never)
                .styles(clapp::styles::plain())
                .subcommand(command_builder("noop"));
        return cmd.freeze();
    }
    static constexpr command_spec layout = make_layout();

    static_assert(layout.find_arg("host")->get_help_heading() == "Networking");
    static_assert(layout.find_arg("port")->get_help_heading() == "Networking");
    static_assert(!layout.find_arg("debug")->get_help_heading().has_value());
    // The injected --help is pushed directly, so next_help_heading() never touches it.
    static_assert(!layout.find_arg("help")->get_help_heading().has_value());
    static_assert(!layout.get_next_help_heading().has_value());  // reset by the empty view

    static_assert(layout.find_arg("host")->get_display_order() == 10);
    static_assert(layout.find_arg("port")->get_display_order() == 11);
    static_assert(layout.find_arg("debug")->get_display_order() == 12);
    // Positionals are skipped by next_display_order(), exactly as in clap.
    static_assert(layout.find_arg("target")->get_display_order() == 999);
    static_assert(layout.find_arg("help")->get_display_order() == 999);

    static_assert(layout.get_before_help() == "before");
    static_assert(layout.get_before_long_help() == "before long");
    static_assert(layout.get_after_help() == "after");
    static_assert(layout.get_after_long_help() == "after long");
    static_assert(layout.get_override_usage() == "layout [OPTIONS]");
    static_assert(layout.get_override_help() == "nothing to see");
    static_assert(layout.get_help_template() == "{about}\n{usage}");
    static_assert(layout.get_subcommand_value_name() == "TASK");
    static_assert(layout.get_subcommand_help_heading() == "Tasks");
    static_assert(layout.get_display_order() == 7);
    static_assert(layout.get_term_width() == 80u);
    static_assert(layout.get_max_term_width() == 120u);
    static_assert(layout.get_color() == color_choice::never);
    static_assert(layout.get_styles() == clapp::styles::plain());
    static_assert(layout.is_set(command_setting::color_never));
    static_assert(!layout.is_set(command_setting::color_auto));

    // ---------------------------------------------------------------------------
    // The wrap widths and the palette are inherited — clap's `app_ext` propagation
    // ---------------------------------------------------------------------------
    //
    // clap keeps `term_width`, `max_term_width` and `styles` in `Command::app_ext` and
    // hands the whole map down in `_propagate_subcommand` with `Extensions::update`. Without
    // that, `myapp --help` wraps and `myapp sub --help` does not — and it is the subcommand
    // page that users read most. clapp does it in
    // clapp::command_builder::propagate_into().
    static_assert(layout.find_subcommand("noop")->get_term_width() == 80u);
    static_assert(layout.find_subcommand("noop")->get_max_term_width() == 120u);
    static_assert(layout.find_subcommand("noop")->get_styles() == clapp::styles::plain());
    // Including the injected `help` subcommand, which clap propagates into explicitly.
    static_assert(layout.find_subcommand("help")->get_term_width() == 80u);
    static_assert(layout.find_subcommand("help")->get_max_term_width() == 120u);
    static_assert(layout.find_subcommand("help")->get_styles() == clapp::styles::plain());

    // The two rules `Extensions::update` implies, which are *not* the same as the version
    // rule two sections up. A key the parent holds is inserted over the child's; a key the
    // parent never set is absent from the map and leaves the child alone.
    //
    // Measured against clap 4.6.5 on this machine: a root with `term_width(40)` and a
    // subcommand with `term_width(100)` renders the subcommand's help at 40 columns. clap
    // documents the intent on `max_term_width` — "this setting applies globally and *not*
    // on a per-command basis".
    consteval command_spec make_inherited_layout() {
        command_builder root("root");
        std::move(root)
                .term_width(40)
                .styles(clapp::styles::plain())
                .subcommand(command_builder("wide")
                                    .term_width(100)
                                    .styles(clapp::styles::styled())
                                    .subcommand(command_builder("deep")))
                .subcommand(command_builder("own").max_term_width(33));
        return root.freeze();
    }
    static constexpr command_spec inherited_layout = make_inherited_layout();
    // The parent wins outright — this is an overwrite, not a fill-in.
    static_assert(inherited_layout.find_subcommand("wide")->get_term_width() == 40u);
    static_assert(inherited_layout.find_subcommand("wide")->get_styles() == clapp::styles::plain());
    // ...and keeps winning all the way down, because the child's value was replaced before
    // the child propagated into its own children.
    static_assert(
            inherited_layout.find_subcommand("wide")->find_subcommand("deep")->get_term_width() ==
            40u);
    static_assert(inherited_layout.find_subcommand("wide")->find_subcommand("deep")->get_styles() ==
                  clapp::styles::plain());
    // A knob the parent left alone stays the child's own.
    static_assert(inherited_layout.find_subcommand("own")->get_max_term_width() == 33u);
    static_assert(inherited_layout.find_subcommand("own")->get_term_width() == 40u);
    static_assert(!inherited_layout.get_max_term_width().has_value());

    // color() is a tri-state, not three knobs: setting it twice leaves one bit standing.
    consteval command_spec make_recoloured() {
        command_builder cmd("colour");
        std::move(cmd).color(color_choice::never).color(color_choice::always);
        return cmd.freeze();
    }
    static constexpr command_spec recoloured = make_recoloured();
    static_assert(recoloured.get_color() == color_choice::always);
    static_assert(!recoloured.is_set(command_setting::color_never));
    // color_always, plus the two flags freeze() implies for a versionless leaf command.
    static_assert(recoloured.get_settings().count() == 3);

    // ---------------------------------------------------------------------------
    // Hidden subcommands
    // ---------------------------------------------------------------------------

    consteval command_spec make_hidden_sub() {
        command_builder cmd("root");
        std::move(cmd).disable_help_subcommand().subcommand(command_builder("secret").hide());
        return cmd.freeze();
    }
    static constexpr command_spec hidden_root = make_hidden_sub();
    static_assert(hidden_root.get_subcommands().size() == 1);
    static_assert(hidden_root.find_subcommand("secret")->is_hide_set());
    static_assert(!hidden_root.has_visible_subcommands());
    static_assert(tree.has_visible_subcommands());

    // ---------------------------------------------------------------------------
    // defer(): the transformation runs at the start of freeze()
    // ---------------------------------------------------------------------------

    constexpr command_builder add_late_argument(command_builder cmd) {
        std::move(cmd).arg(arg_builder("late").long_("late").help("Added by defer()"));
        return cmd;
    }

    consteval command_spec make_deferred() {
        command_builder cmd("lazy");
        std::move(cmd).arg(arg_builder("early").long_("early")).defer(&add_late_argument);
        return cmd.freeze();
    }
    static constexpr command_spec lazy = make_deferred();
    static_assert(lazy.find_arg("early")->get_long() == "early");
    static_assert(lazy.find_arg("late")->get_long() == "late");
    static_assert(lazy.find_arg("late")->get_help() == "Added by defer()");
    static_assert(lazy.get_arguments().size() == 3);  // early, late, help

    // ---------------------------------------------------------------------------
    // operator== compares content, recursively through the subcommands
    // ---------------------------------------------------------------------------

    consteval command_spec make_tree_again() { return make_tree(); }
    static constexpr command_spec tree_copy = make_tree_again();
    static_assert(tree == tree_copy);
    static_assert(!(tree == demo));

    consteval command_spec make_tree_with_a_different_leaf() {
        command_builder root("app");
        std::move(root)
                .version("1.0")
                .propagate_version()
                .arg(arg_builder("config").short_('c').long_("config").global().help("Config file"))
                .subcommand(command_builder("remote")
                                    .about("Manage remotes")
                                    .subcommand(command_builder("add")
                                                        .about("Add a remote")
                                                        .arg(arg_builder("name")))  // no required()
                                    .subcommand(command_builder("remove").about("Drop a remote")))
                .subcommand(command_builder("status").about("Show status"));
        return root.freeze();
    }
    static constexpr command_spec tree_variant = make_tree_with_a_different_leaf();
    static_assert(!(tree == tree_variant));  // a difference three levels down is visible

    // ---------------------------------------------------------------------------
    // The promotion claim M5 and the parser depend on: a frozen tree is .rodata
    // ---------------------------------------------------------------------------

    // A command_spec is usable as a non-type template argument, which is the strictest
    // available proof that everything it points at reached static storage.
    template<command_spec Spec>
    struct tagged_command {
        static constexpr std::string_view name = Spec.get_name();
    };
    static_assert(tagged_command<make_demo()>::name == "demo");

    // An array of command_spec promotes as a whole — this is how subcommand lists are
    // built, and it is what `command_of<T>()` will do for a list of sibling commands.
    struct command_list {
        const command_spec* data = nullptr;
        std::size_t count        = 0;
    };
    template<command_list>
    struct list_probe {};
    using list_is_structural = list_probe<command_list{}>;

    consteval command_list promote_siblings() {
        std::vector<command_spec> specs{make_demo(), make_multicall(), make_grouped()};
        const std::span<const command_spec> promoted = std::define_static_array(specs);
        return command_list{.data = promoted.data(), .count = promoted.size()};
    }
    static constexpr command_list siblings = promote_siblings();
    static_assert(siblings.count == 3);
    static_assert(siblings.data[0].get_name() == "demo");
    static_assert(siblings.data[1].is_multicall_set());
    static_assert(siblings.data[2].find_group("bump")->size() == 3);
    static_assert(siblings.data[0] == demo);

    // ---------------------------------------------------------------------------
    // Global settings reach the whole subtree — clap's `global_setting`
    // ---------------------------------------------------------------------------
    //
    // Fifteen of clap's boolean setters call `global_setting`, not `setting`
    // (clap_builder/src/builder/command.rs:1191, 1227, 1251, 1288, 1329-1331, 1495, 1529,
    // 1555, 1614, 1649, 1675, 1729, 1759, 1788, 1842). Routing them through `setting`
    // looks right at the root and silently stops at the first subcommand.

    consteval command_spec make_globals() {
        command_builder root("root");
        std::move(root)
                .version("9.9")
                .disable_help_flag()
                .infer_long_args()
                .hide_possible_values()
                .color(color_choice::never)
                .subcommand(command_builder("mid").subcommand(command_builder("leaf")));
        return std::move(root).freeze();
    }
    static constexpr command_spec globals = make_globals();

    // disable_help_flag() removes `--help` everywhere, not just at the root.
    static_assert(globals.find_arg("help") == nullptr);
    static_assert(globals.find_subcommand("mid")->find_arg("help") == nullptr);
    static_assert(globals.find_subcommand("mid")->is_disable_help_flag_set());
    static_assert(globals.find_subcommand("mid")->find_subcommand("leaf")->find_arg("help") ==
                  nullptr);
    // The other four travel the same way, including the colour choice.
    static_assert(
            globals.find_subcommand("mid")->find_subcommand("leaf")->is_infer_long_args_set());
    static_assert(
            globals.find_subcommand("mid")->find_subcommand("leaf")->is_hide_possible_values_set());
    static_assert(globals.find_subcommand("mid")->find_subcommand("leaf")->get_color() ==
                  color_choice::never);
    // A global setting is recorded as global on every level it reaches, which is what makes
    // it keep travelling.
    static_assert(globals.get_global_settings().is_set(command_setting::disable_help_flag));
    static_assert(globals.find_subcommand("mid")->get_global_settings().is_set(
            command_setting::disable_help_flag));

    // Local settings still stop where they are written: clap uses `setting` for these.
    consteval command_spec make_locals() {
        command_builder root("root");
        std::move(root).allow_hyphen_values().subcommand(command_builder("mid"));
        return std::move(root).freeze();
    }
    static constexpr command_spec locals = make_locals();
    static_assert(locals.is_allow_hyphen_values_set());
    static_assert(!locals.find_subcommand("mid")->is_allow_hyphen_values_set());

    // ---------------------------------------------------------------------------
    // long_help_exists() — clap's `long_help_exists_`
    // ---------------------------------------------------------------------------
    //
    // The injected `--help` is two-tier whenever *anything* has a long form, not only when
    // the command set long_about(). Detail that lives on an argument counts.

    consteval command_spec make_two_tier() {
        command_builder cmd("demo");
        std::move(cmd)
                .after_long_help("epilogue")
                .arg(arg_builder("out").long_("out").help("Where").long_help("A much longer one"));
        return std::move(cmd).freeze();
    }
    static constexpr command_spec two_tier = make_two_tier();
    static_assert(two_tier.find_arg("help")->get_help() == "Print help (see more with '--help')");
    static_assert(two_tier.find_arg("help")->get_long_help() ==
                  "Print help (see a summary with '-h')");

    consteval command_spec make_one_tier() {
        command_builder cmd("demo");
        std::move(cmd).arg(arg_builder("out").long_("out").help("Where"));
        return std::move(cmd).freeze();
    }
    static constexpr command_spec one_tier = make_one_tier();
    static_assert(one_tier.find_arg("help")->get_help() == "Print help");
    static_assert(!one_tier.find_arg("help")->get_long_help().has_value());

    // A hidden argument's long help does not count, and neither does a hidden one's
    // hide_long_help() — clap's `should_long` filters on is_hide_set() first.
    consteval command_spec make_hidden_long() {
        command_builder cmd("demo");
        std::move(cmd).arg(arg_builder("out").long_("out").long_help("Longer").hide());
        return std::move(cmd).freeze();
    }
    static_assert(make_hidden_long().find_arg("help")->get_help() == "Print help");

    // ---------------------------------------------------------------------------
    // display_order(999) is a value, not a sentinel
    // ---------------------------------------------------------------------------

    consteval command_spec make_display_orders() {
        command_builder cmd("demo");
        std::move(cmd)
                .next_display_order(10)
                .arg(arg_builder("a").long_("a").display_order(999))
                .arg(arg_builder("b").long_("b"));
        return std::move(cmd).freeze();
    }
    static constexpr command_spec display_orders = make_display_orders();
    // The explicit 999 survives the next_display_order() cursor...
    static_assert(display_orders.find_arg("a")->get_display_order() == 999);
    // ... and the cursor was still consumed for it, exactly as clap's `get_or_insert` does.
    static_assert(display_orders.find_arg("b")->get_display_order() == 11);

    // ---------------------------------------------------------------------------
    // The `--long` / `-s` namespace spans arguments, their aliases, subcommand flags
    // and subcommand flag aliases
    // ---------------------------------------------------------------------------
    //
    // Four distinct sources, all checked by one sorted scan. Only the accepting side can
    // live here: a collision fails the build. See the list at the bottom of this file.

    consteval command_spec make_flag_namespace() {
        command_builder root("root");
        std::move(root)
                .arg(arg_builder("x").long_("xray").alias("beam"))
                .subcommand(command_builder("build")
                                    .long_flag("bld")
                                    .long_flag_alias("make")
                                    .short_flag('B'));
        return std::move(root).freeze();
    }
    static constexpr command_spec flag_namespace = make_flag_namespace();
    static_assert(flag_namespace.find_arg("x")->matches_long("beam"));
    static_assert(flag_namespace.find_subcommand("build")->long_flag_aliases_to("make"));
    static_assert(flag_namespace.find_subcommand("build")->get_short_flag() == 'B');
    // Dereferenced rather than compared to nullptr: GCC folds the promoted array's address
    // and then warns `-Waddress` that it "will never be NULL".
    static_assert(flag_namespace.find_long_subcommand("make")->get_name() == "build");
    static_assert(flag_namespace.find_short_subcommand('B')->get_name() == "build");

    // ---------------------------------------------------------------------------
    // action(append) makes a positional multi-valued for the positional rules
    // ---------------------------------------------------------------------------
    //
    // clap's `Arg::is_multiple` is `is_multiple_values_set() || action == Append`, and
    // `_verify_positionals` asserts on *that*. A single-valued append positional with
    // trailing_var_arg() is legal in clap and must be legal here.

    consteval command_spec make_appending_tail() {
        command_builder cmd("demo");
        std::move(cmd).arg(arg_builder("rest").action(arg_action::append).trailing_var_arg());
        return std::move(cmd).freeze();
    }
    static constexpr command_spec appending_tail = make_appending_tail();
    static_assert(appending_tail.find_arg("rest")->is_multiple());
    static_assert(!appending_tail.find_arg("rest")->is_multiple_values_set());
    static_assert(appending_tail.find_arg("rest")->is_trailing_var_arg_set());

    // ---------------------------------------------------------------------------
    // styles() participates in equality
    // ---------------------------------------------------------------------------

    consteval command_spec make_with_palette(bool coloured) {
        command_builder cmd("demo");
        std::move(cmd).styles(coloured ? clapp::styles::styled() : clapp::styles::plain());
        return std::move(cmd).freeze();
    }
    static constexpr command_spec styled_tree = make_with_palette(true);
    static constexpr command_spec plain_tree  = make_with_palette(false);
    static_assert(styled_tree.get_styles() != plain_tree.get_styles());
    static_assert(!(styled_tree == plain_tree));
    static_assert(styled_tree == make_with_palette(true));

    // ---------------------------------------------------------------------------
    // clapp::id_table is not wired in yet — id.hpp's \warning, pinned
    // ---------------------------------------------------------------------------
    //
    // Every make_static_id() call site in include/clapp/builder/ takes the default slot, so
    // no id in a frozen tree is bound. M3 must either bind them or keep reading ids by
    // name; this assertion is here so the day that changes, it changes deliberately.
    static_assert(!demo.get_id().bound());
    static_assert(!demo.get_arguments()[0].get_id().bound());
    static_assert(demo.get_arguments()[0].get_id().slot == clapp::arg_id::unbound);

    // ---------------------------------------------------------------------------
    // Builder-side properties — constant-evaluated, not run
    // ---------------------------------------------------------------------------
    //
    // clapp::command_builder holds std::vector<command_builder>, std::vector<arg_builder>
    // and std::string, so it can never be a constexpr *variable*. It can, however, be
    // built, mutated, queried and thrown away entirely inside a consteval function — the
    // distinction flat_map.hpp:56-60 already draws. So these gate the build rather than a
    // test binary's exit code, as the project's testing rule requires.

    consteval bool a_fresh_command_is_empty() {
        const command_builder cmd("demo");
        return cmd.get_name() == "demo" && !cmd.get_bin_name().has_value() &&
               !cmd.get_version().has_value() && cmd.get_arguments().empty() &&
               cmd.get_groups().empty() && cmd.get_subcommands().empty() &&
               !cmd.has_subcommands() && cmd.get_display_order() == 999 &&
               cmd.get_color() == color_choice::auto_ && cmd.get_settings().empty() &&
               cmd.get_external_subcommand_value_parser() == nullptr;
    }
    static_assert(a_fresh_command_is_empty());

    consteval bool chaining_mutates_in_place() {
        command_builder cmd("demo");
        const command_builder& returned = std::move(cmd).about("blurb").version("1.0");
        return &returned == &cmd && cmd.get_about() == "blurb" && cmd.get_version() == "1.0";
    }
    static_assert(chaining_mutates_in_place());

    consteval bool an_empty_view_resets_an_optional_text_field() {
        command_builder cmd("demo");
        std::move(cmd).about("blurb").bin_name("x").version("1");
        if (!cmd.get_about().has_value()) return false;
        std::move(cmd).about("").bin_name("").version("");
        return !cmd.get_about().has_value() && !cmd.get_bin_name().has_value() &&
               !cmd.get_version().has_value();
    }
    static_assert(an_empty_view_resets_an_optional_text_field());

    consteval bool builder_side_getters_borrow_the_builders_storage() {
        command_builder cmd("demo");
        std::move(cmd)
                .display_name("Demo")
                .long_version("1.0 (abc)")
                .author("Nobody")
                .long_about("Long")
                .short_flag('D')
                .long_flag("demo")
                .override_usage("usage")
                .override_help("help")
                .help_template("{usage}")
                .before_help("b")
                .before_long_help("bl")
                .after_help("a")
                .after_long_help("al")
                .next_help_heading("Section")
                .subcommand_value_name("TASK")
                .subcommand_help_heading("Tasks");
        return cmd.get_display_name() == "Demo" && cmd.get_long_version() == "1.0 (abc)" &&
               cmd.get_author() == "Nobody" && cmd.get_long_about() == "Long" &&
               cmd.get_short_flag() == 'D' && cmd.get_long_flag() == "demo" &&
               cmd.get_override_usage() == "usage" && cmd.get_override_help() == "help" &&
               cmd.get_help_template() == "{usage}" && cmd.get_before_help() == "b" &&
               cmd.get_before_long_help() == "bl" && cmd.get_after_help() == "a" &&
               cmd.get_after_long_help() == "al" && cmd.get_next_help_heading() == "Section" &&
               cmd.get_subcommand_value_name() == "TASK" &&
               cmd.get_subcommand_help_heading() == "Tasks";
    }
    static_assert(builder_side_getters_borrow_the_builders_storage());

    consteval bool composition_accumulates_in_declaration_order() {
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("a").long_("a"))
                .args({arg_builder("b").long_("b"), arg_builder("c").long_("c")})
                .group(group_builder("g").arg("a"))
                .subcommand(command_builder("one"))
                .subcommands({command_builder("two"), command_builder("three")});
        return cmd.get_arguments().size() == 3 && cmd.get_arguments()[0].get_id() == "a" &&
               cmd.get_arguments()[2].get_id() == "c" && cmd.get_groups().size() == 1 &&
               cmd.get_subcommands().size() == 3 && cmd.has_subcommands() &&
               cmd.find_arg("b") != nullptr && cmd.find_arg("zz") == nullptr &&
               cmd.find_group("g") != nullptr && cmd.find_subcommand("two") != nullptr &&
               cmd.find_subcommand("zz") == nullptr;
    }
    static_assert(composition_accumulates_in_declaration_order());

    consteval bool find_subcommand_also_matches_an_alias() {
        command_builder cmd("demo");
        std::move(cmd).subcommand(command_builder("checkout").visible_alias("co"));
        return cmd.find_subcommand("co") != nullptr &&
               cmd.find_subcommand("co")->get_name() == "checkout";
    }
    static_assert(find_subcommand_also_matches_an_alias());

    consteval bool single_item_mutators_move_the_transformed_item_to_the_end() {
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("a").long_("a"))
                .arg(arg_builder("b").long_("b"))
                .arg(arg_builder("c").long_("c"))
                .subcommand(command_builder("one"))
                .subcommand(command_builder("two"))
                .subcommand(command_builder("three"));
        std::move(cmd).mut_arg("b", [](arg_builder a) {
            std::move(a).short_('b').required();
            return a;
        });
        std::move(cmd).mut_subcommand("two", [](command_builder c) {
            std::move(c).about("rewritten");
            return c;
        });
        return cmd.get_arguments()[0].get_id() == "a" && cmd.get_arguments()[1].get_id() == "c" &&
               cmd.get_arguments()[2].get_id() == "b" &&
               cmd.get_subcommands()[0].get_name() == "one" &&
               cmd.get_subcommands()[1].get_name() == "three" &&
               cmd.get_subcommands()[2].get_name() == "two" &&
               cmd.find_arg("b")->get_short() == 'b' && cmd.find_arg("b")->is_required_set() &&
               !cmd.find_arg("a")->get_short().has_value() &&
               cmd.find_subcommand("two")->get_about() == "rewritten";
    }
    static_assert(single_item_mutators_move_the_transformed_item_to_the_end());

    consteval bool mut_args_and_mut_subcommands_touch_every_element() {
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("a").long_("a"))
                .arg(arg_builder("b").long_("b"))
                .subcommand(command_builder("one"))
                .subcommand(command_builder("two"));
        std::move(cmd).mut_args([](arg_builder a) {
            std::move(a).hide();
            return a;
        });
        std::move(cmd).mut_subcommands([](command_builder c) {
            std::move(c).hide();
            return c;
        });
        return cmd.find_arg("a")->is_hide_set() && cmd.find_arg("b")->is_hide_set() &&
               cmd.find_subcommand("one")->is_hide_set() &&
               cmd.find_subcommand("two")->is_hide_set();
    }
    static_assert(mut_args_and_mut_subcommands_touch_every_element());

    consteval bool mut_group_moves_the_transformed_group_to_the_end() {
        command_builder cmd("demo");
        std::move(cmd)
                .arg(arg_builder("a").long_("a"))
                .group(group_builder("g1").arg("a"))
                .group(group_builder("g2").arg("a"))
                .group(group_builder("g3").arg("a"));
        std::move(cmd).mut_group("g2", [](group_builder g) {
            std::move(g).required().multiple();
            return g;
        });
        return cmd.get_groups()[0].get_id() == "g1" && cmd.get_groups()[1].get_id() == "g3" &&
               cmd.get_groups()[2].get_id() == "g2" && cmd.find_group("g2")->is_required_set() &&
               cmd.find_group("g2")->is_multiple();
    }
    static_assert(mut_group_moves_the_transformed_group_to_the_end());

    consteval bool every_boolean_setter_maps_onto_its_own_bit() {
        command_builder cmd("demo");
        std::move(cmd)
                .no_binary_name()
                .ignore_errors()
                .args_override_self()
                .dont_delimit_trailing_values()
                .disable_version_flag()
                .next_line_help()
                .disable_help_flag()
                .disable_help_subcommand()
                .disable_colored_help()
                .help_expected()
                .hide_possible_values()
                .infer_long_args()
                .infer_subcommands()
                .flatten_help()
                .arg_required_else_help()
                .allow_hyphen_values()
                .allow_negative_numbers()
                .trailing_var_arg()
                .allow_missing_positional()
                .hide()
                .subcommand_required()
                .allow_external_subcommands()
                .args_conflicts_with_subcommands()
                .subcommand_precedence_over_arg()
                .subcommand_negates_reqs()
                .multicall();
        return cmd.get_settings().count() == 26 && cmd.is_no_binary_name_set() &&
               cmd.is_ignore_errors_set() && cmd.is_args_override_self() &&
               cmd.is_dont_delimit_trailing_values_set() && cmd.is_disable_version_flag_set() &&
               cmd.is_next_line_help_set() && cmd.is_disable_help_flag_set() &&
               cmd.is_disable_help_subcommand_set() && cmd.is_disable_colored_help_set() &&
               cmd.is_help_expected_set() && cmd.is_hide_possible_values_set() &&
               cmd.is_infer_long_args_set() && cmd.is_infer_subcommands_set() &&
               cmd.is_flatten_help_set() && cmd.is_arg_required_else_help_set() &&
               cmd.is_allow_hyphen_values_set() && cmd.is_allow_negative_numbers_set() &&
               cmd.is_trailing_var_arg_set() && cmd.is_allow_missing_positional_set() &&
               cmd.is_hide_set() && cmd.is_subcommand_required_set() &&
               cmd.is_allow_external_subcommands_set() &&
               cmd.is_args_conflicts_with_subcommands_set() &&
               cmd.is_subcommand_precedence_over_arg_set() &&
               cmd.is_subcommand_negates_reqs_set() && cmd.is_multicall_set();
    }
    static_assert(every_boolean_setter_maps_onto_its_own_bit());

    // Exactly the fifteen setters clap routes through `global_setting` must also land in
    // global_settings_; the rest must not. Getting this wrong is invisible at the root and
    // only shows up one subcommand down.
    consteval bool the_global_setters_are_exactly_claps() {
        command_builder global_ones("demo");
        std::move(global_ones)
                .no_binary_name()
                .ignore_errors()
                .args_override_self()
                .dont_delimit_trailing_values()
                .disable_version_flag()
                .propagate_version()
                .next_line_help()
                .disable_help_flag()
                .disable_help_subcommand()
                .disable_colored_help()
                .help_expected()
                .hide_possible_values()
                .infer_long_args()
                .infer_subcommands()
                .color(color_choice::never);
        // Fifteen setters, sixteen bits: color() clears the other two colour bits and sets
        // one, so it contributes a single bit while occupying one setter slot.
        if (global_ones.get_global_settings().count() != 15) return false;

        command_builder local_ones("demo");
        std::move(local_ones)
                .flatten_help()
                .arg_required_else_help()
                .allow_hyphen_values()
                .allow_negative_numbers()
                .trailing_var_arg()
                .allow_missing_positional()
                .hide()
                .subcommand_required()
                .allow_external_subcommands()
                .args_conflicts_with_subcommands()
                .subcommand_precedence_over_arg()
                .subcommand_negates_reqs()
                .multicall();
        return local_ones.get_global_settings().empty() && local_ones.get_settings().count() == 13;
    }
    static_assert(the_global_setters_are_exactly_claps());

    consteval bool every_boolean_setter_also_turns_its_bit_off() {
        command_builder cmd("demo");
        std::move(cmd).multicall().hide().infer_subcommands();
        if (cmd.get_settings().count() != 3) return false;
        std::move(cmd).multicall(false).hide(false).infer_subcommands(false);
        return cmd.get_settings().empty() && cmd.get_global_settings().empty();
    }
    static_assert(every_boolean_setter_also_turns_its_bit_off());

    consteval bool the_raw_setting_verbs_reach_the_bits() {
        command_builder cmd("demo");
        std::move(cmd).setting(command_setting::flatten_help);
        if (!cmd.is_set(command_setting::flatten_help)) return false;
        std::move(cmd).unset_setting(command_setting::flatten_help);
        if (cmd.is_set(command_setting::flatten_help)) return false;
        std::move(cmd).setting(command_setting::hidden, true);
        if (!cmd.is_hide_set()) return false;
        std::move(cmd).setting(command_setting::hidden, false);
        if (cmd.is_hide_set()) return false;
        std::move(cmd).global_setting(command_setting::infer_long_args);
        if (!cmd.is_infer_long_args_set()) return false;
        if (!cmd.get_global_settings().is_set(command_setting::infer_long_args)) return false;
        std::move(cmd).unset_global_setting(command_setting::infer_long_args);
        return !cmd.is_infer_long_args_set() && cmd.get_global_settings().empty();
    }
    static_assert(the_raw_setting_verbs_reach_the_bits());

    consteval bool is_disable_version_flag_set_is_resolved_not_literal() {
        command_builder cmd("demo");
        // No version anywhere: nothing for --version to print, so it counts as disabled.
        if (!cmd.is_disable_version_flag_set()) return false;
        if (cmd.is_set(command_setting::disable_version_flag)) return false;
        std::move(cmd).version("1.0");
        if (cmd.is_disable_version_flag_set()) return false;
        std::move(cmd).version("").long_version("1.0 (abc)");
        return !cmd.is_disable_version_flag_set();
    }
    static_assert(is_disable_version_flag_set_is_resolved_not_literal());

    consteval bool is_disable_help_subcommand_set_is_resolved_not_literal() {
        command_builder cmd("demo");
        if (!cmd.is_disable_help_subcommand_set()) return false;  // nothing to describe yet
        std::move(cmd).subcommand(command_builder("child"));
        return !cmd.is_disable_help_subcommand_set();
    }
    static_assert(is_disable_help_subcommand_set_is_resolved_not_literal());

    consteval bool aliases_and_flag_aliases_accumulate_separately() {
        command_builder cmd("demo");
        std::move(cmd)
                .alias("d")
                .visible_alias("dem")
                .aliases({"x", "y"})
                .visible_aliases({"z"})
                .short_flag_alias('a')
                .visible_short_flag_alias('b')
                .short_flag_aliases({'c'})
                .visible_short_flag_aliases({'d'})
                .long_flag_alias("la")
                .visible_long_flag_alias("lb")
                .long_flag_aliases({"lc"})
                .visible_long_flag_aliases({"ld"});
        return cmd.get_all_aliases().size() == 5 && cmd.get_all_short_flag_aliases().size() == 4 &&
               cmd.get_all_long_flag_aliases().size() == 4 && !cmd.get_all_aliases()[0].visible &&
               cmd.get_all_aliases()[1].visible && cmd.get_all_long_flag_aliases()[3].name == "ld";
    }
    static_assert(aliases_and_flag_aliases_accumulate_separately());

    consteval bool term_widths_and_display_order_round_trip() {
        command_builder cmd("demo");
        if (cmd.get_term_width().has_value()) return false;
        std::move(cmd).term_width(100).max_term_width(140).display_order(3);
        if (cmd.get_term_width() != 100u) return false;
        if (cmd.get_max_term_width() != 140u) return false;
        if (cmd.get_display_order() != 3u) return false;
        std::move(cmd).term_width(0).max_term_width(0);
        return !cmd.get_term_width().has_value() && !cmd.get_max_term_width().has_value();
    }
    static_assert(term_widths_and_display_order_round_trip());

    consteval bool styles_default_to_clapps_palette_until_one_is_chosen() {
        command_builder cmd("demo");
        if (cmd.get_styles() != clapp::styles::styled()) return false;
        std::move(cmd).styles(clapp::styles::plain());
        return cmd.get_styles() == clapp::styles::plain();
    }
    static_assert(styles_default_to_clapps_palette_until_one_is_chosen());

    // ---------------------------------------------------------------------------
    // Runtime cases — the two properties that genuinely cannot cross the boundary
    // ---------------------------------------------------------------------------

    CLAPP_TEST("command_builder: the external value parser can be set by table too") {
        // Runtime, not static_assert: GCC 16.1.0 rejects a comparison of two unrelated
        // function-table addresses in a constant expression — the same restriction
        // value_parser.hpp records next to its own probes.
        command_builder cmd("demo");
        std::move(cmd).external_subcommand_value_parser(clapp::parser_for<clapp::os_string>());
        CLAPP_CHECK(cmd.get_external_subcommand_value_parser() ==
                    clapp::parser_for<clapp::os_string>());
        std::move(cmd).external_subcommand_value_parser(nullptr);
        CLAPP_CHECK(cmd.get_external_subcommand_value_parser() == nullptr);
    }

    CLAPP_TEST("command_spec: the lazy accessors do not allocate but do materialise") {
        // Runtime because the point *is* the run-time behaviour: these accessors return a
        // freshly allocated std::vector at the point of call, on a spec that already lives
        // in .rodata.
        CLAPP_CHECK(std::ranges::distance(tree.get_subcommands()) == 3);
        const std::vector<std::string_view> names = tree.get_name_and_visible_aliases();
        CLAPP_CHECK(names.size() == 1);
        CLAPP_CHECK(names[0] == "app");
        const std::vector<const arg_spec*> blocked =
                release.get_arg_conflicts_with(*release.find_arg("dry-run"));
        CLAPP_CHECK(blocked.size() == 3);
    }

    // ---------------------------------------------------------------------------
    // Compile-time consistency checks — the compile-error half of this suite
    // ---------------------------------------------------------------------------
    //
    // Each check below was reproduced by compiling a deliberate mistake; every one fails
    // the build with `error: uncaught exception '(const char*)(&"clapp::command_builder::
    // freeze: ...")'` naming both of the colliding things. They cannot live in this file:
    // a translation unit that triggers one does not compile.
    //
    //  1. an empty command name
    //  2. two arguments sharing an id, or an argument and a group sharing one
    //  3. two owners claiming one `--long`: argument vs argument, argument vs argument
    //     *alias*, argument vs subcommand long_flag, argument vs subcommand
    //     long_flag_alias, and subcommand vs subcommand — all one sorted scan, all naming
    //     both owners plus the "call disable_help_flag()" tip when the injected -h or -V
    //     is one of them
    //  4. the same five combinations for a `-s` short option / short flag / short alias
    //  5. two positionals claiming the same index
    //  6. a long option written with its leading '-'
    //  7. conflicts_with / overrides_with / requires / required_unless_present_any |_all /
    //     required_if_eq_any |_all naming an id that is neither an argument nor a group
    //  8. an argument conflicting with, or requiring, itself
    //  9. required() together with global()
    // 10. last() on something with a short or long option
    // 11. required() together with required_if_eq(), required_if_eq_all(),
    //     required_unless_present() or required_unless_present_all()
    // 12. a group id declared twice
    // 13. a group id that shadows an argument id
    // 14. a group listing an argument the command does not have
    // 15. a group requiring or conflicting with an unknown id
    // 16. two subcommands sharing a name, or a name colliding with another's alias
    // 17. value_hint::command_with_arguments on a non-positional, or on a positional
    //     without trailing_var_arg() / last()
    // 18. a subcommand long flag or long-flag alias written with its leading '-'
    // 19. a gap in the positional indices (highest index != number of positionals)
    // 20. trailing_var_arg() on something other than the last positional
    // 21. trailing_var_arg() together with last()
    // 22. trailing_var_arg() on an argument that is not is_multiple()
    // 23. more than one positional with last()
    // 24. any argument on a multicall() command
    // 25. propagate_version() with neither version() nor long_version()
    // 26. arg_action::version with neither version() nor long_version()
    // 27. an accumulating positional that is not the last one and has no guard
    // 28. an optional positional in front of a required one, without
    //     allow_missing_positional()
    // 29. a required last() positional on a command that also has subcommands, without
    //     subcommand_negates_reqs()
    // 30. multicall() together with no_binary_name()      (clap's assert_app_flags)
    // 31. help_expected() with an argument that has neither help() nor long_help()
    //     (clap's _panic_on_missing_help)
    // 32. an accumulating positional that is neither last nor second-to-last, without
    //     last() on the final one                          (clap's _verify_positionals #2)
    // 33. two positionals with an unbounded num_args(1..)  (clap's _verify_positionals #3)

}  // namespace
