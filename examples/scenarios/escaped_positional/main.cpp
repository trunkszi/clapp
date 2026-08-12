#include <clapp/clapp.hpp>

#include <optional>
#include <print>
#include <string>
#include <vector>

namespace {
    /** \brief Two flags and one escaped, variadic positional. */
    struct[[= clapp::cmd{
                .name = "escaped-positional", .version = "1.0.0", .about = "Demonstrates the -- escape"
            }]]
            cli {
        [[= clapp::arg{.short_ = 'f', .no_long = true, .help = "A flag"}]] bool eff;

        [[= clapp::arg{
            .short_ = 'p',
            .no_long = true,
            .help = "A value",
            .value_name = "PEAR"
        }]] std::optional<std::string>
        pea;

        /** Reachable only after `--`; see the file header. */
        [[= clapp::arg{.index = 1, .value_name = "SLOP", .last = true}]] std::vector<std::string> slop;
    };
}

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);

    std::println("-f used: {}", args.eff);
    std::println("-p's value: {}",
                 args.pea.has_value() ? "Some(\"" + *args.pea + "\")" : std::string{"None"});

    std::string joined;
    for (const std::string &word: args.slop) {
        if (!joined.empty()) joined += ", ";
        joined += '"';
        joined += word;
        joined += '"';
    }
    std::println("'slops' values: [{}]", joined);
}
