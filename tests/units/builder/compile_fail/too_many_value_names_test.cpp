#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>

constexpr clapp::command_spec spec = clapp::command_builder{"test"}
                                             .arg(clapp::arg_builder{"foo"}
                                                          .long_("foo")
                                                          .required()
                                                          .action(clapp::arg_action::set)
                                                          .num_args(clapp::value_range::exactly(1))
                                                          .value_names({"one", "two"}))
                                             .freeze();

int main() { return spec.has_arg("foo") ? 0 : 1; }
