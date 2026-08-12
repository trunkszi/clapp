#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"test"}
                .subcommand(clapp::command_builder{"some"}.long_flag("flag"))
                .subcommand(
                        clapp::command_builder{"result"}.long_flag("test").long_flag_alias("flag"))
                .freeze();

int main() { return spec.has_subcommand("some") ? 0 : 1; }
