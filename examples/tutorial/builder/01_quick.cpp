#include <clapp/clapp.hpp>

#include <cstdio>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {
    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::raw_args;

    /**
     * \brief Build the command tree.
     * \return The frozen tree; see the file header for why this is `consteval`.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder test("test");
        std::move(test)
                .about("does testing things")
                .arg(arg_builder("list")
                    .short_('l')
                    .long_("list")
                    .help("lists test values")
                    .action(arg_action::set_true));

        command_builder app("01_quick");
        std::move(app)
                .version("1.0.0")
                .about("A quick tour of clapp's derive API")
                .arg(arg_builder("name")
                    .index(1)
                    .help("Optional name to operate on")
                    .value_parser<std::string>())
                .arg(arg_builder("config")
                    .short_('c')
                    .long_("config")
                    .value_name("FILE")
                    .help("Sets a custom config file")
                    .value_parser<std::filesystem::path>())
                .arg(arg_builder("debug")
                    .short_('d')
                    .long_("debug")
                    .action(arg_action::count)
                    .help("Turn debugging information on"))
                .subcommand(std::move(test));
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /**
     * \brief Print \p err the way clapp::parse<T>() would, and yield its exit status.
     * \param err What clapp::parse() rejected the command line with.
     * \return The status `main` should return.
     *
     * \note This is the whole of what the derive layer's exiting `clapp::parse<T>()` adds
     *       over `clapp::try_parse<T>()`: help and `--version` arrive here as *errors* whose
     *       clapp::error::use_stderr() is false and whose clapp::error::exit_code() is 0.
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

    if (const std::optional<const std::string *> name = got->get_one<std::string>("name");
        name.has_value())
        std::println("Value for name: {}", **name);

    if (const std::optional<const std::filesystem::path *> config =
                got->get_one<std::filesystem::path>("config");
        config.has_value())
        std::println("Value for config: {}", (*config)->string());

    switch (got->get_count("debug")) {
        case 0:
            std::println("Debug mode is off");
            break;
        case 1:
            std::println("Debug mode is kind of on");
            break;
        case 2:
            std::println("Debug mode is on");
            break;
        default:
            std::println("Don't be crazy");
            break;
    }

    if (const std::optional<std::pair<std::string_view, const arg_matches &> > sub =
                got->subcommand();
        sub.has_value() && sub->first == "test") {
        if (sub->second.get_flag("list"))
            std::println("Printing testing lists...");
        else
            std::println("Not printing testing lists...");
    }
}
