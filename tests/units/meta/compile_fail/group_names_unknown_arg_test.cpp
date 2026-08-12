#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"demo"}
                .arg(clapp::arg_builder{"verbose"}.long_("verbose"))
                .group(clapp::group_builder{"mode"}.arg("verbose").arg("nonexistent"))
                .freeze();

int main() { return spec.has_arg("verbose") ? 0 : 1; }
