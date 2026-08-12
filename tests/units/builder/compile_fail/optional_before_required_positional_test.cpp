#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"test"}
                                             .arg(clapp::arg_builder{"FILE1"}.index(1))
                                             .arg(clapp::arg_builder{"FILE2"}.index(2).required())
                                             .freeze();

int main() { return spec.has_arg("FILE1") ? 0 : 1; }
