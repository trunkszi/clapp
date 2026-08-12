#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"conflict"}
                .arg(clapp::arg_builder{"custom-help"}.long_("help").action(
                        clapp::arg_action::set_true))
                .freeze();

int main() { return spec.has_arg("custom-help") ? 0 : 1; }
