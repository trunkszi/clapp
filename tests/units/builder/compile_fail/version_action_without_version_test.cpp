#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"foo"}
                .arg(clapp::arg_builder{"ver"}.long_("ver").action(clapp::arg_action::version))
                .freeze();

int main() { return spec.has_arg("ver") ? 0 : 1; }
