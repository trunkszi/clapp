#include <clapp/clapp.hpp>

#include <cstdio>
#include <expected>
#include <print>
#include <string>
#include <utility>

namespace {
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::raw_args;

    /**
     * \brief Build the command tree.
     * \return The frozen tree.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder app("MyApp");
        std::move(app)
                .version("1.0")
                .about("Does awesome things")
                .long_about("Does awesome things.\n\nThis paragraph only appears under --help, "
                    "which is what .long_about is for: a sentence for the summary "
                    "screen and a description for the full one.")
                .after_help("Run with --help for the long description.")
                .arg(arg_builder("two")
                    .long_("two")
                    .value_name("VALUE")
                    .help("Listed first, because it is declared first")
                    .required()
                    .value_parser<std::string>())
                .arg(arg_builder("one")
                    .long_("one")
                    .value_name("VALUE")
                    .help("Listed second; help order is declaration order")
                    .required()
                    .value_parser<std::string>());
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /**
     * \brief Print \p err the way clapp::parse<T>() would, and yield its exit status.
     * \param err What clapp::parse() rejected the command line with.
     * \return The status `main` should return.
     */
    [[nodiscard]] int report(const error &err) {
        const std::string text = err.render().to_string();
        std::FILE *const out = err.use_stderr() ? stderr : stdout;
        static_cast<void>(std::fwrite(text.data(), 1, text.size(), out));
        if (!text.empty() && text.back() != '\n') static_cast<void>(std::fputc('\n', out));
        return err.exit_code();
    }
} // namespace

int main(int argc, char **argv) {
    const std::expected<arg_matches, error> got = clapp::parse(spec, raw_args(argc, argv));
    if (!got.has_value()) return report(got.error());

    // `.required()` above is what makes both lookups total; an absent value could never
    // reach this line.
    std::println("two: \"{}\"", **got->get_one<std::string>("two"));
    std::println("one: \"{}\"", **got->get_one<std::string>("one"));
}
