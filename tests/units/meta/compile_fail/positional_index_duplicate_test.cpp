#include <clapp/meta/parse.hpp>

#include <string>

namespace {

struct cli {
    [[= clapp::arg{.index = 1}]] std::string source;
    [[= clapp::arg{.index = 1}]] std::string destination;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    return parsed.source.empty() && parsed.destination.empty() ? 0 : 1;
}
