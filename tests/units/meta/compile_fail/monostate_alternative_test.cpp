#include <clapp/meta/parse.hpp>

#include <string>
#include <variant>

namespace {

struct[[= clapp::cmd{.name = "add"}]] cmd_add {
    std::string message;
};

struct[[= clapp::cmd{.name = "ms"}]] cli {
    [[= clapp::subcommand{}]] std::variant<std::monostate, cmd_add> command;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    if (const cmd_add* add = std::get_if<cmd_add>(&parsed.command)) {
        return add->message.empty() ? 0 : 1;
    }
    return 0;
}
