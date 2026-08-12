#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::help_style;
    using clapp::value_range;

    /**
     * \brief The page `--help` prints, with clap's `use_long` collapse applied.
     *        See the fuller note in conformance_hidden_args_test.cpp.
     */
    std::string page(const command_spec& cmd, bool long_form) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd)})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures — clap's `get_app()`, shared by the two layout cases
    // ---------------------------------------------------------------------------

    /**
     * clap's `get_app()`: one option, one required positional, one counted flag and one
     * subcommand — four shapes, so a template that reorders sections has something to
     * reorder.
     */
    consteval command_builder tmpl_base() {
        command_builder app("MyApp");
        std::move(app)
                .version("1.0")
                .author("Kevin K. <kbknapp@gmail.com>")
                .about("Does awesome things")
                .arg(arg_builder("config")
                             .short_('c')
                             .long_("config")
                             .value_name("FILE")
                             .help("Sets a custom config file")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1)))
                .arg(arg_builder("output").index(1).help("Sets an optional output file").required())
                .arg(arg_builder("d")
                             .short_('d')
                             .action(arg_action::count)
                             .help("Turn debugging information on"))
                .subcommand(command_builder("test")
                                    .about("does testing things")
                                    .arg(arg_builder("list")
                                                 .short_('l')
                                                 .long_("list")
                                                 .help("lists test values")
                                                 .action(arg_action::set_true)));
        return app;
    }

    /** clap's `EXAMPLE1_TMPL_S` — the whole argument block behind one tag. */
    consteval command_spec make_simple() {
        command_builder app = tmpl_base();
        std::move(app).help_template("{bin} {version}\n{author}\n{about}\n\nUsage: {usage}\n\n"
                                     "{all-args}");
        return app.freeze();
    }
    constexpr command_spec simple_cmd = make_simple();

    /**
     * clap's `EXAMPLE1_TMPS_F` — the three sections named one at a time, with the author's
     * own headings and in an order the default screen would never produce.
     */
    consteval command_spec make_custom() {
        command_builder app = tmpl_base();
        std::move(app).help_template("{bin} {version}\n{author}\n{about}\n\nUsage: {usage}\n\n"
                                     "Options:\n{options}\nArguments:\n{positionals}\n"
                                     "Commands:\n{subcommands}");
        return app.freeze();
    }
    constexpr command_spec custom_cmd = make_custom();

    consteval command_builder plain_base() {
        command_builder app("MyApp");
        std::move(app)
                .version("1.0")
                .author("Kevin K. <kbknapp@gmail.com>")
                .about("Does awesome things");
        return app;
    }

    consteval command_spec make_notag() {
        command_builder app = plain_base();
        std::move(app).help_template("test no tag test");
        return app.freeze();
    }
    constexpr command_spec notag_cmd = make_notag();

    consteval command_spec make_unknown_tag() {
        command_builder app = plain_base();
        std::move(app).help_template("test {unknown_tag} test");
        return app.freeze();
    }
    constexpr command_spec unknown_tag_cmd = make_unknown_tag();

    consteval command_spec make_author_version() {
        command_builder app = plain_base();
        std::move(app).help_template("{author}\n{version}\n{about}\n{bin}");
        return app.freeze();
    }
    constexpr command_spec author_version_cmd = make_author_version();

    /** clap's `template_empty`. See the divergence note at the top of the file. */
    consteval command_spec make_empty_template() {
        command_builder app = plain_base();
        std::move(app).help_template("");
        return app.freeze();
    }
    constexpr command_spec empty_template_cmd = make_empty_template();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    // ---------------------------------------------------------------------------

    static_assert(simple_cmd.get_help_template().has_value());
    static_assert(notag_cmd.get_help_template() ==
                  std::optional<std::string_view>{"test no tag test"});

    // The divergence, pinned where it is caused rather than where it shows: an empty template
    // is stored as no template at all. This is the one line that would have to change if the
    // length sentinel ever grew a "present but empty" companion bool — the shape the
    // arg_spec::help_heading_present row of the trap-10 table describes.
    static_assert(!empty_template_cmd.get_help_template().has_value());

    // No long form anywhere, so `--help` collapses onto `-h` and the two layout cases below
    // are compact screens.
    static_assert(!clapp::long_help_exists(simple_cmd));

}  // namespace

CLAPP_TEST("template_help.rs::with_template") {
    // `{all-args}` — sections in clap's fixed order, separated by blank lines.
    CLAPP_CHECK(same(page(simple_cmd, true),
                     "MyApp 1.0\n"
                     "Kevin K. <kbknapp@gmail.com>\n"
                     "Does awesome things\n"
                     "\n"
                     "Usage: MyApp [OPTIONS] <output> [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  test  does testing things\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Arguments:\n"
                     "  <output>  Sets an optional output file\n"
                     "\n"
                     "Options:\n"
                     "  -c, --config <FILE>  Sets a custom config file\n"
                     "  -d...                Turn debugging information on\n"
                     "  -h, --help           Print help\n"
                     "  -V, --version        Print version\n"));
}

CLAPP_TEST("template_help.rs::custom_template") {
    // The same content, the author's own headings, the template's order, no inserted
    // blank lines — and each section measured on its own: `<output>` sits two columns in,
    // while the options block is as wide as `-c, --config <FILE>` needs.
    CLAPP_CHECK(same(page(custom_cmd, true),
                     "MyApp 1.0\n"
                     "Kevin K. <kbknapp@gmail.com>\n"
                     "Does awesome things\n"
                     "\n"
                     "Usage: MyApp [OPTIONS] <output> [COMMAND]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --config <FILE>  Sets a custom config file\n"
                     "  -d...                Turn debugging information on\n"
                     "  -h, --help           Print help\n"
                     "  -V, --version        Print version\n"
                     "Arguments:\n"
                     "  <output>  Sets an optional output file\n"
                     "Commands:\n"
                     "  test  does testing things\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"));
}

CLAPP_TEST("template_help.rs::template_notag") {
    CLAPP_CHECK(same(page(notag_cmd, true), "test no tag test\n"));
}

CLAPP_TEST("template_help.rs::template_unknowntag") {
    // The braces survive, which is the only signal a template author gets.
    CLAPP_CHECK(same(page(unknown_tag_cmd, true), "test {unknown_tag} test\n"));
}

CLAPP_TEST("template_help.rs::template_author_version") {
    CLAPP_CHECK(same(page(author_version_cmd, true),
                     "Kevin K. <kbknapp@gmail.com>\n1.0\nDoes awesome things\nMyApp\n"));
}

CLAPP_TEST("template_help.rs::template_empty (clapp divergence)") {
    // clap prints "\n" here. clapp cannot represent an empty template — see the
    // divergence note at the top of this file and item 4 on clapp::render_help()'s \note
    // list — so the automatic layout runs instead. Asserted as what clapp does, so that
    // the day the representation changes this fails loudly rather than drifting.
    CLAPP_CHECK(same(page(empty_template_cmd, true),
                     "Does awesome things\n"
                     "\n"
                     "Usage: MyApp\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}
