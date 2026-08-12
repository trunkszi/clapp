#include <clapp/meta/parse.hpp>

/** A type with no clapp::value_parser specialization and no way to guess one. */
namespace {

struct opaque {
    int a;
    int b;
};

struct cli {
    bool verbose;
    opaque payload;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    return parsed.verbose || parsed.payload.a != 0 || parsed.payload.b != 0 ? 0 : 1;
}
