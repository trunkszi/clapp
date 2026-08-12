#include <clapp/clapp.hpp>

#include <cstdio>
#include <string>

namespace {
    /** \brief The struct from the README, compiled by a stranger. */
    struct[[= clapp::cmd{
                .name = "consumer", .version = "0.1.0", .about = "installed-clapp consumer smoke test"
            }]]
            cli {
        /** Name to greet. */
        [[= clapp::arg{
            .short_ = 'n',
            .long_ = "name",
            .help = "Name to greet",
            .default_value = "world"
        }]] std::string name;

        /** Verbosity flag — the minimal `bool` shape from the task statement. */
        [[= clapp::arg{.short_ = 'v', .long_ = "verbose", .help = "Be verbose"}]] bool verbose = false;
    };
}

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);
    std::printf("hello %s verbose=%d\n", args.name.c_str(), args.verbose ? 1 : 0);
    return 0;
}
