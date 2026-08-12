#include <clapp/meta/parse.hpp>

#include <string>

namespace {

struct cli {
    [[= clapp::arg{.short_ = 'n'}]] std::string name;
    [[= clapp::arg{.short_ = 'n'}]] int number;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    return parsed.name.empty() ? parsed.number : 0;
}
