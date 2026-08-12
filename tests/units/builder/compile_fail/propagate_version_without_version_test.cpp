#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"foo"}
                                             .propagate_version()
                                             .subcommand(clapp::command_builder{"bar"})
                                             .freeze();

int main() { return spec.has_subcommand("bar") ? 0 : 1; }
