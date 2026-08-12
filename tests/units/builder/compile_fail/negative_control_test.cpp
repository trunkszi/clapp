#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>

// --------------------------------------------------------------------------
// Ids, flags, groups and cross-references: everything named exists, exactly once.
// --------------------------------------------------------------------------
constexpr clapp::command_spec references =
        clapp::command_builder{"prog"}
                .arg(clapp::arg_builder{"arg1"}.short_('a').long_("long"))
                .arg(clapp::arg_builder{"arg2"}.short_('b').long_("other"))
                .arg(clapp::arg_builder{"config"}
                             .long_("config")
                             .conflicts_with("arg1")
                             .overrides_with("arg2")
                             .requires_("arg1")
                             .required_if_eq("arg2", "val")
                             .required_unless_present("arg1"))
                .group(clapp::group_builder{"req"}.arg("arg1").arg("arg2"))
                .freeze();

// --------------------------------------------------------------------------
// Positionals: one unbounded collector, in the last slot, with required order intact.
// --------------------------------------------------------------------------
constexpr clapp::command_spec positionals =
        clapp::command_builder{"lip"}
                .arg(clapp::arg_builder{"target"}.index(1).required())
                .arg(clapp::arg_builder{"files"}
                             .index(2)
                             .action(clapp::arg_action::set)
                             .required()
                             .num_args(clapp::value_range::at_least(1)))
                .freeze();

// --------------------------------------------------------------------------
// Arg shape: an action within its envelope, and value_names that num_args can fill.
// --------------------------------------------------------------------------
constexpr clapp::command_spec shapes =
        clapp::command_builder{"test"}
                .arg(clapp::arg_builder{"mammal"}.long_("mammal").action(
                        clapp::arg_action::set_true))
                .arg(clapp::arg_builder{"foo"}
                             .long_("foo")
                             .required()
                             .action(clapp::arg_action::set)
                             .num_args(clapp::value_range::exactly(2))
                             .value_names({"one", "two"}))
                .freeze();

// --------------------------------------------------------------------------
// Subcommands: distinct names, aliases and flags, and a multicall command with no args.
// --------------------------------------------------------------------------
constexpr clapp::command_spec subcommands =
        clapp::command_builder{"test"}
                .subcommand(clapp::command_builder{"some"}.short_flag('f').long_flag("some"))
                .subcommand(clapp::command_builder{"result"}.short_flag('t').long_flag("flag"))
                .subcommand(clapp::command_builder{"unique"}.alias("repeat"))
                .arg(clapp::arg_builder{"probe"}.short_('p'))
                .freeze();

constexpr clapp::command_spec multicall = clapp::command_builder{"repl"}
                                                  .version("1.0.0")
                                                  .propagate_version()
                                                  .multicall()
                                                  .subcommand(clapp::command_builder{"foo"})
                                                  .subcommand(clapp::command_builder{"bar"})
                                                  .freeze();

// --------------------------------------------------------------------------
// The injected flags: taken over legally, by switching the injection off first.
// --------------------------------------------------------------------------
constexpr clapp::command_spec injected =
        clapp::command_builder{"conflict"}
                .disable_help_flag()
                .disable_version_flag()
                .arg(clapp::arg_builder{"help"}.short_('h').action(clapp::arg_action::set_true))
                .arg(clapp::arg_builder{"custom-help"}.long_("help").action(
                        clapp::arg_action::set_true))
                .freeze();

// version() present, so both the injected `--version` and arg_action::version are legal.
constexpr clapp::command_spec versioned =
        clapp::command_builder{"foo"}
                .version("3.0")
                .propagate_version()
                .arg(clapp::arg_builder{"ver"}.long_("ver").action(clapp::arg_action::version))
                .subcommand(clapp::command_builder{"bar"})
                .freeze();

// --------------------------------------------------------------------------
// help_expected(): satisfied, including for the injected `--help`.
// --------------------------------------------------------------------------
constexpr clapp::command_spec documented =
        clapp::command_builder{"myapp"}
                .help_expected()
                .arg(clapp::arg_builder{"foo"}.index(1).help("It does foo stuff"))
                .freeze();

// The forward direction. If a guard started rejecting a valid definition, one of the
// initializers above is where it would show; these assertions additionally pin that the
// frozen trees say what the definitions meant.
static_assert(references.has_arg("config"));
static_assert(references.has_group("req"));
static_assert(positionals.has_positionals());
static_assert(positionals.has_arg("files"));
static_assert(shapes.has_arg("mammal"));
static_assert(subcommands.has_subcommand("unique"));
static_assert(multicall.is_multicall_set());
static_assert(injected.has_arg("custom-help"));
static_assert(versioned.has_arg("ver"));
static_assert(documented.has_arg("foo"));

int main() { return references.has_arg("arg1") ? 0 : 1; }
