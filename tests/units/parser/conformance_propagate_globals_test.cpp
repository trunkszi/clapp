#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <initializer_list>
#include <optional>
#include <ranges>
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
    using clapp::count_type;
    using clapp::error;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    // ---------------------------------------------------------------------------
    // Fixture — clap's `get_app()`
    // ---------------------------------------------------------------------------

    consteval command_spec make_myprog() {
        command_builder app("myprog");
        std::move(app)
                .arg(arg_builder("GLOBAL_ARG")
                             .long_("global-arg")
                             .help("Specifies something needed by the subcommands")
                             .global()
                             .action(arg_action::set)
                             .default_value("default_value"))
                .arg(arg_builder("GLOBAL_FLAG")
                             .long_("global-flag")
                             .help("Specifies something needed by the subcommands")
                             .global()
                             .action(arg_action::count))
                .subcommand(command_builder("outer").subcommand(command_builder("inner")));
        return app.freeze();
    }
    constexpr command_spec myprog = make_myprog();

    static_assert(myprog.find_arg("GLOBAL_ARG")->is_global_set());
    static_assert(myprog.find_arg("GLOBAL_FLAG")->is_global_set());
    static_assert(myprog.find_subcommand("outer")->has_subcommand("inner"));

    // ---------------------------------------------------------------------------
    // clap's `get_outer_matches` / `get_inner_matches` and the three predicates
    // ---------------------------------------------------------------------------

    const arg_matches* outer_of(const arg_matches& m) { return m.subcommand_matches("outer"); }

    const arg_matches* inner_of(const arg_matches& m) {
        const arg_matches* outer = outer_of(m);
        return outer == nullptr ? nullptr : outer->subcommand_matches("inner");
    }

    /**
     * \brief `m.get_one::<String>("GLOBAL_ARG") == want`, with a null level reported as a
     *        mismatch rather than a crash.
     */
    bool arg_is(const arg_matches* m, std::string_view want) {
        if (m == nullptr) return false;
        const std::optional<const std::string*> found = m->get_one<std::string>("GLOBAL_ARG");
        return found.has_value() && **found == want;
    }

    /** \brief clap's `*_can_access_flag`: presence AND occurrence count, together. */
    bool flag_is(const arg_matches* m, bool present, count_type occurrences) {
        if (m == nullptr) return false;
        return m->contains_id("GLOBAL_FLAG") == present &&
               m->get_count("GLOBAL_FLAG") == occurrences;
    }

    /** \brief The same claim at all three levels, which is what every case here asserts. */
    bool arg_everywhere(const arg_matches& m, std::string_view want) {
        return arg_is(&m, want) && arg_is(outer_of(m), want) && arg_is(inner_of(m), want);
    }

    bool flag_everywhere(const arg_matches& m, bool present, count_type occurrences) {
        return flag_is(&m, present, occurrences) && flag_is(outer_of(m), present, occurrences) &&
               flag_is(inner_of(m), present, occurrences);
    }

    outcome run(std::initializer_list<std::string_view> argv) {
        std::vector<std::string> owned;
        for (const std::string_view one : argv) owned.emplace_back(one);
        return clapp::parse(myprog, raw_args(std::from_range, owned));
    }

}  // namespace

// ---------------------------------------------------------------------------
// A value set at one level, read at all three
// ---------------------------------------------------------------------------

CLAPP_TEST("propagate_globals.rs::global_arg_used_top_level") {
    const outcome got = run({"myprog", "--global-arg=some_value", "outer", "inner"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(arg_everywhere(*got, "some_value"));
}

CLAPP_TEST("propagate_globals.rs::global_arg_used_outer") {
    const outcome got = run({"myprog", "outer", "--global-arg=some_value", "inner"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(arg_everywhere(*got, "some_value"));
}

CLAPP_TEST("propagate_globals.rs::global_arg_used_inner") {
    const outcome got = run({"myprog", "outer", "inner", "--global-arg=some_value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(arg_everywhere(*got, "some_value"));
}

CLAPP_TEST("propagate_globals.rs::global_arg_default_value") {
    // Nothing on the command line: the default is applied once and propagates like any
    // other value.
    const outcome got = run({"myprog", "outer", "inner"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(arg_everywhere(*got, "default_value"));
}

// ---------------------------------------------------------------------------
// The same grid for a counting flag
// ---------------------------------------------------------------------------

CLAPP_TEST("propagate_globals.rs::global_flag_used_top_level") {
    const outcome got = run({"myprog", "--global-flag", "outer", "inner"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(flag_everywhere(*got, true, 1));
}

CLAPP_TEST("propagate_globals.rs::global_flag_used_outer") {
    const outcome got = run({"myprog", "outer", "--global-flag", "inner"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(flag_everywhere(*got, true, 1));
}

CLAPP_TEST("propagate_globals.rs::global_flag_used_inner") {
    const outcome got = run({"myprog", "outer", "inner", "--global-flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(flag_everywhere(*got, true, 1));
}

CLAPP_TEST("propagate_globals.rs::global_flag_2x_used_top_level") {
    const outcome got = run({"myprog", "--global-flag", "--global-flag", "outer", "inner"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(flag_everywhere(*got, true, 2));
}

CLAPP_TEST("propagate_globals.rs::global_flag_2x_used_inner") {
    const outcome got = run({"myprog", "outer", "inner", "--global-flag", "--global-flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(flag_everywhere(*got, true, 2));
}
