#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
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
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixture — clap declares this same command four times, once per case.
    // ---------------------------------------------------------------------------

    /**
     * clap:
     *     Command::new("myapp")
     *         .arg(Arg::new("someglobal").short('g').long("some-global").global(true))
     *         .subcommand(Command::new("subcmd").subcommand(Command::new("multi").version("1.0")))
     *
     * clap's default action for an `Arg` with no `.action()` is `ArgAction::Set`, which is
     * why the screens show `<someglobal>` rather than a bare flag; clapp spells that
     * explicitly.
     */
    consteval command_spec make_myapp() {
        command_builder app("myapp");
        std::move(app)
                .arg(arg_builder("someglobal")
                             .short_('g')
                             .long_("some-global")
                             .action(arg_action::set)
                             .global())
                .subcommand(command_builder("subcmd").subcommand(
                        command_builder("multi").version("1.0")));
        return app.freeze();
    }
    constexpr command_spec myapp = make_myapp();

}  // namespace

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::global_args_should_show_on_toplevel_help_message") {
    const outcome got = clapp::parse(myapp, raw_args{"myapp", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Usage: myapp [OPTIONS] [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  subcmd  \n"
                     "  help    Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -g, --some-global <someglobal>  \n"
                     "  -h, --help                      Print help\n"));
}

CLAPP_TEST("help.rs::global_args_should_not_show_on_help_message_for_help_help") {
    // The negative of the family: the generated `help` command does NOT inherit the
    // global, so its page has no `Options:` section at all — not even an empty one.
    const outcome got = clapp::parse(myapp, raw_args{"myapp", "help", "help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Usage: myapp help [COMMAND]...\n"
                     "\n"
                     "Arguments:\n"
                     "  [COMMAND]...  Print help for the subcommand(s)\n"));
}

CLAPP_TEST("help.rs::global_args_should_show_on_help_message_for_subcommand") {
    const outcome got = clapp::parse(myapp, raw_args{"myapp", "help", "subcmd"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Usage: myapp subcmd [OPTIONS] [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  multi  \n"
                     "  help   Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -g, --some-global <someglobal>  \n"
                     "  -h, --help                      Print help\n"));
}

CLAPP_TEST("help.rs::global_args_should_show_on_help_message_for_nested_subcommand") {
    // Two levels down, and `multi` carries its own version — so `-V` appears here and
    // nowhere else in the family. A one-level propagation passes the previous case and
    // fails this one.
    const outcome got = clapp::parse(myapp, raw_args{"myapp", "help", "subcmd", "multi"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(same(message_of(got),
                     "Usage: myapp subcmd multi [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -g, --some-global <someglobal>  \n"
                     "  -h, --help                      Print help\n"
                     "  -V, --version                   Print version\n"));
}
