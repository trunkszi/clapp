#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"test"}
                .arg(clapp::arg_builder{"pos"}.index(1).num_args(clapp::value_range::empty()))
                .freeze();

int main() { return spec.has_arg("pos") ? 0 : 1; }
