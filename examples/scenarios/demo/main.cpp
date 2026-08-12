#include <clapp/clapp.hpp>

#include <print>
#include <string>

namespace {
    /** \brief Simple program to greet a person. */
    struct[[= clapp::cmd{
                .name = "demo", .version = "1.0.0", .about = "Simple program to greet a person"
            }]] args {
        /** Name of the person to greet. */
        [[= clapp::arg{
            .short_ = 'n',
            .long_ = "name",
            .help = "Name of the person to greet"
        }]] std::string name;

        /**
     * Number of times to greet.
     *
     * `.default_value` is what makes this argument optional: the deduction table marks a
     * plain scalar `required` unless it has a default, and this is one of the two ways to
     * give it one (a member initializer, `unsigned count = 1;`, is the other).
     */
        [[= clapp::arg{
            .short_ = 'c',
            .long_ = "count",
            .help = "Number of times to greet",
            .default_value = "1"
        }]] unsigned count;
    };
}

int main(int argc, char **argv) {
    // Prints the error (or `--help`, or `--version`) and exits; the non-exiting spelling
    // is `clapp::try_parse<args>(argc, argv)`, which hands back a
    // `std::expected<args, clapp::error>`.
    const args parsed = clapp::parse<args>(argc, argv);

    for (unsigned i = 0; i < parsed.count; ++i) std::println("Hello {}!", parsed.name);
}
