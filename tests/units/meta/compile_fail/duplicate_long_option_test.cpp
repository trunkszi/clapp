#include <clapp/meta/parse.hpp>

#include <string>

namespace {

struct cli {
    /** Derives `--dry-run` from the field name; the source never says "dry-run". */
    bool dry_run;
    /** Claims `--dry-run` explicitly; the source never derives it from "mode". */
    [[= clapp::arg{.long_ = "dry-run"}]] std::string mode;
};

}  // namespace

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    return parsed.dry_run || !parsed.mode.empty() ? 0 : 1;
}
