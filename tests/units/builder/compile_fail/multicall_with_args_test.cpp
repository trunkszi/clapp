#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"repl"}
                                             .version("1.0.0")
                                             .propagate_version()
                                             .multicall()
                                             .subcommand(clapp::command_builder{"foo"})
                                             .subcommand(clapp::command_builder{"bar"})
                                             .arg(clapp::arg_builder{"oh-no"}.index(1))
                                             .freeze();

int main() { return spec.has_arg("oh-no") ? 0 : 1; }
