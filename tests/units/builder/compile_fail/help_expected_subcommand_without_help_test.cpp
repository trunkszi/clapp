#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

constexpr clapp::command_spec spec =
        clapp::command_builder{"myapp"}
                .help_expected()
                .arg(clapp::arg_builder{"foo"}.index(1).help("foo is documented"))
                .subcommand(clapp::command_builder{"bar"}
                                    .arg(clapp::arg_builder{"create"}.long_("create").help(
                                            "create is documented"))
                                    .arg(clapp::arg_builder{"delete"}.long_("delete")))
                .freeze();
