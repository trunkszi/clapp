#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
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
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    /** \brief The page `-h` or `--help` prints, at clapp::default_terminal_width. */
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
    // Fixture — clap's `cmd()`
    // ---------------------------------------------------------------------------

    consteval command_spec make_prog() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("a")
                             .short_('a')
                             .action(arg_action::set_true)
                             .required_unless_present_any({"b", "c"})
                             .conflicts_with_all({"b", "c"}))
                .arg(arg_builder("b")
                             .short_('b')
                             .action(arg_action::set_true)
                             .required_unless_present("a")
                             .requires_("c"))
                .arg(arg_builder("c")
                             .short_('c')
                             .action(arg_action::set_true)
                             .required_unless_present("a")
                             .requires_("b"));
        return app.freeze();
    }
    constexpr command_spec prog = make_prog();

    static_assert(prog.find_arg("a")->get_required_unless_present_any().size() == 2);
    static_assert(prog.find_arg("b")->get_requires().size() == 1);
    static_assert(prog.find_arg("c")->get_requires().size() == 1);

}  // namespace

// ---------------------------------------------------------------------------
// The command lines that satisfy the graph
// ---------------------------------------------------------------------------

CLAPP_TEST("double_require.rs::valid_cases") {
    CLAPP_CHECK(clapp::parse(prog, raw_args{"", "-a"}).has_value());
    CLAPP_CHECK(clapp::parse(prog, raw_args{"", "-b", "-c"}).has_value());
    CLAPP_CHECK(clapp::parse(prog, raw_args{"", "-c", "-b"}).has_value());
}

// ---------------------------------------------------------------------------
// The help screen
// ---------------------------------------------------------------------------

CLAPP_TEST("double_require.rs::help_text") {
    const outcome got = clapp::parse(prog, raw_args{"prog", "--help"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(page(prog, true),
                     "Usage: prog [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -a          \n"
                     "  -b          \n"
                     "  -c          \n"
                     "  -h, --help  Print help\n"));
}

// ---------------------------------------------------------------------------
// The requirement reached twice
// ---------------------------------------------------------------------------

CLAPP_TEST("double_require.rs::no_duplicate_error") {
    const outcome only_b = clapp::parse(prog, raw_args{"", "-b"});
    CLAPP_CHECK(!only_b.has_value());
    CLAPP_CHECK(kind_of(only_b) == error_kind::missing_required_argument);
    CLAPP_CHECK(message_of(only_b) == "error: the following required arguments were not provided:\n"
                                      "  -c\n"
                                      "\n"
                                      "Usage: prog -b -c\n"
                                      "\n"
                                      "For more information, try '--help'.\n");

    const outcome only_c = clapp::parse(prog, raw_args{"", "-c"});
    CLAPP_CHECK(!only_c.has_value());
    CLAPP_CHECK(kind_of(only_c) == error_kind::missing_required_argument);
    CLAPP_CHECK(message_of(only_c) == "error: the following required arguments were not provided:\n"
                                      "  -b\n"
                                      "\n"
                                      "Usage: prog -c -b\n"
                                      "\n"
                                      "For more information, try '--help'.\n");
}
