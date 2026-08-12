#include <clapp/clapp.hpp>

#include <cstdio>
#include <expected>
#include <optional>
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
    using clapp::value_range;

    /**
     * \brief Build pacman's command tree.
     *
     * \return The tree, ready for clapp::command_builder::freeze().
     *
     * \note `consteval`, so every string below is interned into `.rodata` and the whole tree
     *       costs nothing at run time. That is ADR-0005, and it is the same guarantee the
     *       derive layer gives — only reached through the builder instead of through
     *       reflection.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder query("query");
        std::move(query)
                .short_flag('Q')
                .long_flag("query")
                .about("Query the package database.")
                .arg(arg_builder("search")
                    .short_('s')
                    .long_("search")
                    .help("search locally installed packages for matching strings")
                    .conflicts_with("info")
                    .action(arg_action::set)
                    .num_args(value_range::at_least(1)))
                .arg(arg_builder("info")
                    .short_('i')
                    .long_("info")
                    .help("view package information")
                    .conflicts_with("search")
                    .action(arg_action::set)
                    .num_args(value_range::at_least(1)));

        command_builder sync("sync");
        std::move(sync)
                .short_flag('S')
                .long_flag("sync")
                .about("Synchronize packages.")
                .arg(arg_builder("search")
                    .short_('s')
                    .long_("search")
                    .help("search remote repositories for matching strings")
                    .conflicts_with("info")
                    .action(arg_action::set)
                    .num_args(value_range::at_least(1)))
                .arg(arg_builder("info")
                    .short_('i')
                    .long_("info")
                    .help("view package information")
                    .conflicts_with("search")
                    .action(arg_action::set_true))
                .arg(arg_builder("package")
                    .index(1)
                    .help("packages")
                    .required_unless_present("search")
                    .action(arg_action::set)
                    .num_args(value_range::at_least(1)));

        command_builder app("pacman");
        std::move(app)
                .about("package manager utility")
                .version("5.2.1")
                .subcommand_required()
                .arg_required_else_help()
                .subcommand(std::move(query))
                .subcommand(std::move(sync));
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /**
     * \brief Join the values of \p id with `", "`.
     * \param matches The subcommand's matches.
     * \param id The argument to read.
     * \return The joined values, empty when the argument collected none.
     */
    [[nodiscard]] std::string joined(const arg_matches &matches, std::string_view id) {
        // Bound to a named variable on purpose: `for (auto v : *m.get_many<T>(id))` is
        // rejected by clang-p2996 as `-Wdangling-gsl`, and the class \warning on
        // clapp::arg_matches says so.
        const std::optional<clapp::values_ref<std::string> > values =
                matches.get_many<std::string>(id);
        if (!values.has_value()) return {};

        std::string out;
        for (const std::string &value: *values) {
            if (!out.empty()) out += ", ";
            out += value;
        }
        return out;
    }
} // namespace

int main(int argc, char **argv) {
    const std::expected<arg_matches, error> got = clapp::parse(spec, raw_args(argc, argv));
    if (!got.has_value()) {
        const error &err = got.error();
        const std::string text = err.render().to_string();
        std::FILE *const out = err.use_stderr() ? stderr : stdout;
        static_cast<void>(std::fwrite(text.data(), 1, text.size(), out));
        if (!text.empty() && text.back() != '\n') static_cast<void>(std::fputc('\n', out));
        return err.exit_code();
    }

    const std::optional<std::pair<std::string_view, const arg_matches &> > sub = got->subcommand();
    if (!sub.has_value()) return 0; // subcommand_required() already rejected this.

    if (sub->first == "sync") {
        if (sub->second.contains_id("search")) {
            std::printf("Searching for %s...\n", joined(sub->second, "search").c_str());
            return 0;
        }
        const std::string packages = joined(sub->second, "package");
        if (sub->second.get_flag("info"))
            std::printf("Retrieving info for %s...\n", packages.c_str());
        else
            std::printf("Installing %s...\n", packages.c_str());
        return 0;
    }

    if (sub->first == "query") {
        if (sub->second.contains_id("info"))
            std::printf("Retrieving info for %s...\n", joined(sub->second, "info").c_str());
        else if (sub->second.contains_id("search"))
            std::printf("Searching Locally for %s...\n", joined(sub->second, "search").c_str());
        else
            std::printf("Displaying all locally installed packages...\n");
        return 0;
    }

    return 0;
}
