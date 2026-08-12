#include <clapp/meta/parse.hpp>

#include <string>
#include <variant>

namespace {

struct[[= clapp::cmd{.name = "add"}]] cmd_add {
    std::string message;
};

struct[[= clapp::external_subcommand{}]] cmd_other {
    std::string rest;
};

struct[[= clapp::cmd{.name = "ext"}]] cli {
    [[= clapp::subcommand{}]] std::variant<cmd_add, cmd_other> command;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    if (const cmd_add* add = std::get_if<cmd_add>(&parsed.command)) {
        return add->message.empty() ? 0 : 1;
    }
    return std::get<cmd_other>(parsed.command).rest.empty() ? 0 : 1;
}
