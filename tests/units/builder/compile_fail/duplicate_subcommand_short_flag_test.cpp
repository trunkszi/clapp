#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"test"}
                .subcommand(clapp::command_builder{"some"}.short_flag('f').long_flag("some"))
                .subcommand(clapp::command_builder{"result"}.short_flag('t').short_flag_alias('f'))
                .freeze();

int main() { return spec.has_subcommand("some") ? 0 : 1; }
