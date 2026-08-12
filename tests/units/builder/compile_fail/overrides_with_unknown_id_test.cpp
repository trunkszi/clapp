#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"prog"}
                .arg(clapp::arg_builder{"config"}.long_("config").overrides_with("extra"))
                .freeze();

int main() { return spec.has_arg("config") ? 0 : 1; }
