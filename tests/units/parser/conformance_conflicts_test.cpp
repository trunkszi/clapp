#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::group_builder;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    bool says(const outcome& got, std::string_view fragment) {
        return message_of(got).find(fragment) != std::string::npos;
    }

    /**
     * \brief Whole-block comparison against clap's expected string, printing both on a
     *        mismatch. The `*_conflicts_with_subcommand*` family needs it: those six cases
     *        are the only witnesses to the `context_kind::invalid_subcommand` arm of
     *        `write_argument_conflict()`, and if that arm is deleted the message degrades to
     *        the generic "an argument cannot be used with one or more of the other specified
     *        arguments" — which a kind check and a substring check both accept.
     */
    bool same_block(const outcome& got, std::string_view want) {
        const std::string text = message_of(got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_flag_conflict() {
        command_builder app("flag_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .conflicts_with("other"))
                .arg(arg_builder("other").short_('o').long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_conflict = make_flag_conflict();

    consteval command_spec make_flag_conflict_all() {
        command_builder app("flag_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"other"}))
                .arg(arg_builder("other").short_('o').long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_conflict_all = make_flag_conflict_all();

    consteval command_spec make_exclusive_flag() {
        command_builder app("flag_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .exclusive())
                .arg(arg_builder("other").short_('o').long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec exclusive_flag = make_exclusive_flag();

    consteval command_spec make_exclusive_option() {
        command_builder app("flag_conflict");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").value_name("VALUE").exclusive())
                .arg(arg_builder("other").short_('o').long_("other").value_name("VALUE"));
        return app.freeze();
    }
    constexpr command_spec exclusive_option = make_exclusive_option();

    consteval command_spec make_exclusive_with_default() {
        command_builder app("flag_conflict");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").value_name("VALUE").exclusive())
                .arg(arg_builder("other")
                             .short_('o')
                             .long_("other")
                             .value_name("VALUE")
                             .required(false)
                             .default_value("val1"));
        return app.freeze();
    }
    constexpr command_spec exclusive_with_default = make_exclusive_with_default();

    consteval command_spec make_exclusive_in_group() {
        command_builder app("test");
        std::move(app)
                .group(group_builder("test").arg("foo"))
                .arg(arg_builder("foo").long_("foo").exclusive().action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec exclusive_in_group = make_exclusive_in_group();

    consteval command_spec make_both_defaulted_exclusive() {
        command_builder app("flag_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .value_name("VALUE")
                             .exclusive()
                             .default_value("val2"))
                .arg(arg_builder("other")
                             .short_('o')
                             .long_("other")
                             .value_name("VALUE")
                             .default_value("val1"));
        return app.freeze();
    }
    constexpr command_spec both_defaulted_exclusive = make_both_defaulted_exclusive();

    // The four group-conflict shapes, which must all behave the same way.
    consteval command_spec make_arg_conflicts_group() {
        command_builder app("group_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .conflicts_with("gr"))
                .group(group_builder("gr").arg("some").arg("other"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec arg_conflicts_group = make_arg_conflicts_group();

    consteval command_spec make_group_conflicts_arg() {
        command_builder app("group_conflict");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .group(group_builder("gr").arg("some").arg("other").conflicts_with("flag"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec group_conflicts_arg = make_group_conflicts_arg();

    consteval command_spec make_arg_conflicts_required_group() {
        command_builder app("group_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .conflicts_with("gr"))
                .group(group_builder("gr").required().arg("some").arg("other"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec arg_conflicts_required_group = make_arg_conflicts_required_group();

    consteval command_spec make_required_group_conflicts_arg() {
        command_builder app("group_conflict");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .group(group_builder("gr").required().arg("some").arg("other").conflicts_with(
                        "flag"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec required_group_conflicts_arg = make_required_group_conflicts_arg();

    consteval command_spec make_group_with_required_member() {
        command_builder app("group_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .conflicts_with("gr"))
                .group(group_builder("gr").arg("some").arg("other"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true).required())
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec group_with_required_member = make_group_with_required_member();

    consteval command_spec make_multi_source_group() {
        command_builder app("group_conflict");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .conflicts_with("gr"))
                .group(group_builder("gr").multiple())
                .arg(arg_builder("some").long_("some").value_name("name").group("gr"))
                .arg(arg_builder("other")
                             .long_("other")
                             .value_name("secs")
                             .default_value("1000")
                             .group("gr"));
        return app.freeze();
    }
    constexpr command_spec multi_source_group = make_multi_source_group();

    // The five "a default is never a conflict" shapes.
    consteval command_spec make_unused_default() {
        command_builder app("conflict");
        std::move(app)
                .arg(arg_builder("opt").short_('o').long_("opt").default_value("default"))
                .arg(arg_builder("flag").short_('f').long_("flag").conflicts_with("opt").action(
                        arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec unused_default = make_unused_default();

    consteval command_spec make_default_side_declares() {
        command_builder app("conflict");
        std::move(app)
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .default_value("default")
                             .conflicts_with("flag"))
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec default_side_declares = make_default_side_declares();

    consteval command_spec make_group_in_conflicts_with() {
        command_builder app("conflict");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default").group("one"))
                .arg(arg_builder("flag").long_("flag").conflicts_with("one").action(
                        arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec group_in_conflicts_with = make_group_in_conflicts_with();

    consteval command_spec make_group_counts_default() {
        command_builder app("conflict");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default").group("one"))
                .arg(arg_builder("flag").long_("flag").group("one").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec group_counts_default = make_group_counts_default();

    consteval command_spec make_group_conflicts_default_arg() {
        command_builder app("conflict");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default"))
                .arg(arg_builder("flag").long_("flag").group("one").action(arg_action::set_true))
                .group(group_builder("one").conflicts_with("opt"));
        return app.freeze();
    }
    constexpr command_spec group_conflicts_default_arg = make_group_conflicts_default_arg();

    // exclusive versus the four required flavours.
    consteval command_spec make_exclusive_with_required() {
        command_builder app("bug");
        std::move(app)
                .arg(arg_builder("test").long_("test").action(arg_action::set_true).exclusive())
                .arg(arg_builder("input").index(1).action(arg_action::set).required());
        return app.freeze();
    }
    constexpr command_spec exclusive_with_required = make_exclusive_with_required();

    consteval command_spec make_exclusive_unless_present() {
        command_builder app("bug");
        std::move(app)
                .arg(arg_builder("exclusive")
                             .long_("exclusive")
                             .action(arg_action::set_true)
                             .exclusive())
                .arg(arg_builder("required")
                             .long_("required")
                             .action(arg_action::set_true)
                             .required_unless_present("alternative"))
                .arg(arg_builder("alternative").long_("alternative").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec exclusive_unless_present = make_exclusive_unless_present();

    consteval command_spec make_exclusive_unless_any() {
        command_builder app("bug");
        std::move(app)
                .arg(arg_builder("exclusive")
                             .long_("exclusive")
                             .action(arg_action::set_true)
                             .exclusive())
                .arg(arg_builder("required")
                             .long_("required")
                             .action(arg_action::set_true)
                             .required_unless_present_any({"alternative"}))
                .arg(arg_builder("alternative").long_("alternative").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec exclusive_unless_any = make_exclusive_unless_any();

    consteval command_spec make_exclusive_unless_all() {
        command_builder app("bug");
        std::move(app)
                .arg(arg_builder("exclusive")
                             .long_("exclusive")
                             .action(arg_action::set_true)
                             .exclusive())
                .arg(arg_builder("required")
                             .long_("required")
                             .action(arg_action::set_true)
                             .required_unless_present_all({"alternative"}))
                .arg(arg_builder("alternative").long_("alternative").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec exclusive_unless_all = make_exclusive_unless_all();

    // Two and three mutually conflicting arguments — the message shape changes at three.
    consteval command_spec make_two_conflicting() {
        command_builder app("two_conflicting_arguments");
        std::move(app)
                .arg(arg_builder("develop")
                             .long_("develop")
                             .action(arg_action::set_true)
                             .conflicts_with("production"))
                .arg(arg_builder("production")
                             .long_("production")
                             .action(arg_action::set_true)
                             .conflicts_with("develop"));
        return app.freeze();
    }
    constexpr command_spec two_conflicting = make_two_conflicting();

    consteval command_spec make_three_conflicting() {
        command_builder app("three_conflicting_arguments");
        std::move(app)
                .arg(arg_builder("one")
                             .long_("one")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"two", "three"}))
                .arg(arg_builder("two")
                             .long_("two")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"one", "three"}))
                .arg(arg_builder("three")
                             .long_("three")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"one", "two"}));
        return app.freeze();
    }
    constexpr command_spec three_conflicting = make_three_conflicting();

    consteval command_spec make_repeated_group_conflict_order() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("target")
                             .long_("target")
                             .action(arg_action::set_true)
                             .conflicts_with_all({"zulu", "choice"}))
                .arg(arg_builder("mike").long_("mike").action(arg_action::count).group("choice"))
                .arg(arg_builder("zulu").long_("zulu").action(arg_action::set_true))
                .group(group_builder("choice").arg("mike"));
        return app.freeze();
    }
    constexpr command_spec repeated_group_conflict_order = make_repeated_group_conflict_order();

    // args_conflicts_with_subcommands.
    consteval command_spec make_subcommand_negates_required() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .subcommand(command_builder("config"))
                .arg(arg_builder("place")
                             .short_('p')
                             .long_("place")
                             .value_name("place id")
                             .required());
        return app.freeze();
    }
    constexpr command_spec subcommand_negates_required = make_subcommand_negates_required();

    consteval command_spec make_negate_one_level() {
        command_builder app("disablehelp");
        std::move(app)
                .args_conflicts_with_subcommands()
                .subcommand_negates_reqs()
                .arg(arg_builder("arg1").index(1).required())
                .arg(arg_builder("arg2").index(2).required())
                .subcommand(command_builder("sub1").subcommand(
                        command_builder("sub2").subcommand(command_builder("sub3"))));
        return app.freeze();
    }
    constexpr command_spec negate_one_level = make_negate_one_level();

    consteval command_spec make_negate_two_levels() {
        command_builder app("disablehelp");
        std::move(app)
                .args_conflicts_with_subcommands()
                .subcommand_negates_reqs()
                .arg(arg_builder("arg1").index(1).required())
                .arg(arg_builder("arg2").index(2).required())
                .subcommand(command_builder("sub1")
                                    .args_conflicts_with_subcommands()
                                    .subcommand_negates_reqs()
                                    .arg(arg_builder("arg").index(1).required())
                                    .arg(arg_builder("arg2").index(2).required())
                                    .subcommand(command_builder("sub2").subcommand(
                                            command_builder("sub3"))));
        return app.freeze();
    }
    constexpr command_spec negate_two_levels = make_negate_two_levels();

    consteval command_spec make_group_conflicts_subcommands() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("directory")
                             .short_('g')
                             .long_("directory")
                             .value_name("PATH")
                             .global())
                .arg(arg_builder("start").index(1).required())
                .group(group_builder("Cli").arg("directory").arg("start"))
                .subcommand(command_builder("delete").short_flag('d'));
        return app.freeze();
    }
    constexpr command_spec group_conflicts_subcommands = make_group_conflicts_subcommands();

    // --- The six `*_conflicts_with_subcommand*` shapes -------------------------------
    //
    // One rule, six ways for the subcommand to arrive: named, named-after-a-positional,
    // as a long flag, as a short flag, and twice more under `subcommand_precedence_over_arg`
    // (which changes which reading wins, not whether the conflict fires).

    consteval command_spec make_opt_vs_sub() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("place")
                             .short_('p')
                             .long_("place")
                             .value_name("place id")
                             .help("Place ID to open")
                             .action(arg_action::set))
                .subcommand(command_builder("sub1"));
        return app.freeze();
    }
    constexpr command_spec opt_vs_sub = make_opt_vs_sub();

    consteval command_spec make_pos_vs_sub() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("arg1").index(1).required().help("some arg"))
                .subcommand(command_builder("sub1"));
        return app.freeze();
    }
    constexpr command_spec pos_vs_sub = make_pos_vs_sub();

    consteval command_spec make_flag_vs_sub_long_flag() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("hello").long_("hello").action(arg_action::set_true))
                .subcommand(command_builder("sub").long_flag("sub"));
        return app.freeze();
    }
    constexpr command_spec flag_vs_sub_long_flag = make_flag_vs_sub_long_flag();

    consteval command_spec make_flag_vs_sub_short_flag() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("hello").long_("hello").action(arg_action::set_true))
                .subcommand(command_builder("sub").short_flag('s'));
        return app.freeze();
    }
    constexpr command_spec flag_vs_sub_short_flag = make_flag_vs_sub_short_flag();

    consteval command_spec make_pos_vs_sub_precedence() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .subcommand_precedence_over_arg()
                .arg(arg_builder("arg1").index(1).required().help("some arg"))
                .subcommand(command_builder("sub"));
        return app.freeze();
    }
    constexpr command_spec pos_vs_sub_precedence = make_pos_vs_sub_precedence();

    consteval command_spec make_flag_vs_sub_precedence() {
        command_builder app("test");
        std::move(app)
                .args_conflicts_with_subcommands()
                .subcommand_precedence_over_arg()
                .arg(arg_builder("hello").long_("hello").action(arg_action::set_true))
                .subcommand(command_builder("sub"));
        return app.freeze();
    }
    constexpr command_spec flag_vs_sub_precedence = make_flag_vs_sub_precedence();

    // The fixtures carry what the cases name.
    static_assert(exclusive_flag.find_arg("flag")->is_exclusive_set());
    // has_group(), never `find_group(...) != nullptr`: GCC 16.1.0 under `-fsanitize=null`
    // refuses to fold a cross-object pointer comparison inside a constant expression, so the
    // latter compiles on the primary preset and breaks the ubsan one. CLAUDE.md trap 10.
    static_assert(arg_conflicts_group.has_group("gr"));
    static_assert(arg_conflicts_required_group.find_group("gr")->is_required_set());
    static_assert(multi_source_group.find_group("gr")->is_multiple());
    static_assert(subcommand_negates_required.is_args_conflicts_with_subcommands_set());

}  // namespace

// ---------------------------------------------------------------------------
// conflicts_with is symmetric
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::flag_conflict") {
    const outcome got = clapp::parse(flag_conflict, raw_args{"myprog", "-f", "-o"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

CLAPP_TEST("conflicts.rs::flag_conflict_2") {
    // The other order. Only `flag` declares the conflict; `other` must still be caught.
    const outcome got = clapp::parse(flag_conflict, raw_args{"myprog", "-o", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

CLAPP_TEST("conflicts.rs::flag_conflict_with_all") {
    const outcome got = clapp::parse(flag_conflict_all, raw_args{"myprog", "-o", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

// ---------------------------------------------------------------------------
// exclusive
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::exclusive_flag") {
    const outcome alone = clapp::parse(exclusive_flag, raw_args{"myprog", "-f"});
    CLAPP_CHECK(alone.has_value());

    const outcome with_other = clapp::parse(exclusive_flag, raw_args{"myprog", "-o", "-f"});
    CLAPP_CHECK(!with_other.has_value());
    CLAPP_CHECK(kind_of(with_other) == error_kind::argument_conflict);
}

CLAPP_TEST("conflicts.rs::exclusive_option") {
    const outcome got = clapp::parse(exclusive_option, raw_args{"myprog", "-o=val1", "-f=val2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

CLAPP_TEST("conflicts.rs::not_exclusive_with_defaults") {
    // `-o` is present only through its default. An exclusive `-f` must not mind.
    const outcome got = clapp::parse(exclusive_with_default, raw_args{"myprog", "-f=val2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "other") == std::optional<std::string>{"val1"});
}

CLAPP_TEST("conflicts.rs::not_exclusive_with_group") {
    // Being a member of a group is not "another argument is present".
    const outcome got = clapp::parse(exclusive_in_group, raw_args{"test", "--foo"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("foo"));
}

CLAPP_TEST("conflicts.rs::default_doesnt_activate_exclusive") {
    // Neither argument was typed. The exclusive one is present only by default, so it
    // must not exclude anything.
    const outcome got = clapp::parse(both_defaulted_exclusive, raw_args{"myprog"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"val2"});
    CLAPP_CHECK(one_string(*got, "other") == std::optional<std::string>{"val1"});
}

// ---------------------------------------------------------------------------
// Groups, in both directions
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::arg_conflicts_with_group") {
    CLAPP_CHECK(kind_of(clapp::parse(arg_conflicts_group, raw_args{"myprog", "--other", "-f"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(kind_of(clapp::parse(arg_conflicts_group, raw_args{"myprog", "-f", "--some"})) ==
                error_kind::argument_conflict);
    // Each half on its own is fine.
    CLAPP_CHECK(clapp::parse(arg_conflicts_group, raw_args{"myprog", "--some"}).has_value());
    CLAPP_CHECK(clapp::parse(arg_conflicts_group, raw_args{"myprog", "--other"}).has_value());
    CLAPP_CHECK(clapp::parse(arg_conflicts_group, raw_args{"myprog", "--flag"}).has_value());
}

CLAPP_TEST("conflicts.rs::group_conflicts_with_arg") {
    // The mirror declaration: the GROUP names the argument. Same outcomes.
    CLAPP_CHECK(kind_of(clapp::parse(group_conflicts_arg, raw_args{"myprog", "--other", "-f"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(kind_of(clapp::parse(group_conflicts_arg, raw_args{"myprog", "-f", "--some"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(clapp::parse(group_conflicts_arg, raw_args{"myprog", "--some"}).has_value());
    CLAPP_CHECK(clapp::parse(group_conflicts_arg, raw_args{"myprog", "--other"}).has_value());
    CLAPP_CHECK(clapp::parse(group_conflicts_arg, raw_args{"myprog", "--flag"}).has_value());
}

CLAPP_TEST("conflicts.rs::arg_conflicts_with_required_group") {
    CLAPP_CHECK(kind_of(clapp::parse(arg_conflicts_required_group,
                                     raw_args{"myprog", "--other", "-f"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(kind_of(clapp::parse(arg_conflicts_required_group,
                                     raw_args{"myprog", "-f", "--some"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(
            clapp::parse(arg_conflicts_required_group, raw_args{"myprog", "--some"}).has_value());
    CLAPP_CHECK(
            clapp::parse(arg_conflicts_required_group, raw_args{"myprog", "--other"}).has_value());
}

CLAPP_TEST("conflicts.rs::required_group_conflicts_with_arg") {
    CLAPP_CHECK(kind_of(clapp::parse(required_group_conflicts_arg,
                                     raw_args{"myprog", "--other", "-f"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(kind_of(clapp::parse(required_group_conflicts_arg,
                                     raw_args{"myprog", "-f", "--some"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(
            clapp::parse(required_group_conflicts_arg, raw_args{"myprog", "--some"}).has_value());
    CLAPP_CHECK(
            clapp::parse(required_group_conflicts_arg, raw_args{"myprog", "--other"}).has_value());
}

CLAPP_TEST("conflicts.rs::arg_conflicts_with_group_with_required_memeber") {
    CLAPP_CHECK(kind_of(clapp::parse(group_with_required_member,
                                     raw_args{"myprog", "--other", "-f"})) ==
                error_kind::argument_conflict);
    CLAPP_CHECK(
            kind_of(clapp::parse(group_with_required_member, raw_args{"myprog", "-f", "--some"})) ==
            error_kind::argument_conflict);
    CLAPP_CHECK(clapp::parse(group_with_required_member, raw_args{"myprog", "--some"}).has_value());
    // `--some` is required, but `-f` conflicts with the group it belongs to, which
    // suppresses the requirement. clap asserts exactly this.
    CLAPP_CHECK(clapp::parse(group_with_required_member, raw_args{"myprog", "--flag"}).has_value());
}

CLAPP_TEST("conflicts.rs::arg_conflicts_with_group_with_multiple_sources") {
    // `other` has a default, so the group is "occupied" from the moment defaults run.
    // That must not make `-f` alone a conflict.
    CLAPP_CHECK(clapp::parse(multi_source_group, raw_args{"myprog", "-f"}).has_value());
    CLAPP_CHECK(clapp::parse(multi_source_group, raw_args{"myprog", "--some", "usb1"}).has_value());
    CLAPP_CHECK(
            clapp::parse(multi_source_group, raw_args{"myprog", "--some", "usb1", "--other", "40"})
                    .has_value());
    CLAPP_CHECK(
            kind_of(clapp::parse(multi_source_group, raw_args{"myprog", "-f", "--some", "usb1"})) ==
            error_kind::argument_conflict);
}

// ---------------------------------------------------------------------------
// A default is never a conflict
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::conflict_with_unused_default") {
    const outcome got = clapp::parse(unused_default, raw_args{"myprog", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("conflicts.rs::conflicts_with_alongside_default") {
    // The same relation declared from the DEFAULTED side.
    const outcome got = clapp::parse(default_side_declares, raw_args{"myprog", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("conflicts.rs::group_in_conflicts_with") {
    const outcome got = clapp::parse(group_in_conflicts_with, raw_args{"myprog", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("conflicts.rs::group_conflicts_with_default_value") {
    // Both are in the same single-valued group, so a group occupancy count that includes
    // defaults reports two members and rejects the line.
    const outcome got = clapp::parse(group_counts_default, raw_args{"myprog", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("conflicts.rs::group_conflicts_with_default_arg") {
    const outcome got = clapp::parse(group_conflicts_default_arg, raw_args{"myprog", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(got->get_flag("flag"));
}

// ---------------------------------------------------------------------------
// The message names both sides
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::two_conflicting_arguments") {
    const outcome got = clapp::parse(two_conflicting, raw_args{"", "--develop", "--production"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '--develop' cannot be used with '--production'"));
}

CLAPP_TEST("conflicts.rs::three_conflicting_arguments") {
    // At three the sentence changes shape: a colon and a bullet list.
    const outcome got = clapp::parse(three_conflicting, raw_args{"", "--one", "--two", "--three"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '--one' cannot be used with:"));
    CLAPP_CHECK(says(got, "--two"));
    CLAPP_CHECK(says(got, "--three"));
}

CLAPP_TEST("a repeated group member keeps the group's first conflict-list position") {
    const outcome got = clapp::parse(repeated_group_conflict_order,
                                     raw_args{"prog", "--target", "--mike", "--zulu", "--mike"});
    CLAPP_CHECK(same_block(got,
                           "error: the argument '--target' cannot be used with:\n"
                           "  --mike...\n"
                           "  --zulu\n"
                           "\n"
                           "Usage: prog --target\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// exclusive suppresses every flavour of required
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::exclusive_with_required") {
    // `--test` with the required positional supplied is still a conflict ...
    CLAPP_CHECK(!clapp::parse(exclusive_with_required, raw_args{"bug", "--test", "required"})
                         .has_value());
    // ... the positional alone is fine ...
    CLAPP_CHECK(clapp::parse(exclusive_with_required, raw_args{"bug", "required"}).has_value());
    // ... and `--test` alone SUPPRESSES the requirement rather than tripping it.
    CLAPP_CHECK(clapp::parse(exclusive_with_required, raw_args{"bug", "--test"}).has_value());
}

CLAPP_TEST("conflicts.rs::exclusive_with_required_unless_present") {
    CLAPP_CHECK(clapp::parse(exclusive_unless_present, raw_args{"bug", "--required"}).has_value());
    CLAPP_CHECK(
            clapp::parse(exclusive_unless_present, raw_args{"bug", "--alternative"}).has_value());
    CLAPP_CHECK(!clapp::parse(exclusive_unless_present, raw_args{"bug"}).has_value());
    CLAPP_CHECK(
            !clapp::parse(exclusive_unless_present, raw_args{"bug", "--exclusive", "--required"})
                     .has_value());
    CLAPP_CHECK(clapp::parse(exclusive_unless_present, raw_args{"bug", "--exclusive"}).has_value());
}

CLAPP_TEST("conflicts.rs::exclusive_with_required_unless_present_any") {
    CLAPP_CHECK(clapp::parse(exclusive_unless_any, raw_args{"bug", "--required"}).has_value());
    CLAPP_CHECK(clapp::parse(exclusive_unless_any, raw_args{"bug", "--alternative"}).has_value());
    CLAPP_CHECK(!clapp::parse(exclusive_unless_any, raw_args{"bug"}).has_value());
    CLAPP_CHECK(!clapp::parse(exclusive_unless_any, raw_args{"bug", "--exclusive", "--required"})
                         .has_value());
    CLAPP_CHECK(clapp::parse(exclusive_unless_any, raw_args{"bug", "--exclusive"}).has_value());
}

CLAPP_TEST("conflicts.rs::exclusive_with_required_unless_present_all") {
    CLAPP_CHECK(clapp::parse(exclusive_unless_all, raw_args{"bug", "--required"}).has_value());
    CLAPP_CHECK(clapp::parse(exclusive_unless_all, raw_args{"bug", "--alternative"}).has_value());
    CLAPP_CHECK(!clapp::parse(exclusive_unless_all, raw_args{"bug"}).has_value());
    CLAPP_CHECK(!clapp::parse(exclusive_unless_all, raw_args{"bug", "--exclusive", "--required"})
                         .has_value());
    CLAPP_CHECK(clapp::parse(exclusive_unless_all, raw_args{"bug", "--exclusive"}).has_value());
}

// ---------------------------------------------------------------------------
// args_conflicts_with_subcommands
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::subcommand_conflict_negates_required") {
    const outcome got = clapp::parse(subcommand_negates_required, raw_args{"test", "config"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"config"});
}

CLAPP_TEST("conflicts.rs::args_negate_subcommands_one_level") {
    // `sub1` is consumed as the SECOND POSITIONAL, not as a subcommand, because a
    // positional was already in flight.
    const outcome got = clapp::parse(negate_one_level, raw_args{"", "pickles", "sub1"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "arg2") == std::optional<std::string>{"sub1"});
}

CLAPP_TEST("conflicts.rs::args_negate_subcommands_two_levels") {
    const outcome got = clapp::parse(negate_two_levels, raw_args{"", "sub1", "arg", "sub2"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* child = got->subcommand_matches("sub1");
    CLAPP_CHECK(child != nullptr);
    if (child != nullptr)
        CLAPP_CHECK(one_string(*child, "arg2") == std::optional<std::string>{"sub2"});
}

CLAPP_TEST("conflicts.rs::group_conrflicts_with_subcommands") {
    const outcome got =
            clapp::parse(group_conflicts_subcommands, raw_args{"test", "-g", "./", "-d"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

// ---------------------------------------------------------------------------
// An argument conflicting with a SUBCOMMAND
//
// `args_conflicts_with_subcommands` produces the one conflict message whose two sides
// are not both arguments, and it is rendered by a separate arm of
// `write_argument_conflict()` (`error.hpp`, the `context_kind::invalid_subcommand`
// branch). These six cases are its only witnesses in the tree; `group_conrflicts_with_
// subcommands` above asserts nothing but the kind, which that arm's deletion survives.
//
// Whole blocks, and note the two-line usage: `test [OPTIONS]` and `test <COMMAND>` are
// printed as ALTERNATIVES, which is itself a consequence of the setting — with the
// conflict off, clap and clapp both print one combined line.
// ---------------------------------------------------------------------------

CLAPP_TEST("conflicts.rs::option_conflicts_with_subcommand") {
    const outcome got = clapp::parse(opt_vs_sub, raw_args{"test", "--place", "id", "sub1"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(got,
                           "error: the subcommand 'sub1' cannot be used with "
                           "'--place <place id>'\n"
                           "\n"
                           "Usage: test [OPTIONS]\n"
                           "       test <COMMAND>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("conflicts.rs::positional_conflicts_with_subcommand") {
    // The offended party is a positional, so it is named by its placeholder `<arg1>`,
    // and the first usage line shows the positional rather than `[OPTIONS]`.
    const outcome got = clapp::parse(pos_vs_sub, raw_args{"test", "value1", "sub1"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(got,
                           "error: the subcommand 'sub1' cannot be used with '<arg1>'\n"
                           "\n"
                           "Usage: test <arg1>\n"
                           "       test <COMMAND>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("conflicts.rs::flag_conflicts_with_subcommand_long_flag") {
    // The subcommand arrived as `--sub`, and the message still names it `'sub'` — the
    // canonical name, not the spelling that triggered it.
    const outcome got = clapp::parse(flag_vs_sub_long_flag, raw_args{"test", "--hello", "--sub"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(got,
                           "error: the subcommand 'sub' cannot be used with '--hello'\n"
                           "\n"
                           "Usage: test [OPTIONS]\n"
                           "       test <COMMAND>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("conflicts.rs::flag_conflicts_with_subcommand_short_flag") {
    // Same again through `-s`. Two spellings, one canonical name in the message.
    const outcome got = clapp::parse(flag_vs_sub_short_flag, raw_args{"test", "--hello", "-s"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(got,
                           "error: the subcommand 'sub' cannot be used with '--hello'\n"
                           "\n"
                           "Usage: test [OPTIONS]\n"
                           "       test <COMMAND>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("conflicts.rs::positional_conflicts_with_subcommand_precedent") {
    // `subcommand_precedence_over_arg` decides that `sub` is a command level rather than
    // a second value of `<arg1>`. It changes the READING, not the verdict — the conflict
    // still fires, and this is the case that separates the two.
    const outcome got = clapp::parse(pos_vs_sub_precedence, raw_args{"test", "hello", "sub"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(got,
                           "error: the subcommand 'sub' cannot be used with '<arg1>'\n"
                           "\n"
                           "Usage: test <arg1>\n"
                           "       test <COMMAND>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("conflicts.rs::flag_conflicts_with_subcommand_precedent") {
    const outcome got = clapp::parse(flag_vs_sub_precedence, raw_args{"test", "--hello", "sub"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(got,
                           "error: the subcommand 'sub' cannot be used with '--hello'\n"
                           "\n"
                           "Usage: test [OPTIONS]\n"
                           "       test <COMMAND>\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}
