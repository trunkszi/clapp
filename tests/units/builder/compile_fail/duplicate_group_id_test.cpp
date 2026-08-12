#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"group"}
                .arg(clapp::arg_builder{"flag"}.short_('f').long_("flag"))
                .arg(clapp::arg_builder{"color"}.short_('c').long_("color"))
                .group(clapp::group_builder{"req"}.arg("flag").required())
                .group(clapp::group_builder{"req"}.arg("color").required())
                .freeze();

int main() { return spec.has_group("req") ? 0 : 1; }
