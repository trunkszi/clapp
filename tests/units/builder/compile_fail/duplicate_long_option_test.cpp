#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"some"}
                                             .arg(clapp::arg_builder{"arg1"}.long_("long"))
                                             .arg(clapp::arg_builder{"arg2"}.long_("long"))
                                             .freeze();

int main() { return spec.has_arg("arg1") ? 0 : 1; }
