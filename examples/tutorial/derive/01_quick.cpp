#include <clapp/clapp.hpp>

#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <variant>

namespace {
    /** \brief `01_quick test` — a subcommand is just another struct. */
    struct[[= clapp::cmd{.name = "test", .about = "does testing things"}]] cmd_test {
        /** A `bool` member is a flag: it takes no value and defaults to `false`. */
        [[= clapp::arg{.short_ = 'l', .long_ = "list", .help = "lists test values"}]] bool list;
    };

    /** \brief The root command. */
    struct[[= clapp::cmd{
                .name = "01_quick", .version = "1.0.0", .about = "A quick tour of clapp's derive API"
            }]]
            cli {
        /** `std::optional` plus `.index = 1` is an optional positional. */
        [[= clapp::arg{.index = 1, .help = "Optional name to operate on"}]] std::optional<std::string>
        name;

        /**
     * `std::filesystem::path` is one of the types clapp already parses, and naming it
     * also seeds the shell-completion hint with clapp::value_hint::any_path.
     */
        [[= clapp::arg{
            .short_ = 'c',
            .long_ = "config",
            .help = "Sets a custom config file",
            .value_name = "FILE"
        }]] std::optional<std::filesystem::path>
        config;

        /**
     * `.act = clapp::action::count` turns a flag into a counter, so `-ddd` is 3. The
     * field type must be able to hold the count; clapp::count_type is the one that
     * always can.
     */
        [[= clapp::arg{
            .short_ = 'd',
            .long_ = "debug",
            .act = clapp::action::count,
            .help = "Turn debugging information on"
        }]] clapp::count_type debug;

        /**
     * `std::optional<std::variant<...>>` is the *optional* subcommand set. A bare
     * `std::variant` would additionally imply `subcommand_required` and
     * `arg_required_else_help`, which is what step 4 uses.
     */
        [[= clapp::subcommand{}]] std::optional<std::variant<cmd_test> > command;
    };
}

int main(int argc, char **argv) {
    // Prints help, the version or an error and exits when it has to; the non-exiting
    // spelling is `clapp::try_parse<cli>(argc, argv)`.
    const cli args = clapp::parse<cli>(argc, argv);

    if (args.name.has_value()) std::println("Value for name: {}", *args.name);
    if (args.config.has_value()) std::println("Value for config: {}", args.config->string());

    switch (args.debug) {
        case 0:
            std::println("Debug mode is off");
            break;
        case 1:
            std::println("Debug mode is kind of on");
            break;
        case 2:
            std::println("Debug mode is on");
            break;
        default:
            std::println("Don't be crazy");
            break;
    }

    if (args.command.has_value()) {
        const cmd_test &chosen = std::get<cmd_test>(*args.command);
        if (chosen.list)
            std::println("Printing testing lists...");
        else
            std::println("Not printing testing lists...");
    }
}
