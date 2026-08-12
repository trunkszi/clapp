#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
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

    using outcome = std::expected<arg_matches, error>;

    // ---------------------------------------------------------------------------
    // borrowed.rs: one arg_builder, three uses
    // ---------------------------------------------------------------------------

    consteval command_spec make_shared_args() {
        // Named objects, then passed by value three times. clap writes `.arg(&arg)`; the
        // C++ equivalent of "the definition is reusable" is that the copies do not interfere.
        const arg_builder some = arg_builder("some").short_('s').long_("some").help("other help");
        const arg_builder some2 =
                arg_builder("some2").short_('S').long_("some-thing").help("other help");

        command_builder app("sub_command_negate");
        std::move(app)
                .arg(arg_builder("test").index(1))
                .arg(some)
                .arg(some2)
                .subcommand(command_builder("sub1").arg(some));
        return app.freeze();
    }

    constexpr command_spec shared_args = make_shared_args();

    // Both uses survived, in both commands, with their spellings and their help text intact.
    // If `.arg(some)` had consumed the builder, the second use would carry an empty id and
    // freeze() would have refused the whole definition — so reaching these assertions at all
    // is half the claim, and the other half is that the CHILD got a real copy rather than a
    // hollow one.
    static_assert(shared_args.has_arg("some"));
    static_assert(shared_args.has_arg("some2"));
    static_assert(shared_args.has_arg("test"));
    static_assert(shared_args.has_subcommand("sub1"));
    static_assert(shared_args.find_subcommand("sub1")->has_arg("some"));
    static_assert(shared_args.find_arg("some")->get_short() == std::optional<char>{'s'});
    static_assert(shared_args.find_subcommand("sub1")->find_arg("some")->get_short() ==
                  std::optional<char>{'s'});
    static_assert(shared_args.find_arg("some2")->get_long().value() == "some-thing");

    // ---------------------------------------------------------------------------
    // The escape hatches
    // ---------------------------------------------------------------------------

    /**
     * help.rs's three `#[should_panic]` cases, legalised the way clapp's own diagnostic
     * says to: "(call disable_help_flag() to drop the injected '--help')".
     */
    consteval command_spec make_own_help() {
        command_builder app("conflict");
        std::move(app).disable_help_flag().arg(
            arg_builder("help").short_('h').long_("help").action(arg_action::set_true));
        return app.freeze();
    }

    constexpr command_spec own_help = make_own_help();

    /**
     * version.rs's two, likewise: "(call disable_version_flag() to drop the injected
     * '--version')". Note version() is still set, so the *text* exists — it is only the
     * injected flag that steps aside.
     */
    consteval command_spec make_own_version() {
        command_builder app("foo");
        std::move(app).version("3.0").disable_version_flag().arg(
            arg_builder("ver").short_('V').long_("version").action(arg_action::set_true));
        return app.freeze();
    }

    constexpr command_spec own_version = make_own_version();

    /**
     * positionals.rs::missing_required, legalised with the call clapp's
     * check_required_positional_order() names: "or set allow_missing_positional()".
     */
    consteval command_spec make_missing_positional() {
        command_builder app("test");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("FILE1").index(1))
                .arg(arg_builder("FILE2").index(2).required());
        return app.freeze();
    }

    constexpr command_spec missing_positional = make_missing_positional();

    static_assert(own_help.is_disable_help_flag_set());
    static_assert(own_version.is_disable_version_flag_set());
    static_assert(missing_positional.is_allow_missing_positional_set());
} // namespace

// ===========================================================================
// borrowed.rs
// ===========================================================================

