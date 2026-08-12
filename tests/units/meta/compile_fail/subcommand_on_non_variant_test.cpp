#include <clapp/meta/parse.hpp>

#include <string>

namespace {

struct cli {
    [[= clapp::subcommand{}]] std::string not_a_variant;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    return parsed.not_a_variant.empty() ? 0 : 1;
}
