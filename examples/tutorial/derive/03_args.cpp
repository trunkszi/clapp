#include <clapp/clapp.hpp>

#include <cstddef>
#include <print>
#include <string>
#include <vector>

namespace {
    /** \brief One command, five argument shapes. */
    struct[[= clapp::cmd{
                .name = "03_args",
                .version = "1.0.0",
                .about = "Positionals, options, flags and counters"
            }]] cli {
        /** Required: a bare `std::string` has no way to spell "the user did not give one". */
        [[= clapp::arg{.index = 1, .help = "Who to greet", .value_name = "NAME"}]] std::string name;

        /**
     * Optional, because `.default_value` says what absence means. It is also what
     * renders `[default: 2020]` in help — see the \warning on clapp::arg_attr's member:
     * an annotation default outranks a member initializer, so state a default once.
     */
        [[= clapp::arg{
            .index = 2,
            .help = "Port to connect on",
            .value_name = "PORT",
            .default_value = "2020"
        }]] unsigned port;

        /** Repeatable. Every occurrence appends one value, so `-t red -t blue` collects two. */
        [[= clapp::arg{
            .short_ = 't',
            .long_ = "tag",
            .help = "Tag to attach; repeat for more",
            .value_name = "TAG"
        }]] std::vector<std::string>
        tags;

        /**
     * A counter rather than a flag: `-vv` is 2. `.act` is needed because the *type*
     * cannot distinguish a counter from an ordinary integer option.
     */
        [[= clapp::arg{
            .short_ = 'v',
            .long_ = "verbose",
            .act = clapp::action::count,
            .help = "Increase logging verbosity"
        }]] clapp::count_type verbose;

        /** A flag. `bool` takes no value and is `false` when the flag is absent. */
        [[= clapp::arg{.short_ = 'q', .long_ = "quiet", .help = "Silence all output"}]] bool quiet;
    };
}

namespace {
    /**
     * \brief Render \p values as a bracketed, quoted list, so an empty one is visible.
     * \param values The collected tags.
     * \return `[]`, `["red"]`, `["red", "blue"]`.
     */
    [[nodiscard]] std::string quoted_list(const std::vector<std::string> &values) {
        std::string out{"["};
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) out += ", ";
            out += '"';
            out += values[i];
            out += '"';
        }
        out += ']';
        return out;
    }
} // namespace

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);

    std::println("name: \"{}\"", args.name);
    std::println("port: {}", args.port);
    std::println("tags: {}", quoted_list(args.tags));
    std::println("verbose: {}", static_cast<unsigned>(args.verbose));
    std::println("quiet: {}", args.quiet);
}
