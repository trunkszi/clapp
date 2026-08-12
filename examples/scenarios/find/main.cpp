#include <clapp/clapp.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::group_builder;
    using clapp::raw_args;
    using clapp::value_range;
    using clapp::value_source;

    /**
     * \brief Turn a flag into a value-less option, so each occurrence has an argv index.
     *
     * \param flag The argument to convert.
     * \return The same argument, storing `true` per occurrence and `false` by default.
     *
     * \note clap's `position_sensitive_flag`, verbatim. Without it `indices_of()` reports
     *       nothing for a flag, because `ArgAction::SetTrue` collapses every occurrence into
     *       one stored value.
     */
    [[nodiscard]] consteval arg_builder position_sensitive_flag(arg_builder flag) {
        return std::move(flag)
                .num_args(value_range::empty())
                .value_parser<bool>()
                .default_missing_value("true")
                .default_value("false");
    }

    /**
     * \brief Build find's command tree.
     * \return The frozen tree.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder app("find");
        std::move(app)
                .about("Walk a directory tree, testing each entry")
                .version("1.0.0")
                .group(group_builder("tests").multiple())
                .next_help_heading("TESTS")
                .args({
                    position_sensitive_flag(arg_builder("empty"))
                    .long_("empty")
                    .action(arg_action::append)
                    .help("File is empty and is either a regular file or a directory")
                    .group("tests"),
                    arg_builder("name")
                    .long_("name")
                    .action(arg_action::append)
                    .help("Base of file name matches shell pattern pattern")
                    .group("tests")
                })
                .group(group_builder("operators").multiple())
                .next_help_heading("OPERATORS")
                .args({
                    position_sensitive_flag(arg_builder("or"))
                    .short_('o')
                    .long_("or")
                    .action(arg_action::append)
                    .help("expr2 is not evaluated if expr1 is true")
                    .group("operators"),
                    position_sensitive_flag(arg_builder("and"))
                    .short_('a')
                    .long_("and")
                    .action(arg_action::append)
                    .help("Same as `expr1 expr1`")
                    .group("operators")
                });
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /** \brief One value, with the argv position it was written at. */
    struct located {
        std::size_t index = 0;
        std::string id;
        std::string text;
    };

    /**
     * \brief Collect every command-line value of \p id, paired with its argv index.
     *
     * \tparam T The type the argument's clapp::value_parser produces.
     * \param matches The parse result.
     * \param id The argument to read.
     * \param into Receives one #located per value.
     * \param render How to turn a `T` into text.
     *
     * \note Values whose clapp::value_source is not `command_line` are skipped: a
     *       `default_value` of `"false"` is recorded for every flag whether or not the user
     *       wrote it, and reporting those would mean printing the whole argument list on
     *       every run.
     */
    template<class T, class F>
    void
    collect(const arg_matches &matches, std::string_view id, std::vector<located> &into, F render) {
        if (matches.value_source(id) != std::optional{value_source::command_line}) return;

        const std::optional<clapp::values_ref<T> > values = matches.get_many<T>(id);
        if (!values.has_value()) return;
        const std::optional<std::span<const std::size_t> > indices = matches.indices_of(id);
        if (!indices.has_value()) return;

        std::size_t slot = 0;
        for (const T &value: *values) {
            if (slot >= indices->size()) break;
            into.push_back(
                {.index = (*indices)[slot], .id = std::string{id}, .text = render(value)});
            ++slot;
        }
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

    std::vector<located> values;
    for (const clapp::arg_id &id: got->ids()) {
        const std::string_view name = id.name();
        // `ids()` reports groups as well as arguments; a group's stored values are its
        // member ids, which are not user data. Asking the *spec* is the cheapest way to
        // tell the two apart, and unlike clap's `try_get_many::<Id>` it needs no
        // speculative downcast.
        if (!spec.has_arg(name)) continue;

        if (name == "name")
            collect<std::string>(*got, name, values, [](const std::string &s) { return s; });
        else
            collect<bool>(
                *got, name, values, [](bool b) { return std::string{b ? "true" : "false"}; });
    }

    std::ranges::sort(values, {}, &located::index);
    for (const located &one: values)
        std::printf("%zu %s = %s\n", one.index, one.id.c_str(), one.text.c_str());
}
