// CONTRACT: mut_group() names an explicitly declared group on this command.

#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>

#include <utility>

consteval clapp::command_spec make_command() {
    clapp::command_builder cmd{"myapp"};
    std::move(cmd).mut_group("typo", [](clapp::group_builder group) { return group; });
    return std::move(cmd).freeze();
}

constexpr clapp::command_spec command = make_command();
