#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"lip"}
                .arg(clapp::arg_builder{"files"}
                             .index(1)
                             .action(clapp::arg_action::set)
                             .required()
                             .num_args(clapp::value_range::at_least(1)))
                .arg(clapp::arg_builder{"target"}.index(2))
                .freeze();

int main() { return spec.has_arg("files") ? 0 : 1; }
