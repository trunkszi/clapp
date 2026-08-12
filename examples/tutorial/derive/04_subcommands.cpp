#include <clapp/clapp.hpp>

#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <variant>

namespace {
    /** \brief `04_subcommands add [NAME]`. */
    struct[[= clapp::cmd{.name = "add", .about = "Adds files to myapp"}]] cmd_add {
        [[= clapp::arg{
            .index = 1, .help = "File to add", .value_name = "NAME"
        }]] std::optional<std::string>
        name;
    };

    /** \brief `04_subcommands remove [NAME]`. */
    struct[[= clapp::cmd{.name = "remove", .about = "Removes files from myapp"}]] cmd_remove {
        [[= clapp::arg{
            .index = 1, .help = "File to remove", .value_name = "NAME"
        }]] std::optional<std::string>
        name;
    };

    /** \brief The root command. */
    struct[[= clapp::cmd{
                .name = "04_subcommands",
                .version = "1.0.0",
                .about = "A program with more than one verb",
                .propagate_version = true
            }]] cli {
        /** A bare variant: the subcommand is mandatory. See the file header. */
        [[= clapp::subcommand{}]] std::variant<cmd_add, cmd_remove> command;
    };
}

namespace {
    /**
     * \brief Print the line clap's tutorial prints, for whichever verb ran.
     * \param verb The subcommand's name as the user spelled it.
     * \param name The optional positional it collected.
     */
    void report(std::string_view verb, const std::optional<std::string> &name) {
        if (name.has_value())
            std::println("'{}' was used, name is: Some(\"{}\")", verb, *name);
        else
            std::println("'{}' was used, name is: None", verb);
    }
} // namespace

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);

    // Exhaustive by construction: a new alternative with no arm here is a compile error.
    std::visit(
        [](const auto &chosen) {
            using chosen_type = std::remove_cvref_t<decltype(chosen)>;
            if constexpr (std::same_as<chosen_type, cmd_add>)
                report("add", chosen.name);
            else
                report("remove", chosen.name);
        },
        args.command);
}
