#include <clapp/clapp.hpp>

#include <cstdio>
#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::raw_args;

    /**
     * \brief One verb, with its optional positional.
     * \param verb_name
     * \param about Its one-line description.
     * \param value_help
     * \return The subcommand, ready to hand to clapp::command_builder::subcommand.
     */
    [[nodiscard]] consteval command_builder
    verb(std::string_view verb_name, std::string_view about, std::string_view value_help) {
        command_builder sub{verb_name};
        std::move(sub).about(about).arg(arg_builder("name")
            .index(1)
            .value_name("NAME")
            .help(value_help)
            .value_parser<std::string>());
        return sub;
    }

    /**
     * \brief Build the command tree.
     * \return The frozen tree.
     *
     * \note `verb()` above is why a builder tree can be *computed*: two subcommands that
     *       differ only in three strings are one function, and a hundred would be a loop.
     *       The derive layer has no equivalent — every alternative of the `std::variant` is a
     *       distinct type that has to be written out.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder app("04_subcommands");
        std::move(app)
                .version("1.0.0")
                .about("A program with more than one verb")
                .propagate_version()
                .subcommand_required()
                .arg_required_else_help()
                .subcommand(verb("add", "Adds files to myapp", "File to add"))
                .subcommand(verb("remove", "Removes files from myapp", "File to remove"));
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /**
     * \brief Print the line clap's tutorial prints, for whichever verb ran.
     * \param verb_name The subcommand's name as the user spelled it.
     * \param matches That subcommand's own matches.
     */
    void report(std::string_view verb_name, const arg_matches &matches) {
        const std::optional<const std::string *> name = matches.get_one<std::string>("name");
        if (name.has_value())
            std::println("'{}' was used, name is: Some(\"{}\")", verb_name, **name);
        else
            std::println("'{}' was used, name is: None", verb_name);
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

    const std::optional<std::pair<std::string_view, const arg_matches &> > sub = got->subcommand();
    // `subcommand_required()` already rejected the empty command line, so this cannot be
    // empty — but saying so out loud is cheaper than discovering otherwise.
    if (!sub.has_value()) {
        std::println(stderr, "internal error: subcommand_required() let an empty line through");
        return 70;
    }

    if (sub->first == "add" || sub->first == "remove") {
        report(sub->first, sub->second);
        return 0;
    }

    // See the file header: nothing checks this chain for completeness, so it must fail
    // loudly rather than silently succeed.
    std::println(stderr, "internal error: no arm for subcommand '{}'", sub->first);
    return 70;
}
