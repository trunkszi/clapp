#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

// clap splits this into two cases, positional_arg_with_long and
// positional_arg_with_short; both land on the same clapp rule, so one snippet covers
// them and the short half is pinned by the negative control's `source`/`destination`.
constexpr clapp::command_spec spec = clapp::command_builder{"test"}
                                             .arg(clapp::arg_builder{"arg"}.index(1).long_("arg"))
                                             .freeze();

int main() { return spec.has_arg("arg") ? 0 : 1; }
