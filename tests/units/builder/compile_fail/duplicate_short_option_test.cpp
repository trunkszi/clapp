#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"some"}
                                             .arg(clapp::arg_builder{"arg1"}.short_('a'))
                                             .arg(clapp::arg_builder{"arg2"}.short_('a'))
                                             .freeze();

int main() { return spec.has_arg("arg1") ? 0 : 1; }
