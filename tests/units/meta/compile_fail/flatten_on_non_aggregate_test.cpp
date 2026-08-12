#include <clapp/meta/parse.hpp>

namespace {

struct cli {
    [[= clapp::flatten{}]] int not_a_struct;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    return parsed.not_a_struct;
}
