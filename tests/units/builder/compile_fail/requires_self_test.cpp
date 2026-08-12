#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"flag_required"}
                .arg(clapp::arg_builder{"flag"}.short_('f').long_("flag").requires_("flag"))
                .freeze();

int main() { return spec.has_arg("flag") ? 0 : 1; }
