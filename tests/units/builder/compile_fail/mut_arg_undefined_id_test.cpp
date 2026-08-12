#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>

#include <utility>

constexpr clapp::command_spec spec =
        clapp::command_builder{"myapp"}
                .arg(clapp::arg_builder{"real"}.long_("real").help("a real argument"))
                .mut_arg("typo", [](clapp::arg_builder a) { return std::move(a).hide(); })
                .freeze();
