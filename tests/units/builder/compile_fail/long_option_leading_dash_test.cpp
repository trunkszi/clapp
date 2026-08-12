#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"mycat"}
                .arg(clapp::arg_builder{"filename"}.long_("--filename"))
                .freeze();

int main() { return spec.has_arg("filename") ? 0 : 1; }
