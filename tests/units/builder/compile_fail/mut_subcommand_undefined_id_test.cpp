// CONTRACT: mut_subcommand() names an explicitly declared child command.

#include <clapp/builder/command.hpp>

#include <utility>

consteval clapp::command_spec make_command() {
    clapp::command_builder cmd{"myapp"};
    std::move(cmd).mut_subcommand("typo", [](clapp::command_builder child) { return child; });
    return std::move(cmd).freeze();
}

constexpr clapp::command_spec command = make_command();
