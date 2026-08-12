#include <clapp/clapp.hpp>

#include <print>
#include <string>

namespace {
    /** \brief The root command; every knob it carries is metadata rather than behaviour. */
    struct[[= clapp::cmd{
                .name = "MyApp",
                .version = "1.0",
                .about = "Does awesome things",
                .long_about =
                "Does awesome things.\n\nThis paragraph only appears under --help, "
                "which is what .long_about is for: a sentence for the summary "
                "screen and a description for the full one.",
                .after_help = "Run with --help for the long description."
            }]] cli {
        /** A bare `std::string` with no default is a required argument. */
        [[= clapp::arg{
            .long_ = "two",
            .help = "Listed first, because it is declared first",
            .value_name = "VALUE"
        }]] std::string two;

        [[= clapp::arg{
            .long_ = "one",
            .help = "Listed second; help order is declaration order",
            .value_name = "VALUE"
        }]] std::string one;
    };
}

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);

    std::println("two: \"{}\"", args.two);
    std::println("one: \"{}\"", args.one);
}
