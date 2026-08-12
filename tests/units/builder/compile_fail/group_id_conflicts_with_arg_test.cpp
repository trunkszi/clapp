#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"group"}
                                             .arg(clapp::arg_builder{"a"}.long_("a"))
                                             .group(clapp::group_builder{"a"})
                                             .freeze();

int main() { return spec.has_arg("a") ? 0 : 1; }
