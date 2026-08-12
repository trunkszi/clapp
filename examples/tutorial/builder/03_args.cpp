#include <clapp/clapp.hpp>

#include <cstdio>
#include <expected>
#include <optional>
#include <print>
#include <string>
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
     * \return The frozen tree.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder app("03_args");
        std::move(app)
                .version("1.0.0")
                .about("Positionals, options, flags and counters")
                .arg(arg_builder("name")
                    .index(1)
                    .value_name("NAME")
                    .help("Who to greet")
                    .required()
                    .value_parser<std::string>())
                .arg(arg_builder("port")
                    .index(2)
                    .value_name("PORT")
                    .help("Port to connect on")
                    .default_value("2020")
                    .value_parser<unsigned>())
                .arg(arg_builder("tags")
                    .short_('t')
                    .long_("tag")
                    .value_name("TAG")
                    .help("Tag to attach; repeat for more")
                    // `at_least(1)`, not the action's default of exactly one, because
                    // that is what the derive layer's `std::vector<T>` row deduces —
                    // one occurrence may carry several values. It is also what puts
                    // the `...` after `<TAG>` in help, so dropping it makes the two
                    // halves of this pair print different screens.
                    .action(arg_action::append)
                    .num_args(clapp::value_range::at_least(1))
                    .value_parser<std::string>())
                .arg(arg_builder("verbose")
                    .short_('v')
                    .long_("verbose")
                    .help("Increase logging verbosity")
                    .action(arg_action::count))
                .arg(arg_builder("quiet")
                    .short_('q')
                    .long_("quiet")
                    .help("Silence all output")
                    .action(arg_action::set_true));
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /**
     * \brief Render the values of \p id as a bracketed, quoted list.
     *
     * \param matches What clapp::parse() produced.
     * \param id The appending argument to read.
     * \return `[]`, `["red"]`, `["red", "blue"]`.
     *
     * \note The optional is bound to a named variable before it is iterated. Iterating the
     *       temporary — `for (const std::string& v : *matches.get_many<std::string>(id))` —
     *       is rejected by clang-p2996 as `-Wdangling-gsl`, and the class \warning on
     *       clapp::arg_matches explains why it is right to reject it.
     */
    [[nodiscard]] std::string quoted_list(const arg_matches &matches, std::string_view id) {
        std::string out{"["};
        const std::optional<clapp::values_ref<std::string> > values =
                matches.get_many<std::string>(id);
        if (values.has_value()) {
            bool first = true;
            for (const std::string &value: *values) {
                if (!first) out += ", ";
                first = false;
                out += '"';
                out += value;
                out += '"';
            }
        }
        out += ']';
        return out;
    }

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

    std::println("name: \"{}\"", **got->get_one<std::string>("name"));
    std::println("port: {}", **got->get_one<unsigned>("port"));
    std::println("tags: {}", quoted_list(*got, "tags"));
    std::println("verbose: {}", static_cast<unsigned>(got->get_count("verbose")));
    std::println("quiet: {}", got->get_flag("quiet"));
}
