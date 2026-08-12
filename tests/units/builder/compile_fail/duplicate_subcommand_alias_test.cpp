#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"test"}
                .subcommand(clapp::command_builder{"repeat"})
                .subcommand(clapp::command_builder{"unique"}.alias("repeat"))
                .freeze();

int main() { return spec.has_subcommand("repeat") ? 0 : 1; }
