#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"test"}
                .subcommand(clapp::command_builder{"some"}.short_flag('f').long_flag("some"))
                .arg(clapp::arg_builder{"test"}.short_('f'))
                .freeze();

int main() { return spec.has_arg("test") ? 0 : 1; }
