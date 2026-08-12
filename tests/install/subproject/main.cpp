#include <clapp/clapp.hpp>

#include <cstdio>
#include <string>

namespace {
    struct[[= clapp::cmd{
                .name = "subproject-consumer",
                .version = "0.1.0",
                .about = "add_subdirectory clapp consumer smoke test"
            }]] cli {
        [[= clapp::arg{
            .short_ = 'n',
            .long_ = "name",
            .help = "Name to greet",
            .default_value = "world"
        }]] std::string name;

        [[= clapp::arg{.short_ = 'v', .long_ = "verbose", .help = "Be verbose"}]] bool verbose = false;
    };
}

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);
    std::printf("hello %s verbose=%d\n", args.name.c_str(), args.verbose ? 1 : 0);
    return 0;
}