CLAPP_TEST("borrowed.rs::borrowed_args") {
    // clap asserts only that the parse succeeds with no arguments at all. The point is
    // the DEFINITION, not the parse.
    const outcome got = clapp::parse(shared_args, raw_args{"prog"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("borrowed.rs: the copies are independent at parse time") {
    // The parent's `some` and the child's `some` are separate arguments in separate
    // tables. Setting one must not set the other — which is the thing a shallow copy
    // that shared state would get wrong, and which the definition alone cannot show.
    // `-s` inherits the default arg_action::set, so it takes a value — same as clap's
    // `Arg::new("some").short('s')`.
    const outcome parent = clapp::parse(shared_args, raw_args{"prog", "-s", "v"});
    CLAPP_CHECK(parent.has_value());
    CLAPP_CHECK(parent->contains_id("some"));
    CLAPP_CHECK(!parent->has_subcommand());

    const outcome child = clapp::parse(shared_args, raw_args{"prog", "x", "sub1", "-s", "v"});
    CLAPP_CHECK(child.has_value());
    const arg_matches *sub = child->subcommand_matches("sub1");
    CLAPP_CHECK(sub != nullptr);
    CLAPP_CHECK(sub->contains_id("some"));
    // The parent level saw the positional, not the flag.
    CLAPP_CHECK(child->contains_id("test"));
    CLAPP_CHECK(!child->contains_id("some"));
}

// ===========================================================================
// The escape hatches the diagnostics recommend
// ===========================================================================

CLAPP_TEST("help.rs::arg_*_conflict_with_help — disable_help_flag() really frees -h/--help") {
    // Without disable_help_flag() this definition does not compile; that half is
    // tests/units/builder/compile_fail/help_flag_{id,short,long}_conflict_test.cpp. Here
    // the hatch is open, so `-h` and `--help` must reach the USER's argument and must not
    // print a help screen.
    const outcome shortform = clapp::parse(own_help, raw_args{"conflict", "-h"});
    CLAPP_CHECK(shortform.has_value());
    const std::optional<const bool *> shortval = shortform->get_one<bool>("help");
    CLAPP_CHECK(shortval.has_value());
    CLAPP_CHECK(**shortval);

    const outcome longform = clapp::parse(own_help, raw_args{"conflict", "--help"});
    CLAPP_CHECK(longform.has_value());
    const std::optional<const bool *> longval = longform->get_one<bool>("help");
    CLAPP_CHECK(longval.has_value());
    CLAPP_CHECK(**longval);
}

CLAPP_TEST("version.rs::override_version_* — disable_version_flag() really frees -V/--version") {
    const outcome shortform = clapp::parse(own_version, raw_args{"foo", "-V"});
    CLAPP_CHECK(shortform.has_value());
    const std::optional<const bool *> shortval = shortform->get_one<bool>("ver");
    CLAPP_CHECK(shortval.has_value());
    CLAPP_CHECK(**shortval);

    const outcome longform = clapp::parse(own_version, raw_args{"foo", "--version"});
    CLAPP_CHECK(longform.has_value());
    const std::optional<const bool *> longval = longform->get_one<bool>("ver");
    CLAPP_CHECK(longval.has_value());
    CLAPP_CHECK(**longval);
}

CLAPP_TEST("positionals.rs::missing_required — allow_missing_positional() really allows it") {
    // `prog [FILE1] <FILE2>`: with one value the parser must fill the REQUIRED slot and
    // leave the optional one empty, which is the whole reason the setting exists. Without
    // it the definition does not compile
    // (compile_fail/optional_before_required_positional_test.cpp).
    const outcome one = clapp::parse(missing_positional, raw_args{"test", "only"});
    CLAPP_CHECK(one.has_value());
    CLAPP_CHECK(one->contains_id("FILE2"));
    CLAPP_CHECK(!one->contains_id("FILE1"));

    const outcome both = clapp::parse(missing_positional, raw_args{"test", "first", "second"});
    CLAPP_CHECK(both.has_value());
    const std::optional<const std::string *> first = both->get_one<std::string>("FILE1");
    const std::optional<const std::string *> second = both->get_one<std::string>("FILE2");
    CLAPP_CHECK(first.has_value());
    CLAPP_CHECK(second.has_value());
    CLAPP_CHECK(**first == "first");
    CLAPP_CHECK(**second == "second");

    // And the required one is still required.
    const outcome none = clapp::parse(missing_positional, raw_args{"test"});
    CLAPP_CHECK(!none.has_value());
    CLAPP_CHECK(none.error().kind() == clapp::error_kind::missing_required_argument);
}
