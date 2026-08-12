#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>

// clap spends seven #[should_panic] cases on this one rule -- SetTrue, SetFalse, Count,
// Help, HelpShort, HelpLong and Version, each with num_args(1..). All seven are the same
// clapp::max_num_args() envelope, and a compile-fail snippet can only witness the FIRST
// rejection, so the remaining six are pinned as static_asserts on max_num_args() in
// tests/units/builder/action_test.cpp instead.
constexpr clapp::command_spec spec =
        clapp::command_builder{"test"}
                .arg(clapp::arg_builder{"mammal"}
                             .long_("mammal")
                             .action(clapp::arg_action::set_true)
                             .num_args(clapp::value_range::at_least(1)))
                .freeze();

int main() { return spec.has_arg("mammal") ? 0 : 1; }
