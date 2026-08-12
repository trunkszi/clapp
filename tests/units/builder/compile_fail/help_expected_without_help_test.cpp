#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"myapp"}
                                             .help_expected()
                                             .arg(clapp::arg_builder{"foo"}.index(1))
                                             .freeze();

int main() { return spec.has_arg("foo") ? 0 : 1; }
