#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"foo"}
        .version("3.0")
        .arg(clapp::arg_builder{"ver"}.long_("version").action(clapp::arg_action::set_true))
        .freeze();

int main() { return spec.has_arg("ver") ? 0 : 1; }
