#include <clapp/builder/command.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <limits>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::help_style;

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
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

    consteval command_spec make_large_order() {
        command_builder app("test");
        std::move(app).subcommand(
                command_builder("sub").display_order(std::numeric_limits<std::size_t>::max()));
        return app.freeze();
    }
    constexpr command_spec large_order_cmd = make_large_order();

    // The order survives freeze() intact. Written against the same expression clap uses
    // rather than a literal, so a change to the width of the field is a compile error here
    // instead of a reordered help screen somewhere else.
    static_assert(large_order_cmd.find_subcommand("sub")->get_display_order() ==
                  std::numeric_limits<std::size_t>::max());

    // `sub` is visible, so — unlike hidden_args.rs's `hide_subcmds` — the section and the
    // `[COMMAND]` slot are both present. Asserting the populated side keeps this file honest
    // about which of the two states it is testing.
    static_assert(large_order_cmd.has_visible_subcommands());

}  // namespace

CLAPP_TEST("display_order.rs::very_large_display_order") {
    // `sub` sorts below `help` despite being declared first and sorting first
    // alphabetically, and its empty description is padded, not trimmed.
    CLAPP_CHECK(same(page(large_order_cmd, true),
                     "Usage: test [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "  sub   \n"
                     "\n"
                     "Options:\n"
                     "  -h, --help  Print help\n"));
}
